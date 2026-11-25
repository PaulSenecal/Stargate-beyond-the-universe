#include "campaignlife.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QSettings>
#include <algorithm>

namespace {

struct ChoiceDef {
    const char *id;
    const char *title;
    const char *text;
    QList<const char *> options;
};

const QList<ChoiceDef> &choiceDefs()
{
    static const QList<ChoiceDef> defs{
        {"distress", QT_TRANSLATE_NOOP("CampaignLife", "Distress signal"),
         QT_TRANSLATE_NOOP("CampaignLife", "A faint distress call comes from a damaged ship nearby. It could be survivors... or bait."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Send a rescue team"), QT_TRANSLATE_NOOP("CampaignLife", "Ignore it")}},
        {"sickness", QT_TRANSLATE_NOOP("CampaignLife", "Sick crew"),
         QT_TRANSLATE_NOOP("CampaignLife", "Several crew members show the symptoms of an alien virus picked up on the last planet."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Quarantine them (they stop working for a while)"),
          QT_TRANSLATE_NOOP("CampaignLife", "Treat them with our supplies (water, food)"),
          QT_TRANSLATE_NOOP("CampaignLife", "Do nothing")}},
        {"probe", QT_TRANSLATE_NOOP("CampaignLife", "Ancient probe"),
         QT_TRANSLATE_NOOP("CampaignLife", "An Ancient probe drifts along the Destiny. Its database is intact."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Download its database"), QT_TRANSLATE_NOOP("CampaignLife", "Take it apart for parts"),
          QT_TRANSLATE_NOOP("CampaignLife", "Leave it")}},
        {"derelict", QT_TRANSLATE_NOOP("CampaignLife", "Derelict ship"),
         QT_TRANSLATE_NOOP("CampaignLife", "A derelict alien hulk floats in the system. Its reactor still shows some activity."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Salvage it with a full team"), QT_TRANSLATE_NOOP("CampaignLife", "Send a Kino first"),
          QT_TRANSLATE_NOOP("CampaignLife", "Keep away")}},
        {"dispute", QT_TRANSLATE_NOOP("CampaignLife", "Crew dispute"),
         QT_TRANSLATE_NOOP("CampaignLife", "Civilians and military argue over the rations and the command of the ship."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Side with Colonel Young"), QT_TRANSLATE_NOOP("CampaignLife", "Side with Camile Wray"),
          QT_TRANSLATE_NOOP("CampaignLife", "Hold a council and share the rations")}},
        {"stowaway", QT_TRANSLATE_NOOP("CampaignLife", "Stowaway"),
         QT_TRANSLATE_NOOP("CampaignLife", "Motion sensors detect someone hiding in the lower decks. A Lucian Alliance soldier?"),
         {QT_TRANSLATE_NOOP("CampaignLife", "Send Greer and a team"), QT_TRANSLATE_NOOP("CampaignLife", "Seal the lower decks and wait")}},
        {"nebula", QT_TRANSLATE_NOOP("CampaignLife", "Nebula shortcut"),
         QT_TRANSLATE_NOOP("CampaignLife", "Rush thinks a nebula could recharge the FTL drive faster. The hull would suffer."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Go through the nebula"), QT_TRANSLATE_NOOP("CampaignLife", "Stay on course")}},
        {"seedvault", QT_TRANSLATE_NOOP("CampaignLife", "Seed vault"),
         QT_TRANSLATE_NOOP("CampaignLife", "A planet of the system has an old Ancient seed vault in orbit."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Send the Puddle Jumper (naquadah)"), QT_TRANSLATE_NOOP("CampaignLife", "Not worth the fuel")}},
        {"mutiny", QT_TRANSLATE_NOOP("CampaignLife", "Unrest"),
         QT_TRANSLATE_NOOP("CampaignLife", "The crew is exhausted. Some want to stop and settle on the next habitable planet."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Promise a day of rest"), QT_TRANSLATE_NOOP("CampaignLife", "Remind them of the mission")}},
        {"signal", QT_TRANSLATE_NOOP("CampaignLife", "The Destiny's signal"),
         QT_TRANSLATE_NOOP("CampaignLife", "Eli finds a pattern in the background radiation, the one the Destiny was built to follow."),
         {QT_TRANSLATE_NOOP("CampaignLife", "Let Eli and Rush study it"), QT_TRANSLATE_NOOP("CampaignLife", "Keep them on repairs")}},
    };
    return defs;
}

QString t(const char *text)
{
    return QCoreApplication::translate("CampaignLife", text);
}

} // namespace

CampaignLife::CampaignLife()
{
    QSettings settings("Stargate", "stargate");
    m_unlocked = settings.value("achievements/unlocked").toStringList();
}

QStringList CampaignLife::choiceIds()
{
    QStringList ids;
    for (const ChoiceDef &d : choiceDefs())
        ids << d.id;
    return ids;
}

void CampaignLife::addCustomChoice(const QString &id, const QString &title, const QString &text,
                                   const QStringList &options, const QList<Outcome> &outcomes)
{
    Custom custom;
    custom.choice = {id, title, text, options};
    custom.outcomes = outcomes;
    m_custom.erase(std::remove_if(m_custom.begin(), m_custom.end(), [&id](const Custom &c) { return c.choice.id == id; }),
                   m_custom.end());
    m_custom << custom;
}

bool CampaignLife::startChoice(const QString &id)
{
    for (const Custom &custom : m_custom)
        if (custom.choice.id == id) {
            m_pending = custom.choice;
            return true;
        }
    for (const ChoiceDef &d : choiceDefs())
        if (d.id == id) {
            m_pending.id = d.id;
            m_pending.title = t(d.title);
            m_pending.text = t(d.text);
            m_pending.options.clear();
            for (const char *o : d.options)
                m_pending.options << t(o);
            return true;
        }
    return false;
}

bool CampaignLife::rollChoice(double chance)
{
    if (!m_pending.id.isEmpty() || m_rng.generateDouble() >= chance)
        return false;
    const QList<ChoiceDef> &defs = choiceDefs();
    const int pick = int(m_rng.bounded(int(defs.size() + m_custom.size())));
    return startChoice(pick < defs.size() ? QString::fromLatin1(defs[pick].id) : m_custom[pick - defs.size()].choice.id);
}

bool CampaignLife::choose(int index, Outcome *outcome)
{
    if (m_pending.id.isEmpty() || index < 0 || index >= m_pending.options.size())
        return false;
    const Outcome result = resolve(m_pending.id, index);
    m_pending = Choice();
    addStat("choices");
    if (outcome)
        *outcome = result;
    return true;
}

Outcome CampaignLife::expire()
{
    Outcome outcome;
    if (m_pending.id.isEmpty())
        return outcome;
    // Without orders the crew takes the cautious way: the last option
    choose(int(m_pending.options.size()) - 1, &outcome);
    outcome.text = QCoreApplication::translate("CampaignLife", "No order given - ") + outcome.text;
    return outcome;
}

// Consequences: partly random, so that a choice is a real gamble
Outcome CampaignLife::resolve(const QString &id, int option)
{
    for (const Custom &custom : m_custom)
        if (custom.choice.id == id && option < custom.outcomes.size())
            return custom.outcomes[option];
    Outcome o;
    const float roll = rand01();
    if (id == "distress") {
        if (option == 0 && roll < 0.55f) {
            o.text = QCoreApplication::translate("CampaignLife", "Survivors found! They join the crew and bring their stores.");
            o.tone = 1;
            o.crew = 2 + int(m_rng.bounded(3));
            o.resources = {{"food", 30}, {"naquadah", 4}};
            o.morale = 6;
            if (rand01() < 0.4f)
                o.blueprint = true;
        } else if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "It was a trap: hostile ships drop out of hyperspace!");
            o.tone = 3;
            o.hostiles = "*";
            o.injuries = 1;
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The signal fades behind the Destiny. Nobody talks about it at dinner.");
            o.morale = -5;
        }
    } else if (id == "sickness") {
        if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "The quarantine holds. The sick ones are out of duty for a while, the virus does not spread.");
            o.injuries = 2;
            o.morale = -2;
        } else if (option == 1) {
            o.text = QCoreApplication::translate("CampaignLife", "TJ treats them with our supplies. Everybody recovers.");
            o.tone = 1;
            o.resources = {{"water", -25}, {"food", -15}};
            o.morale = 3;
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The virus spreads through the ship.");
            o.tone = 3;
            o.injuries = 4;
            o.crew = roll < 0.3f ? -1 : 0;
            o.morale = -8;
        }
    } else if (id == "probe") {
        if (option == 0 && roll < 0.7f) {
            o.text = QCoreApplication::translate("CampaignLife", "The database holds the plans of an Ancient device.");
            o.tone = 1;
            o.blueprint = true;
        } else if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "A computer virus spreads through the Destiny's systems: power drops.");
            o.tone = 2;
            o.power = -0.25;
        } else if (option == 1) {
            o.text = QCoreApplication::translate("CampaignLife", "The probe is taken apart: crystals and alloy.");
            o.tone = 1;
            o.resources = {{"crystal", 2}, {"ancient_alloy", 1}};
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The probe drifts away.");
        }
    } else if (id == "derelict") {
        if (option == 0 && roll < 0.6f) {
            o.text = QCoreApplication::translate("CampaignLife", "The team brings back plates, naquadah and missiles.");
            o.tone = 1;
            o.resources = {{"minerals", 10}, {"naquadah", 6}, {"trinium", 2}};
            o.missiles = 2;
        } else if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "The reactor explodes as the team leaves: injured people and a fire aboard.");
            o.tone = 3;
            o.injuries = 2;
            o.fire = true;
            o.resources = {{"minerals", 4}};
        } else if (option == 1) {
            o.text = roll < 0.8f ? QCoreApplication::translate("CampaignLife", "The Kino maps a safe path: the team salvages what it can without risk.")
                                 : QCoreApplication::translate("CampaignLife", "The Kino is lost in the reactor room, the team turns back.");
            o.tone = roll < 0.8f ? 1 : 2;
            if (roll < 0.8f)
                o.resources = {{"minerals", 6}, {"naquadah", 3}};
            else
                o.kinos = -1;
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The Destiny keeps its distance.");
        }
    } else if (id == "dispute") {
        if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "Young restores order. The military are pleased, the civilians grumble.");
            o.morale = -3;
        } else if (option == 1) {
            o.text = QCoreApplication::translate("CampaignLife", "Wray takes charge of the rations. The soldiers are unhappy.");
            o.morale = -3;
            o.resources = {{"food", 10}};
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The council calms everyone down. The shared meal does good.");
            o.tone = 1;
            o.morale = 8;
            o.resources = {{"food", -10}};
        }
    } else if (id == "stowaway") {
        if (option == 0 && roll < 0.7f) {
            o.text = QCoreApplication::translate("CampaignLife", "Greer catches a Lucian Alliance soldier. He talks: he knows where the Alliance keeps its supplies.");
            o.tone = 1;
            o.resources = {{"naquadah", 5}};
            o.reveal = QCoreApplication::translate("CampaignLife", "The Alliance keeps missiles on mineral worlds.");
            o.missiles = 2;
        } else if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "He was not alone: a firefight breaks out in the lower decks!");
            o.tone = 3;
            o.boarders = 3;
            o.injuries = 1;
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The sealed decks starve him out. He surrenders after a day.");
            o.morale = -1;
        }
    } else if (id == "nebula") {
        if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "The collectors drink the nebula's energy. The hull suffers from the friction.");
            o.tone = 1;
            o.ftl = 0.6;
            o.power = 0.2;
            o.hullDamage = 10.0f + rand01() * 10.0f;
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The Destiny stays on course.");
        }
    } else if (id == "seedvault") {
        if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "The vault is full of seeds: hydroponics will love them.");
            o.tone = 1;
            o.resources = {{"naquadah", -2}, {"food", 40}};
            o.morale = 3;
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The vault stays where it is.");
        }
    } else if (id == "mutiny") {
        if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "A day of rest. Everyone is grateful.");
            o.tone = 1;
            o.morale = 12;
            o.ftl = -0.1;
        } else if (roll < 0.5f) {
            o.text = QCoreApplication::translate("CampaignLife", "The speech works: the crew stays focused on the mission.");
            o.morale = 2;
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "Some refuse to work. Tension aboard.");
            o.tone = 2;
            o.morale = -10;
        }
    } else if (id == "signal") {
        if (option == 0) {
            o.text = QCoreApplication::translate("CampaignLife", "Eli and Rush decode part of the signal: the Destiny's mission becomes clearer. Everyone feels it.");
            o.tone = 1;
            o.morale = 6;
            o.reveal = QCoreApplication::translate("CampaignLife", "The signal comes from the far end of the route: the Destiny's purpose lies there.");
            addStat("signal");
        } else {
            o.text = QCoreApplication::translate("CampaignLife", "The repairs move on. The pattern is forgotten for now.");
        }
    }
    return o;
}

