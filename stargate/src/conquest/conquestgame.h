#ifndef CONQUESTGAME_H
#define CONQUESTGAME_H

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>

// ================================================================
//  Conquest mode - game logic (no UI, testable)
//  A generated galaxy of planets linked by hyperspace lanes. Each
//  faction owns planets; the player's flagship travels from planet to
//  planet: a neutral planet is taken by staying in orbit, an enemy
//  one by winning the battle against its fleet (fought in the 3D view
//  by ConquestScreen). Owned planets produce naquadah and trinium.
//  The game is won when no other faction owns a planet.
//  Phase 1: the AI factions do not move yet.
// ================================================================

namespace Conquest {

enum class Size { Small, Medium, Large };

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
    int defense = 1;                // 1..5: size of the defending fleet
    bool capital = false;
    QList<int> lanes;               // neighbour planet ids
};

struct Faction
{
    QString id;
    QString color;          // "#rrggbb"
    bool player = false;
    double naquadah = 0;
    double trinium = 0;
};

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

    const QList<Conquest::Planet> &planets() const { return m_planets; }
    const QList<Conquest::Faction> &factions() const { return m_factions; }
    int playerIndex() const { return m_player; }
    const Conquest::Faction &player() const { return m_factions.at(m_player); }
    int planetCount(int faction) const;
    int findPlanet(const QString &nameOrNumber) const;     // "3", "abydos"… -1 if none
    static int planetCountFor(Conquest::Size size);

    // --- Flagship
    int flagshipPlanet() const { return m_at; }             // planet it orbits / leaves
    bool travelling() const { return !m_route.isEmpty(); }
    QList<int> route() const { return m_route; }            // next planets, in order
    QPointF flagshipPos() const;
    bool orderTravel(int planetId, QString *error = nullptr);
    QList<int> path(int from, int to) const;               // shortest lane path (from excluded)
    double captureProgress() const { return m_capture; }   // 0..1 on a neutral planet
    double flagshipHull() const { return m_hull; }
    void setFlagshipHull(double hull) { m_hull = hull; }

    // --- Battles (fought by the screen)
    bool inBattle() const { return m_battlePlanet >= 0; }
    int battlePlanet() const { return m_battlePlanet; }
    void battleWon();
    void battleLost();              // the flagship retreats to where it came from

    // --- Orders paid with resources
    bool fortify(int planetId, QString *error = nullptr);
    bool repairFlagship(QString *error = nullptr);
    static constexpr double kFortifyCost = 60;             // naquadah per defense level

    // Advances the game (seconds of real time)
    void tick(double seconds);

    bool isOver() const { return m_over; }

    // Planet income per second for its owner
    static double naquadahRate(const Conquest::Planet &p);
    static double triniumRate(const Conquest::Planet &p);
    static QString bonusName(Conquest::Bonus bonus);

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
    void capture(int planetId);
    void checkVictory();
    int fleetMotherships(const Conquest::Planet &p) const;
    int fleetFighters(const Conquest::Planet &p) const;

    QList<Conquest::Planet> m_planets;
    QList<Conquest::Faction> m_factions;
    int m_player = 0;
    int m_at = 0;                   // planet of the flagship
    int m_previous = 0;             // where it came from (retreat)
    QList<int> m_route;
    double m_legProgress = 0;       // 0..1 on the current lane
    double m_capture = 0;
    double m_hull = 100;
    int m_battlePlanet = -1;
    bool m_over = false;
};

#endif // CONQUESTGAME_H
