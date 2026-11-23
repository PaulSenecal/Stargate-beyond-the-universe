#include "worldrepository.h"

#include <QVariant>
#include <algorithm>
#include <utility>

#include "../databasemanager.h"

namespace {

const QString kRegionName = QStringLiteral("Destiny route");

int findOrInsert(const QString &findSql, const QString &insertSql,
                 const QList<std::pair<QString, QVariant>> &bindings)
{
    DatabaseManager &db = DatabaseManager::instance();
    QSqlQuery find = db.query(findSql);
    for (const auto &[name, value] : bindings)
        if (findSql.contains(name))
            find.bindValue(name, value);
    if (find.exec() && find.next())
        return find.value(0).toInt();

    QSqlQuery insert = db.query(insertSql);
    for (const auto &[name, value] : bindings)
        insert.bindValue(name, value);
    if (!insert.exec()) {
        qWarning() << "[DB] WorldRepository insert failed:" << insert.lastError().text();
        return -1;
    }
    return insert.lastInsertId().toInt();
}

int resourceId(const QString &name)
{
    QSqlQuery q = DatabaseManager::instance().query("SELECT id FROM Resource WHERE name = :name");
    q.bindValue(":name", name);
    return q.exec() && q.next() ? q.value(0).toInt() : -1;
}

bool insertPlanetResource(int planetId, const QString &resource, float chance)
{
    const int id = resourceId(resource);
    if (id < 0)
        return true;     // resource not in this save: nothing to record
    chance = std::clamp(chance, 0.0f, 1.0f);

    QSqlQuery q = DatabaseManager::instance().query(
        "INSERT OR IGNORE INTO Planet_Resource (planet_id, resource_id, drop_chance, quantity_min, quantity_max) "
        "VALUES (:planet, :resource, :chance, 1, :max)");
    q.bindValue(":planet", planetId);
    q.bindValue(":resource", id);
    q.bindValue(":chance", double(chance));
    q.bindValue(":max", 1 + qRound(chance * 10.0f));
    if (!q.exec()) {
        qWarning() << "[DB] Planet_Resource insert failed:" << q.lastError().text();
        return false;
    }
    return true;
}

} // namespace

bool WorldRepository::recordSystem(const QString &galaxyName, const QList<Planet> &planets)
{
    DatabaseManager &db = DatabaseManager::instance();
    if (!db.isOpen())
        return false;

    db.beginTransaction();
    const int galaxyId = findOrInsert(
        "SELECT id FROM Galaxy WHERE name = :name",
        "INSERT INTO Galaxy (name, is_discovered) VALUES (:name, 1)",
        {{":name", galaxyName}});
    const int regionId = galaxyId < 0 ? -1 : findOrInsert(
        "SELECT id FROM Region WHERE galaxy_id = :galaxy AND name = :name",
        "INSERT INTO Region (galaxy_id, name) VALUES (:galaxy, :name)",
        {{":galaxy", galaxyId}, {":name", kRegionName}});
    if (regionId < 0) {
        db.rollback();
        return false;
    }

    for (const Planet &p : planets) {
        QSqlQuery find = db.query("SELECT id FROM Planet WHERE region_id = :region AND name = :name");
        find.bindValue(":region", regionId);
        find.bindValue(":name", p.code);
        if (find.exec() && find.next())
            continue;   // already recorded

        QSqlQuery insert = db.query(
            "INSERT INTO Planet (region_id, name, type, has_stargate, is_discovered) "
            "VALUES (:region, :name, :type, 1, 1)");
        insert.bindValue(":region", regionId);
        insert.bindValue(":name", p.code);
        insert.bindValue(":type", p.classificationName().toLower());
        if (!insert.exec()) {
            qWarning() << "[DB] Planet insert failed:" << insert.lastError().text();
            db.rollback();
            return false;
        }
        const int planetId = insert.lastInsertId().toInt();
        const PlanetConditions &c = p.conditions;
        const bool ok = insertPlanetResource(planetId, "water", c.water)
                     && insertPlanetResource(planetId, "minerals", c.minerals)
                     && insertPlanetResource(planetId, "naquadah", c.energy)
                     && insertPlanetResource(planetId, "food", c.organics)
                     && (!p.isAncientSite() || insertPlanetResource(planetId, "ancient_alloy", 0.5f));
        if (!ok) {
            db.rollback();
            return false;
        }
    }
    db.commit();
    return true;
}

int WorldRepository::discoveredPlanetCount()
{
    QSqlQuery q = DatabaseManager::instance().query("SELECT COUNT(*) FROM Planet WHERE is_discovered = 1");
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}
