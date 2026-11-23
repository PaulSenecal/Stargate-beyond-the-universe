#include "eventsystem.h"

#include <QList>
#include <QRandomGenerator>

namespace {

GameEvent makeEvent(const char *title, const char *description, GameEvent::Severity severity,
                    double power, double ftl, int crew)
{
    GameEvent e;
    e.title = QString::fromUtf8(title);
    e.description = QString::fromUtf8(description);
    e.severity = severity;
    e.powerDelta = power;
    e.ftlDelta = ftl;
    e.crewDelta = crew;
    return e;
}

QList<GameEvent> buildTransitEvents()
{
    using S = GameEvent::Severity;
    QList<GameEvent> list;
    GameEvent e;

    // --- Good
    e = makeEvent("Ancient beacon", "The Destiny picks up an Ancient beacon on its path. Navigation data recovered, power stabilised.", S::Good, +0.15, +0.10, 0);
    e.naquadah = 3;
    list << e;

    list << makeEvent("Ionising nebula", "Transit through an ion-rich nebula. The collectors absorb extra energy.", S::Good, +0.20, +0.05, 0);

    e = makeEvent("Ice field", "The Destiny scoops ice fragments during the transit. Water reserves partly refilled.", S::Good, 0.0, 0.0, 0);
    e.water = 40;
    list << e;

    e = makeEvent("Ancient ship debris", "Drifting debris holds salvageable parts. The engineering team collects them.", S::Good, +0.05, 0.0, 0);
    e.minerals = 6;
    e.naquadah = 2;
    list << e;

    list << makeEvent("Favourable hyperspace current", "A natural hyperspace current speeds the Destiny up. FTL charge saved.", S::Good, 0.0, +0.20, 0);

    // --- Neutral
    list << makeEvent("Gravitational anomaly", "A gravitational anomaly forces a small detour. The transit takes longer.", S::Neutral, 0.0, -0.08, 0);
    list << makeEvent("Unknown radio signal", "A signal on an unknown frequency is received during the transit. Origin unidentified.", S::Neutral, 0.0, 0.0, 0);
    list << makeEvent("Engine maintenance", "The engineers use the transit for preventive maintenance.", S::Neutral, +0.08, -0.05, 0);

    // --- Warning
    list << makeEvent("Cosmic radiation storm", "High radiation zone. The shields absorb part of it.", S::Warning, -0.15, 0.0, -1);
    list << makeEvent("Secondary systems failure", "Several secondary systems fail. Overall power reduced.", S::Warning, -0.18, 0.0, 0);

    e = makeEvent("Hostile scout", "A hostile scout detects the Destiny. Crew on full alert.", S::Warning, -0.05, -0.10, -1);
    e.hostiles = true;
    list << e;

    list << makeEvent("Micrometeorites", "A swarm of micrometeorites hits the hull. Minor damage, several sections sealed.", S::Warning, -0.10, 0.0, -2);
    list << makeEvent("Fuel leak", "A leak is detected in the FTL circuit. The next charge will take longer.", S::Warning, -0.08, -0.20, 0);

    // --- Critical
    e = makeEvent("Drone attack", "Automated drones target the Destiny. Defensive combat engaged, heavy losses.", S::Critical, -0.30, -0.15, -3);
    e.hostiles = true;
    list << e;

    list << makeEvent("FTL chamber explosion", "An FTL chamber explodes during the transit. Critical damage, emergency stop.", S::Critical, -0.25, -0.50, -1);
    list << makeEvent("Hull breach", "A breach opens in section E. Partial depressurisation. Casualties.", S::Critical, -0.15, 0.0, -4);

    return list;
}

} // namespace

GameEvent EventSystem::rollTransitEvent()
{
    if (QRandomGenerator::global()->generateDouble() > kEventChance) {
        GameEvent none;
        none.title = QStringLiteral("Nominal transit");
        none.description = QStringLiteral("No incident reported during the FTL transit.");
        return none;
    }

    static const QList<GameEvent> events = buildTransitEvents();
    return events.at(int(QRandomGenerator::global()->bounded(quint32(events.size()))));
}
