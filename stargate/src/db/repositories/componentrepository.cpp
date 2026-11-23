#include "componentrepository.h"

#include <QVariant>

#include "../databasemanager.h"

QList<ComponentInfo> ComponentRepository::all()
{
    QList<ComponentInfo> components;
    QSqlQuery q = DatabaseManager::instance().query(
        "SELECT id, name, category, max_hp, energy_cost FROM Component ORDER BY id");
    if (!q.exec()) {
        qWarning() << "[DB] ComponentRepository::all:" << q.lastError().text();
        return components;
    }
    while (q.next())
        components.append({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString(),
                           q.value(3).toInt(), q.value(4).toInt()});
    return components;
}

QList<RepairCost> ComponentRepository::repairCost(int componentId)
{
    QList<RepairCost> costs;
    QSqlQuery q = DatabaseManager::instance().query(
        "SELECT r.id, r.name, c.quantity "
        "FROM Component_Repair_Cost c JOIN Resource r ON r.id = c.resource_id "
        "WHERE c.component_id = :component ORDER BY r.id");
    q.bindValue(":component", componentId);
    if (!q.exec()) {
        qWarning() << "[DB] ComponentRepository::repairCost:" << q.lastError().text();
        return costs;
    }
    while (q.next())
        costs.append({q.value(0).toInt(), q.value(1).toString(), q.value(2).toInt()});
    return costs;
}
