#ifndef ENTITY_H
#define ENTITY_H

#include <QPointF>

// ================================================================
//  Entity
//  Base class for every object that has a position in the game
//  world and can move toward a target (ships, drones, shuttles…).
//  Origin: prototype from branch `testfeature` (GameMecha).
// ================================================================

class Entity
{
public:
    explicit Entity(double x = 0, double y = 0);
    virtual ~Entity() = default;

    // Advance the simulation by deltaTime seconds
    virtual void update(double deltaTime);

    // Start moving toward target
    void moveTo(const QPointF &target);

    QPointF position() const { return m_position; }
    void setPosition(const QPointF &pos) { m_position = pos; }
    QPointF targetPosition() const { return m_targetPosition; }

    bool isActive() const { return m_isActive; }
    void setActive(bool active) { m_isActive = active; }
    bool isMoving() const { return m_isMoving; }

protected:
    QPointF m_position;
    QPointF m_targetPosition;
    bool m_isActive = true;
    bool m_isMoving = false;
    double m_moveSpeed = 100.0; // world units per second
};

#endif // ENTITY_H
