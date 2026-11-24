#ifndef SHIPLIBRARYREPOSITORY_H
#define SHIPLIBRARYREPOSITORY_H

#include <QList>
#include <QString>

#include "src/core/shipclass.h"

// ================================================================
//  ShipLibraryRepository
//  Every known ship class (table Ship_Type) and its attacks
//  (table Ship_Attack): used to build enemy waves and shown in the
//  ship library.
// ================================================================

class ShipLibraryRepository
{
public:
    // Adds the library columns / table and the seed data to databases
    // created before the library existed. Safe to call on every launch.
    static bool migrate();

    // All ship classes with their attacks, ordered by faction then size
    static QList<ShipClass> all();
};

#endif // SHIPLIBRARYREPOSITORY_H
