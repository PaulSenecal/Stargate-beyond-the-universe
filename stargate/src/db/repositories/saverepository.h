#ifndef SAVEREPOSITORY_H
#define SAVEREPOSITORY_H

#include <QJsonObject>
#include <QList>
#include <optional>

// ================================================================
//  SaveRepository
//  Progress of the current game: Save_Slot (slot 1 = autosave) ->
//  Session (galaxy, game time) -> Session_State (route position,
//  crew, power, FTL, gate window, hull, shield).
//  Session_State was added after the first saves: ensureSchema()
//  creates it in older databases.
// ================================================================

struct GameSave
{
    int routeIndex = 0;
    double hours = 0;           // total game time
    int crew = 80;
    double power = 0.6;
    double ftlCharge = 1.0;
    bool gateOpen = true;
    double gateRemaining = 0;
    double gateTotal = 0;
    int teamPlanet = -1;
    QList<int> collected;       // planets already explored at this stop
    float hull = 100;
    float shield = 100;
    bool shieldOn = true;
    // Everything added later (supplies, crew, compartments, statistics…),
    // stored as JSON in Session_State.extra
    QJsonObject extra;
};

class SaveRepository
{
public:
    static bool ensureSchema();

    // Session of the autosave slot of the player owning `shipId`
    // (created on first call). Returns -1 on error.
    static int autosaveSession(int shipId);

    static std::optional<GameSave> load(int sessionId);
    static bool save(int sessionId, const GameSave &save, const QString &galaxyName);

    // Forgets the progress (new game on the same ship)
    static bool reset(int sessionId);
};

#endif // SAVEREPOSITORY_H
