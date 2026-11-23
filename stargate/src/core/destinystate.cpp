#include "destinystate.h"

#include <QVariant>
#include <QtMath>
#include <algorithm>
#include <utility>

#include "src/db/databasemanager.h"

namespace {

// Repair cost of the hull until it gets its own table
const QList<RepairCost> kHullRepairCost{{0, QStringLiteral("minerals"), 5}};

int resourceIdByName(const QString &name)
{
    QSqlQuery q = DatabaseManager::instance().query("SELECT id FROM Resource WHERE name = :name");
    q.bindValue(":name", name);
    return q.exec() && q.next() ? q.value(0).toInt() : -1;
}

} // namespace

bool DestinyState::load(const QString &username)
{
    migrateSupplies();
    m_shipId = ShipRepository::ensurePlayerDestiny(username);
    if (m_shipId >= 0) {
        // Saves created before food existed: one-time supply delivery
        QSqlQuery q = DatabaseManager::instance().query(
            "SELECT COUNT(*) FROM Ship_Resource s JOIN Resource r ON r.id = s.resource_id "
            "WHERE s.ship_id = :ship AND r.name = 'food'");
        q.bindValue(":ship", m_shipId);
        if (q.exec() && q.next() && q.value(0).toInt() == 0) {
            ResourceRepository::addByName(m_shipId, "food", 150);
            const int water = stockOf("water");
            if (water < 250)
                ResourceRepository::addByName(m_shipId, "water", 250 - water);
        }
    }
    m_components = m_shipId >= 0 ? ShipRepository::components(m_shipId) : QList<ShipComponent>();
    return m_shipId >= 0;
}

void DestinyState::migrateSupplies()
{
    // "food" was added after the first saves: make sure it exists
    DatabaseManager::instance().exec(
        "INSERT OR IGNORE INTO Resource (name, description, rarity, base_drop_chance) "
        "VALUES ('food', 'Edible biomass and supplies for the crew', 'COMMON', 0.4)");
}

QList<int> DestinyState::componentIndexes(const QString &key) const
{
    QList<int> indexes;
    for (int i = 0; i < m_components.size(); ++i) {
        const ShipComponent &c = m_components.at(i);
        const bool match = (key == "engines" && c.name == "Sublight Engine")
                        || (key == "shields" && c.name == "Shield Generator")
                        || (key == "lifesupport" && c.name == "Life Support")
                        || (key == "weapons" && c.category == "WEAPON");
        if (match)
            indexes.append(i);
    }
    return indexes;
}

float DestinyState::systemHealth(const QString &key) const
{
    int current = 0, max = 0;
    for (int i : componentIndexes(key)) {
        current += m_components.at(i).currentHp;
        max += m_components.at(i).maxHp;
    }
    return max > 0 ? 100.0f * float(current) / float(max) : 100.0f;
}

void DestinyState::setSystemHealth(const QString &key, float percent)
{
    if (!isLoaded())
        return;
    for (int i : componentIndexes(key)) {
        ShipComponent &c = m_components[i];
        const int hp = qBound(0, qRound(percent / 100.0f * float(c.maxHp)), c.maxHp);
        if (hp == c.currentHp)
            continue;
        if (ShipRepository::setComponentHp(c.id, hp, c.maxHp))
            c.currentHp = hp;
    }
}

QList<RepairCost> DestinyState::repairCost(const QString &key) const
{
    if (key == "hull")
        return kHullRepairCost;

    QList<RepairCost> total;
    for (int i : componentIndexes(key)) {
        for (const RepairCost &cost : ComponentRepository::repairCost(m_components.at(i).componentId)) {
            auto it = std::find_if(total.begin(), total.end(),
                                   [&](const RepairCost &r) { return r.resourceName == cost.resourceName; });
            if (it == total.end())
                total.append(cost);
            else
                it->quantity += cost.quantity;
        }
    }
    return total;
}

