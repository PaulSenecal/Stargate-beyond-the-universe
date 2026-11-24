#ifndef RESOURCEREPOSITORY_H
#define RESOURCEREPOSITORY_H

#include <QList>
#include <QString>

// ================================================================
//  ResourceRepository
//  Resources stored aboard a ship (table Ship_Resource joined with
//  the Resource catalogue).
// ================================================================

struct ResourceStock
{
    int resourceId = 0;
    QString name;
    QString rarity;
    int quantity = 0;
};

class ResourceRepository
{
public:
    // Every resource of the catalogue with the quantity aboard (0 if none)
    static QList<ResourceStock> shipStock(int shipId);
    static int quantity(int shipId, int resourceId);

    // Adds (or removes, when negative) a quantity. Refuses to go below 0.
    static bool add(int shipId, int resourceId, int delta);
    static bool addByName(int shipId, const QString &resourceName, int delta);
};

#endif // RESOURCEREPOSITORY_H
