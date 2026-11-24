#include "conquestgame.h"
#include "src/core/balance.h"

#include <QJsonArray>
#include <QLineF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

using namespace Conquest;

namespace {

using namespace Balance;               // all the tunable values: src/core/balance.h
constexpr double kBattleRate = kAutoBattleRate;

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
        {QStringLiteral("Asgard"), QStringLiteral("#b8c4d6"), QStringLiteral("Orilla")},
        {QStringLiteral("Ancients"), QStringLiteral("#6fd0e0"), QStringLiteral("Atlantis")},
        {QStringLiteral("Ori"), QStringLiteral("#e07b30"), QStringLiteral("Celestis")},
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

const QList<TechInfo> &Conquest::techInfos()
{
    static const QList<TechInfo> infos{
        {QStringLiteral("weapons1"), 0, 1, 100, 60},  {QStringLiteral("weapons2"), 0, 2, 200, 90},
        {QStringLiteral("weapons3"), 0, 3, 350, 120}, {QStringLiteral("defense1"), 1, 1, 100, 60},
        {QStringLiteral("defense2"), 1, 2, 200, 90},  {QStringLiteral("defense3"), 1, 3, 350, 120},
        {QStringLiteral("industry1"), 2, 1, 100, 60}, {QStringLiteral("industry2"), 2, 2, 200, 90},
        {QStringLiteral("industry3"), 2, 3, 350, 120},
    };
    return infos;
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
    case Size::Huge: return 56;
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
    const int enemies = setup.size == Size::Small ? 1 : setup.size == Size::Medium ? 2 : setup.size == Size::Large ? 3 : 6;

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
    for (int i : chosen) {
        Faction faction;
        faction.id = infos[i].id;
        faction.color = infos[i].color;
        faction.player = i == playerInfo;
        faction.naquadah = 100;
        faction.trinium = 50;
        m_factions << faction;
    }
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

int ConquestGame::garrisonPower(const Planet &p) const
{
    const int perLevel = hasTech(p.owner, "defense1") ? 4 : 3;        // reinforced garrisons
    const int drones = hasTrait(p.owner, "Ancients") ? p.defense / 2 : 0;  // drone defenses
    return p.defense * perLevel + drones + 4 * p.dockMotherships + p.dockFighters;
}

double ConquestGame::naquadahRate(const Planet &p) const
{
    const double slavery = hasTrait(p.owner, "Goa'uld") ? 1.6 : 1.0;
    const double refining = hasTech(p.owner, "industry3") ? 1.25 : 1.0;
    return 0.02 * p.population * (p.bonus == Bonus::NaquadahMine ? 2.0 : 1.0) * slavery * refining;
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
    double weapons = 1.0 + (hasTech(faction, "weapons1") ? 0.10 : 0.0) + (hasTech(faction, "weapons2") ? 0.10 : 0.0)
                   + (hasTech(faction, "weapons3") ? 0.15 : 0.0);
    if (hasTrait(faction, "Asgard"))
        weapons *= 1.15;                // Asgard technology
    if (hasTrait(faction, "Ori"))
        weapons *= 1.1;                 // faith of the crusade
    if (!hasTrait(faction, "Tau'ri"))
        return weapons;
    const int sites = int(std::count_if(m_planets.cbegin(), m_planets.cend(), [faction](const Planet &p) {
        return p.owner == faction && p.bonus == Bonus::AncientSite;
    }));
    return weapons * (1.0 + std::min(0.4, 0.08 * sites));
}

// ================================================================
//  Technologies
// ================================================================

bool ConquestGame::hasTech(int faction, const QString &tech) const
{
    return faction >= 0 && faction < m_factions.size() && m_factions[faction].techs.contains(tech);
}

double ConquestGame::researchCost(int faction, const TechInfo &tech) const
{
    return hasTrait(faction, "Asgard") ? tech.cost * 0.75 : tech.cost;
}

double ConquestGame::researchTime(int faction, const TechInfo &tech) const
{
    return hasTrait(faction, "Asgard") ? tech.seconds * 0.5 : tech.seconds;
}

bool ConquestGame::canResearch(int faction, const QString &tech, QString *why) const
{
    auto no = [why](const QString &text) {
        if (why)
            *why = text;
        return false;
    };
    const auto info = std::find_if(techInfos().cbegin(), techInfos().cend(),
                                   [&tech](const TechInfo &t) { return t.id == tech; });
    if (info == techInfos().cend() || faction < 0 || faction >= m_factions.size())
        return no(tr("unknown technology"));
    const Faction &f = m_factions[faction];
    if (f.techs.contains(tech))
        return no(tr("already researched"));
    if (!f.research.isEmpty())
        return no(tr("%1 is being researched").arg(techName(f.research)));
    for (const TechInfo &t : techInfos())
        if (t.branch == info->branch && t.tier == info->tier - 1 && !f.techs.contains(t.id))
            return no(tr("needs %1 first").arg(techName(t.id)));
    if (f.naquadah < researchCost(faction, *info))
        return no(tr("%1 naquadah needed (%2 available)").arg(researchCost(faction, *info)).arg(int(f.naquadah)));
    return true;
}

bool ConquestGame::startResearch(const QString &tech, QString *error)
{
    if (!canResearch(m_player, tech, error))
        return false;
    const auto info = std::find_if(techInfos().cbegin(), techInfos().cend(),
                                   [&tech](const TechInfo &t) { return t.id == tech; });
    Faction &me = m_factions[m_player];
    me.naquadah -= researchCost(m_player, *info);
    me.research = tech;
    me.researchProgress = 0;
    emit message(tr("Research started: %1 (%2 s)").arg(techName(tech)).arg(researchTime(m_player, *info)), Tone::Info);
    return true;
}

int ConquestGame::findTech(const QString &nameOrNumber) const
{
    const QString wanted = nameOrNumber.trimmed().toLower();
    bool isNumber = false;
    const int number = wanted.toInt(&isNumber);
    if (isNumber)
        return number >= 1 && number <= techInfos().size() ? number - 1 : -1;
    for (int i = 0; i < techInfos().size(); ++i)
        if (!wanted.isEmpty()
            && (techInfos()[i].id == wanted || techName(techInfos()[i].id).toLower().startsWith(wanted)))
            return i;
    return -1;
}

QString ConquestGame::techName(const QString &tech)
{
    if (tech == "weapons1") return tr("Naquadah-enhanced weapons");
    if (tech == "weapons2") return tr("Improved plasma");
    if (tech == "weapons3") return tr("Asgard beams");
    if (tech == "defense1") return tr("Reinforced garrisons");
    if (tech == "defense2") return tr("Shield generators");
    if (tech == "defense3") return tr("Planetary shields");
    if (tech == "industry1") return tr("Improved shipyards");
    if (tech == "industry2") return tr("Hyperdrive");
    if (tech == "industry3") return tr("Naquadah refining");
    return tech;
}

QString ConquestGame::techEffect(const QString &tech)
{
    if (tech == "weapons1") return tr("fleets +10%, flagship damage +15%");
    if (tech == "weapons2") return tr("fleets +10%, flagship damage +15%");
    if (tech == "weapons3") return tr("fleets +15%, flagship damage +25%");
    if (tech == "defense1") return tr("garrisons 4 instead of 3 per defense level");
    if (tech == "defense2") return tr("flagship shield regenerates 50% faster");
    if (tech == "defense3") return tr("defenses rebuilt twice as fast, up to 4");
    if (tech == "industry1") return tr("shipyards +25%");
    if (tech == "industry2") return tr("fleets and flagship 30% faster");
    if (tech == "industry3") return tr("naquadah +25%");
    return QString();
}

double ConquestGame::flagshipDamageBonus() const
{
    return 1.0 + (hasTech(m_player, "weapons1") ? 0.15 : 0.0) + (hasTech(m_player, "weapons2") ? 0.15 : 0.0)
         + (hasTech(m_player, "weapons3") ? 0.25 : 0.0);
}

double ConquestGame::flagshipShieldBonus() const
{
    return hasTech(m_player, "defense2") ? 1.5 : 1.0;
}

// ================================================================
//  Alliances
// ================================================================

bool ConquestGame::isAllied(int faction) const
{
    return faction >= 0 && faction < m_factions.size() && faction != m_player && m_factions[faction].allied;
}

bool ConquestGame::hostile(int a, int b) const
{
    if (a == b)
        return false;
    if (a < 0 || b < 0)
        return true;                // neutral worlds can be taken by anyone
    if (a == m_player)
        return !isAllied(b);
    if (b == m_player)
        return !isAllied(a);
    return !hasPact(a, b);
}

bool ConquestGame::hasPact(int a, int b) const
{
    return a >= 0 && b >= 0 && a < m_factions.size() && b < m_factions.size()
        && m_factions[a].pacts.contains(m_factions[b].id);
}

// Battles in progress between two factions that just made peace stop
void ConquestGame::stopFightsBetween(int a, int b)
{
    m_skirmishes.erase(std::remove_if(m_skirmishes.begin(), m_skirmishes.end(),
                                      [this](const Skirmish &s) { return !hostile(s.attacker, s.defender); }),
                       m_skirmishes.end());
    for (Fleet &fleet : m_fleets)
        if ((fleet.owner == a || fleet.owner == b) && fleet.target >= 0
            && !hostile(fleet.owner, m_planets[fleet.target].owner)) {
            fleet.route.clear();
            fleet.target = -1;
        }
}

// AI factions ally against a much stronger common enemy, part when the
// threat is gone, and the stronger one may betray the weaker
void ConquestGame::aiDiplomacy()
{
    auto alive = [this](int f) { return planetCount(f) > 0; };
    for (int a = 0; a < m_factions.size(); ++a) {
        for (int b = a + 1; b < m_factions.size(); ++b) {
            if (a == m_player || b == m_player || !alive(a) || !alive(b))
                continue;
            const int pa = planetCount(a), pb = planetCount(b);
            int strongest = -1, strength = 0;
            for (int g = 0; g < m_factions.size(); ++g)
                if (g != a && g != b && planetCount(g) > strength) {
                    strength = planetCount(g);
                    strongest = g;
                }
            const bool threat = strongest >= 0 && strength >= 4 && strength >= 1.3 * std::max(pa, pb);
            Faction &fa = m_factions[a];
            Faction &fb = m_factions[b];
            if (!hasPact(a, b)) {
                if (threat && m_rng.generateDouble() < kPactChance) {
                    fa.pacts << fb.id;
                    fb.pacts << fa.id;
                    stopFightsBetween(a, b);
                    emit message(tr("The %1 and the %2 ally against the %3").arg(fa.id, fb.id, m_factions[strongest].id),
                                 strongest == m_player ? Tone::Warning : Tone::Info);
                }
                continue;
            }
            if (std::max(pa, pb) >= 2 * std::min(pa, pb) && m_rng.generateDouble() < kBetrayalChance) {
                const QString traitor = pa > pb ? fa.id : fb.id;
                const QString victim = pa > pb ? fb.id : fa.id;
                fa.pacts.removeAll(fb.id);
                fb.pacts.removeAll(fa.id);
                emit message(tr("The %1 betray the %2!").arg(traitor, victim), Tone::Warning);
            } else if (!threat && m_rng.generateDouble() < kPactEndChance) {
                fa.pacts.removeAll(fb.id);
                fb.pacts.removeAll(fa.id);
                emit message(tr("The %1 and the %2 end their alliance").arg(fa.id, fb.id), Tone::Info);
            }
        }
    }
}

int ConquestGame::findFaction(const QString &name) const
{
    const QString wanted = name.trimmed().toLower().remove('\'').remove(' ');
    if (wanted.isEmpty())
        return -1;
    for (int f = 0; f < m_factions.size(); ++f)
        if (m_factions[f].id.toLower().remove('\'').remove(' ').startsWith(wanted))
            return f;
    return -1;
}

bool ConquestGame::proposeAlliance(const QString &faction, QString *answer)
{
    auto reply = [answer](const QString &text, bool ok) {
        if (answer)
            *answer = text;
        return ok;
    };
    const int f = findFaction(faction);
    if (f < 0 || f == m_player)
        return reply(tr("unknown faction"), false);
    Faction &other = m_factions[f];
    if (other.allied)
        return reply(tr("the %1 are already your allies").arg(other.id), false);
    if (other.allyCooldown > 0)
        return reply(tr("the %1 will not listen again before %2 s").arg(other.id).arg(qRound(other.allyCooldown)),
                     false);
    if (planetCount(f) == 0 && fleetCount(f) == 0)
        return reply(tr("the %1 are no more").arg(other.id), false);

    // They accept against a common enemy at least as strong as them,
    // if the player does not already dominate the galaxy
    const double share = double(planetCount(m_player)) / std::max<qsizetype>(1, m_planets.size());
    bool commonEnemy = false;
    for (int g = 0; g < m_factions.size(); ++g)
        if (g != f && g != m_player && planetCount(g) >= planetCount(f))
            commonEnemy = true;
    if (share >= 0.4) {
        other.allyCooldown = 60;
        return reply(tr("the %1 refuse: your power frightens them").arg(other.id), false);
    }
    if (!commonEnemy) {
        other.allyCooldown = 60;
        return reply(tr("the %1 refuse: they see no common enemy").arg(other.id), false);
    }
    other.allied = true;
    // Battles between the new allies stop
    m_skirmishes.erase(std::remove_if(m_skirmishes.begin(), m_skirmishes.end(),
                                      [this](const Skirmish &b) { return !hostile(b.attacker, b.defender); }),
                       m_skirmishes.end());
    for (Fleet &fleet : m_fleets)
        if (fleet.owner == f && fleet.target >= 0 && !hostile(f, m_planets[fleet.target].owner))
            fleet.route.clear(), fleet.target = -1;
    emit message(tr("Alliance sealed with the %1: they will not attack you").arg(other.id), Tone::Success);
    return reply(QString(), true);
}

bool ConquestGame::breakAlliance(const QString &faction, QString *error)
{
    const int f = findFaction(faction);
    if (!isAllied(f)) {
        if (error)
            *error = tr("no alliance with that faction");
        return false;
    }
    m_factions[f].allied = false;
    m_factions[f].allyCooldown = 120;
    emit message(tr("Alliance with the %1 broken").arg(m_factions[f].id), Tone::Warning);
    return true;
}

QString ConquestGame::hangarFighter(const QString &factionId)
{
    if (factionId == "Goa'uld")
        return QStringLiteral("Death Glider");
    if (factionId == "Wraith")
        return QStringLiteral("Wraith Dart");
    if (factionId == "Lucian Alliance")
        return QStringLiteral("Glider");
    if (factionId == "Asgard")
        return QStringLiteral("Asgard Scout");
    if (factionId == "Ancients")
        return QStringLiteral("Puddle Jumper");
    if (factionId == "Ori")
        return QStringLiteral("Ori Fighter");
    return QStringLiteral("F-302");
}

void ConquestGame::setFleetStrength(int fleetId, int motherships, int fighters)
{
    for (int i = 0; i < m_fleets.size(); ++i) {
        if (m_fleets[i].id != fleetId)
            continue;
        m_fleets[i].motherships = std::max(0, motherships);
        m_fleets[i].fighters = std::max(0, fighters);
        if (m_fleets[i].motherships + m_fleets[i].fighters == 0)
            m_fleets.removeAt(i);
        return;
    }
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
                  "neighbours may join, the flagship rallies worlds twice as fast. Shipyards x1.1.");
    if (factionId == "Lucian Alliance")
        return tr("Smuggling: fighters built x1.25 faster. Raids: every world taken steals naquadah from its owner.");
    if (factionId == "Asgard")
        return tr("Advanced technology: research twice as fast and 25% cheaper, fleets +15%; but shipyards "
                  "x0.7 - the Asgard are few.");
    if (factionId == "Ancients")
        return tr("Knowledge of the Ancients: Ancient sites give 200 naquadah, drone defenses (stronger "
                  "garrisons); but shipyards x0.8.");
    if (factionId == "Ori")
        return tr("Crusade: conquered worlds keep most of their people, who grow faster; the Priors convert "
                  "neutral neighbours; fleets +10%; but the crusade builds slowly (shipyards x0.85).");
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
            p.population += (hasTrait(p.owner, "Ori") ? 0.025 : 0.02) * p.population
                          * (1.0 - p.population / p.maxPopulation) * seconds;
        // Garrisons are rebuilt little by little
        const bool shields = hasTech(p.owner, "defense3");          // planetary shields
        const int rebuiltMax = p.capital ? 5 : shields ? 4 : 3;
        if (p.defense < rebuiltMax && p.id != m_battlePlanet) {
            p.rebuild += seconds * (shields ? 2.0 : 1.0) / kDefenseRebuild;
            if (p.rebuild >= 1.0) {
                p.rebuild = 0;
                ++p.defense;
            }
        }
    }

