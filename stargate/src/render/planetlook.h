#ifndef PLANETLOOK_H
#define PLANETLOOK_H

#include <QVector3D>

#include "src/world/planet.h"

// ================================================================
//  PlanetLook
//  Inputs of the procedural planet shader of the battle view, and
//  how they are derived from a generated planet's conditions.
// ================================================================

// Appearance of the planet behind the Destiny (procedural shader inputs)
struct PlanetLook {
    QVector3D ocean{0.02f, 0.05f, 0.12f};
    QVector3D ground{0.16f, 0.14f, 0.11f};
    QVector3D groundAlt{0.32f, 0.29f, 0.23f};
    QVector3D atmosphere{0.25f, 0.45f, 1.0f};
    float waterLevel = 0.52f;   // land above this noise level: higher = more ocean, < 0 = no ocean
    float cloudCover = 0.85f;   // 0..1
    float lights = 0.0f;        // 1 = city lights on the night side
    QVector3D seed;             // offsets the relief so each planet differs
};

// Ocean share from water, ground colour from the most striking
// condition (radiation, toxicity, life, dryness, minerals), clouds and
// atmosphere from oxygen, city lights on Ancient sites.
PlanetLook lookOf(const Planet &planet);

#endif // PLANETLOOK_H