// ================================================================
//  Communication stones
// ================================================================

Outcome CampaignLife::useStones(double hours)
{
    Outcome o;
    if (!stonesReady(hours)) {
        o.text = QCoreApplication::translate("CampaignLife", "The stones are not ready yet");
        o.tone = 2;
        return o;
    }
    m_stonesUsedAt = hours;
    addStat("stones");
    const int pick = int(m_rng.bounded(5));
    switch (pick) {
    case 0:
        o.text = QCoreApplication::translate("CampaignLife", "Earth: families send their news. Spirits rise aboard.");
        o.morale = 10;
        o.tone = 1;
        break;
    case 1:
        o.text = QCoreApplication::translate("CampaignLife", "Earth: scientists of the SGC send the plans of a device they adapted from Asgard technology.");
        o.blueprint = true;
        o.tone = 1;
        break;
    case 2:
        o.text = QCoreApplication::translate("CampaignLife", "Earth: an analysis of our route - the next systems should be safer. FTL drive tuned.");
        o.ftl = 0.25;
        o.tone = 1;
        break;
    case 3:
        o.text = QCoreApplication::translate("CampaignLife", "Earth: the IOA asks for a report. Nothing useful comes from it.");
        o.morale = -2;
        break;
    default:
        o.text = QCoreApplication::translate("CampaignLife", "Earth: a doctor helps TJ with the injured through the stones.");
        o.tone = 1;
        o.reveal = QStringLiteral("heal");
        break;
    }
    return o;
}