    // Autonomous fleets of every faction
    // Research and diplomacy clocks
    for (int f = 0; f < m_factions.size(); ++f) {
        Faction &faction = m_factions[f];
        faction.allyCooldown = std::max(0.0, faction.allyCooldown - seconds);
        if (faction.research.isEmpty())
            continue;
        const auto info = std::find_if(techInfos().cbegin(), techInfos().cend(),
                                       [&faction](const TechInfo &t) { return t.id == faction.research; });
        if (info == techInfos().cend()) {
            faction.research.clear();
            continue;
        }
        faction.researchProgress += seconds / researchTime(f, *info);
        if (faction.researchProgress >= 1.0) {
            faction.techs << faction.research;
            if (f == m_player)
                emit message(tr("Research complete: %1 - %2").arg(techName(faction.research), techEffect(faction.research)),
                             Tone::Success);
            faction.research.clear();
            faction.researchProgress = 0;
        }
    }

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
        m_legProgress += kFlagshipSpeed * (hasTech(m_player, "industry2") ? 1.3 : 1.0) * seconds / length;
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
    if (p.owner >= 0 && hostile(p.owner, againstOwner)) {
        owner = p.owner;
    } else {
        int best = 0;
        for (int f = 0; f < m_factions.size(); ++f) {
            if (!hostile(f, againstOwner))
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
    // A battle between two other factions (allies too): the player decides ('engage')
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
        p.population *= hasTrait(m_player, "Ori") ? 0.9 : 0.8;              // the battle took its toll
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
        const int archives = hasTrait(newOwner, "Ancients") ? 200 : 100;     // they can read them
        m_factions[newOwner].naquadah += archives;
        if (newOwner == m_player)
            emit message(tr("Ancient site on %1: +%2 naquadah from its archives").arg(p.name).arg(archives), Tone::Success);
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
        if (hostile(f, m_player) && (planetCount(f) > 0 || fleetCount(f) > 0))
            enemyLeft = true;
    if (!enemyLeft || planetCount(m_player) == 0) {
        if (!enemyLeft && std::any_of(m_factions.cbegin(), m_factions.cend(), [](const Faction &f) { return f.allied; }))
            emit message(tr("Allied victory: only you and your allies remain"), Tone::Success);
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
        // Ori Priors convert a neutral neighbour
        if (p.owner < 0 && quiet(p)) {
            bool converted = false;
            for (int n : p.lanes) {
                const int f = m_planets[n].owner;
                if (hasTrait(f, "Ori") && m_rng.generateDouble() < kConversionChance) {
                    emit message(tr("The Priors convert %1 to Origin").arg(p.name), f == m_player ? Tone::Success : Tone::Info);
                    capture(p.id, f);
                    converted = true;
                    break;
                }
            }
            if (converted)
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
    // AI research: the cheapest technology they can afford, keeping a reserve
    for (int f = 0; f < m_factions.size(); ++f) {
        if (f == m_player || !m_factions[f].research.isEmpty())
            continue;
        for (const TechInfo &t : techInfos()) {
            if (canResearch(f, t.id) && m_factions[f].naquadah >= researchCost(f, t) + 40) {
                m_factions[f].naquadah -= researchCost(f, t);
                m_factions[f].research = t.id;
                m_factions[f].researchProgress = 0;
                break;
            }
        }
    }

    // Allies fear a player who holds too much of the galaxy
    const double share = double(planetCount(m_player)) / std::max<qsizetype>(1, m_planets.size());
    for (int f = 0; f < m_factions.size(); ++f)
        if (isAllied(f) && share > 0.45) {
            m_factions[f].allied = false;
            m_factions[f].allyCooldown = 120;
            emit message(tr("The %1 fear your power and break the alliance!").arg(m_factions[f].id), Tone::Critical);
        }

    aiDiplomacy();

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
        const double yard = hasTrait(p.owner, "Wraith") ? 1.6
                          : hasTrait(p.owner, "Goa'uld") ? 1.25
                          : hasTrait(p.owner, "Asgard") ? 0.7
                          : hasTrait(p.owner, "Ancients") ? 0.8
                          : hasTrait(p.owner, "Tau'ri") ? 1.1
                          : hasTrait(p.owner, "Ori") ? 0.85
                                                          : 1.0;
        const double shipyards = hasTech(p.owner, "industry1") ? 1.25 : 1.0;
        const double rate = std::clamp(0.5 + p.population / 10.0, 0.6, 1.6) * seconds * yard * shipyards;
        const double smuggling = hasTrait(p.owner, "Lucian Alliance") ? 1.25 : 1.0;
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
        fleet.leg += kFleetSpeed * (hasTech(fleet.owner, "industry2") ? 1.3 : 1.0) * seconds / length;
        if (fleet.leg < 1.0)
            continue;
        fleet.leg = 0;
        fleet.at = fleet.route.takeFirst();
        const Planet &p = m_planets[fleet.at];
        // Stops at the first world that is not its own, or where enemies wait
        bool enemies = hostile(fleet.owner, p.owner);
        for (const Fleet &other : m_fleets)
            if (hostile(other.owner, fleet.owner) && other.at == fleet.at && !other.moving())
                enemies = true;
        if (hostile(fleet.owner, m_player) && fleet.at == m_at && !travelling())
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

        if (t.owner != me && !hostile(me, t.owner))
            continue;               // allied world
        if (t.owner != me) {
            // Attack: only where the fleet clearly outguns the defenders
            double defense = sidePower(t.id, t.owner);
            if (hostile(me, m_player) && t.id == m_at && !travelling())
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
                if (hostile(enemy.owner, me) && (enemy.target == t.id || (enemy.at == t.id && !enemy.moving())))
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
    if (owner < 0 || (owner != battle->attacker && owner != battle->defender) || !hostile(owner, m_player))
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
            if (!hostile(f, p.owner))
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
        if (flagshipHere && hostile(attacker, m_player))
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
        p.population *= hasTrait(battle.attacker, "Ori") ? 0.9 : 0.75;     // the Ori convert the people
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
                                    {"trinium", f.trinium}, {"techs", QJsonArray::fromStringList(f.techs)},
                                    {"research", f.research}, {"researchProgress", f.researchProgress},
                                    {"allied", f.allied}, {"allyCooldown", f.allyCooldown},
                                    {"pacts", QJsonArray::fromStringList(f.pacts)}});
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
    return {{"version", 4}, {"factions", factions}, {"planets", planets}, {"fleets", fleets},
            {"skirmishes", skirmishes}, {"eventClock", m_eventClock},
            {"nextFleet", m_nextFleetId}, {"at", m_at}, {"previous", m_previous}, {"route", route},
            {"leg", m_legProgress}, {"capture", m_capture}, {"hull", m_hull}, {"battle", m_battlePlanet},
            {"battleOwner", m_battleOwner}, {"over", m_over}};
}

bool ConquestGame::fromJson(const QJsonObject &json)
{
    const int version = json.value("version").toInt();
    if (version < 1 || version > 4)
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
        Faction f;
        f.id = o.value("id").toString();
        f.color = factionInfos()[info].color;
        f.player = o.value("player").toBool();
        f.naquadah = o.value("naquadah").toDouble();
        f.trinium = o.value("trinium").toDouble();
        for (const QJsonValue &t : o.value("techs").toArray())
            if (std::any_of(techInfos().cbegin(), techInfos().cend(), [&t](const TechInfo &i) { return i.id == t.toString(); }))
                f.techs << t.toString();
        f.research = o.value("research").toString();
        f.researchProgress = std::clamp(o.value("researchProgress").toDouble(), 0.0, 1.0);
        f.allied = o.value("allied").toBool() && !f.player;
        f.allyCooldown = std::max(0.0, o.value("allyCooldown").toDouble());
        for (const QJsonValue &pact : o.value("pacts").toArray())
            if (factionInfoIndex(pact.toString()) >= 0)
                f.pacts << pact.toString();
        factions << f;
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
