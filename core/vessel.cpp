#include "Vessel.h"

Vessel::Vessel(double x, double y) : Entity(x, y) {
    moveSpeed = 150.0f; // Le vaisseau est plus rapide
}

void Vessel::update(double deltaTime) {
    Entity::update(deltaTime);

    // Consommer de l'énergie en bougeant
    if (isMoving) {
        consumeEnergy(5.0f * deltaTime); // 5 energie/seconde
    }
}

void Vessel::consumeEnergy(float amount) {
    energy -= amount;
    if (energy < 0) energy = 0;
}
