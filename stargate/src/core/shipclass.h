#ifndef SHIPCLASS_H
#define SHIPCLASS_H

#include <QList>
#include <QString>

// ================================================================
//  Ship library data (tables Ship_Type and Ship_Attack)
//  Shared by the repository, the battle simulation and the library
//  screen. Plain data, no Qt object.
// ================================================================

struct ShipAttack
{
    QString name;
    // bolt     : `shots` energy bolts aimed at the hull
    // slug     : same with kinetic railgun slugs
    // missile  : slow homing missile, railguns can shoot it down
    // beam     : very fast beam, half of it goes through the shield
    // kamikaze : the ship dives into the Destiny (railguns can stop it)
    // launch   : launches `shots` ships of class `payload`
    QString kind;
    float damage = 0;           // per shot
    int shots = 1;
    float cooldown = 3;         // seconds between two attempts
    float chance = 1;           // probability of use at each attempt (0..1)
    QString trigger;            // always | damaged (hull below 50 %)
    QString payload;            // launch: ship class name
    bool special = false;
    QString description;

    bool interceptable() const { return kind == "missile" || kind == "kamikaze"; }
    // Average uses per minute when the trigger is met
    float usesPerMinute() const { return cooldown > 0 ? 60.0f * chance / cooldown : 0.0f; }
};

struct ShipClass
{
    int id = 0;
    QString name;
    QString role;               // MotherShip | Fighter | Automated
    QString faction;
    QString description;
    int hull = 100;             // hit points in battle
    bool hostile = false;       // can appear in an enemy wave
    QString color;              // "#rrggbb" hull tint
    QString picture;            // picture key in the mod assets (see Assets::shipPicture)
    QList<ShipAttack> attacks;

    bool isMothership() const { return role.compare("MotherShip", Qt::CaseInsensitive) == 0; }

    // Ships used when the database is unavailable
    static QList<ShipClass> fallbackRoster()
    {
        ShipClass hatak;
        hatak.name = "Ha'tak";
        hatak.role = "MotherShip";
        hatak.faction = "Goa'uld";
        hatak.hull = 420;
        hatak.hostile = true;
        hatak.color = "#9e8047";
        hatak.attacks = {{"Staff cannon barrage", "bolt", 2.5f, 2, 3.4f, 1.0f, "always", {}, false, {}},
                         {"Naquadah missile", "missile", 14.0f, 1, 6.5f, 1.0f, "always", {}, false, {}}};
        ShipClass glider;
        glider.name = "Death Glider";
        glider.role = "Fighter";
        glider.faction = "Goa'uld";
        glider.hull = 60;
        glider.hostile = true;
        glider.color = "#736b61";
        glider.attacks = {{"Staff cannon", "bolt", 1.2f, 1, 2.4f, 1.0f, "always", {}, false, {}}};
        return {hatak, glider};
    }
};

#endif // SHIPCLASS_H
