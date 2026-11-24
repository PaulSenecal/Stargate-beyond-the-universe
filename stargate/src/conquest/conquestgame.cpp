#include "conquestgame.h"

#include <QJsonArray>
#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

using namespace Conquest;

namespace {

constexpr double kFlagshipSpeed = 70.0;         // map units per second
constexpr double kFleetSpeed = 28.0;
constexpr double kCaptureBase = 4.0;            // seconds in orbit, + kCapturePerDefense per level
constexpr double kCapturePerDefense = 3.0;
constexpr double kHullRegen = 0.3;              // % per second at an owned planet
constexpr double kShipyardRegen = 1.5;
constexpr double kTriniumPerHull = 0.5;         // repair cost
constexpr double kFighterBuild = 40.0;          // seconds per ship at a shipyard (average population)
constexpr double kMothershipBuild = 120.0;
constexpr int kDockFighters = 8;                // shipyard capacity
constexpr int kDockMotherships = 3;
constexpr double kThinkDelay = 4.0;             // seconds between two AI decisions of a fleet
constexpr double kAttackRatio = 1.6;            // AI attacks only with that much more power
constexpr double kDefenseRebuild = 60.0;        // seconds per defense level rebuilt (up to 3, 5 for capitals)
constexpr int kFlagshipPower = 12;              // AI estimate of the flagship strength
constexpr double kBattleRate = 0.04;            // damage per second per point of power
constexpr double kEventPeriod = 30.0;           // seconds between two faction events (revolts…)
constexpr double kRevoltChance = 0.04;          // per weak Goa'uld world and period
constexpr double kDiplomacyChance = 0.04;       // per neutral neighbour of the Tau'ri and period
constexpr int kMaxBattleMotherships = 4;        // 3D battle limits (12 contacts)
constexpr int kMaxBattleFighters = 8;

const QStringList kNamedWorlds{"Abydos", "Dakara", "Tollana", "Langara", "Cimmeria", "Hebridan", "Kheb",
                               "Hala", "Vis Uban", "Delmak", "Netu", "Hasara", "Juna", "Orban", "Edora",
                               "Argos", "Euronda", "Sateda", "Athos", "Olesia", "Lantea", "Taranis",
                               "Proculus", "Praclarush", "Tartarus", "Vorash", "Revanna", "Kallana"};

double distance(const Planet &a, const Planet &b)
{
    return QLineF(a.pos, b.pos).length();
}

QString designation(QRandomGenerator &rng)
{
    return QStringLiteral("%1%2%3-%4")
        .arg(QChar(rng.bounded(2) ? 'P' : 'M'))
        .arg(1 + rng.bounded(9))
        .arg(QChar('A' + rng.bounded(26)))
        .arg(100 + rng.bounded(900));
}

} // namespace

// ================================================================
//  Factions
// ================================================================

const QList<FactionInfo> &Conquest::factionInfos()
{
    static const QList<FactionInfo> infos{
        {QStringLiteral("Tau'ri"), QStringLiteral("#4f8fd6"), QStringLiteral("Earth")},
        {QStringLiteral("Goa'uld"), QStringLiteral("#d4a63a"), QStringLiteral("Chulak")},
        {QStringLiteral("Wraith"), QStringLiteral("#5fbf6a"), QStringLiteral("Wraith Prime")},
        {QStringLiteral("Lucian Alliance"), QStringLiteral("#c0563f"), QStringLiteral("Lucia")},
    };
    return infos;
}

int Conquest::factionInfoIndex(const QString &id)
{
    const QList<FactionInfo> &infos = factionInfos();
    for (int i = 0; i < infos.size(); ++i)
        if (infos[i].id == id)
            return i;
    return -1;
}

// ================================================================
//  Setup / galaxy generation
// ================================================================

ConquestGame::ConquestGame(QObject *parent)
    : QObject(parent)
    , m_rng(QRandomGenerator::global()->generate())
{
}

int ConquestGame::planetCountFor(Size size)
{
    switch (size) {
    case Size::Small: return 12;
    case Size::Medium: return 24;
    case Size::Large: return 40;
    }
    return 24;
}

void ConquestGame::start(const Setup &setup)
{
    generate(setup);
    emit message(tr("Conquest begins: %1 planets, %2 enemy faction(s). Your flagship orbits %3.")
                     .arg(m_planets.size())
                     .arg(m_factions.size() - 1)
                     .arg(m_planets.at(m_at).name),
                 Tone::Success);
    emit message(tr("Take every planet of the enemy factions. Neutral worlds join you after a stay in orbit."),
                 Tone::Info);
    emit message(tr("Shipyards build fleets that act on their own - yours too. Enemy fleets will attack."),
                 Tone::Info);
}

