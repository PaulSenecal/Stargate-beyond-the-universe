#ifndef PLANET_H
#define PLANET_H

#include <QString>

// ================================================================
//  Planet
//  A planet on the Destiny's route. Planets are never stored as a
//  whole: PlanetGenerator rebuilds the same planet from its position
//  (galaxy, sequence, variant). Ported from the stargateuniverseTest
//  prototype.
// ================================================================

enum class Classification {
    Unknown,        // not evaluated
    Viable,         // habitable, resources available
    Hostile,        // dangerous but reachable
    Barren,         // nothing useful
    ResourceRich,   // rich in resources
    Ancient         // Ancient site / technology
};

// Surface conditions, every value between 0 and 1
struct PlanetConditions {
    float water = 0.f;
    float oxygen = 0.f;
    float minerals = 0.f;
    float energy = 0.f;      // exotic energy (naquadah-like), rare
    float organics = 0.f;    // biomass / food
    float toxicity = 0.f;    // danger
    float radiation = 0.f;   // danger
    bool hasLife = false;
};

struct Planet {
    QString code;            // "G04-0017-B"
    QString nickname;        // "Ember"
    QString ancientName;     // "Ara Nova" — empty when no Ancient site

    Classification classification = Classification::Unknown;
    PlanetConditions conditions;

    int galaxy = 0;
    int sequence = 0;        // system index inside the galaxy
    int variant = 0;         // 0 = A, 1 = B, 2 = C

    // Gate address typed in the console: the code without dashes ("G040017B")
    QString gateAddress() const;
    QString classificationName() const;
    bool isAncientSite() const { return !ancientName.isEmpty(); }
    // Some planets have no Stargate: only the Puddle Jumper reaches them
    bool hasGate = true;
};

#endif // PLANET_H
