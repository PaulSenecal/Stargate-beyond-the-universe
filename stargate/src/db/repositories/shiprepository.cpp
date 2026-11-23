#include "shiprepository.h"

#include <QVariant>
#include <utility>

#include "../databasemanager.h"
#include "componentrepository.h"
#include "resourcerepository.h"

namespace {

int playerId(const QString &username)
{
    DatabaseManager &db = DatabaseManager::instance();

    QSqlQuery find = db.query("SELECT id FROM Player WHERE username = :name");
    find.bindValue(":name", username);
    if (find.exec() && find.next())
        return find.value(0).toInt();

    QSqlQuery insert = db.query("INSERT INTO Player (username) VALUES (:name)");
    insert.bindValue(":name", username);
    if (!insert.exec()) {
        qWarning() << "[DB] Cannot create player:" << insert.lastError().text();
        return -1;
    }
    return insert.lastInsertId().toInt();
}

int createDestiny(int player)
{
    DatabaseManager &db = DatabaseManager::instance();

    QSqlQuery ship = db.query(
        "INSERT INTO Ship (ship_type_id, player_id, is_player) "
        "SELECT id, :player, 1 FROM Ship_Type WHERE name = 'Destiny'");
    ship.bindValue(":player", player);
    if (!ship.exec() || ship.numRowsAffected() != 1) {
        qWarning() << "[DB] Cannot create the Destiny:" << ship.lastError().text();
        return -1;
    }
    const int shipId = ship.lastInsertId().toInt();

    for (const ComponentInfo &c : ComponentRepository::all()) {
        QSqlQuery install = db.query(
            "INSERT INTO Component_Ship (component_id, ship_id, current_hp, status) "
            "VALUES (:component, :ship, :hp, 'OPERATIONAL')");
        install.bindValue(":component", c.id);
        install.bindValue(":ship", shipId);
        install.bindValue(":hp", c.maxHp);
        if (!install.exec()) {
            qWarning() << "[DB] Cannot install" << c.name << ":" << install.lastError().text();
            return -1;
        }
    }

    const std::pair<const char *, int> startingStock[] = {
        {"water", 40}, {"naquadah", 20}, {"minerals", 15},
        {"trinium", 6}, {"crystal", 4}, {"ancient_alloy", 2},
    };
    for (const auto &[name, quantity] : startingStock)
        if (!ResourceRepository::addByName(shipId, name, quantity))
            return -1;

    qDebug() << "[DB] New Destiny created, ship id" << shipId;
    return shipId;
}

} // namespace

int ShipRepository::ensurePlayerDestiny(const QString &username)
{
    DatabaseManager &db = DatabaseManager::instance();
    if (!db.isOpen())
        return -1;

    db.beginTransaction();

    const int player = playerId(username);
    if (player < 0) {
        db.rollback();
        return -1;
    }

    QSqlQuery find = db.query(
        "SELECT s.id FROM Ship s JOIN Ship_Type t ON t.id = s.ship_type_id "
        "WHERE s.player_id = :player AND s.is_player = 1 AND t.name = 'Destiny' "
        "ORDER BY s.id LIMIT 1");
    find.bindValue(":player", player);
    int shipId = find.exec() && find.next() ? find.value(0).toInt() : createDestiny(player);

    if (shipId < 0) {
        db.rollback();
        return -1;
    }
    db.commit();
    return shipId;
}

QList<ShipComponent> ShipRepository::components(int shipId)
{
    QList<ShipComponent> list;
    QSqlQuery q = DatabaseManager::instance().query(
        "SELECT cs.id, c.id, c.name, c.category, cs.current_hp, c.max_hp, cs.status "
        "FROM Component_Ship cs JOIN Component c ON c.id = cs.component_id "
        "WHERE cs.ship_id = :ship ORDER BY c.id");
    q.bindValue(":ship", shipId);
    if (!q.exec()) {
        qWarning() << "[DB] ShipRepository::components:" << q.lastError().text();
        return list;
    }
    while (q.next())
        list.append({q.value(0).toInt(), q.value(1).toInt(), q.value(2).toString(), q.value(3).toString(),
                     q.value(4).toInt(), q.value(5).toInt(), q.value(6).toString()});
    return list;
}

bool ShipRepository::setComponentHp(int shipComponentId, int hp, int maxHp)
{
    hp = qBound(0, hp, maxHp);
    const QString status = hp <= 0      ? QStringLiteral("DESTROYED")
                         : hp < maxHp  ? QStringLiteral("DAMAGED")
                                       : QStringLiteral("OPERATIONAL");

    QSqlQuery q = DatabaseManager::instance().query(
        "UPDATE Component_Ship SET current_hp = :hp, status = :status WHERE id = :id");
    q.bindValue(":hp", hp);
    q.bindValue(":status", status);
    q.bindValue(":id", shipComponentId);
    if (!q.exec()) {
        qWarning() << "[DB] ShipRepository::setComponentHp:" << q.lastError().text();
        return false;
    }
    return true;
}
