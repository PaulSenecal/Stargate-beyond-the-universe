#ifndef CONQUESTGAME_H
#define CONQUESTGAME_H

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>

// ================================================================
//  Conquest mode - game logic (no UI, testable)
//  A generated galaxy of planets linked by hyperspace lanes. Each
//  faction owns planets; the player's flagship travels from planet to
//  planet: a neutral planet is taken by staying in orbit, an enemy
//  one by winning the battle against its forces (fought in the 3D view
//  by ConquestScreen). Owned planets produce naquadah and trinium.
//  Phase 2: shipyards build ships; fleets of every faction (the
//  player's too) are led by a utility AI: attack the planets they can
//  beat, defend the threatened ones, regroup when too weak. Battles
//  away from the flagship are resolved automatically.
//  The game is won when no other faction owns a planet or a fleet.
// ================================================================

namespace Conquest {

enum class Size { Small, Medium, Large, Huge };

enum class Bonus { None, Shipyard, NaquadahMine, TriniumMine, AncientSite };

enum class Tone { Info, Success, Warning, Critical };

// Playable factions (ships in the library with motherships and fighters)
struct FactionInfo
{
    QString id;             // faction name in the ship library ("Goa'uld"…)
    QString color;          // "#rrggbb"
    QString capital;        // name of its home world
};
const QList<FactionInfo> &factionInfos();
int factionInfoIndex(const QString &id);

struct Planet
{
    int id = 0;
    QString name;
    QPointF pos;                    // map coordinates (kMapWidth x kMapHeight)
    int owner = -1;                 // index in ConquestGame::factions(), -1 = neutral
    double population = 0;          // millions
    double maxPopulation = 0;
    Bonus bonus = Bonus::None;
    int defense = 1;                // 1..5: garrison (size of the defending fleet)
    bool capital = false;
    QList<int> lanes;               // neighbour planet ids
    // Shipyard production (capitals and shipyards only)
    int dockMotherships = 0;
    int dockFighters = 0;
    double fighterProgress = 0;     // 0..1
    double mothershipProgress = 0;
    double rebuild = 0;             // 0..1 toward the next defense level
    double yardDown = 0;            // seconds left with a sabotaged shipyard
    double mineDown = 0;            // seconds left without income (sabotage, plague)

    bool hasShipyard() const { return capital || bonus == Bonus::Shipyard; }
};

struct Fleet
{
    int id = 0;
    int owner = 0;
    int motherships = 0;
    int fighters = 0;
    int at = 0;                     // planet it orbits / leaves
    QList<int> route;               // next planets
    double leg = 0;                 // 0..1 on the current lane
    int target = -1;                // planet it goes to
    bool ordered = false;           // sent by the player: no AI decision until it arrives
    double think = 0;               // seconds before the next AI decision
    double damage = 0;              // 0..0.9: hurt in a 3D battle, repaired on its worlds

    int power() const { return 4 * motherships + fighters; }
    bool moving() const { return !route.isEmpty(); }
};

// Battle in progress at a planet between two sides (fought over time):
// the attacking faction's fleets against the planet side (garrison,
// shipyard and the owner's fleets; owner -1 = neutral militia)
struct Skirmish
{
    int planet = 0;
    int attacker = 0;
    int defender = -1;
    double attackerPending = 0;     // damage received, not yet turned into losses
    double defenderPending = 0;
    double elapsed = 0;             // seconds
};

struct Faction
{
    QString id;
    QString color;          // "#rrggbb"
    bool player = false;
    double naquadah = 0;
    double trinium = 0;
    // Phase 4
    QStringList techs;              // researched technologies
    QString research;               // technology being researched ("" = none)
    double researchProgress = 0;    // 0..1
    bool allied = false;            // allied with the player
    double allyCooldown = 0;        // seconds before a new proposal
    QStringList pacts;              // AI factions it is allied with (AI diplomacy)
    double gateTeams = 0;           // seconds before the gate team can leave again
};

// Technology tree: 3 branches (weapons, defense, industry) of 3 tiers;
// a tier needs the previous one of its branch
struct TechInfo
{
    QString id;                     // "weapons1"…
    int branch = 0;                 // 0 weapons, 1 defense, 2 industry
    int tier = 1;
    double cost = 0;                // naquadah
    double seconds = 0;             // research time
};
const QList<TechInfo> &techInfos();

constexpr double kMapWidth = 1000.0;
constexpr double kMapHeight = 640.0;

} // namespace Conquest