void ConquestGame::generate(const Setup &setup)
{
    m_rng.seed(setup.seed ? setup.seed : QRandomGenerator::global()->generate());
    QRandomGenerator &rng = m_rng;
    m_planets.clear();
    m_factions.clear();
    m_fleets.clear();
    m_skirmishes.clear();
    m_nextFleetId = 1;
    m_route.clear();
    m_legProgress = m_capture = 0;
    m_hull = 100;
    m_battlePlanet = m_battleOwner = -1;
    m_over = false;

    const int count = planetCountFor(setup.size);
    const int enemies = setup.size == Size::Small ? 1 : setup.size == Size::Medium ? 2 : 3;

    // --- Positions: random, kept apart from each other
    constexpr double margin = 50;
    double minDistance = 0.7 * std::sqrt(kMapWidth * kMapHeight / count);
    int failures = 0;
    while (m_planets.size() < count) {
        const QPointF p(margin + rng.generateDouble() * (kMapWidth - 2 * margin),
                        margin + rng.generateDouble() * (kMapHeight - 2 * margin));
        const bool free = std::none_of(m_planets.cbegin(), m_planets.cend(),
                                       [&](const Planet &o) { return QLineF(o.pos, p).length() < minDistance; });
        if (!free) {
            if (++failures % 300 == 0)
                minDistance *= 0.95;
            continue;
        }
        Planet planet;
        planet.id = int(m_planets.size());
        planet.pos = p;
        m_planets << planet;
    }

    // --- Lanes: the two nearest neighbours, then join the separate groups
    auto link = [this](int a, int b) {
        if (a != b && !m_planets[a].lanes.contains(b)) {
            m_planets[a].lanes << b;
            m_planets[b].lanes << a;
        }
    };
    for (Planet &p : m_planets) {
        QList<int> others(count);
        std::iota(others.begin(), others.end(), 0);
        others.removeAll(p.id);
        std::sort(others.begin(), others.end(), [&](int a, int b) {
            return distance(p, m_planets[a]) < distance(p, m_planets[b]);
        });
        link(p.id, others[0]);
        link(p.id, others[1]);
    }
    while (true) {
        QList<int> reached{0};
        for (int i = 0; i < reached.size(); ++i)
            for (int n : m_planets[reached[i]].lanes)
                if (!reached.contains(n))
                    reached << n;
        if (reached.size() == count)
            break;
        int bestA = -1, bestB = -1;
        double best = std::numeric_limits<double>::max();
        for (int a : reached)
            for (const Planet &b : m_planets)
                if (!reached.contains(b.id) && distance(m_planets[a], b) < best) {
                    best = distance(m_planets[a], b);
                    bestA = a;
                    bestB = b.id;
                }
        link(bestA, bestB);
    }

    // --- Factions: the player, then enemies picked at random
    const QList<FactionInfo> &infos = factionInfos();
    const int playerInfo = std::max(0, factionInfoIndex(setup.playerFaction));
    QList<int> others;
    for (int i = 0; i < infos.size(); ++i)
        if (i != playerInfo)
            others << i;
    std::shuffle(others.begin(), others.end(), rng);
    QList<int> chosen{playerInfo};
    chosen << others.mid(0, enemies);
    for (int i : chosen)
        m_factions << Faction{infos[i].id, infos[i].color, i == playerInfo, 100, 50};
    m_player = 0;

    // --- Planets: names, population, defenses, bonus
    QStringList names = kNamedWorlds;
    std::shuffle(names.begin(), names.end(), rng);
    for (Planet &p : m_planets) {
        p.name = rng.bounded(2) && !names.isEmpty() ? names.takeFirst() : designation(rng);
        p.population = 2 + rng.bounded(7);
        p.maxPopulation = p.population * 1.5;
        p.defense = 1 + rng.bounded(3);
        const double roll = rng.generateDouble();
        p.bonus = roll < 0.15 ? Bonus::Shipyard
                : roll < 0.35 ? Bonus::NaquadahMine
                : roll < 0.50 ? Bonus::TriniumMine
                : roll < 0.60 ? Bonus::AncientSite
                              : Bonus::None;
    }

    // --- Capitals far from each other, each enemy holds a few planets around
    QList<int> capitals{int(rng.bounded(count))};
    while (capitals.size() < m_factions.size()) {
        int best = -1;
        double bestDistance = -1;
        for (const Planet &p : m_planets) {
            if (capitals.contains(p.id))
                continue;
            double nearest = std::numeric_limits<double>::max();
            for (int c : capitals)
                nearest = std::min(nearest, distance(p, m_planets[c]));
            if (nearest > bestDistance) {
                bestDistance = nearest;
                best = p.id;
            }
        }
        capitals << best;
    }
    const int territory = setup.size == Size::Small ? 1 : 2;
    for (int f = 0; f < m_factions.size(); ++f) {
        Planet &capital = m_planets[capitals[f]];
        capital.owner = f;
        capital.capital = true;
        capital.name = infos[factionInfoIndex(m_factions[f].id)].capital;
        capital.population = 10;
        capital.maxPopulation = 12;
        capital.defense = 5;
        capital.bonus = Bonus::Shipyard;
        // Every faction starts with a small fleet at home
        Fleet fleet;
        fleet.id = m_nextFleetId++;
        fleet.owner = f;
        fleet.motherships = 1;
        fleet.fighters = 3;
        fleet.at = capital.id;
        fleet.think = 5 + rng.generateDouble() * 5;
        m_fleets << fleet;
        if (f == m_player)
            continue;
        int taken = 0;
        for (int n : capital.lanes) {
            Planet &p = m_planets[n];
            if (p.owner >= 0 || taken >= territory)
                continue;
            p.owner = f;
            p.defense = 3;
            p.population = std::max(p.population, 4.0);
            p.maxPopulation = p.population * 1.5;
            ++taken;
        }
    }
    m_at = m_previous = capitals[m_player];
}

// ================================================================
//  Queries
// ================================================================

int ConquestGame::planetCount(int faction) const
{
    return int(std::count_if(m_planets.cbegin(), m_planets.cend(),
                             [faction](const Planet &p) { return p.owner == faction; }));
}

int ConquestGame::fleetCount(int faction) const
{
    return int(std::count_if(m_fleets.cbegin(), m_fleets.cend(),
                             [faction](const Fleet &f) { return f.owner == faction; }));
}

int ConquestGame::findPlanet(const QString &nameOrNumber) const
{
    const QString wanted = nameOrNumber.trimmed().toLower().remove(' ').remove('\'');
    if (wanted.isEmpty())
        return -1;
    bool isNumber = false;
    const int number = wanted.toInt(&isNumber);
    if (isNumber)
        return number >= 1 && number <= m_planets.size() ? number - 1 : -1;
    auto normalized = [](const QString &s) { return s.toLower().remove(' ').remove('\''); };
    for (const Planet &p : m_planets)
        if (normalized(p.name) == wanted)
            return p.id;
    for (const Planet &p : m_planets)
        if (normalized(p.name).startsWith(wanted))
            return p.id;
    return -1;
}

int ConquestGame::garrisonPower(const Planet &p)
{
    return p.defense * 3 + 4 * p.dockMotherships + p.dockFighters;
}

double ConquestGame::naquadahRate(const Planet &p) const
{
    const double slavery = hasTrait(p.owner, "Goa'uld") ? 1.6 : 1.0;
    return 0.02 * p.population * (p.bonus == Bonus::NaquadahMine ? 2.0 : 1.0) * slavery;
}

double ConquestGame::triniumRate(const Planet &p) const
{
    return 0.01 * p.population * (p.bonus == Bonus::TriniumMine ? 2.0 : 1.0);
}

bool ConquestGame::hasTrait(int faction, const char *factionId) const
{
    return faction >= 0 && faction < m_factions.size() && m_factions[faction].id == QLatin1String(factionId);
}

double ConquestGame::powerMultiplier(int faction) const
{
    if (!hasTrait(faction, "Tau'ri"))
        return 1.0;
    const int sites = int(std::count_if(m_planets.cbegin(), m_planets.cend(), [faction](const Planet &p) {
        return p.owner == faction && p.bonus == Bonus::AncientSite;
    }));
    return 1.0 + std::min(0.4, 0.08 * sites);
}

QString ConquestGame::traitSummary(const QString &factionId)
{
    if (factionId == "Wraith")
        return tr("Culling: captured worlds lose half their population (it feeds the hives: naquadah) and never "
                  "grow back; shipyards x1.6; fleets regrow fighters at home.");
    if (factionId == "Goa'uld")
        return tr("Slavery: naquadah x1.6 and shipyards x1.25, but worlds without a fleet in orbit may rise in "
                  "Jaffa revolts.");
    if (factionId == "Tau'ri")
        return tr("Ancient technology: +8% fleet power per Ancient site (max +40%). Diplomacy: neutral "
                  "neighbours may join, the flagship rallies worlds twice as fast.");
    if (factionId == "Lucian Alliance")
        return tr("Smuggling: fighters built x1.4 faster. Raids: every world taken steals naquadah from its owner.");
    return QString();
}

QString ConquestGame::bonusName(Bonus bonus)
{
    switch (bonus) {
    case Bonus::Shipyard: return tr("Shipyard (fast repairs)");
    case Bonus::NaquadahMine: return tr("Naquadah mine (x2)");
    case Bonus::TriniumMine: return tr("Trinium mine (x2)");
    case Bonus::AncientSite: return tr("Ancient site");
    case Bonus::None: break;
    }
    return tr("None");
}

QString ConquestGame::ownerName(int owner) const
{
    return owner < 0 ? tr("independent world") : m_factions.at(owner).id;
}

