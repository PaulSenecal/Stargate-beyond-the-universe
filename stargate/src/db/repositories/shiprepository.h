#ifndef SHIPREPOSITORY_H
#define SHIPREPOSITORY_H

#include <QList>
#include <QString>

// ================================================================
//  ShipRepository
//  The player's Destiny and the state of its installed components
//  (tables Player, Ship, Component_Ship).
// ================================================================

struct ShipComponent
{
    int id = 0;             // Component_Ship.id
    int componentId = 0;    // Component.id
    QString name;
    QString category;
    int currentHp = 0;
    int maxHp = 0;
    QString status;         // OPERATIONAL | DAMAGED | DESTROYED
};

class ShipRepository
{
public:
    // Returns the id of the player's Destiny. On first launch, creates
    // the player, the ship, every component at full HP and the starting
    // resources. Returns -1 on error.
    static int ensurePlayerDestiny(const QString &username);

    static QList<ShipComponent> components(int shipId);

    // Updates HP (clamped to 0..max) and the matching status
    static bool setComponentHp(int shipComponentId, int hp, int maxHp);
};

#endif // SHIPREPOSITORY_H
