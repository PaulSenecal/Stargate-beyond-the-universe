#include "conquestgame.h"

#include <QJsonArray>
#include <QLineF>
#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

using namespace Conquest;

namespace {

constexpr double kFlagshipSpeed = 70.0;         // map units per second
constexpr double kCaptureBase = 4.0;            // seconds in orbit, + kCapturePerDefense per level
constexpr double kCapturePerDefense = 3.0;
constexpr double kHullRegen = 0.3;              // % per second at an owned planet
constexpr double kShipyardRegen = 1.5;
constexpr double kTriniumPerHull = 0.5;         // repair cost

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
}

void ConquestGame::generate(const Setup &setup)
{
    QRandomGenerator rng(setup.seed ? setup.seed : QRandomGenerator::global()->generate());
    m_planets.clear();
    m_factions.clear();
    m_route.clear();
    m_legProgress = m_capture = 0;
    m_hull = 100;
    m_battlePlanet = -1;
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

double ConquestGame::naquadahRate(const Planet &p)
{
    return 0.02 * p.population * (p.bonus == Bonus::NaquadahMine ? 2.0 : 1.0);
}

double ConquestGame::triniumRate(const Planet &p)
{
    return 0.01 * p.population * (p.bonus == Bonus::TriniumMine ? 2.0 : 1.0);
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

QPointF ConquestGame::flagshipPos() const
{
    const QPointF from = m_planets.value(m_at).pos;
    if (m_route.isEmpty())
        return from;
    const QPointF to = m_planets.value(m_route.first()).pos;
    return from + (to - from) * m_legProgress;
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
        p.population += 0.02 * p.population * (1.0 - p.population / p.maxPopulation) * seconds;
    }

    if (inBattle())
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

    const Planet &here = m_planets[m_at];
    if (here.owner < 0) {
        // Neutral world: taken after a stay in orbit
        m_capture += seconds / (kCaptureBase + kCapturePerDefense * here.defense);
        if (m_capture >= 1.0) {
            m_capture = 0;
            emit message(tr("%1 joins your faction").arg(here.name), Tone::Success);
            capture(m_at);
        }
    } else if (here.owner == m_player && m_hull < 100.0) {
        m_hull = std::min(100.0, m_hull + (here.bonus == Bonus::Shipyard ? kShipyardRegen : kHullRegen) * seconds);
    }
}

void ConquestGame::arrive(int planetId)
{
    const Planet &p = m_planets[planetId];
    if (p.owner >= 0 && p.owner != m_player) {
        m_route.clear();
        m_battlePlanet = planetId;
        const int big = fleetMotherships(p), small = fleetFighters(p);
        emit message(tr("%1 is defended by the %2 fleet: battle!").arg(p.name, m_factions[p.owner].id),
                     Tone::Critical);
        emit battleStarted(planetId, m_factions[p.owner].id, big, small);
        return;
    }
    if (!m_route.isEmpty())
        return;                     // only passing through
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

void ConquestGame::battleWon()
{
    if (!inBattle())
        return;
    const int id = m_battlePlanet;
    m_battlePlanet = -1;
    Planet &p = m_planets[id];
    const QString loser = m_factions[p.owner].id;
    p.defense = std::max(1, p.defense - 1);
    p.population *= 0.8;                    // the battle took its toll
    emit message(tr("%1 taken from the %2").arg(p.name, loser), Tone::Success);
    capture(id);
}

void ConquestGame::battleLost()
{
    if (!inBattle())
        return;
    const QString planet = m_planets[m_battlePlanet].name;
    m_battlePlanet = -1;
    m_route.clear();
    m_at = m_previous;
    emit message(tr("Hull breached: the flagship retreats from %1 to %2").arg(planet, m_planets[m_at].name),
                 Tone::Critical);
}

void ConquestGame::capture(int planetId)
{
    Planet &p = m_planets[planetId];
    const int previousOwner = p.owner;
    p.owner = m_player;
    if (p.bonus == Bonus::AncientSite) {
        m_factions[m_player].naquadah += 100;
        emit message(tr("Ancient site on %1: +100 naquadah from its archives").arg(p.name), Tone::Success);
    }
    if (previousOwner >= 0 && planetCount(previousOwner) == 0)
        emit message(tr("The %1 have lost their last world").arg(m_factions[previousOwner].id), Tone::Success);
    emit planetChanged(planetId);
    checkVictory();
}

void ConquestGame::checkVictory()
{
    if (m_over)
        return;
    bool enemyLeft = false;
    for (int f = 0; f < m_factions.size(); ++f)
        if (f != m_player && planetCount(f) > 0)
            enemyLeft = true;
    if (!enemyLeft || planetCount(m_player) == 0) {
        m_over = true;
        emit gameOver(!enemyLeft);
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
                                   {"lanes", lanes}});
    }
    QJsonArray route;
    for (int r : m_route)
        route.append(r);
    return {{"version", 1}, {"factions", factions}, {"planets", planets}, {"at", m_at},
            {"previous", m_previous}, {"route", route}, {"leg", m_legProgress}, {"capture", m_capture},
            {"hull", m_hull}, {"battle", m_battlePlanet}, {"over", m_over}};
}

bool ConquestGame::fromJson(const QJsonObject &json)
{
    if (json.value("version").toInt() != 1)
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
        p.defense = std::clamp(o.value("defense").toInt(1), 1, 5);
        p.capital = o.value("capital").toBool();
        for (const QJsonValue &n : o.value("lanes").toArray())
            p.lanes << n.toInt();
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

    m_factions = factions;
    m_planets = planets;
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
    m_over = json.value("over").toBool();
    return true;
}
