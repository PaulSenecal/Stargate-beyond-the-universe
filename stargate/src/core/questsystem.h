#ifndef QUESTSYSTEM_H
#define QUESTSYSTEM_H

#include <QObject>
#include <QStringList>
#include <functional>
#include <tuple>
#include <utility>

#include "src/db/repositories/questrepository.h"

// ================================================================
//  QuestSystem
//  Campaign quests used as the game's tutorial. One quest is active
//  at a time; the next one unlocks when its prerequisite is
//  completed. The game reports what the player does through
//  notify(type, target, amount); progress is saved immediately.
//  Types: REPAIR (component), STATUS, COLLECT (resource, amount),
//  EXPLORE (team through the gate), SURVIVE (team back aboard).
// ================================================================

class QuestSystem : public QObject
{
    Q_OBJECT

public:
    explicit QuestSystem(QObject *parent = nullptr);

    // Game time (hours) stored when quests start / end
    void setClock(std::function<double()> clock) { m_clock = std::move(clock); }

    bool load(int playerId);
    bool isLoaded() const { return !m_quests.isEmpty(); }

    const QuestData *activeQuest() const;
    int completedCount() const;
    int questCount() const { return int(m_quests.size()); }

    void notify(const QString &type, const QString &target = QString(), int amount = 1);

    // Texts for the HUD and the console
    QStringList objectiveLines() const;     // empty when nothing is active
    QString hint() const;
    QStringList journal() const;

signals:
    void questStarted(const QString &title, const QString &description, bool firstQuest);
    void objectiveCompleted(const QString &description);
    void questCompleted(const QString &title, const QList<std::pair<QString, int>> &rewards);
    void campaignCompleted();

private:
    void process(const QString &type, const QString &target, int amount);
    void activateAvailable();
    double now() const { return m_clock ? m_clock() : 0.0; }

    QList<QuestData> m_quests;
    std::function<double()> m_clock;
    bool m_busy = false;
    QList<std::tuple<QString, QString, int>> m_pending;    // events received while busy
};

#endif // QUESTSYSTEM_H
