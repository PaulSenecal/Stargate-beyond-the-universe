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
    m_shipId = ShipRepository::ensurePlayerDestiny(username);
    m_components = m_shipId >= 0 ? ShipRepository::components(m_shipId) : QList<ShipComponent>();
    return m_shipId >= 0;
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

    QStringList gained;
    for (const auto &[name, quantity] : loot)
        if (ResourceRepository::addByName(m_shipId, name, quantity))
            gained << QStringLiteral("+%1 %2").arg(quantity).arg(name);
    return gained.join(", ");
}

QList<ResourceStock> DestinyState::stock() const
{
    return isLoaded() ? ResourceRepository::shipStock(m_shipId) : QList<ResourceStock>();
}
