#include "eventsystem.h"

#include <QCoreApplication>
#include <QList>
#include <QRandomGenerator>

namespace {

GameEvent makeEvent(const char *title, const char *description, GameEvent::Severity severity,
                    double power, double ftl, int crew)
{
    GameEvent e;
    e.title = QCoreApplication::translate("EventSystem", title);
    e.description = QCoreApplication::translate("EventSystem", description);
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
    e = makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Ancient beacon"), QT_TRANSLATE_NOOP("EventSystem", "The Destiny picks up an Ancient beacon on its path. Navigation data recovered, power stabilised."), S::Good, +0.15, +0.10, 0);
    e.naquadah = 3;
    list << e;

    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Ionising nebula"), QT_TRANSLATE_NOOP("EventSystem", "Transit through an ion-rich nebula. The collectors absorb extra energy."), S::Good, +0.20, +0.05, 0);

    e = makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Ice field"), QT_TRANSLATE_NOOP("EventSystem", "The Destiny scoops ice fragments during the transit. Water reserves partly refilled."), S::Good, 0.0, 0.0, 0);
    e.water = 40;
    list << e;

    e = makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Ancient ship debris"), QT_TRANSLATE_NOOP("EventSystem", "Drifting debris holds salvageable parts. The engineering team collects them."), S::Good, +0.05, 0.0, 0);
    e.minerals = 6;
    e.naquadah = 2;
    list << e;

    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Favourable hyperspace current"), QT_TRANSLATE_NOOP("EventSystem", "A natural hyperspace current speeds the Destiny up. FTL charge saved."), S::Good, 0.0, +0.20, 0);

    // --- Neutral
    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Gravitational anomaly"), QT_TRANSLATE_NOOP("EventSystem", "A gravitational anomaly forces a small detour. The transit takes longer."), S::Neutral, 0.0, -0.08, 0);
    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Unknown radio signal"), QT_TRANSLATE_NOOP("EventSystem", "A signal on an unknown frequency is received during the transit. Origin unidentified."), S::Neutral, 0.0, 0.0, 0);
    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Engine maintenance"), QT_TRANSLATE_NOOP("EventSystem", "The engineers use the transit for preventive maintenance."), S::Neutral, +0.08, -0.05, 0);

    // --- Warning
    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Cosmic radiation storm"), QT_TRANSLATE_NOOP("EventSystem", "High radiation zone. The shields absorb part of it."), S::Warning, -0.15, 0.0, -1);
    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Secondary systems failure"), QT_TRANSLATE_NOOP("EventSystem", "Several secondary systems fail. Overall power reduced."), S::Warning, -0.18, 0.0, 0);

    e = makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Hostile scout"), QT_TRANSLATE_NOOP("EventSystem", "A hostile scout detects the Destiny. Crew on full alert."), S::Warning, -0.05, -0.10, -1);
    e.hostiles = true;
    list << e;

    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Micrometeorites"), QT_TRANSLATE_NOOP("EventSystem", "A swarm of micrometeorites hits the hull. Minor damage, several sections sealed."), S::Warning, -0.10, 0.0, -2);
    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Fuel leak"), QT_TRANSLATE_NOOP("EventSystem", "A leak is detected in the FTL circuit. The next charge will take longer."), S::Warning, -0.08, -0.20, 0);

    // --- Critical
    e = makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Drone attack"), QT_TRANSLATE_NOOP("EventSystem", "Automated drones target the Destiny. Defensive combat engaged, heavy losses."), S::Critical, -0.30, -0.15, -3);
    e.hostiles = true;
    list << e;

    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "FTL chamber explosion"), QT_TRANSLATE_NOOP("EventSystem", "An FTL chamber explodes during the transit. Critical damage, emergency stop."), S::Critical, -0.25, -0.50, -1);
    list << makeEvent(QT_TRANSLATE_NOOP("EventSystem", "Hull breach"), QT_TRANSLATE_NOOP("EventSystem", "A breach opens in section E. Partial depressurisation. Casualties."), S::Critical, -0.15, 0.0, -4);

    return list;
}

} // namespace

GameEvent EventSystem::rollTransitEvent()
{
    if (QRandomGenerator::global()->generateDouble() > kEventChance) {
        GameEvent none;
        none.title = QCoreApplication::translate("EventSystem", "Nominal transit");
        none.description = QCoreApplication::translate("EventSystem", "No incident reported during the FTL transit.");
        return none;
    }

    static const QList<GameEvent> events = buildTransitEvents();
    return events.at(int(QRandomGenerator::global()->bounded(quint32(events.size()))));
}