bool DestinyState::consume(const QList<RepairCost> &cost, QString *missing)
{
    if (!isLoaded())
        return true;

    QStringList lacking;
    QList<std::pair<int, int>> toRemove;
    for (const RepairCost &c : cost) {
        const int id = c.resourceId > 0 ? c.resourceId : resourceIdByName(c.resourceName);
        const int have = ResourceRepository::quantity(m_shipId, id);
        if (have < c.quantity)
            lacking << QStringLiteral("%1 %2 (have %3)").arg(c.quantity).arg(c.resourceName).arg(have);
        toRemove.append({id, c.quantity});
    }
    if (!lacking.isEmpty()) {
        if (missing)
            *missing = lacking.join(", ");
        return false;
    }

    DatabaseManager &db = DatabaseManager::instance();
    db.beginTransaction();
    for (const auto &[id, quantity] : toRemove) {
        if (!ResourceRepository::add(m_shipId, id, -quantity)) {
            db.rollback();
            if (missing)
                *missing = QStringLiteral("database error");
            return false;
        }
    }
    db.commit();
    return true;
}

QString DestinyState::collectSalvage(bool mothership)
{
    if (!isLoaded())
        return QString();

    const QList<std::pair<QString, int>> loot = mothership
        ? QList<std::pair<QString, int>>{{"naquadah", 3}, {"trinium", 1}, {"minerals", 2}}
        : QList<std::pair<QString, int>>{{"minerals", 2}};

    return addResources(loot);
}

QString DestinyState::addResources(const QList<std::pair<QString, int>> &resources)
{
    if (!isLoaded())
        return QString();
    QStringList gained;
    for (const auto &[name, quantity] : resources) {
        if (quantity > 0 && ResourceRepository::addByName(m_shipId, name, quantity)) {
            gained << QStringLiteral("+%1 %2").arg(quantity).arg(name);
            if (m_onGain)
                m_onGain(name, quantity);
        }
    }
    return gained.join(", ");
}

QString DestinyState::collectFromPlanet(const Planet &planet)
{
    const PlanetConditions &c = planet.conditions;

    // Dangerous conditions and thin air reduce the team's efficiency
    float efficiency = std::max(0.05f, 1.0f - c.toxicity * 0.55f - c.radiation * 0.45f);
    if (c.oxygen < 0.2f)
        efficiency *= 0.5f + c.oxygen * 2.5f;

    QList<std::pair<QString, int>> loot{
        {"water", qRound(c.water * efficiency * 60.0f)},
        {"food", qRound(c.organics * efficiency * 40.0f)},
        {"minerals", qRound(c.minerals * efficiency * 20.0f)},
        {"naquadah", qRound(c.energy * efficiency * 12.0f)},
    };
    if (c.minerals > 0.6f)
        loot.append({"trinium", qRound(c.minerals * efficiency * 3.0f)});
    if (planet.isAncientSite()) {
        loot.append({"crystal", 2});
        loot.append({"ancient_alloy", 1 + planet.variant % 2});
    }

    const QString gained = addResources(loot);
    return gained.isEmpty() ? QStringLiteral("nothing recoverable") : gained;
}

bool DestinyState::consumeSupplies(int crew, double hours)
{
    if (!isLoaded())
        return true;

    // Per person and per hour (prototype values)
    m_waterNeed += crew * 0.02 * hours;
    m_foodNeed += crew * 0.015 * hours;

    bool enough = true;
    auto take = [&](const QString &name, double &need) {
        const int units = int(need + 1e-6);   // tolerate float accumulation
        if (units <= 0)
            return;
        need -= units;
        const int available = stockOf(name);
        const int taken = std::min(units, available);
        if (taken > 0)
            ResourceRepository::addByName(m_shipId, name, -taken);
        if (taken < units)
            enough = false;
    };
    take("water", m_waterNeed);
    take("food", m_foodNeed);
    return enough;
}

QStringList DestinyState::componentNames(const QString &key) const
{
    QStringList names;
    for (int i : componentIndexes(key))
        names << m_components.at(i).name;
    return names;
}

int DestinyState::stockOf(const QString &resourceName) const
{
    for (const ResourceStock &r : stock())
        if (r.name == resourceName)
            return r.quantity;
    return 0;
}

QList<ResourceStock> DestinyState::stock() const
{
    return isLoaded() ? ResourceRepository::shipStock(m_shipId) : QList<ResourceStock>();
}