QPointF ConquestGame::flagshipPos() const
{
    const QPointF from = m_planets.value(m_at).pos;
    if (m_route.isEmpty())
        return from;
    const QPointF to = m_planets.value(m_route.first()).pos;
    return from + (to - from) * m_legProgress;
}

QPointF ConquestGame::fleetPos(const Fleet &fleet) const
{
    const QPointF from = m_planets.value(fleet.at).pos;
    if (fleet.route.isEmpty())
        return from;
    const QPointF to = m_planets.value(fleet.route.first()).pos;
    return from + (to - from) * fleet.leg;
}

// Dijkstra on the lanes (lengths = distances)
QList<int> ConquestGame::path(int from, int to) const
{
    const int count = int(m_planets.size());
    if (from < 0 || to < 0 || from >= count || to >= count || from == to)
        return {};
    QList<double> best(count, std::numeric_limits<double>::max());
    QList<int> previous(count, -1);
    QList<bool> done(count, false);
    best[from] = 0;
    for (int step = 0; step < count; ++step) {
        int u = -1;
        for (int i = 0; i < count; ++i)
            if (!done[i] && (u < 0 || best[i] < best[u]))
                u = i;
        if (u < 0 || best[u] == std::numeric_limits<double>::max() || u == to)
            break;
        done[u] = true;
        for (int n : m_planets[u].lanes) {
            const double d = best[u] + distance(m_planets[u], m_planets[n]);
            if (d < best[n]) {
                best[n] = d;
                previous[n] = u;
            }
        }
    }
    if (previous[to] < 0)
        return {};
    QList<int> result;
    for (int p = to; p != from; p = previous[p])
        result.prepend(p);
    return result;
}

double ConquestGame::pathLength(int from, int to) const
{
    if (from == to)
        return 0;
    const QList<int> steps = path(from, to);
    if (steps.isEmpty())
        return -1;
    double length = 0;
    int previous = from;
    for (int p : steps) {
        length += distance(m_planets[previous], m_planets[p]);
        previous = p;
    }
    return length;
}

// ================================================================
//  Orders
// ================================================================

bool ConquestGame::orderTravel(int planetId, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (m_over)
        return fail(tr("the game is over"));
    if (inBattle())
        return fail(tr("the flagship is in battle - win it first"));
    if (planetId < 0 || planetId >= m_planets.size())
        return fail(tr("unknown planet"));
    if (planetId == m_at && m_route.isEmpty())
        return fail(tr("the flagship is already there"));

    // Already on a lane: finish it first, the new route starts from its end
    const int start = m_route.isEmpty() ? m_at : m_route.first();
    QList<int> route = m_route.isEmpty() ? QList<int>() : QList<int>{m_route.first()};
    if (planetId != start)
        route << path(start, planetId);
    if (route.isEmpty())
        return fail(tr("no route to that planet"));
    m_route = route;
    m_capture = 0;
    emit message(tr("Course set for %1 (%2 jump(s))").arg(m_planets[planetId].name).arg(m_route.size()),
                 Tone::Info);
    return true;
}

bool ConquestGame::fortify(int planetId, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (planetId < 0 || planetId >= m_planets.size())
        return fail(tr("unknown planet"));
    Planet &p = m_planets[planetId];
    if (p.owner != m_player)
        return fail(tr("%1 is not yours").arg(p.name));
    if (p.defense >= 5)
        return fail(tr("%1 already has the strongest defenses").arg(p.name));
    Faction &me = m_factions[m_player];
    if (me.naquadah < kFortifyCost)
        return fail(tr("%1 naquadah needed (%2 available)").arg(kFortifyCost).arg(int(me.naquadah)));
    me.naquadah -= kFortifyCost;
    ++p.defense;
    emit message(tr("%1 fortified: defense %2/5").arg(p.name).arg(p.defense), Tone::Success);
    emit planetChanged(p.id);
    return true;
}

bool ConquestGame::repairFlagship(QString *error)
{
    Faction &me = m_factions[m_player];
    const double missing = 100.0 - m_hull;
    if (missing < 1.0) {
        if (error)
            *error = tr("the hull is intact");
        return false;
    }
    const double affordable = std::min(missing, me.trinium / kTriniumPerHull);
    if (affordable < 1.0) {
        if (error)
            *error = tr("not enough trinium (%1 per hull point)").arg(kTriniumPerHull);
        return false;
    }
    me.trinium -= affordable * kTriniumPerHull;
    m_hull += affordable;
    emit message(tr("Flagship repaired: hull %1% (-%2 trinium)")
                     .arg(qRound(m_hull))
                     .arg(qRound(affordable * kTriniumPerHull)),
                 Tone::Success);
    return true;
}

bool ConquestGame::build(int planetId, bool mothership, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (planetId < 0 || planetId >= m_planets.size())
        return fail(tr("unknown planet"));
    Planet &p = m_planets[planetId];
    if (p.owner != m_player)
        return fail(tr("%1 is not yours").arg(p.name));
    if (!p.hasShipyard())
        return fail(tr("%1 has no shipyard").arg(p.name));
    if (mothership ? p.dockMotherships >= kDockMotherships : p.dockFighters >= kDockFighters)
        return fail(tr("the shipyard of %1 is full").arg(p.name));
    const double cost = mothership ? kMothershipCost : kFighterCost;
    Faction &me = m_factions[m_player];
    if (me.naquadah < cost)
        return fail(tr("%1 naquadah needed (%2 available)").arg(cost).arg(int(me.naquadah)));
    me.naquadah -= cost;
    (mothership ? p.dockMotherships : p.dockFighters) += 1;
    emit message(mothership ? tr("Mothership built at %1").arg(p.name) : tr("Fighter built at %1").arg(p.name),
                 Tone::Success);
    return true;
}

bool ConquestGame::orderFleet(int fleetId, int planetId, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    auto it = std::find_if(m_fleets.begin(), m_fleets.end(), [fleetId](const Fleet &f) { return f.id == fleetId; });
    if (it == m_fleets.end() || it->owner != m_player)
        return fail(tr("no fleet of yours with that number"));
    if (planetId < 0 || planetId >= m_planets.size())
        return fail(tr("unknown planet"));
    if (it->at == m_battlePlanet && !it->moving())
        return fail(tr("that fleet is in battle"));
    const int start = it->moving() ? it->route.first() : it->at;
    QList<int> route = it->moving() ? QList<int>{it->route.first()} : QList<int>();
    if (planetId != start)
        route << path(start, planetId);
    if (route.isEmpty())
        return fail(tr("the fleet is already there"));
    it->route = route;
    it->target = planetId;
    it->ordered = true;
    emit message(tr("Fleet %1 heads for %2").arg(it->id).arg(m_planets[planetId].name), Tone::Info);
    return true;
}

// ================================================================
//  Simulation
// ================================================================

