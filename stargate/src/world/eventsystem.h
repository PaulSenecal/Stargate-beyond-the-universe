#ifndef EVENTSYSTEM_H
#define EVENTSYSTEM_H

#include <QString>

// ================================================================
//  Transit events
//  Rolled at every FTL jump (65 % chance). Each event can change the
//  Destiny's power, FTL charge and crew, bring resources, or bring
//  hostile ships into the sector. Ported from stargateuniverseTest.
// ================================================================

struct GameEvent {
    enum class Severity { Good, Neutral, Warning, Critical };

    QString title;
    QString description;
    Severity severity = Severity::Neutral;

    double powerDelta = 0.0;   // ±0..1
    double ftlDelta = 0.0;     // ±0..1
    int crewDelta = 0;         // ±people
    int water = 0;             // resources added to the ship stock
    int food = 0;
    int naquadah = 0;
    int minerals = 0;
    bool hostiles = false;     // enemy ships appear when dropping out of FTL
};

class EventSystem
{
public:
    static constexpr double kEventChance = 0.65;

    // Returns "Nominal transit" (no effect) when nothing happens
    static GameEvent rollTransitEvent();
};

#endif // EVENTSYSTEM_H
