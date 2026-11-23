#include "questrepository.h"

#include <QVariant>
#include <QtMath>

#include "../databasemanager.h"

bool QuestRepository::migrate()
{
    DatabaseManager &db = DatabaseManager::instance();
    // Each statement only changes rows still in their original form
    return db.exec("UPDATE Quest_Objective SET target_id = (SELECT id FROM Component WHERE name = 'Life Support') "
                   "WHERE id = 1 AND type = 'REPAIR' AND target_id IS NULL")
        && db.exec("UPDATE Quest_Objective SET target_id = (SELECT id FROM Component WHERE name = 'Sublight Engine') "
                   "WHERE id = 2 AND type = 'REPAIR' AND target_id IS NULL")
        && db.exec("UPDATE Quest_Objective SET type = 'STATUS', "
                   "description = 'Report to the bridge: check the ship status' "
                   "WHERE id = 3 AND description = 'Assign crew to bridge'");
}

int QuestRepository::playerIdOfShip(int shipId)
{
    QSqlQuery q = DatabaseManager::instance().query("SELECT player_id FROM Ship WHERE id = :ship");
    q.bindValue(":ship", shipId);
    return q.exec() && q.next() && !q.value(0).isNull() ? q.value(0).toInt() : -1;
}

QList<QuestData> QuestRepository::load(int playerId)
{
    DatabaseManager &db = DatabaseManager::instance();
    QList<QuestData> quests;

    QSqlQuery create = db.query(
        "INSERT OR IGNORE INTO Player_Quest (player_id, quest_id, status) "
        "SELECT :player, id, 'LOCKED' FROM Quest WHERE mode = 'CAMPAIGN'");
    create.bindValue(":player", playerId);
    create.exec();

    QSqlQuery q = db.query(
        "SELECT q.id, pq.id, q.title, q.description, COALESCE(q.prerequisite_quest_id, 0), pq.status "
        "FROM Quest q JOIN Player_Quest pq ON pq.quest_id = q.id AND pq.player_id = :player "
        "WHERE q.mode = 'CAMPAIGN' ORDER BY q.order_index, q.id");
    q.bindValue(":player", playerId);
    if (!q.exec()) {
        qWarning() << "[DB] QuestRepository::load:" << q.lastError().text();
        return quests;
    }
    while (q.next()) {
        QuestData quest;
        quest.id = q.value(0).toInt();
        quest.playerQuestId = q.value(1).toInt();
        quest.title = q.value(2).toString();
        quest.description = q.value(3).toString();
        quest.prerequisite = q.value(4).toInt();
        quest.status = q.value(5).toString();
        quests << quest;
    }

    for (QuestData &quest : quests) {
        QSqlQuery o = db.query(
            "SELECT o.id, o.description, o.type, COALESCE(c.name, r.name, ''), o.target_quantity, "
            "       COALESCE(p.current_quantity, 0), COALESCE(p.is_completed, 0) "
            "FROM Quest_Objective o "
            "LEFT JOIN Component c ON o.type = 'REPAIR' AND c.id = o.target_id "
            "LEFT JOIN Resource r ON o.type = 'COLLECT' AND r.id = o.target_id "
            "LEFT JOIN Player_Quest_Objective p ON p.objective_id = o.id AND p.player_quest_id = :pq "
            "WHERE o.quest_id = :quest ORDER BY o.id");
        o.bindValue(":pq", quest.playerQuestId);
        o.bindValue(":quest", quest.id);
        if (o.exec()) {
            while (o.next())
                quest.objectives << QuestObjective{o.value(0).toInt(), o.value(1).toString(), o.value(2).toString(),
                                                   o.value(3).toString(), o.value(4).toInt(), o.value(5).toInt(),
                                                   o.value(6).toBool()};
        }

        QSqlQuery r = db.query(
            "SELECT r.name, qr.quantity FROM Quest_Reward qr JOIN Resource r ON r.id = qr.resource_id "
            "WHERE qr.quest_id = :quest ORDER BY r.id");
        r.bindValue(":quest", quest.id);
        if (r.exec())
            while (r.next())
                quest.rewards.append({r.value(0).toString(), r.value(1).toInt()});
    }
    return quests;
}

bool QuestRepository::setStatus(int playerQuestId, const QString &status, double gameHours)
{
    const QString column = status == "COMPLETED" ? "completed_at" : "started_at";
    QSqlQuery q = DatabaseManager::instance().query(
        QStringLiteral("UPDATE Player_Quest SET status = :status, %1 = :time WHERE id = :id").arg(column));
    q.bindValue(":status", status);
    q.bindValue(":time", qRound64(gameHours * 60.0));     // minutes, like Session.game_time
    q.bindValue(":id", playerQuestId);
    return q.exec();
}

bool QuestRepository::setProgress(int playerQuestId, const QuestObjective &objective)
{
    QSqlQuery q = DatabaseManager::instance().query(
        "INSERT INTO Player_Quest_Objective (player_quest_id, objective_id, current_quantity, is_completed) "
        "VALUES (:pq, :objective, :qty, :done) "
        "ON CONFLICT (player_quest_id, objective_id) DO UPDATE SET "
        "current_quantity = excluded.current_quantity, is_completed = excluded.is_completed");
    q.bindValue(":pq", playerQuestId);
    q.bindValue(":objective", objective.id);
    q.bindValue(":qty", objective.current);
    q.bindValue(":done", objective.done);
    if (!q.exec()) {
        qWarning() << "[DB] QuestRepository::setProgress:" << q.lastError().text();
        return false;
    }
    return true;
}