class ConquestGame : public QObject
{
    Q_OBJECT

public:
    struct Setup
    {
        QString playerFaction = QStringLiteral("Tau'ri");
        Conquest::Size size = Conquest::Size::Medium;
        quint32 seed = 0;           // 0 = random
    };

    explicit ConquestGame(QObject *parent = nullptr);

    void start(const Setup &setup);
    void setSeed(quint32 seed) { m_rng.seed(seed); }       // battles and AI (tests)

    const QList<Conquest::Planet> &planets() const { return m_planets; }
    const QList<Conquest::Faction> &factions() const { return m_factions; }
    const QList<Conquest::Fleet> &fleets() const { return m_fleets; }
    const QList<Conquest::Skirmish> &skirmishes() const { return m_skirmishes; }
    const Conquest::Skirmish *skirmishAt(int planetId) const;
    // Current strength of a side at a planet (fleets, + garrison for the owner)
    int sidePower(int planetId, int owner) const;
    // Flagship at a battle between two other factions, not fighting yet
    bool observing() const;
    int playerIndex() const { return m_player; }
    const Conquest::Faction &player() const { return m_factions.at(m_player); }
    int planetCount(int faction) const;
    int fleetCount(int faction) const;
    int findPlanet(const QString &nameOrNumber) const;     // "3", "abydos"… -1 if none
    static int planetCountFor(Conquest::Size size);
    QPointF fleetPos(const Conquest::Fleet &fleet) const;
    // Strength of a planet garrison (neutral militia is weaker)
    int garrisonPower(const Conquest::Planet &p) const;

    // --- Flagship
    int flagshipPlanet() const { return m_at; }             // planet it orbits / leaves
    bool travelling() const { return !m_route.isEmpty(); }
    QList<int> route() const { return m_route; }            // next planets, in order
    QPointF flagshipPos() const;
    bool orderTravel(int planetId, QString *error = nullptr);
    QList<int> path(int from, int to) const;               // shortest lane path (from excluded)
    double pathLength(int from, int to) const;              // < 0 if no route
    double captureProgress() const { return m_capture; }   // 0..1 on a neutral planet
    double flagshipHull() const { return m_hull; }
    void setFlagshipHull(double hull) { m_hull = hull; }

    // --- Battles of the flagship (fought by the screen)
    bool inBattle() const { return m_battlePlanet >= 0; }
    int battlePlanet() const { return m_battlePlanet; }
    QString battleFaction() const;
    void battleWon();
    // The flagship retreats to where it came from; enemyDamage (0..1) is
    // what the enemy motherships suffered: they keep it
    void battleLost(double enemyDamage = 0);
    // Damage the enemy fleets bring into the current flagship battle
    double battleDamage() const;
    // A mothership captured in a flagship battle joins your fleet there
    void addCapturedShip();
    // Pays from the player's stores; false (and nothing paid) if short
    bool spend(double naquadah, double trinium, QString *missing = nullptr);
    void gain(double naquadah, double trinium);

    // --- Orders paid with resources, fleet orders
    bool fortify(int planetId, QString *error = nullptr);
    bool repairFlagship(QString *error = nullptr);
    bool build(int planetId, bool mothership, QString *error = nullptr);
    bool orderFleet(int fleetId, int planetId, QString *error = nullptr);
    // Joins the battle where the flagship is, against that faction
    bool engage(const QString &faction, QString *error = nullptr);
    static constexpr double kFortifyCost = 60;             // naquadah per defense level
    static constexpr double kFighterCost = 30;
    static constexpr double kMothershipCost = 120;

    // Advances the game (seconds of real time)
    void tick(double seconds);

    bool isOver() const { return m_over; }

    // Planet income per second for its owner
    double naquadahRate(const Conquest::Planet &p) const;
    double triniumRate(const Conquest::Planet &p) const;

    // --- Faction traits (phase 3)
    //  Wraith: cull populations (captured worlds halved, no growth), fast
    //          shipyards, fleets regrow fighters at home
    //  Goa'uld: slavery (naquadah x1.6, faster shipyards), Jaffa revolts on
//           worlds left without a fleet
    //  Tau'ri: Ancient technology (+8% fleet power per Ancient site, max
    //          +40%), diplomacy (neutral neighbours may join, faster rallying)
    //  Lucian Alliance: smuggling (fast fighters), raids steal naquadah
    double powerMultiplier(int faction) const;

