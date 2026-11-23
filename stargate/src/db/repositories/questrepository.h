#ifndef QUESTREPOSITORY_H
#define QUESTREPOSITORY_H

#include <QList>
#include <QString>
#include <utility>

// ================================================================
//  QuestRepository
//  Campaign quests (Quest, Quest_Objective, Quest_Reward) and the
//  player's progress (Player_Quest, Player_Quest_Objective).
//
//  Objective types used by the game:
//   REPAIR  target = Component id   ("repair <system>")
//   STATUS  check the ship status   ("destinystatus")
//   COLLECT target = Resource id, target_quantity units gained
//   EXPLORE send a team through the Stargate
//   SURVIVE bring the team back aboard
// ================================================================

struct QuestObjective
{
    int id = 0;
    QString description;
    QString type;
    QString target;          // component or resource name (REPAIR / COLLECT)
    int targetQuantity = 1;
    int current = 0;
    bool done = false;
};

struct QuestData
{
    int id = 0;
    int playerQuestId = 0;
    QString title;
    QString description;
    int prerequisite = 0;    // 0 = none
    QString status;          // LOCKED | ACTIVE | COMPLETED
    QList<QuestObjective> objectives;
    QList<std::pair<QString, int>> rewards;
};

class QuestRepository
{
public:
    // Links the original objectives to game actions (older saves too)
    static bool migrate();

    // Campaign quests in order, with the player's progress
    // (progress rows are created when missing)
    static QList<QuestData> load(int playerId);

    static int playerIdOfShip(int shipId);
    static bool setStatus(int playerQuestId, const QString &status, double gameHours);
    static bool setProgress(int playerQuestId, const QuestObjective &objective);
};

#endif // QUESTREPOSITORY_H
