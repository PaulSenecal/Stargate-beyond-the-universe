#include "resourcerepository.h"

#include <QVariant>

#include "../databasemanager.h"

QList<ResourceStock> ResourceRepository::shipStock(int shipId)
{
    QList<ResourceStock> stock;
    QSqlQuery q = DatabaseManager::instance().query(
        "SELECT r.id, r.name, r.rarity, COALESCE(s.quantity, 0) "
        "FROM Resource r "
        "LEFT JOIN Ship_Resource s ON s.resource_id = r.id AND s.ship_id = :ship "
        "ORDER BY r.id");
    q.bindValue(":ship", shipId);
    if (!q.exec()) {
        qWarning() << "[DB] ResourceRepository::shipStock:" << q.lastError().text();
        return stock;
    }
    while (q.next())
        stock.append({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString(), q.value(3).toInt()});
    return stock;
}

int ResourceRepository::quantity(int shipId, int resourceId)
{
    QSqlQuery q = DatabaseManager::instance().query(
        "SELECT quantity FROM Ship_Resource WHERE ship_id = :ship AND resource_id = :resource");
    q.bindValue(":ship", shipId);
    q.bindValue(":resource", resourceId);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

bool ResourceRepository::add(int shipId, int resourceId, int delta)
{
    const int current = quantity(shipId, resourceId);
    if (current + delta < 0)
        return false;

    QSqlQuery q = DatabaseManager::instance().query(
        "INSERT INTO Ship_Resource (ship_id, resource_id, quantity) VALUES (:ship, :resource, :qty) "
        "ON CONFLICT (ship_id, resource_id) DO UPDATE SET quantity = excluded.quantity");
    q.bindValue(":ship", shipId);
    q.bindValue(":resource", resourceId);
    q.bindValue(":qty", current + delta);
    if (!q.exec()) {
        qWarning() << "[DB] ResourceRepository::add:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ResourceRepository::addByName(int shipId, const QString &resourceName, int delta)
{
    QSqlQuery q = DatabaseManager::instance().query("SELECT id FROM Resource WHERE name = :name");
    q.bindValue(":name", resourceName);
    if (!q.exec() || !q.next()) {
        qWarning() << "[DB] Unknown resource:" << resourceName;
        return false;
    }
    return add(shipId, q.value(0).toInt(), delta);
}
