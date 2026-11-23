#include "destinyroute.h"

#include <QRandomGenerator>
#include <QStringList>
#include <algorithm>

#include "planetgenerator.h"

namespace {

const QStringList kGalaxyNames = {
    "Haeble", "Othala", "Ida", "Pegasus", "Kaliam",
    "Ori", "Dakara", "Vis Uban", "Ardena", "Novus",
    "Celesta", "Terminus", "Proxima", "Aurum", "Ferrus",
    "Tempus", "Vacuum", "Ignis", "Glacius", "Finis"};

constexpr double kTransitHours = 8.0;
constexpr double kBasePower = 0.6;
constexpr int kCrewLostWithGate = 3;

} // namespace

DestinyRoute::DestinyRoute(QObject *parent)
    : QObject(parent)
{
    generateRoute(int(kGalaxyNames.size()));
    openGateWindow();
}

// ================================================================
//  Route
// ================================================================

void DestinyRoute::generateRoute(int galaxies)
{
    m_route.clear();
    for (int g = 0; g < galaxies; ++g) {
        QRandomGenerator rng(quint64(g) * 0xF00D5EEDu);   // deterministic per galaxy
        const int stops = 5 + int(rng.bounded(20u));
        for (int s = 0; s < stops; ++s) {
            RouteStop stop;
            stop.galaxy = g;
            stop.sequence = s;
            stop.numVariants = 1 + int(rng.bounded(3u));
            stop.chargeHours = 12.0 + double(rng.bounded(37u));
            m_route.append(stop);
        }
    }
}

QString DestinyRoute::galaxyName(int galaxy) const
{
    return galaxy < kGalaxyNames.size() ? kGalaxyNames.at(galaxy)
                                        : QStringLiteral("Galaxy-%1").arg(galaxy, 2, 10, QChar('0'));
}

QList<Planet> DestinyRoute::currentPlanets() const
{
    QList<Planet> planets;
    const RouteStop &stop = currentStop();
    for (int v = 0; v < stop.numVariants; ++v)
        planets << PlanetGenerator::generate(stop.galaxy, stop.sequence, v);
    return planets;
}

int DestinyRoute::variantForAddress(const QString &address) const
{
    for (const Planet &p : currentPlanets())
        if (p.gateAddress().compare(address, Qt::CaseInsensitive) == 0)
            return p.variant;
    return -1;
}

// ================================================================
//  FTL
// ================================================================

bool DestinyRoute::canJump(QString *reason) const
{
    QString why;
    if (m_index >= m_route.size() - 1)
        why = tr("end of the known route reached");
    else if (m_state.teamPlanet >= 0)
        why = tr("a team is still on %1 - recall it with 'stargate destiny'")
                  .arg(PlanetGenerator::generate(currentStop().galaxy, currentStop().sequence,
                                                 m_state.teamPlanet).code);
    else if (m_state.ftlCharge < 1.0)
        why = tr("FTL drive charging (%1%)").arg(qRound(m_state.ftlCharge * 100));

    if (reason)
        *reason = why;
    return why.isEmpty();
}

bool DestinyRoute::jump(GameEvent *event, QString *reason)
{
    if (!canJump(reason))
        return false;

    const GameEvent ev = EventSystem::rollTransitEvent();

    ++m_index;
    m_collected.clear();
    m_state.ftlCharge = 0.0;
    m_state.hours += kTransitHours;
    m_state.power = std::clamp(m_state.power + ev.powerDelta, 0.05, 1.0);
    m_state.ftlCharge = std::clamp(m_state.ftlCharge + ev.ftlDelta, 0.0, 1.0);
    m_state.crew = std::max(1, m_state.crew + ev.crewDelta);
    m_chargeAnnounced = false;

    openGateWindow();
    if (event)
        *event = ev;
    return true;
}

// ================================================================
//  Gate
// ================================================================

void DestinyRoute::openGateWindow()
{
    // Window = charge time / 2.5, between 4 and 20 hours
    const double duration = std::clamp(currentStop().chargeHours / 2.5, 4.0, 20.0);
    m_state.gateOpen = true;
    m_state.gateRemaining = duration;
    m_state.gateTotal = duration;
    m_state.teamPlanet = -1;
    m_closingWarned = false;
}

bool DestinyRoute::dial(int variant, QString *error)
{
    QString why;
    if (!m_state.gateOpen)
        why = tr("gate window closed - wait for the next system");
    else if (variant < 0 || variant >= currentStop().numVariants)
        why = tr("no such gate address in this system (see 'scansector')");
    else if (m_state.teamPlanet == variant)
        why = tr("the team is already there");
    else if (m_state.teamPlanet >= 0)
        why = tr("a team is already deployed - recall it with 'stargate destiny'");

    if (!why.isEmpty()) {
        if (error)
            *error = why;
        return false;
    }
    m_state.teamPlanet = variant;
    return true;
}

bool DestinyRoute::recallTeam(QString *error)
{
    if (m_state.teamPlanet < 0) {
        if (error)
            *error = tr("no team deployed");
        return false;
    }
    m_state.teamPlanet = -1;
    return true;
}

// ================================================================
//  Simulation
// ================================================================

void DestinyRoute::changeCrew(int delta)
{
    m_state.crew = std::max(1, m_state.crew + delta);
}

void DestinyRoute::tick(double hours)
{
    m_state.hours += hours;

    // FTL recharge, scaled by power
    if (m_state.ftlCharge < 1.0) {
        m_state.ftlCharge = std::min(1.0, m_state.ftlCharge + hours * m_state.power / currentStop().chargeHours);
        if (m_state.ftlCharge >= 1.0 && !m_chargeAnnounced) {
            m_chargeAnnounced = true;
            emit notice(tr("FTL drive fully charged - ready to jump ('engine ftl')"), Severity::Good);
        }
    }

    // Power slowly returns to its nominal level
    if (m_state.power < kBasePower)
        m_state.power = std::min(kBasePower, m_state.power + 0.01 * hours);
    else
        m_state.power = std::max(kBasePower, m_state.power - 0.002 * hours);

    if (!m_state.gateOpen)
        return;

    m_state.gateRemaining -= hours;
    if (m_state.teamPlanet >= 0 && !m_closingWarned && m_state.gateRemaining <= m_state.gateTotal * 0.25) {
        m_closingWarned = true;
        emit notice(tr("Gate window closing in %1 h - recall the team ('stargate destiny')")
                        .arg(std::max(0.0, m_state.gateRemaining), 0, 'f', 1),
                    Severity::Warning);
    }

    if (m_state.gateRemaining <= 0.0) {
        m_state.gateRemaining = 0.0;
        m_state.gateOpen = false;
        if (m_state.teamPlanet >= 0) {
            m_state.teamPlanet = -1;
            changeCrew(-kCrewLostWithGate);
            emit notice(tr("Gate window closed - team stranded on the surface! %1 crew lost")
                            .arg(kCrewLostWithGate),
                        Severity::Critical);
        } else {
            emit notice(tr("Gate window closed"), Severity::Neutral);
        }
    }
}