void ConquestGame::tick(double seconds)
{
    if (m_over || seconds <= 0)
        return;

    // Economy and population of every owned planet
    for (Planet &p : m_planets) {
        if (p.owner < 0)
            continue;
        Faction &f = m_factions[p.owner];
        f.naquadah += naquadahRate(p) * seconds;
        f.trinium += triniumRate(p) * seconds;
        if (hasTrait(p.owner, "Wraith"))
            p.population = std::max(0.2 * p.maxPopulation, p.population - 0.002 * p.population * seconds);
        else
            p.population += 0.02 * p.population * (1.0 - p.population / p.maxPopulation) * seconds;
        // Garrisons are rebuilt little by little
        const int rebuiltMax = p.capital ? 5 : 3;
        if (p.defense < rebuiltMax && p.id != m_battlePlanet) {
            p.rebuild += seconds / kDefenseRebuild;
            if (p.rebuild >= 1.0) {
                p.rebuild = 0;
                ++p.defense;
            }
        }
    }

    // Autonomous fleets of every faction
    m_eventClock += seconds;
    if (m_eventClock >= kEventPeriod) {
        m_eventClock -= kEventPeriod;
        factionEvents();
    }
    produce(seconds);
    moveFleets(seconds);
    mergeFleets();
    startSkirmishes();
    updateSkirmishes(seconds);
    for (Fleet &fleet : m_fleets) {
        if (fleet.moving() || fleet.ordered || fleet.at == m_battlePlanet || skirmishAt(fleet.at))
            continue;
        fleet.think -= seconds;
        if (fleet.think <= 0) {
            fleet.think = kThinkDelay + m_rng.generateDouble();
            think(fleet);
        }
    }
    checkVictory();
    if (m_over || inBattle())
        return;

    // Flagship on a lane
    if (!m_route.isEmpty()) {
        const double length = std::max(1.0, distance(m_planets[m_at], m_planets[m_route.first()]));
        m_legProgress += kFlagshipSpeed * seconds / length;
        if (m_legProgress >= 1.0) {
            m_legProgress = 0;
            m_previous = m_at;
            m_at = m_route.takeFirst();
            arrive(m_at);
        }
        return;
    }

    // Enemy fleets reaching the flagship: it defends the planet
    if (startFlagshipBattle(m_at))
        return;
    if (skirmishAt(m_at))
        return;                     // watching a battle between two other factions

    const Planet &here = m_planets[m_at];
    if (here.owner < 0) {
        // Neutral world: taken after a stay in orbit
        const double rally = hasTrait(m_player, "Tau'ri") ? 2.0 : 1.0;
        m_capture += rally * seconds / (kCaptureBase + kCapturePerDefense * here.defense);
        if (m_capture >= 1.0) {
            m_capture = 0;
            emit message(tr("%1 joins your faction").arg(here.name), Tone::Success);
            capture(m_at, m_player);
        }
    } else if (here.owner == m_player && m_hull < 100.0) {
        m_hull = std::min(100.0, m_hull + (here.bonus == Bonus::Shipyard ? kShipyardRegen : kHullRegen) * seconds);
    }
}

void ConquestGame::arrive(int planetId)
{
    const Planet &p = m_planets[planetId];
    if (startFlagshipBattle(planetId))
        return;
    if (!m_route.isEmpty())
        return;                     // only passing through
    if (const Skirmish *battle = skirmishAt(planetId)) {
        emit message(tr("Battle in progress at %1: the %2 attack the %3. 'engage <faction>' to join in, "
                        "or wait for the winner")
                         .arg(p.name, m_factions[battle->attacker].id, ownerName(battle->defender)),
                     Tone::Warning);
        return;
    }
    if (p.owner < 0)
        emit message(tr("In orbit of %1: the planet joins you in %2 s if you stay")
                         .arg(p.name)
                         .arg(qRound(kCaptureBase + kCapturePerDefense * p.defense)),
                     Tone::Info);
    else
        emit message(tr("Arrived at %1").arg(p.name), Tone::Info);
}

int ConquestGame::fleetMotherships(const Planet &p) const
{
    return std::min(3, 1 + (p.defense - 1) / 2);
}

int ConquestGame::fleetFighters(const Planet &p) const
{
    return std::min(7, 2 + p.defense);
}

// Forces at a planet that fight `againstOwner`: the planet owner (garrison
// and its fleets) if hostile, otherwise the strongest hostile fleets.
int ConquestGame::hostileForces(int planetId, int againstOwner, int *motherships, int *fighters) const
{
    const Planet &p = m_planets[planetId];
    int owner = -1;
    if (p.owner >= 0 && p.owner != againstOwner) {
        owner = p.owner;
    } else {
        int best = 0;
        for (int f = 0; f < m_factions.size(); ++f) {
            if (f == againstOwner)
                continue;
            int power = 0;
            for (const Fleet &fleet : m_fleets)
                if (fleet.owner == f && fleet.at == planetId && !fleet.moving())
                    power += fleet.power();
            if (power > best) {
                best = power;
                owner = f;
            }
        }
    }
    if (owner < 0)
        return -1;
    const bool garrison = owner == p.owner && p.defense > 0;
    int big = owner == p.owner ? (garrison ? fleetMotherships(p) : 0) + p.dockMotherships : 0;
    int small = owner == p.owner ? (garrison ? fleetFighters(p) : 0) + p.dockFighters : 0;
    for (const Fleet &fleet : m_fleets)
        if (fleet.owner == owner && fleet.at == planetId && !fleet.moving()) {
            big += fleet.motherships;
            small += fleet.fighters;
        }
    if (motherships)
        *motherships = std::min(kMaxBattleMotherships, big);
    if (fighters)
        *fighters = std::min(kMaxBattleFighters, small);
    return owner;
}

bool ConquestGame::startFlagshipBattle(int planetId)
{
    // A battle between two other factions: the player decides ('engage')
    if (const Skirmish *battle = skirmishAt(planetId))
        if (battle->attacker != m_player && battle->defender != m_player)
            return false;
    const int owner = hostileForces(planetId, m_player, nullptr, nullptr);
    return owner >= 0 && startFlagshipBattleAgainst(planetId, owner);
}

bool ConquestGame::startFlagshipBattleAgainst(int planetId, int owner)
{
    const Planet &p = m_planets[planetId];
    int big = 0, small = 0;
    if (owner == p.owner && p.defense > 0) {
        big = fleetMotherships(p) + p.dockMotherships;
        small = fleetFighters(p) + p.dockFighters;
    } else if (owner == p.owner) {
        big = p.dockMotherships;
        small = p.dockFighters;
    }
    for (const Fleet &fleet : m_fleets)
        if (fleet.owner == owner && fleet.at == planetId && !fleet.moving()) {
            big += fleet.motherships;
            small += fleet.fighters;
        }
    big = std::min(kMaxBattleMotherships, big);
    small = std::min(kMaxBattleFighters, small);
    if (big + small == 0)
        return false;
    m_route.clear();
    m_capture = 0;
    m_battlePlanet = planetId;
    m_battleOwner = owner;
    if (owner == p.owner)
        emit message(tr("%1 is defended by the %2 fleet: battle!").arg(p.name, m_factions[owner].id),
                     Tone::Critical);
    else
        emit message(tr("A %1 fleet attacks the flagship at %2: battle!").arg(m_factions[owner].id, p.name),
                     Tone::Critical);
    emit battleStarted(planetId, m_factions[owner].id, big, small);
    return true;
}

QString ConquestGame::battleFaction() const
{
    return m_battleOwner >= 0 && m_battleOwner < m_factions.size() ? m_factions[m_battleOwner].id : QString();
}

