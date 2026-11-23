#include "vessel.h"

#include <QtGlobal>

Vessel::Vessel(double x, double y)
    : Entity(x, y)
{
    m_moveSpeed = 150.0;
}

void Vessel::update(double deltaTime)
{
    if (isDestroyed() || m_energy <= 0.0) {
        m_isMoving = false;
        return;
    }

    Entity::update(deltaTime);

    if (m_isMoving)
        consumeEnergy(m_moveEnergyCost * deltaTime);
}

void Vessel::consumeEnergy(double amount)
{
    m_energy = qMax(0.0, m_energy - amount);
}

void Vessel::restoreEnergy(double amount)
{
    m_energy = qMin(m_maxEnergy, m_energy + amount);
}

void Vessel::takeDamage(double amount)
{
    m_health = qMax(0.0, m_health - amount);
    if (isDestroyed())
        m_isActive = false;
}
