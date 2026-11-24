#include "saverepository.h"

#include <QStringList>
#include <QVariant>
#include <QtMath>

#include "../databasemanager.h"

bool SaveRepository::ensureSchema()
{
    return DatabaseManager::instance().exec(
        "CREATE TABLE IF NOT EXISTS Session_State ("
        "  session_id     INTEGER PRIMARY KEY REFERENCES Session(id),"
        "  route_index    INTEGER NOT NULL DEFAULT 0,"
        "  crew           INTEGER NOT NULL DEFAULT 80,"
        "  power          REAL    NOT NULL DEFAULT 0.6,"
        "  ftl_charge     REAL    NOT NULL DEFAULT 1.0,"
        "  gate_open      BOOLEAN NOT NULL DEFAULT 1,"
        "  gate_remaining REAL    NOT NULL DEFAULT 0,"
        "  gate_total     REAL    NOT NULL DEFAULT 0,"
        "  team_planet    INTEGER NOT NULL DEFAULT -1,"
        "  collected      TEXT    NOT NULL DEFAULT '',"   // explored planets, e.g. "0,2"
        "  hull           REAL    NOT NULL DEFAULT 100,"
        "  shield         REAL    NOT NULL DEFAULT 100,"
        "  shield_on      BOOLEAN NOT NULL DEFAULT 1,"
        "  updated_at     DATETIME DEFAULT CURRENT_TIMESTAMP"
        ")");
}

int SaveRepository::autosaveSession(int shipId)
{
    DatabaseManager &db = DatabaseManager::instance();

    QSqlQuery player = db.query("SELECT player_id FROM Ship WHERE id = :ship");
    player.bindValue(":ship", shipId);
    if (!player.exec() || !player.next() || player.value(0).isNull())
        return -1;
    const int playerId = player.value(0).toInt();

    QSqlQuery slot = db.query(
        "INSERT INTO Save_Slot (player_id, slot_number, name) VALUES (:player, 1, 'Autosave') "
        "ON CONFLICT (player_id, slot_number) DO NOTHING");
    slot.bindValue(":player", playerId);
    slot.exec();

    QSqlQuery session = db.query(
        "SELECT se.id FROM Session se JOIN Save_Slot sl ON sl.id = se.save_slot_id "
        "WHERE sl.player_id = :player AND sl.slot_number = 1 ORDER BY se.id DESC LIMIT 1");
    session.bindValue(":player", playerId);
    if (session.exec() && session.next())
        return session.value(0).toInt();

    QSqlQuery create = db.query(
        "INSERT INTO Session (save_slot_id) "
        "SELECT id FROM Save_Slot WHERE player_id = :player AND slot_number = 1");
    create.bindValue(":player", playerId);
    if (!create.exec()) {
        qWarning() << "[DB] Cannot create session:" << create.lastError().text();
        return -1;
    }
    return create.lastInsertId().toInt();
}

std::optional<GameSave> SaveRepository::load(int sessionId)
{
    QSqlQuery q = DatabaseManager::instance().query(
        "SELECT st.route_index, se.game_time, st.crew, st.power, st.ftl_charge, st.gate_open, "
        "       st.gate_remaining, st.gate_total, st.team_planet, st.collected, st.hull, st.shield, st.shield_on "
        "FROM Session_State st JOIN Session se ON se.id = st.session_id "
        "WHERE st.session_id = :session");
    q.bindValue(":session", sessionId);
    if (!q.exec() || !q.next())
        return std::nullopt;

    GameSave s;
    s.routeIndex = q.value(0).toInt();
    s.hours = q.value(1).toDouble() / 60.0;          // game_time is stored in minutes
    s.crew = q.value(2).toInt();
    s.power = q.value(3).toDouble();
    s.ftlCharge = q.value(4).toDouble();
    s.gateOpen = q.value(5).toBool();
    s.gateRemaining = q.value(6).toDouble();
    s.gateTotal = q.value(7).toDouble();
    s.teamPlanet = q.value(8).toInt();
    for (const QString &v : q.value(9).toString().split(',', Qt::SkipEmptyParts))
        s.collected << v.toInt();
    s.hull = q.value(10).toFloat();
    s.shield = q.value(11).toFloat();
    s.shieldOn = q.value(12).toBool();
    return s;
}

bool SaveRepository::save(int sessionId, const GameSave &s, const QString &galaxyName)
{
    DatabaseManager &db = DatabaseManager::instance();
    db.beginTransaction();

    QSqlQuery session = db.query(
        "UPDATE Session SET game_time = :time, "
        "current_galaxy_id = (SELECT id FROM Galaxy WHERE name = :galaxy) WHERE id = :session");
    session.bindValue(":time", qRound64(s.hours * 60.0));
    session.bindValue(":galaxy", galaxyName);
    session.bindValue(":session", sessionId);

    QStringList collected;
    for (int v : s.collected)
        collected << QString::number(v);

    QSqlQuery state = db.query(
        "INSERT INTO Session_State (session_id, route_index, crew, power, ftl_charge, gate_open, "
        "  gate_remaining, gate_total, team_planet, collected, hull, shield, shield_on, updated_at) "
        "VALUES (:session, :index, :crew, :power, :ftl, :gateOpen, :gateRemaining, :gateTotal, "
        "  :team, :collected, :hull, :shield, :shieldOn, CURRENT_TIMESTAMP) "
        "ON CONFLICT (session_id) DO UPDATE SET route_index = excluded.route_index, crew = excluded.crew, "
        "  power = excluded.power, ftl_charge = excluded.ftl_charge, gate_open = excluded.gate_open, "
        "  gate_remaining = excluded.gate_remaining, gate_total = excluded.gate_total, "
        "  team_planet = excluded.team_planet, collected = excluded.collected, hull = excluded.hull, "
        "  shield = excluded.shield, shield_on = excluded.shield_on, updated_at = CURRENT_TIMESTAMP");
    state.bindValue(":session", sessionId);
    state.bindValue(":index", s.routeIndex);
    state.bindValue(":crew", s.crew);
    state.bindValue(":power", s.power);
    state.bindValue(":ftl", s.ftlCharge);
    state.bindValue(":gateOpen", s.gateOpen);
    state.bindValue(":gateRemaining", s.gateRemaining);
    state.bindValue(":gateTotal", s.gateTotal);
    state.bindValue(":team", s.teamPlanet);
    // An empty QStringList joins to a null QString, which SQLite stores as NULL
    state.bindValue(":collected", collected.isEmpty() ? QStringLiteral("") : collected.join(','));
    state.bindValue(":hull", double(s.hull));
    state.bindValue(":shield", double(s.shield));
    state.bindValue(":shieldOn", s.shieldOn);

    QSqlQuery slot = db.query(
        "UPDATE Save_Slot SET last_saved_at = CURRENT_TIMESTAMP "
        "WHERE id = (SELECT save_slot_id FROM Session WHERE id = :session)");
    slot.bindValue(":session", sessionId);

    if (!session.exec() || !state.exec() || !slot.exec()) {
        qWarning() << "[DB] Save failed:" << session.lastError().text() << state.lastError().text()
                   << slot.lastError().text();
        db.rollback();
        return false;
    }
    db.commit();
    return true;
}

bool SaveRepository::reset(int sessionId)
{
    QSqlQuery q = DatabaseManager::instance().query("DELETE FROM Session_State WHERE session_id = :session");
    q.bindValue(":session", sessionId);
    QSqlQuery time = DatabaseManager::instance().query("UPDATE Session SET game_time = 0 WHERE id = :session");
    time.bindValue(":session", sessionId);
    return q.exec() && time.exec();
}