void ConquestGame::battleWon()
{
    if (!inBattle())
        return;
    const int id = m_battlePlanet;
    const int owner = m_battleOwner;
    m_battlePlanet = m_battleOwner = -1;
    Planet &p = m_planets[id];
    m_fleets.erase(std::remove_if(m_fleets.begin(), m_fleets.end(),
                                  [&](const Fleet &f) { return f.owner == owner && f.at == id && !f.moving(); }),
                   m_fleets.end());
    if (p.owner == owner) {
        p.defense = std::max(1, p.defense - 1);
        p.population *= 0.8;                // the battle took its toll
        emit message(tr("%1 taken from the %2").arg(p.name, m_factions[owner].id), Tone::Success);
        capture(id, m_player);
    } else {
        emit message(tr("The %1 fleet at %2 is destroyed").arg(m_factions[owner].id, p.name), Tone::Success);
    }
    checkVictory();
}

void ConquestGame::battleLost()
{
    if (!inBattle())
        return;
    const QString planet = m_planets[m_battlePlanet].name;
    m_battlePlanet = m_battleOwner = -1;
    m_route.clear();
    if (m_previous == m_at) {
        // Attacked where it stood: fall back to the nearest world of ours
        double best = std::numeric_limits<double>::max();
        for (const Planet &p : m_planets)
            if (p.owner == m_player && p.id != m_at) {
                const double d = pathLength(m_at, p.id);
                if (d >= 0 && d < best) {
                    best = d;
                    m_previous = p.id;
                }
            }
    }
    m_at = m_previous;
    emit message(tr("Hull breached: the flagship retreats from %1 to %2").arg(planet, m_planets[m_at].name),
                 Tone::Critical);
}

void ConquestGame::capture(int planetId, int newOwner)
{
    Planet &p = m_planets[planetId];
    const int previousOwner = p.owner;
    p.owner = newOwner;
    p.dockMotherships = p.dockFighters = 0;
    if (hasTrait(newOwner, "Wraith") && previousOwner != newOwner) {
        // Culling: half of the population feeds the hives (naquadah)
        m_factions[newOwner].naquadah += 6.0 * p.population * 0.5;
        p.population *= 0.5;
        if (newOwner == m_player || previousOwner == m_player)
            emit message(tr("The Wraith cull the population of %1").arg(p.name), Tone::Warning);
    }
    p.fighterProgress = p.mothershipProgress = 0;
    if (p.bonus == Bonus::AncientSite && newOwner >= 0) {
        m_factions[newOwner].naquadah += 100;
        if (newOwner == m_player)
            emit message(tr("Ancient site on %1: +100 naquadah from its archives").arg(p.name), Tone::Success);
    }
    if (previousOwner >= 0 && previousOwner != newOwner && planetCount(previousOwner) == 0)
        emit message(tr("The %1 have lost their last world").arg(m_factions[previousOwner].id),
                     previousOwner == m_player ? Tone::Critical : Tone::Success);
    emit planetChanged(planetId);
}

void ConquestGame::checkVictory()
{
    if (m_over)
        return;
    bool enemyLeft = false;
    for (int f = 0; f < m_factions.size(); ++f)
        if (f != m_player && (planetCount(f) > 0 || fleetCount(f) > 0))
            enemyLeft = true;
    if (!enemyLeft || planetCount(m_player) == 0) {
        m_over = true;
        m_battlePlanet = m_battleOwner = -1;
        emit gameOver(!enemyLeft);
    }
}

// ================================================================
//  Phase 2: production, fleets, AI, automatic battles
// ================================================================

// Periodic faction events: Jaffa revolts, Tau'ri diplomacy, Wraith regrowth
void ConquestGame::factionEvents()
{
    auto quiet = [this](const Planet &p) {
        if (skirmishAt(p.id) || p.id == m_battlePlanet || (p.id == m_at && !travelling()))
            return false;
        return std::none_of(m_fleets.cbegin(), m_fleets.cend(),
                            [&p](const Fleet &f) { return f.at == p.id && !f.moving(); });
    };
    for (Planet &p : m_planets) {
        // Jaffa revolt on a Goa'uld world left without a fleet
        if (hasTrait(p.owner, "Goa'uld") && !p.capital && quiet(p)
            && m_rng.generateDouble() < kRevoltChance) {
            const int previous = p.owner;
            p.owner = -1;
            p.defense = 2;
            p.dockMotherships = p.dockFighters = 0;
            emit message(tr("Jaffa revolt on %1: the world breaks free from the Goa'uld").arg(p.name),
                         previous == m_player ? Tone::Critical : Tone::Info);
            emit planetChanged(p.id);
            continue;
        }
        // Tau'ri diplomacy with a neutral neighbour
        if (p.owner < 0 && quiet(p)) {
            for (int n : p.lanes) {
                const int f = m_planets[n].owner;
                if (hasTrait(f, "Tau'ri") && m_rng.generateDouble() < kDiplomacyChance) {
                    emit message(tr("%1 joins the Tau'ri through diplomacy").arg(p.name),
                                 f == m_player ? Tone::Success : Tone::Info);
                    capture(p.id, f);
                    break;
                }
            }
        }
    }
    // Wraith fleets regrow fighters on their worlds
    for (Fleet &fleet : m_fleets)
        if (hasTrait(fleet.owner, "Wraith") && !fleet.moving() && m_planets[fleet.at].owner == fleet.owner
            && fleet.fighters < 12)
            ++fleet.fighters;
}

void ConquestGame::produce(double seconds)
{
    for (Planet &p : m_planets) {
        if (p.owner < 0 || !p.hasShipyard() || p.id == m_battlePlanet)
            continue;
        const double yard = hasTrait(p.owner, "Wraith") ? 1.6 : hasTrait(p.owner, "Goa'uld") ? 1.25 : 1.0;
        const double rate = std::clamp(0.5 + p.population / 10.0, 0.6, 1.6) * seconds * yard;
        const double smuggling = hasTrait(p.owner, "Lucian Alliance") ? 1.4 : 1.0;
        if (p.dockFighters < kDockFighters) {
            p.fighterProgress += rate * smuggling / kFighterBuild;
            if (p.fighterProgress >= 1.0) {
                p.fighterProgress -= 1.0;
                ++p.dockFighters;
            }
        }
        if (p.dockMotherships < kDockMotherships) {
            p.mothershipProgress += rate / kMothershipBuild;
            if (p.mothershipProgress >= 1.0) {
                p.mothershipProgress -= 1.0;
                ++p.dockMotherships;
            }
        }

        // A fleet takes off when the shipyard holds enough ships; a fleet of
        // the faction waiting there takes them as reinforcements
        const bool ready = (p.dockMotherships >= 1 && p.dockFighters >= 3) || p.dockFighters >= 6;
        if (!ready)
            continue;
        auto waiting = std::find_if(m_fleets.begin(), m_fleets.end(), [&p](const Fleet &f) {
            return f.owner == p.owner && f.at == p.id && !f.moving();
        });
        if (waiting != m_fleets.end()) {
            waiting->motherships += p.dockMotherships;
            waiting->fighters += p.dockFighters;
            p.dockMotherships = p.dockFighters = 0;
            continue;
        }
        const int maxFleets = std::min(4, 1 + planetCount(p.owner) / 5);
        if (fleetCount(p.owner) >= maxFleets)
            continue;
        Fleet fleet;
        fleet.id = m_nextFleetId++;
        fleet.owner = p.owner;
        fleet.motherships = p.dockMotherships;
        fleet.fighters = p.dockFighters;
        fleet.at = p.id;
        fleet.think = 0.5;
        p.dockMotherships = p.dockFighters = 0;
        m_fleets << fleet;
        if (fleet.owner == m_player)
            emit message(tr("New fleet %1 launched from %2 (%3 mothership(s), %4 fighter(s))")
                             .arg(fleet.id)
                             .arg(p.name)
                             .arg(fleet.motherships)
                             .arg(fleet.fighters),
                         Tone::Success);
    }
}

