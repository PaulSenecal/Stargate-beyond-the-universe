#ifndef PLANETGENERATOR_H
#define PLANETGENERATOR_H

#include <QtGlobal>

#include "planet.h"

// ================================================================
//  PlanetGenerator
//  Fully deterministic generation from (galaxy, sequence, variant):
//  the same triplet always gives the same planet.
// ================================================================

class PlanetGenerator
{
public:
    static Planet generate(int galaxy, int sequence, int variant);
    // Seed of the whole universe (0 = the classic one): the same seed
    // always gives the same planets, to replay or compare a journey
    static void setUniverseSeed(quint32 seed);
    static quint32 universeSeed();

private:
    static quint64 makeSeed(int galaxy, int sequence, int variant);
    static PlanetConditions generateConditions(quint64 seed);
    static QString generateNickname(const PlanetConditions &c, quint64 seed);
    static Classification classify(const PlanetConditions &c);
    static bool rollAncientSite(quint64 seed);
    static QString generateAncientName(quint64 seed);
};

#endif // PLANETGENERATOR_H
