#ifndef INTERIOR_H
#define INTERIOR_H

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRandomGenerator>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <utility>

// ================================================================
//  Interior
//  The inside of the Destiny: compartments and the crew working in them.
//  Pure game logic (no widget), driven by tick(seconds), saved as JSON.
//
//  Compartments sit on the ship plan in ship-local coordinates (x along
//  the ship, z sideways, like the 3D model). A hit on the hull damages
//  the compartment under it: fires start, breaches let the air out,
//  power conduits leak. Fires grow with air and spread through open
//  doors; sealing a compartment stops the spread, venting it to space
//  kills the fire (and hurts whoever is inside). Life support refills
//  the air of every compartment that is not breached nor vented.
//
//  Some sections are closed at the start: repairing them (resources +
//  crew work) unlocks their capability (hydroponics: food, archives:
//  blueprints…). Unlocked module bays receive modules built from found
//  blueprints (limited bays).
//
//  The crew: key characters with a bonus (Rush: energy, Eli: Ancient
//  systems, Young: combat, Greer: boarding, TJ: medicine…) and generic
//  repair teams drawn from the crew count. Every repair is a job that
//  takes time; jobs run in parallel only as long as there are free
//  workers. Working tires people (fatigue), rest restores them; injured
//  people heal in the infirmary and lose their bonus meanwhile. Morale
//  speeds up or slows down all the work.
//
//  Boarding parties can land in a compartment: 'defend' sends the
//  soldiers; the crew can also board a crippled enemy ship.
// ================================================================

namespace InteriorDef {

enum class Role { Military, Scientist, Engineer, Medic, Pilot, Civilian };

struct Module {
    QString id;                 // "asgard_shield"…
    QString name;
    QString effect;             // shown to the player
    QList<std::pair<QString, int>> cost;
    float work = 40;            // seconds of work for one worker of skill 1
};
const QList<Module> &modules();
const Module *module(const QString &id);

} // namespace InteriorDef

class Interior
{
public:
    struct Compartment {
        QString id;                 // "bridge"
        QString name;               // "Bridge" (translated at display)
        QRectF rect;                // ship-local (x, z)
        QString system;             // BattleView system key hosted here ("" = none)
        QString capability;         // command, gate, infirmary, food, water, archives, armory, modules…
        bool locked = false;
        QList<std::pair<QString, int>> unlockCost;
        float unlockWork = 0;
        int bays = 0;              // module bays
        QStringList modules;        // installed module ids
        float air = 100;            // 0..100
        float fire = 0;             // 0..1 intensity
        float integrity = 100;      // structure
        bool breach = false;        // hole in the hull: air escapes
        bool leak = false;          // power conduit leaking
        bool sealed = false;        // doors closed
        bool vented = false;        // opened to space
        int boarders = 0;           // enemy boarding party
        float boarderTimer = 0;     // seconds before they move on
        QList<int> neighbours;      // indexes of adjacent compartments
    };

    struct Person {
        QString id;                 // "rush"
        QString name;               // "Nicholas Rush"
        InteriorDef::Role role = InteriorDef::Role::Civilian;
        QString bonus;              // energy, ancient, combat, boarding, medicine, pilot, morale, supplies
        float skill = 1;            // work speed
        float fatigue = 0;          // 0..100
        float injury = 0;           // seconds left to heal (0 = healthy)
        bool resting = false;
        int job = -1;               // repair job id
        QString location = "quarters";
        bool generic = false;       // a repair team of the crew, not a named character
    };

    struct Job {
        int id = 0;
        QString kind;               // system, hull, fire, breach, leak, integrity, unlock, install
        QString target;             // system key, compartment id, module id
        QString compartment;
        float work = 0, total = 0;  // seconds of work for skill 1
        QList<int> workers;         // indexes in people()
        int wanted = 1;             // workers it can use at most
    };

    struct Message {
        QString text;
        int tone = 0;               // 0 info, 1 success, 2 warning, 3 critical
    };

    struct Effects {
        QList<std::pair<QString, float>> systemDamage;  // system key, percent lost
        QList<std::pair<QString, float>> systemRepair;  // system key, percent gained
        float hullRepair = 0;
        int crewLost = 0;           // generic crew members killed
        int firesOut = 0;           // fires put out by the crew
        float food = 0, water = 0;  // produced (units)
    };