void ConquestGame::moveFleets(double seconds)
{
    for (Fleet &fleet : m_fleets) {
        if (!fleet.moving())
            continue;
        const double length = std::max(1.0, distance(m_planets[fleet.at], m_planets[fleet.route.first()]));
        fleet.leg += kFleetSpeed * seconds / length;
        if (fleet.leg < 1.0)
            continue;
        fleet.leg = 0;
        fleet.at = fleet.route.takeFirst();
        const Planet &p = m_planets[fleet.at];
        // Stops at the first world that is not its own, or where enemies wait
        bool enemies = p.owner != fleet.owner;
        for (const Fleet &other : m_fleets)
            if (other.owner != fleet.owner && other.at == fleet.at && !other.moving())
                enemies = true;
        if (fleet.owner != m_player && fleet.at == m_at && !travelling())
            enemies = true;
        if (enemies || fleet.route.isEmpty()) {
            fleet.route.clear();
            fleet.target = -1;
            fleet.ordered = false;
            fleet.think = kThinkDelay;
        }
    }
}

// Utility AI: the best of attacking a beatable world, defending a threatened
// one, or regrouping with another fleet when too weak.
void ConquestGame::think(Fleet &fleet)
{
    const int me = fleet.owner;
    const double power = fleet.power() * powerMultiplier(me);
    double bestScore = 0.5;
    int bestTarget = -1;

    for (const Planet &t : m_planets) {
        if (t.id == fleet.at)
            continue;
        const double length = pathLength(fleet.at, t.id);
        if (length < 0)
            continue;
        const double nearness = 1.0 + length / 250.0;

        if (t.owner != me) {
            // Attack: only where the fleet clearly outguns the defenders
            double defense = sidePower(t.id, t.owner);
            if (me != m_player && t.id == m_at && !travelling())
                defense += kFlagshipPower;
            const double ratio = power / std::max(1.0, defense);
            if (ratio < kAttackRatio)
                continue;
            const double value = (t.capital ? 4.0 : 1.0) + t.population / 5.0 + (t.bonus != Bonus::None ? 1.0 : 0.0)
                               + (t.owner >= 0 ? 0.5 : 0.0);
            const double score = value * std::min(ratio, 3.0) / nearness;
            if (score > bestScore) {
                bestScore = score;
                bestTarget = t.id;
            }
        } else {
            // Defense: enemy fleets heading for one of our worlds
            double threat = 0;
            for (const Fleet &enemy : m_fleets)
                if (enemy.owner != me && (enemy.target == t.id || (enemy.at == t.id && !enemy.moving())))
                    threat += enemy.power();
            if (threat <= 0)
                continue;
            const double score = (t.capital ? 10.0 : 6.0) * std::min(power / threat, 2.0) / nearness;
            if (score > bestScore) {
                bestScore = score;
                bestTarget = t.id;
            }
        }
    }

    // Too weak alone: join another fleet of the faction
    if (power < 6) {
        for (const Fleet &other : m_fleets) {
            if (other.id == fleet.id || other.owner != me || other.moving() || other.at == fleet.at)
                continue;
            const double length = pathLength(fleet.at, other.at);
            if (length < 0)
                continue;
            const double score = 3.0 / (1.0 + length / 250.0);
            if (score > bestScore) {
                bestScore = score;
                bestTarget = other.at;
            }
        }
    }

    // Nothing to do: gather the forces (another fleet, or a full shipyard)
    if (bestTarget < 0) {
        double idleScore = 0;
        for (const Fleet &other : m_fleets) {
            if (other.id == fleet.id || other.owner != me || other.moving() || other.at == fleet.at)
                continue;
            const double length = pathLength(fleet.at, other.at);
            const double score = length < 0 ? 0 : 1.0 / (1.0 + length / 250.0);
            if (score > idleScore) {
                idleScore = score;
                bestTarget = other.at;
            }
        }
        for (const Planet &yard : m_planets) {
            if (yard.owner != me || yard.id == fleet.at || yard.dockMotherships * 4 + yard.dockFighters < 4)
                continue;
            const double length = pathLength(fleet.at, yard.id);
            const double score = length < 0 ? 0 : 1.2 / (1.0 + length / 250.0);
            if (score > idleScore) {
                idleScore = score;
                bestTarget = yard.id;
            }
        }
    }

    if (bestTarget < 0 || bestTarget == fleet.at)
        return;
    const Planet &target = m_planets[bestTarget];
    if (me != m_player && target.owner == m_player && fleet.target != bestTarget)
        emit message(tr("A %1 fleet (%2 mothership(s), %3 fighter(s)) heads for %4!")
                         .arg(m_factions[me].id)
                         .arg(fleet.motherships)
                         .arg(fleet.fighters)
                         .arg(target.name),
                     Tone::Warning);
    fleet.route = path(fleet.at, bestTarget);
    fleet.target = bestTarget;
}

const Skirmish *ConquestGame::skirmishAt(int planetId) const
{
    for (const Skirmish &s : m_skirmishes)
        if (s.planet == planetId)
            return &s;
    return nullptr;
}

int ConquestGame::sidePower(int planetId, int owner) const
{
    int power = 0;
    for (const Fleet &fleet : m_fleets)
        if (fleet.owner == owner && fleet.at == planetId && !fleet.moving())
            power += fleet.power();
    if (owner == m_planets[planetId].owner)
        power += garrisonPower(m_planets[planetId]);
    return int(std::round(power * powerMultiplier(owner)));
}

bool ConquestGame::observing() const
{
    if (travelling() || inBattle())
        return false;
    const Skirmish *battle = skirmishAt(m_at);
    return battle && battle->attacker != m_player && battle->defender != m_player;
}

bool ConquestGame::engage(const QString &faction, QString *error)
{
    auto fail = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (travelling() || inBattle())
        return fail(tr("the flagship must be in orbit, out of battle"));
    const Skirmish *battle = skirmishAt(m_at);
    if (!battle)
        return fail(tr("no battle here"));
    const QString wanted = faction.trimmed().toLower().remove('\'').remove(' ');
    int owner = -1;
    for (int f = 0; f < m_factions.size(); ++f)
        if (!wanted.isEmpty() && m_factions[f].id.toLower().remove('\'').remove(' ').startsWith(wanted))
            owner = f;
    if (owner < 0 || (owner != battle->attacker && owner != battle->defender) || owner == m_player)
        return fail(tr("the factions fighting here: %1 and %2")
                        .arg(m_factions[battle->attacker].id, ownerName(battle->defender)));
    if (!startFlagshipBattleAgainst(m_at, owner))
        return fail(tr("nothing left to fight"));
    return true;
}

