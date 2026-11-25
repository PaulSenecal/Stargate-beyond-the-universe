#ifndef CAMPAIGNLIFE_H
#define CAMPAIGNLIFE_H

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <utility>

// ================================================================
//  CampaignLife
//  What happens to the Destiny between battles, as pure logic:
//   - choice events after a jump (a distress signal, sick crew…): the
//     player answers 'choose <n>' and lives with the consequences;
//   - the communication stones (a link with Earth, once a day);
//   - statistics of the journey and achievements (kept across games).
//  The game menu applies the Outcome to the ship.
// ================================================================

struct Outcome {
    QString text;
    int tone = 0;                       // 0 info, 1 good, 2 warning, 3 critical
    QList<std::pair<QString, int>> resources;   // added (negative = removed)
    int crew = 0;
    float morale = 0;
    double power = 0, ftl = 0;
    float hullDamage = 0;
    int injuries = 0;                   // random crew members injured
    bool fire = false;                  // a fire breaks out aboard
    bool blueprint = false;             // a new module blueprint
    QString hostiles;                   // faction of an attack ("" = none, "*" = random)
    int boarders = 0;
    int drones = 0, missiles = 0, slugs = 0;
    int kinos = 0;
    QString reveal;                     // information given to the player
};

class CampaignLife
{
public:
    struct Choice {
        QString id;
        QString title;
        QString text;
        QStringList options;
    };

    struct Achievement {
        QString id;
        QString title;
        QString description;
        QString stat;                   // statistic that unlocks it
        double threshold = 1;
    };

    CampaignLife();
    void seed(quint32 seed) { m_rng.seed(seed); }

    // --- Choice events
    // Rolled after a jump: true when an event waits for an answer
    bool rollChoice(double chance = 0.45);
    bool startChoice(const QString &id);        // a given event (tests, story)
    const Choice *pendingChoice() const { return m_pending.id.isEmpty() ? nullptr : &m_pending; }
    // Applies option `index` (0-based); invalid index = nothing
    bool choose(int index, Outcome *outcome);
    // No answer in time: the first option… or the worst
    Outcome expire();
    static QStringList choiceIds();
    // Events of the mods (data only): one fixed outcome per option
    void addCustomChoice(const QString &id, const QString &title, const QString &text, const QStringList &options,
                         const QList<Outcome> &outcomes);

    // --- Communication stones: once every 24 game hours
    bool stonesReady(double hours) const { return hours - m_stonesUsedAt >= 24.0; }
    double stonesCooldown(double hours) const { return std::max(0.0, 24.0 - (hours - m_stonesUsedAt)); }
    Outcome useStones(double hours);

    // --- Statistics and achievements
    void addStat(const QString &stat, double amount = 1.0);
    void setStatMax(const QString &stat, double value);
    double stat(const QString &stat) const { return m_stats.value(stat, 0.0); }
    const QMap<QString, double> &stats() const { return m_stats; }
    // Achievements unlocked since the last call (to announce them)
    QStringList takeUnlocked();
    bool unlocked(const QString &id) const { return m_unlocked.contains(id); }
    static const QList<Achievement> &achievements();
    // Unlocked achievements are also remembered across games (QSettings)
    void setGlobalStore(bool on) { m_globalStore = on; }

    // --- Save
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);

private:
    Outcome resolve(const QString &id, int option);
    void checkAchievements();
    float rand01() { return float(m_rng.generateDouble()); }

    struct Custom {
        Choice choice;
        QList<Outcome> outcomes;
    };
    QList<Custom> m_custom;
    Choice m_pending;
    double m_stonesUsedAt = -1000;
    QMap<QString, double> m_stats;
    QStringList m_unlocked;
    QStringList m_fresh;
    bool m_globalStore = true;
    QRandomGenerator m_rng{quint32(QRandomGenerator::global()->generate())};
};

#endif // CAMPAIGNLIFE_H