    // --- Technologies (phase 4)
    bool hasTech(int faction, const QString &tech) const;
    double researchCost(int faction, const Conquest::TechInfo &tech) const;     // Asgard: cheaper
    double researchTime(int faction, const Conquest::TechInfo &tech) const;     // Asgard: faster
    bool canResearch(int faction, const QString &tech, QString *why = nullptr) const;
    bool startResearch(const QString &tech, QString *error = nullptr);     // for the player
    int findTech(const QString &nameOrNumber) const;                       // index in techInfos(), -1
    static QString techName(const QString &tech);
    static QString techEffect(const QString &tech);
    double flagshipDamageBonus() const;     // weapons techs of the player (3D battles)
    double flagshipShieldBonus() const;     // shield generators

    // --- Alliances with the player (phase 4)
    bool hostile(int a, int b) const;       // can a attack b (-1 = neutral)
    bool isAllied(int faction) const;
    bool hasPact(int a, int b) const;       // alliance between two AI factions
    bool proposeAlliance(const QString &faction, QString *answer = nullptr);
    bool breakAlliance(const QString &faction, QString *error = nullptr);
    int findFaction(const QString &name) const;
    static QString traitSummary(const QString &factionId);
    static QString hangarFighter(const QString &factionId);     // fighter class of the flagship hangar
    // Survivors of a fleet after a 3D battle (removed when none is left)
    void setFleetStrength(int fleetId, int motherships, int fighters);
    bool hasTrait(int faction, const char *factionId) const;
    static QString bonusName(Conquest::Bonus bonus);

    // --- Espionage and sabotage by gate teams (the player's)
    // Planets reachable by gate: at most kGateRange lanes from one of yours
    bool inGateRange(int planetId) const;
    double gateTeamCooldown() const { return m_factions.at(m_player).gateTeams; }
    // Intelligence on a planet (garrison, docks, fleets around, income)
    bool spy(int planetId, QStringList *report, QString *error = nullptr);
    // target: yard | defense | mine. Returns false if it could not be
    // tried; *success tells the result
    bool sabotage(int planetId, const QString &target, bool *success, QString *error = nullptr);

    // --- Galactic events: wraith (starving invasion), plague, ascended
    // (an Ancient leaves technology), storm (hyperspace storm)
    bool triggerGalacticEvent(const QString &id);
    // Random galactic events and AI covert operations (off in some tests)
    void setRandomEvents(bool on) { m_randomEvents = on; }
    double stormLeft() const { return m_storm; }

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json);

signals:
    void message(const QString &text, Conquest::Tone tone);
    void battleStarted(int planetId, const QString &faction, int motherships, int fighters);
    void planetChanged(int planetId);
    void gameOver(bool victory);

private:
    void generate(const Setup &setup);
    void arrive(int planetId);
    void capture(int planetId, int newOwner);
    void checkVictory();
    int fleetMotherships(const Conquest::Planet &p) const;
    int fleetFighters(const Conquest::Planet &p) const;

    // Phase 2
    void produce(double seconds);
    void moveFleets(double seconds);
    void think(Conquest::Fleet &fleet);
    void startSkirmishes();
    void updateSkirmishes(double seconds);
    void endSkirmish(int index, bool attackerWon);
    void applyDamage(int planetId, int owner, double &pending);
    bool startFlagshipBattleAgainst(int planetId, int owner);
    void mergeFleets();
    bool startFlagshipBattle(int planetId);
    int hostileForces(int planetId, int againstOwner, int *motherships, int *fighters) const;
    static void takeLosses(Conquest::Fleet &fleet, double fraction);
    bool involvesPlayer(int owner) const { return owner == m_player; }
    QString ownerName(int owner) const;
    void factionEvents();
    void aiDiplomacy();
    void stopFightsBetween(int a, int b);
    void galacticEvents();
    void aiEconomy();
    int leadingFaction() const;

    QList<Conquest::Planet> m_planets;
    QList<Conquest::Faction> m_factions;
    QList<Conquest::Fleet> m_fleets;
    QList<Conquest::Skirmish> m_skirmishes;
    int m_nextFleetId = 1;
    int m_player = 0;
    int m_at = 0;                   // planet of the flagship
    int m_previous = 0;             // where it came from (retreat)
    QList<int> m_route;
    double m_legProgress = 0;       // 0..1 on the current lane
    double m_capture = 0;
    double m_hull = 100;
    int m_battlePlanet = -1;
    int m_battleOwner = -1;         // faction fought by the flagship
    bool m_over = false;
    double m_eventClock = 0;        // seconds toward the next faction events
    double m_galacticClock = 0;     // seconds toward the next galactic event
    double m_storm = 0;             // hyperspace storm: fleets slowed
    bool m_randomEvents = true;
    QRandomGenerator m_rng;
};

#endif // CONQUESTGAME_H