// A battle begins where a faction's fleets wait at a world that is not theirs
void ConquestGame::startSkirmishes()
{
    for (const Planet &p : m_planets) {
        if (p.id == m_battlePlanet || skirmishAt(p.id))
            continue;
        int attacker = -1;
        int attackPower = 0;
        for (int f = 0; f < m_factions.size(); ++f) {
            if (f == p.owner)
                continue;
            const int power = sidePower(p.id, f);
            if (power > attackPower) {
                attackPower = power;
                attacker = f;
            }
        }
        if (attacker < 0)
            continue;
        const bool flagshipHere = m_at == p.id && !travelling() && !inBattle();
        if (flagshipHere && attacker != m_player)
            continue;               // the flagship fights them (3D battle)
        if (flagshipHere && p.owner < 0)
            continue;               // the player's fleets wait with the flagship
        m_skirmishes << Skirmish{p.id, attacker, p.owner, 0, 0, 0};
        if (attacker == m_player)
            emit message(tr("Your fleet attacks %1").arg(p.name), Tone::Info);
        else if (p.owner == m_player)
            emit message(tr("%1 is under attack by the %2!").arg(p.name, m_factions[attacker].id), Tone::Critical);
    }
}

// Both sides deal damage over time, in proportion to their strength
void ConquestGame::updateSkirmishes(double seconds)
{
    for (int i = int(m_skirmishes.size()) - 1; i >= 0; --i) {
        Skirmish &battle = m_skirmishes[i];
        if (battle.planet == m_battlePlanet)
            continue;               // paused while the flagship fights there
        const Planet &p = m_planets[battle.planet];
        if (p.owner != battle.defender) {
            // The world changed hands meanwhile (taken by the flagship)
            if (p.owner == battle.attacker) {
                m_skirmishes.removeAt(i);
                continue;
            }
            battle.defender = p.owner;
        }
        int attack = sidePower(battle.planet, battle.attacker);
        int defense = sidePower(battle.planet, battle.defender);
        if (attack > 0 && defense > 0) {
            battle.elapsed += seconds;
            battle.defenderPending += attack * kBattleRate * seconds * (0.8 + 0.4 * m_rng.generateDouble());
            battle.attackerPending += defense * kBattleRate * seconds * (0.8 + 0.4 * m_rng.generateDouble());
            applyDamage(battle.planet, battle.defender, battle.defenderPending);
            applyDamage(battle.planet, battle.attacker, battle.attackerPending);
            attack = sidePower(battle.planet, battle.attacker);
            defense = sidePower(battle.planet, battle.defender);
        }
        if (attack <= 0)
            endSkirmish(i, false);
        else if (defense <= 0)
            endSkirmish(i, true);
    }
    m_fleets.erase(std::remove_if(m_fleets.begin(), m_fleets.end(),
                                  [](const Fleet &f) { return f.motherships + f.fighters <= 0; }),
                   m_fleets.end());
}

// Losses of one side: fighters first, then the garrison, then motherships
void ConquestGame::applyDamage(int planetId, int owner, double &pending)
{
    Planet &p = m_planets[planetId];
    const bool home = owner == p.owner;
    auto fleetWith = [&](bool motherships) -> Fleet * {
        for (Fleet &f : m_fleets)
            if (f.owner == owner && f.at == planetId && !f.moving() && (motherships ? f.motherships : f.fighters) > 0)
                return &f;
        return nullptr;
    };
    while (pending >= 1.0) {
        if (Fleet *f = fleetWith(false)) {
            --f->fighters;
            pending -= 1.0;
        } else if (home && p.dockFighters > 0) {
            --p.dockFighters;
            pending -= 1.0;
        } else if (home && p.defense > 0) {
            if (pending < 3.0)
                break;
            --p.defense;
            pending -= 3.0;
        } else if (Fleet *f = fleetWith(true)) {
            if (pending < 4.0)
                break;
            --f->motherships;
            pending -= 4.0;
        } else if (home && p.dockMotherships > 0) {
            if (pending < 4.0)
                break;
            --p.dockMotherships;
            pending -= 4.0;
        } else {
            pending = 0;
        }
    }
}

void ConquestGame::endSkirmish(int index, bool attackerWon)
{
    const Skirmish battle = m_skirmishes.takeAt(index);
    Planet &p = m_planets[battle.planet];
    const QString name = p.name;
    const QString defender = ownerName(battle.defender);
    const QString attacker = m_factions[battle.attacker].id;
    if (attackerWon) {
        const int previous = p.owner;
        p.defense = 1;
        p.population *= 0.75;
        if (battle.attacker == m_player)
            emit message(tr("Your fleet takes %1 from the %2").arg(name, defender), Tone::Success);
        else if (previous == m_player)
            emit message(tr("%1 falls to the %2!").arg(name, attacker), Tone::Critical);
        else
            emit message(tr("The %1 take %2 from the %3").arg(attacker, name, defender), Tone::Info);
        if (hasTrait(battle.attacker, "Lucian Alliance") && previous >= 0) {
            const double loot = std::min(60.0, m_factions[previous].naquadah);
            m_factions[previous].naquadah -= loot;
            m_factions[battle.attacker].naquadah += loot;
            if (previous == m_player || battle.attacker == m_player)
                emit message(tr("Lucian raid on %1: %2 naquadah stolen").arg(name).arg(qRound(loot)), Tone::Warning);
        }
        capture(battle.planet, battle.attacker);
    } else {
        p.defense = std::max(1, p.defense);
        if (p.owner == m_player)
            emit message(tr("%1 holds against a %2 attack").arg(name, attacker), Tone::Success);
        else if (battle.attacker == m_player)
            emit message(tr("Your fleet is destroyed attacking %1").arg(name), Tone::Warning);
        emit planetChanged(battle.planet);
    }
}

// Fleets of the same faction waiting at the same world become one
void ConquestGame::mergeFleets()
{
    for (int i = 0; i < m_fleets.size(); ++i) {
        Fleet &a = m_fleets[i];
        if (a.moving())
            continue;
        for (int j = int(m_fleets.size()) - 1; j > i; --j) {
            const Fleet &b = m_fleets[j];
            if (b.moving() || b.owner != a.owner || b.at != a.at)
                continue;
            a.motherships += b.motherships;
            a.fighters += b.fighters;
            a.ordered = a.ordered && b.ordered;
            m_fleets.removeAt(j);
        }
    }
}

// ================================================================
//  Save
// ================================================================