// ================================================================
//  Statistics and achievements
// ================================================================

const QList<CampaignLife::Achievement> &CampaignLife::achievements()
{
    static const QList<Achievement> list{
        {"first_blood", QT_TRANSLATE_NOOP("CampaignLife", "First blood"), QT_TRANSLATE_NOOP("CampaignLife", "Destroy an enemy ship"), "kills", 1},
        {"ace", QT_TRANSLATE_NOOP("CampaignLife", "Ace"), QT_TRANSLATE_NOOP("CampaignLife", "Destroy 50 enemy ships"), "kills", 50},
        {"giant_killer", QT_TRANSLATE_NOOP("CampaignLife", "Giant killer"), QT_TRANSLATE_NOOP("CampaignLife", "Destroy 10 motherships"), "motherships", 10},
        {"traveller", QT_TRANSLATE_NOOP("CampaignLife", "Traveller"), QT_TRANSLATE_NOOP("CampaignLife", "Make 10 FTL jumps"), "jumps", 10},
        {"voyager", QT_TRANSLATE_NOOP("CampaignLife", "Voyager"), QT_TRANSLATE_NOOP("CampaignLife", "Make 50 FTL jumps"), "jumps", 50},
        {"explorer", QT_TRANSLATE_NOOP("CampaignLife", "Explorer"), QT_TRANSLATE_NOOP("CampaignLife", "Explore 10 planets"), "explored", 10},
        {"firefighter", QT_TRANSLATE_NOOP("CampaignLife", "Firefighter"), QT_TRANSLATE_NOOP("CampaignLife", "Put out 10 fires"), "fires", 10},
        {"pirate", QT_TRANSLATE_NOOP("CampaignLife", "Pirate"), QT_TRANSLATE_NOOP("CampaignLife", "Capture an enemy ship by boarding"), "captures", 1},
        {"sharpshooter", QT_TRANSLATE_NOOP("CampaignLife", "Sharpshooter"), QT_TRANSLATE_NOOP("CampaignLife", "End a battle with 90% accuracy"), "bestAccuracy", 90},
        {"untouchable", QT_TRANSLATE_NOOP("CampaignLife", "Untouchable"), QT_TRANSLATE_NOOP("CampaignLife", "Win a battle without hull damage"), "flawless", 1},
        {"star_diver", QT_TRANSLATE_NOOP("CampaignLife", "Star diver"), QT_TRANSLATE_NOOP("CampaignLife", "Recharge the Destiny in a star"), "stars", 1},
        {"archaeologist", QT_TRANSLATE_NOOP("CampaignLife", "Archaeologist"), QT_TRANSLATE_NOOP("CampaignLife", "Open every closed section"), "unlocked", 6},
        {"diplomat", QT_TRANSLATE_NOOP("CampaignLife", "Hard choices"), QT_TRANSLATE_NOOP("CampaignLife", "Answer 10 events"), "choices", 10},
        {"homesick", QT_TRANSLATE_NOOP("CampaignLife", "Homesick"), QT_TRANSLATE_NOOP("CampaignLife", "Use the communication stones 5 times"), "stones", 5},
        {"purpose", QT_TRANSLATE_NOOP("CampaignLife", "Destiny's purpose"), QT_TRANSLATE_NOOP("CampaignLife", "Reach the end of the route"), "ending", 1},
        {"ironman", QT_TRANSLATE_NOOP("CampaignLife", "Iron will"), QT_TRANSLATE_NOOP("CampaignLife", "Make 25 jumps in Ironman mode"), "ironmanJumps", 25},
    };
    return list;
}

