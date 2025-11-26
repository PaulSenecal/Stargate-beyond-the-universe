#ifndef ENTITY_H
#define ENTITY_H

#include <QPointF>

class Entity {
public:
    Entity(double x = 0, double y = 0);
    virtual ~Entity() = default;

    // Position
    QPointF position;
    QPointF targetPosition;

    // Status
    bool isActive = true;
    bool isMoving = false;

    // Mouvement
    virtual void update(double deltaTime);
    void moveTo(const QPointF& target);

protected:
    float moveSpeed = 100.0f; // pixels par seconde
};

#endif