QJsonObject ConquestGame::toJson() const
{
    QJsonArray factions;
    for (const Faction &f : m_factions)
        factions.append(QJsonObject{{"id", f.id}, {"player", f.player}, {"naquadah", f.naquadah},
                                    {"trinium", f.trinium}});
    QJsonArray planets;
    for (const Planet &p : m_planets) {
        QJsonArray lanes;
        for (int n : p.lanes)
            lanes.append(n);
        planets.append(QJsonObject{{"name", p.name}, {"x", p.pos.x()}, {"y", p.pos.y()}, {"owner", p.owner},
                                   {"population", p.population}, {"maxPopulation", p.maxPopulation},
                                   {"bonus", int(p.bonus)}, {"defense", p.defense}, {"capital", p.capital},
                                   {"lanes", lanes}, {"dockM", p.dockMotherships}, {"dockF", p.dockFighters},
                                   {"buildF", p.fighterProgress}, {"buildM", p.mothershipProgress}});
    }
    QJsonArray fleets;
    for (const Fleet &f : m_fleets) {
        QJsonArray route;
        for (int r : f.route)
            route.append(r);
        fleets.append(QJsonObject{{"id", f.id}, {"owner", f.owner}, {"motherships", f.motherships},
                                  {"fighters", f.fighters}, {"at", f.at}, {"route", route}, {"leg", f.leg},
                                  {"target", f.target}, {"ordered", f.ordered}, {"think", f.think}});
    }
    QJsonArray route;
    for (int r : m_route)
        route.append(r);
    QJsonArray skirmishes;
    for (const Skirmish &b : m_skirmishes)
        skirmishes.append(QJsonObject{{"planet", b.planet}, {"attacker", b.attacker}, {"defender", b.defender},
                                      {"attackerPending", b.attackerPending}, {"defenderPending", b.defenderPending},
                                      {"elapsed", b.elapsed}});
    return {{"version", 3}, {"factions", factions}, {"planets", planets}, {"fleets", fleets},
            {"skirmishes", skirmishes}, {"eventClock", m_eventClock},
            {"nextFleet", m_nextFleetId}, {"at", m_at}, {"previous", m_previous}, {"route", route},
            {"leg", m_legProgress}, {"capture", m_capture}, {"hull", m_hull}, {"battle", m_battlePlanet},
            {"battleOwner", m_battleOwner}, {"over", m_over}};
}

bool ConquestGame::fromJson(const QJsonObject &json)
{
    const int version = json.value("version").toInt();
    if (version < 1 || version > 3)
        return false;
    QList<Faction> factions;
    int player = -1;
    for (const QJsonValue &v : json.value("factions").toArray()) {
        const QJsonObject o = v.toObject();
        const int info = factionInfoIndex(o.value("id").toString());
        if (info < 0)
            return false;
        if (o.value("player").toBool())
            player = int(factions.size());
        factions << Faction{o.value("id").toString(), factionInfos()[info].color, o.value("player").toBool(),
                            o.value("naquadah").toDouble(), o.value("trinium").toDouble()};
    }
    QList<Planet> planets;
    for (const QJsonValue &v : json.value("planets").toArray()) {
        const QJsonObject o = v.toObject();
        Planet p;
        p.id = int(planets.size());
        p.name = o.value("name").toString();
        p.pos = QPointF(o.value("x").toDouble(), o.value("y").toDouble());
        p.owner = o.value("owner").toInt(-1);
        p.population = o.value("population").toDouble();
        p.maxPopulation = std::max(1.0, o.value("maxPopulation").toDouble());
        p.bonus = Bonus(std::clamp(o.value("bonus").toInt(), 0, int(Bonus::AncientSite)));
        p.defense = std::clamp(o.value("defense").toInt(1), 0, 5);
        p.capital = o.value("capital").toBool();
        for (const QJsonValue &n : o.value("lanes").toArray())
            p.lanes << n.toInt();
        p.dockMotherships = std::clamp(o.value("dockM").toInt(), 0, kDockMotherships);
        p.dockFighters = std::clamp(o.value("dockF").toInt(), 0, kDockFighters);
        p.fighterProgress = std::clamp(o.value("buildF").toDouble(), 0.0, 1.0);
        p.mothershipProgress = std::clamp(o.value("buildM").toDouble(), 0.0, 1.0);
        planets << p;
    }
    const int count = int(planets.size());
    auto valid = [count](int id) { return id >= 0 && id < count; };
    if (player < 0 || count < 2 || !valid(json.value("at").toInt(-1)))
        return false;
    for (const Planet &p : planets) {
        if (p.owner >= int(factions.size()))
            return false;
        for (int n : p.lanes)
            if (!valid(n))
                return false;
    }
    QList<Fleet> fleets;
    int nextFleet = 1;
    for (const QJsonValue &v : json.value("fleets").toArray()) {
        const QJsonObject o = v.toObject();
        Fleet f;
        f.id = o.value("id").toInt();
        f.owner = o.value("owner").toInt(-1);
        f.motherships = std::max(0, o.value("motherships").toInt());
        f.fighters = std::max(0, o.value("fighters").toInt());
        f.at = o.value("at").toInt(-1);
        if (f.owner < 0 || f.owner >= factions.size() || !valid(f.at) || f.motherships + f.fighters == 0)
            continue;
        for (const QJsonValue &r : o.value("route").toArray())
            if (valid(r.toInt(-1)))
                f.route << r.toInt();
        f.leg = std::clamp(o.value("leg").toDouble(), 0.0, 1.0);
        f.target = valid(o.value("target").toInt(-1)) ? o.value("target").toInt() : -1;
        f.ordered = o.value("ordered").toBool();
        f.think = o.value("think").toDouble();
        nextFleet = std::max(nextFleet, f.id + 1);
        fleets << f;
    }

    QList<Skirmish> skirmishes;
    for (const QJsonValue &v : json.value("skirmishes").toArray()) {
        const QJsonObject o = v.toObject();
        const Skirmish b{o.value("planet").toInt(-1), o.value("attacker").toInt(-1), o.value("defender").toInt(-1),
                         o.value("attackerPending").toDouble(), o.value("defenderPending").toDouble(),
                         o.value("elapsed").toDouble()};
        if (valid(b.planet) && b.attacker >= 0 && b.attacker < factions.size() && b.defender < factions.size())
            skirmishes << b;
    }

    m_factions = factions;
    m_planets = planets;
    m_fleets = fleets;
    m_skirmishes = skirmishes;
    m_eventClock = std::clamp(json.value("eventClock").toDouble(), 0.0, kEventPeriod);
    m_nextFleetId = std::max(nextFleet, json.value("nextFleet").toInt(1));
    m_player = player;
    m_at = json.value("at").toInt();
    m_previous = valid(json.value("previous").toInt(-1)) ? json.value("previous").toInt() : m_at;
    m_route.clear();
    for (const QJsonValue &v : json.value("route").toArray())
        if (valid(v.toInt(-1)))
            m_route << v.toInt();
    m_legProgress = std::clamp(json.value("leg").toDouble(), 0.0, 1.0);
    m_capture = std::clamp(json.value("capture").toDouble(), 0.0, 1.0);
    m_hull = std::clamp(json.value("hull").toDouble(100), 1.0, 100.0);
    m_battlePlanet = valid(json.value("battle").toInt(-1)) ? json.value("battle").toInt() : -1;
    m_battleOwner = json.value("battleOwner").toInt(-1);
    if (m_battlePlanet >= 0 && (m_battleOwner < 0 || m_battleOwner >= m_factions.size()))
        m_battleOwner = m_planets[m_battlePlanet].owner;
    if (m_battleOwner < 0)
        m_battlePlanet = -1;
    m_over = json.value("over").toBool();
    return true;
}
