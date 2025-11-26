#ifndef VESSEL_H
#define VESSEL_H

#include "Entity.h"

class Vessel : public Entity {
public:
    Vessel(double x = 400, double y = 300);

    // Stats
    float energy = 100.0f;
    float maxEnergy = 100.0f;
    float health = 100.0f;

    void update(double deltaTime) override;
    void consumeEnergy(float amount);
};

#endif
