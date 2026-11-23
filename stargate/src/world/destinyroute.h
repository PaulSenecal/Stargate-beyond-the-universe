#ifndef DESTINYROUTE_H
#define DESTINYROUTE_H

#include <QList>
#include <QObject>
#include <QSet>
#include <QVector>

#include "eventsystem.h"
#include "planet.h"

// ================================================================
//  DestinyRoute
//  The Destiny follows a predetermined route through 20 galaxies.
//  At each stop it drops out of FTL in a system of 1 to 3 planets:
//   - a gate window opens for a limited time: a team can dial one
//     planet ("stargate <address>"), collect once ("explore"), and
//     must come back ("stargate destiny") before the window closes,
//     otherwise crew members are lost;
//   - the FTL drive recharges, faster with more power;
//   - jumping ("engine ftl") rolls a transit event.
//  Time is counted in game hours. Ported from stargateuniverseTest.
// ================================================================

struct RouteStop {
    int galaxy = 0;
    int sequence = 0;
    int numVariants = 1;       // planets in the system (1..3)
    double chargeHours = 24;   // FTL charge time at full power
};

struct RouteState {
    double ftlCharge = 1.0;    // 0..1
    double power = 0.6;        // 0..1
    int crew = 80;
    double hours = 0;          // total game time
    bool gateOpen = false;
    double gateRemaining = 0;  // hours
    double gateTotal = 0;
    int teamPlanet = -1;       // variant where the team is, -1 = aboard
};

class DestinyRoute : public QObject
{
    Q_OBJECT

public:
    enum class Severity { Good, Neutral, Warning, Critical };
    Q_ENUM(Severity)

    explicit DestinyRoute(QObject *parent = nullptr);

    // --- Navigation
    bool canJump(QString *reason = nullptr) const;
    // Jumps to the next stop; *event receives the transit event (its
    // power / FTL / crew effects are already applied)
    bool jump(GameEvent *event, QString *reason);

    const RouteStop &currentStop() const { return m_route.at(m_index); }
    int currentIndex() const { return m_index; }
    int totalStops() const { return int(m_route.size()); }
    QString galaxyName(int galaxy) const;
    QList<Planet> currentPlanets() const;
    int variantForAddress(const QString &address) const;   // -1 if not in this system

    // --- Gate
    bool dial(int variant, QString *error);
    bool recallTeam(QString *error);
    bool hasCollected(int variant) const { return m_collected.contains(variant); }
    void markCollected(int variant) { m_collected.insert(variant); }

    // --- Simulation
    void tick(double hours);
    void changeCrew(int delta);
    const RouteState &state() const { return m_state; }

    // --- Save / restore
    QList<int> collectedPlanets() const { return m_collected.values(); }
    void restore(int stopIndex, const RouteState &state, const QList<int> &collected);

signals:
    void notice(const QString &text, DestinyRoute::Severity severity);

private:
    void generateRoute(int galaxies);
    void openGateWindow();

    QVector<RouteStop> m_route;
    int m_index = 0;
    RouteState m_state;
    QSet<int> m_collected;
    bool m_chargeAnnounced = true;
    bool m_closingWarned = false;
};

#endif // DESTINYROUTE_H
