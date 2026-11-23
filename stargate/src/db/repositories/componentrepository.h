#ifndef COMPONENTREPOSITORY_H
#define COMPONENTREPOSITORY_H

#include <QList>
#include <QString>

// ================================================================
//  ComponentRepository
//  Read access to the component catalogue (table Component) and to
//  the resources needed to repair each component
//  (table Component_Repair_Cost).
// ================================================================

struct ComponentInfo
{
    int id = 0;
    QString name;
    QString category;   // SYSTEM | WEAPON | UTILITY
    int maxHp = 0;
    int energyCost = 0;
};

struct RepairCost
{
    int resourceId = 0;
    QString resourceName;
    int quantity = 0;
};

class ComponentRepository
{
public:
    static QList<ComponentInfo> all();
    static QList<RepairCost> repairCost(int componentId);
};

#endif // COMPONENTREPOSITORY_H
