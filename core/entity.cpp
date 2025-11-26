#include "Entity.h"
#include <QtMath>

Entity::Entity(double x, double y)
    : position(x, y), targetPosition(x, y) {
}

void Entity::moveTo(const QPointF& target) {
    targetPosition = target;
    isMoving = true;
}

void Entity::update(double deltaTime) {
    if (!isMoving) return;

    // Direction vers la cible
    QPointF direction = targetPosition - position;
    float distance = qSqrt(direction.x() * direction.x() +
                           direction.y() * direction.y());

    if (distance < 2.0f) {
        // Arrivé à destination
        position = targetPosition;
        isMoving = false;
    } else {
        // Normaliser et appliquer la vitesse
        direction /= distance;
        position += direction * moveSpeed * deltaTime;
    }
}