    // style: "" = the Destiny (campaign), "flagship" = the player's ship in
    // the conquest, "enemy" = an AI ship (no named crew). faction gives the
    // plan and the crew: Tau'ri (BC-304), Goa'uld / Lucian Alliance (Ha'tak),
    // Wraith (hive), Asgard (O'Neill), Ancients (Aurora), Ori.
    explicit Interior(const QString &style = QString(), const QString &faction = QString());
    QString style() const { return m_style; }
    QString faction() const { return m_faction; }
    // How a team can go aboard another ship from this one
    bool hasRings() const { return hasCapability("rings"); }
    bool hasBeam() const { return hasCapability("beam"); }
    bool hasGate() const { return hasCapability("gate"); }

    // --- Layout and crew
    const QList<Compartment> &compartments() const { return m_compartments; }
    const QList<Person> &people() const { return m_people; }
    const QList<Job> &jobs() const { return m_jobs; }
    int find(const QString &nameOrId) const;            // compartment index, -1
    int findPerson(const QString &name) const;
    int compartmentOfSystem(const QString &system) const;
    int compartmentAt(const QPointF &local) const;      // nearest one
    QRectF bounds() const;
    // Generic repair teams follow the crew count (1 team per 15 people)
    void setCrewCount(int crew);
    int crewCount() const { return m_crewCount; }
    void seed(quint32 seed) { m_rng.seed(seed); }

    // --- Damage
    // A hit on the hull at a ship-local point (x, z)
    void damageAt(const QPointF &local, float damage);
    void startFire(int compartment, float intensity = 0.25f);

    // --- Orders. On failure `error` explains why.
    // repair <system|hull|compartment>: creates a job (resources are paid
    // by the caller, see repairCost)
    int orderRepair(const QString &what, QString *error);
    bool setSealed(const QString &compartment, bool sealed, QString *error);
    bool vent(const QString &compartment, QString *error);
    int orderUnlock(const QString &compartment, QString *error);
    int orderInstall(const QString &moduleId, const QString &compartment, QString *error);
    bool rest(const QString &person, QString *error);
    int rotate();                                       // tired people go to rest
    bool cancelJob(int id);
    // Soldiers fight the boarders in a compartment
    bool defend(const QString &compartment, QString *error);
    // A boarding party lands in a random compartment
    void board(int strength);
    // Chance that a team boarding an enemy ship takes it (0..1)
    float boardingChance() const;
    // Result of a boarding attempt: the team fights; failure injures it
    bool boardEnemy();

    // --- Blueprints (modules that can be built)
    QStringList blueprints() const { return m_blueprints; }
    bool learnBlueprint(const QString &moduleId);       // false if already known / unknown
    QString randomUnknownBlueprint();                   // "" if all known

    // --- Effects on the ship
    float morale() const { return m_morale; }
    void changeMorale(float delta);
    // Random crew members hurt (events); returns how many
    int injureRandom(int count, float seconds);
    // Medical help: every injury heals by `seconds`
    void heal(float seconds);
    // In orbit of a friendly world: dock crews fix the rooms little by
    // little (fires, breaches, leaks, structure) and the injured heal
    void dockRepair(float seconds);
    bool hasCapability(const QString &capability) const;   // unlocked and usable
    bool hasModule(const QString &moduleId) const;
    bool bonusActive(const QString &bonus) const;          // key character fit for duty
    int powerLoss() const;                                 // leaking conduits
    float workSpeed() const;                               // morale factor
    int freeWorkers() const;
    int boarders() const;

    // --- Simulation: `seconds` of game time
    Effects tick(float seconds, float lifeSupport, bool inBattle);
    QList<Message> takeMessages();

    // --- Save
    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);

private:
    void buildShip();
    void buildCrew();
    void say(const QString &text, int tone);
    int addJob(Job job);
    void assignWorkers();
    void finishJob(const Job &job, Effects &effects);
    float personSpeed(const Person &p, const Job &job) const;
    void injure(int person, float seconds);
    void injureIn(int compartment, float chance);
    float rand01() { return float(m_rng.generateDouble()); }

    QString m_style;
    QString m_faction;
    QList<Compartment> m_compartments;
    QList<Person> m_people;
    QList<Job> m_jobs;
    QStringList m_blueprints;
    QStringList m_defenders;                // compartment ids where soldiers fight
    int m_nextJob = 1;
    int m_crewCount = 80;
    float m_morale = 70;
    float m_productionClock = 0;
    QList<Message> m_messages;
    QRandomGenerator m_rng{quint32(QRandomGenerator::global()->generate())};
};

#endif // INTERIOR_H
