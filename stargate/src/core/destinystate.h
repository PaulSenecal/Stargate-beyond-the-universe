#ifndef DESTINYSTATE_H
#define DESTINYSTATE_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <utility>

#include "src/db/repositories/componentrepository.h"
#include "src/db/repositories/resourcerepository.h"
#include "src/db/repositories/shiprepository.h"
#include "src/world/planet.h"

// ================================================================
//  DestinyState
//  Persistent state of the player's Destiny, backed by the database.
//  Maps the gameplay "systems" shown in the battle view to the
//  components installed aboard (Component_Ship):
//      engines     -> Sublight Engine
//      shields     -> Shield Generator
//      weapons     -> every WEAPON component
//      lifesupport -> Life Support
//  The hull has no table yet: its repair uses a fixed cost.
// ================================================================

class DestinyState
{
public:
    // Loads (or creates on first launch) the player's Destiny
    bool load(const QString &username);
    bool isLoaded() const { return m_shipId >= 0; }
    int shipId() const { return m_shipId; }

    // Health of a system in percent (100 when unknown)
    float systemHealth(const QString &key) const;
    // Writes a system health (percent) to its components
    void setSystemHealth(const QString &key, float percent);

    // Resources needed to repair a system (summed over its components)
    QList<RepairCost> repairCost(const QString &key) const;
    // Removes the cost from the ship stock. On failure nothing is
    // removed and `missing` describes what is lacking.
    bool consume(const QList<RepairCost> &cost, QString *missing);

    // Adds salvage after a kill; returns a text like "+3 naquadah, +1 trinium"
    QString collectSalvage(bool mothership);

    // Adds resources by name; returns a text like "+40 water, +2 naquadah"
    QString addResources(const QList<std::pair<QString, int>> &resources);

    // Team collection on a planet, from its surface conditions
    QString collectFromPlanet(const Planet &planet);

    // Crew consumption of water and food over `hours`.
    // Returns false when the stock could not cover the needs.
    bool consumeSupplies(int crew, double hours);
    int stockOf(const QString &resourceName) const;

    // Names of the components behind a system key ("engines" -> Sublight Engine)
    QStringList componentNames(const QString &key) const;
    // Inverse: system key of a component ("Sublight Engine" -> "engines"), empty if none
    QString systemKeyOf(const QString &componentName) const;
    // Called for every resource added to the stock (quests count them)
    void setGainListener(std::function<void(const QString &, int)> listener) { m_onGain = std::move(listener); }

    QList<ResourceStock> stock() const;
    QList<ShipComponent> components() const { return m_components; }

private:
    QList<int> componentIndexes(const QString &key) const;
    void migrateSupplies();

    int m_shipId = -1;
    QList<ShipComponent> m_components;
    double m_waterNeed = 0;     // fractional consumption not yet removed
    double m_foodNeed = 0;
    std::function<void(const QString &, int)> m_onGain;
};

#endif // DESTINYSTATE_H
