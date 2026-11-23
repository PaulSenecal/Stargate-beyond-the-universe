#include "entity.h"

#include <QtMath>

Entity::Entity(double x, double y)
    : m_position(x, y)
    , m_targetPosition(x, y)
{
}

void Entity::moveTo(const QPointF &target)
{
    m_targetPosition = target;
    m_isMoving = true;
}

void Entity::update(double deltaTime)
{
    if (!m_isActive || !m_isMoving)
        return;

    const QPointF direction = m_targetPosition - m_position;
    const double distance = qSqrt(QPointF::dotProduct(direction, direction));
    const double step = m_moveSpeed * deltaTime;

    // Arrived (or would overshoot this frame)
    if (distance <= step || distance < 2.0) {
        m_position = m_targetPosition;
        m_isMoving = false;
        return;
    }

    m_position += (direction / distance) * step;
}
