#ifndef WORLDREPOSITORY_H
#define WORLDREPOSITORY_H

#include <QList>
#include <QString>

#include "src/world/planet.h"

// ================================================================
//  WorldRepository
//  Records the systems visited by the Destiny in the save database
//  (tables Galaxy, Region, Planet, Planet_Resource). Planets are
//  generated, so this is the log of what has been discovered.
// ================================================================

class WorldRepository
{
public:
    // Idempotent: a planet already recorded is left untouched
    static bool recordSystem(const QString &galaxyName, const QList<Planet> &planets);
    static int discoveredPlanetCount();
};

#endif // WORLDREPOSITORY_H