void CampaignLife::addStat(const QString &stat, double amount)
{
    m_stats[stat] = m_stats.value(stat, 0.0) + amount;
    checkAchievements();
}

void CampaignLife::setStatMax(const QString &stat, double value)
{
    if (value > m_stats.value(stat, 0.0)) {
        m_stats[stat] = value;
        checkAchievements();
    }
}

void CampaignLife::checkAchievements()
{
    bool changed = false;
    for (const Achievement &a : achievements())
        if (!m_unlocked.contains(a.id) && m_stats.value(a.stat, 0.0) >= a.threshold) {
            m_unlocked << a.id;
            m_fresh << a.id;
            changed = true;
        }
    if (changed && m_globalStore) {
        QSettings settings("Stargate", "stargate");
        QStringList all = settings.value("achievements/unlocked").toStringList();
        for (const QString &id : m_unlocked)
            if (!all.contains(id))
                all << id;
        settings.setValue("achievements/unlocked", all);
    }
}

QStringList CampaignLife::takeUnlocked()
{
    QStringList fresh;
    fresh.swap(m_fresh);
    return fresh;
}

// ================================================================
//  Save
// ================================================================

QJsonObject CampaignLife::toJson() const
{
    QJsonObject stats;
    for (auto it = m_stats.cbegin(); it != m_stats.cend(); ++it)
        stats.insert(it.key(), it.value());
    QJsonObject o{{"stats", stats}, {"stonesUsedAt", m_stonesUsedAt}};
    if (!m_pending.id.isEmpty())
        o.insert("pending", m_pending.id);
    return o;
}

void CampaignLife::fromJson(const QJsonObject &json)
{
    const QJsonObject stats = json.value("stats").toObject();
    m_stats.clear();
    for (auto it = stats.begin(); it != stats.end(); ++it)
        m_stats.insert(it.key(), it.value().toDouble());
    m_stonesUsedAt = json.value("stonesUsedAt").toDouble(-1000);
    m_pending = Choice();
    if (json.contains("pending"))
        startChoice(json.value("pending").toString());
}
