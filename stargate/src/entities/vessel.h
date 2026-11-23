#ifndef VESSEL_H
#define VESSEL_H

#include "entity.h"

// ================================================================
//  Vessel
//  A ship: an Entity with energy and hull points. Moving consumes
//  energy; a vessel with no energy cannot move.
// ================================================================

class Vessel : public Entity
{
public:
    explicit Vessel(double x = 0, double y = 0);

    void update(double deltaTime) override;

    double energy() const { return m_energy; }
    double maxEnergy() const { return m_maxEnergy; }
    void consumeEnergy(double amount);
    void restoreEnergy(double amount);

    double health() const { return m_health; }
    double maxHealth() const { return m_maxHealth; }
    void takeDamage(double amount);
    bool isDestroyed() const { return m_health <= 0.0; }

protected:
    double m_energy = 100.0;
    double m_maxEnergy = 100.0;
    double m_health = 100.0;
    double m_maxHealth = 100.0;
    double m_moveEnergyCost = 5.0; // energy per second while moving
};

#endif // VESSEL_H
