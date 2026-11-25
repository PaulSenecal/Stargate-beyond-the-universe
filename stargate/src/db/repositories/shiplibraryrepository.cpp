#include "shiplibraryrepository.h"

#include <QHash>
#include <QSet>
#include <QVariant>
#include <utility>

#include "../databasemanager.h"

namespace {

struct ClassSeed
{
    const char *name, *role, *faction;
    int hull;
    bool hostile;
    const char *color, *description;
};

struct AttackSeed
{
    const char *ship, *name, *kind;
    double damage;
    int shots;
    double cooldown, chance;
    const char *trigger, *payload;
    bool special;
    const char *description;
};

// Ship classes. Balance reference: the Destiny's secondary towers deal
// ~7 per bolt, its main tower 160; railguns x2.5 against small ships.
// Mothership fire rates were tuned with BattleView::simulate(): idle for
// 2 min (10 runs) the Destiny keeps 85-98 % hull against every faction.
// The shield is a tipping point: a few tenths of a second on a cooldown
// decide whether it holds or collapses.
const ClassSeed kClasses[] = {
    {"Destiny", "MotherShip", "Ancients", 1000, false, "#8a8478",
     "Ancient seed ship launched millions of years ago, crossing the universe ahead of the Stargate "
     "builders. Powered by stars, it drops out of FTL on its own schedule."},
    {"Ha'tak", "MotherShip", "Goa'uld", 420, true, "#9e8047",
     "Goa'uld pyramid mothership. Heavy staff cannon batteries and a hangar of Death Gliders."},
    {"Death Glider", "Fighter", "Goa'uld", 60, true, "#736b61",
     "Two-seat Goa'uld attack fighter armed with twin staff cannons."},
    {"Lucian Ha'tak", "MotherShip", "Lucian Alliance", 380, true, "#7d6a52",
     "Ha'tak taken over by the Lucian Alliance and refitted with naquadah-enhanced missile racks."},
    {"Glider", "Fighter", "Lucian Alliance", 55, true, "#6e665c",
     "Stolen Death Glider flown by Alliance mercenaries."},
    {"Wraith Hive Ship", "MotherShip", "Wraith", 520, true, "#4b3a5c",
     "Organic Wraith flagship. Self-repairing hull, energy batteries and swarms of Darts."},
    {"Wraith Dart", "Fighter", "Wraith", 45, true, "#5d4d6b",
     "Light Wraith interceptor. Badly damaged Darts dive into their target."},
    {"Drone Command Ship", "MotherShip", "Drones", 600, true, "#5e6670",
     "Automated control ship of the Berserker drones. Destroying it does not stop its drones."},
    {"Drone", "Automated", "Drones", 40, true, "#7a838c",
     "Small automated attack drone, fast and relentless, firing twin plasma bolts."},
    {"Daedalus", "MotherShip", "Tau'ri", 500, false, "#8c949c",
     "BC-304 battlecruiser of the Tau'ri, upgraded with Asgard shields and the Asgard plasma beam."},
    {"F-302", "Fighter", "Tau'ri", 70, false, "#9aa2aa",
     "Tau'ri interceptor carried by the BC-304s. Railguns and air-to-air missiles."},

    // Added from the Stargate Reborn mod data (tools/reborn/extract_units.py):
    // hull = (hull + armor + shield) and fire rate scaled on the Ha'tak
    {"Wraith Cruiser", "MotherShip", "Wraith", 406, true, "#55436a",
     "Wraith battlecruiser escorting the hives. Fills its hangar with Darts ready to sacrifice themselves."},
    {"Al'kesh", "Fighter", "Goa'uld", 88, true, "#86704c",
     "Goa'uld mid-range bomber. Slow, but its plasma bombs hit hard."},
    {"Aurora", "MotherShip", "Ancients", 772, false, "#8f97a0",
     "Ancient warship of the Lantean fleet, armed with drone weapons and an energy-absorptive shield."},
    {"Puddle Jumper", "Fighter", "Ancients", 40, false, "#9a9488",
     "Small Ancient gateship able to cloak and fire drones."},
    {"O'Neill", "MotherShip", "Asgard", 854, false, "#b8c4cc",
     "Most advanced Asgard warship, built to fight the Replicators. Huge shields, fragile hull."},
    {"Beliskner", "MotherShip", "Asgard", 711, false, "#a8b4bc",
     "Asgard Beliskner-class vessel, Thor's ship. Asgard beams and laser barrages."},
    {"Apophis Flagship", "MotherShip", "Goa'uld", 1192, false, "#b08a3e",
     "Ha'tak of the System Lord Apophis, reinforced and heavily armed. A boss for later."},
    {"Anubis Mothership", "MotherShip", "Goa'uld", 1287, false, "#4a4038",
     "Flagship of Anubis with Ancient-derived shields and a superweapon able to destroy a planet."},
    {"Wraith Superhive", "MotherShip", "Wraith", 1016, false, "#3e2f4d",
     "Hive powered by a ZPM: grows, regenerates and launches endless swarms of Darts."},
    // Conquest factions (not in the survival waves)
    {"Asgard Cruiser", "MotherShip", "Asgard", 480, false, "#b8c4d6",
     "Line ship of the Asgard fleet: few in number, but shielded and armed far beyond the younger races."},
    {"Asgard Scout", "Fighter", "Asgard", 80, false, "#c8d2dc",
     "Small Asgard escort ship with an ion cannon."},
    {"Lantean Warship", "MotherShip", "Ancients", 520, false, "#8fb7c4",
     "Ancient warship of the Lantean fleet, launching drones and firing energy batteries."},
    {"Ori Warship", "MotherShip", "Ori", 560, false, "#e07b30",
     "Ori crusade mothership, powered by the Ori themselves. Its main beam crosses any shield."},
    {"Ori Fighter", "Fighter", "Ori", 65, false, "#c9883e",
     "Small Ori attack craft escorting the warships of the crusade."},
    // Pegasus / Destiny-era aliens met along the route (campaign waves)
    {"Nakai Carrier", "MotherShip", "Nakai", 450, true, "#4f6d3a",
     "Insectoid Nakai mothership. It harries its prey with swarms of fighters and sends teams to "
     "abduct crew members."},
    {"Nakai Fighter", "Fighter", "Nakai", 50, true, "#6a8a4c",
     "Fast Nakai attack craft, firing light energy bolts."},
    {"Ursini Warship", "MotherShip", "Ursini", 580, true, "#6b5a8c",
     "Heavily armored Ursini battleship. Slow, but its batteries and missile racks hit hard; the "
     "Ursini fight anyone who enters their space."},
    {"Ursini Interceptor", "Fighter", "Ursini", 65, true, "#7f6fa0",
     "Armored Ursini interceptor with a kinetic cannon."},
};

const AttackSeed kAttacks[] = {
    // Destiny (reference values of the battle view)
    {"Destiny", "Main tower", "bolt", 160, 1, 4.0, 1.0, "always", "", false,
     "Heavy shot from the main tower on the locked target (command 'maintower')."},
    {"Destiny", "Pulse volley", "bolt", 22, 3, 8.0, 1.0, "always", "", false,
     "Three bolts on every contact at once (command 'pulseweapon')."},
    {"Destiny", "Secondary towers", "bolt", 7, 1, 0.45, 1.0, "always", "", false,
     "Automatic fire of the secondary towers, slower when the weapons are damaged."},
    {"Destiny", "Railguns", "slug", 8, 1, 0.3, 1.0, "always", "", false,
     "Automatic point defense: shoots down missiles and diving ships, otherwise shreds fighters."},

    {"Ha'tak", "Staff cannon barrage", "bolt", 2.5, 2, 3.4, 1.0, "always", "", false,
     "Pair of heavy plasma bolts from the hull batteries."},
    {"Ha'tak", "Glider launch", "launch", 0, 2, 30, 0.5, "always", "Death Glider", true,
     "Opens its hangar and launches two Death Gliders."},
    {"Death Glider", "Staff cannon", "bolt", 1.2, 1, 2.4, 1.0, "always", "", false,
     "Light plasma bolt."},

    {"Lucian Ha'tak", "Staff cannon barrage", "bolt", 2.5, 2, 3.3, 1.0, "always", "", false,
     "Pair of heavy plasma bolts from the hull batteries."},
    {"Lucian Ha'tak", "Naquadah missile", "missile", 14, 1, 6.5, 0.85, "always", "", false,
     "Slow homing missile. Railguns can shoot it down."},
    {"Lucian Ha'tak", "Missile salvo", "missile", 12, 3, 30, 0.4, "always", "", true,
     "Three missiles at once to saturate the point defense."},
    {"Glider", "Staff cannon", "bolt", 1.2, 1, 2.6, 1.0, "always", "", false,
     "Light plasma bolt."},

    {"Wraith Hive Ship", "Energy barrage", "bolt", 2.0, 3, 2.7, 1.0, "always", "", false,
     "Three energy bolts from the organic batteries."},
    {"Wraith Hive Ship", "Dart swarm", "launch", 0, 3, 22, 0.55, "always", "Wraith Dart", true,
     "Releases three Darts."},
    {"Wraith Dart", "Energy bolt", "bolt", 1.0, 1, 2.2, 1.0, "always", "", false,
     "Light energy bolt."},
    {"Wraith Dart", "Kamikaze dive", "kamikaze", 22, 1, 4.0, 0.35, "damaged", "", true,
     "Below half hull, may dive into the Destiny. Railguns can stop it on the way."},

    {"Drone Command Ship", "Plasma barrage", "bolt", 3.0, 2, 2.7, 1.0, "always", "", false,
     "Pair of heavy plasma bolts."},
    {"Drone Command Ship", "Drone launch", "launch", 0, 2, 18, 0.7, "always", "Drone", true,
     "Launches two new drones."},
    {"Drone", "Twin plasma", "bolt", 0.7, 2, 2.6, 1.0, "always", "", false,
     "Two light plasma bolts."},

    {"Daedalus", "Railgun barrage", "slug", 1.5, 4, 2.0, 1.0, "always", "", false,
     "Four railgun slugs."},
    {"Daedalus", "Mark VIII missile", "missile", 30, 1, 10, 0.7, "always", "", false,
     "Nuclear-tipped missile, can be shot down."},
    {"Daedalus", "Asgard plasma beam", "beam", 40, 1, 20, 0.6, "always", "", true,
     "Asgard beam weapon: half of its energy goes through shields."},
    {"F-302", "Railgun", "slug", 1.0, 2, 2.0, 1.0, "always", "", false,
     "Two railgun slugs."},
    {"F-302", "AIM-120 missile", "missile", 8, 1, 9, 0.5, "always", "", false,
     "Light air-to-air missile."},

    {"Wraith Cruiser", "Energy batteries", "bolt", 1.6, 2, 2.0, 1.0, "always", "", false,
     "Pair of energy bolts."},
    {"Wraith Cruiser", "Kamikaze Darts", "launch", 0, 2, 25, 0.5, "always", "Wraith Dart", true,
     "Releases two Darts, ready to dive into the Destiny once damaged."},
    {"Al'kesh", "Plasma bombs", "bolt", 3.0, 1, 8, 1.0, "always", "", false,
     "Slow, heavy plasma bomb."},
    {"Aurora", "Drone weapons", "missile", 6, 4, 7, 1.0, "always", "", false,
     "Four homing Ancient drones."},
    {"Aurora", "Drone swarm", "missile", 5, 8, 30, 0.5, "always", "", true,
     "A cloud of eight drones to saturate the point defense."},
    {"Puddle Jumper", "Drone launcher", "missile", 4, 2, 6, 1.0, "always", "", false,
     "Two drones from the side pods."},
    {"O'Neill", "Asgard plasma beam", "beam", 22, 1, 6, 1.0, "always", "", false,
     "Asgard beam weapon: half of its energy goes through shields."},
    {"O'Neill", "Ion burst", "bolt", 6, 5, 25, 0.5, "always", "", true,
     "Five ion bolts in a row."},
    {"Beliskner", "Asgard beam", "beam", 12, 1, 6, 1.0, "always", "", false,
     "Asgard beam weapon: half of its energy goes through shields."},
    {"Beliskner", "Laser barrage", "bolt", 2, 8, 20, 0.6, "always", "", true,
     "Eight quick laser shots."},
    {"Apophis Flagship", "Staff cannon barrage", "bolt", 3, 4, 2.2, 1.0, "always", "", false,
     "Four heavy plasma bolts."},
    {"Apophis Flagship", "Fleet bombardment", "missile", 18, 3, 30, 0.5, "always", "", true,
     "Three heavy plasma torpedoes."},
    {"Anubis Mothership", "Main batteries", "bolt", 4, 6, 2.0, 1.0, "always", "", false,
     "Six heavy plasma bolts."},
    {"Anubis Mothership", "Superweapon", "beam", 80, 1, 45, 0.5, "always", "", true,
     "Ancient-derived beam weapon, the most powerful attack of the library."},
    {"Wraith Superhive", "Plasma barrage", "bolt", 3, 8, 1.6, 1.0, "always", "", false,
     "Eight energy bolts at once."},
    {"Wraith Superhive", "Endless swarm", "launch", 0, 5, 20, 0.7, "always", "Wraith Dart", true,
     "Releases five Darts."},
    {"Asgard Cruiser", "Asgard beam", "beam", 10, 1, 6, 1.0, "always", "", false,
     "Asgard beam weapon: half of its energy goes through shields."},
    {"Asgard Cruiser", "Ion bolts", "bolt", 2, 2, 3.0, 1.0, "always", "", false,
     "Pair of ion bolts."},
    {"Asgard Scout", "Ion bolt", "bolt", 1.4, 1, 2.4, 1.0, "always", "", false,
     "Light ion bolt."},
    {"Lantean Warship", "Drone weapons", "missile", 5, 3, 7, 1.0, "always", "", false,
     "Three homing Ancient drones."},
    {"Lantean Warship", "Energy batteries", "bolt", 2, 2, 3.0, 1.0, "always", "", false,
     "Pair of energy bolts."},
    {"Ori Warship", "Ori main beam", "beam", 16, 1, 9, 0.8, "always", "", true,
     "The main beam of the crusade: half of its energy goes through shields."},
    {"Ori Warship", "Plasma batteries", "bolt", 2.4, 2, 3.2, 1.0, "always", "", false,
     "Pair of heavy plasma bolts."},
    {"Ori Fighter", "Plasma bolt", "bolt", 1.3, 1, 2.4, 1.0, "always", "", false,
     "Light plasma bolt."},

    {"Nakai Carrier", "Energy barrage", "bolt", 1.8, 2, 3.0, 1.0, "always", "", false,
     "Pair of energy bolts."},
    {"Nakai Carrier", "Fighter swarm", "launch", 0, 3, 20, 0.6, "always", "Nakai Fighter", true,
     "Releases three Nakai fighters."},
    {"Nakai Fighter", "Energy bolt", "bolt", 1.0, 1, 1.9, 1.0, "always", "", false,
     "Light, fast energy bolt."},
    {"Ursini Warship", "Heavy batteries", "bolt", 3.0, 3, 4.2, 1.0, "always", "", false,
     "Three heavy bolts from the armored batteries."},
    {"Ursini Warship", "Missile racks", "missile", 12, 2, 12, 0.7, "always", "", true,
     "Two homing missiles. Railguns can shoot them down."},
    {"Ursini Interceptor", "Kinetic cannon", "slug", 1.5, 1, 2.6, 1.0, "always", "", false,
     "Fast kinetic slug."},
};

// Pictures of the Stargate Reborn mod (optional local assets)
const std::pair<const char *, const char *> kPictures[] = {
    {"Destiny", "ancients_destiny"},
    {"Ha'tak", "goauld_cruiser_hatak"},
    {"Death Glider", "goauld_interceptor_deathglider"},
    {"Al'kesh", "goauld_corvette_alkesh"},
    {"Lucian Ha'tak", "goauld_item_hatak_specialization_assault"},
    {"Glider", "goauld_interceptor_deathglider"},
    {"Wraith Hive Ship", "wraith_capital_hiveship"},
    {"Wraith Cruiser", "wraith_cruiser_battlecruiser"},
    {"Wraith Dart", "wraith_strikecraft_dart"},
    {"Wraith Superhive", "wraith_titan_superhive"},
    {"Aurora", "ancients_aurora"},
    {"Puddle Jumper", "ancients_puddlejumper"},
    {"O'Neill", "asgard_capital_oneill"},
    {"Beliskner", "asgard_capital_beliskner"},
    {"Apophis Flagship", "goauld_capital_apophis"},
    {"Anubis Mothership", "goauld_titan_anubis"},
};

QSet<QString> columns(const QString &table)
{
    QSet<QString> names;
    QSqlQuery q = DatabaseManager::instance().query(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    if (q.exec())
        while (q.next())
            names.insert(q.value(1).toString());
    return names;
}

} // namespace

bool ShipLibraryRepository::migrate()
{
    DatabaseManager &db = DatabaseManager::instance();
    if (!db.isOpen())
        return false;

    const QSet<QString> existing = columns("Ship_Type");
    const std::pair<const char *, const char *> added[] = {
        {"faction", "TEXT"}, {"description", "TEXT"}, {"hull", "INTEGER NOT NULL DEFAULT 100"},
        {"hostile", "BOOLEAN NOT NULL DEFAULT 0"}, {"color", "TEXT"}, {"picture", "TEXT"},
    };
    for (const auto &[name, type] : added)
        if (!existing.contains(name)
            && !db.exec(QStringLiteral("ALTER TABLE Ship_Type ADD COLUMN %1 %2").arg(name, type)))
            return false;

    if (!db.exec("CREATE TABLE IF NOT EXISTS Ship_Attack ("
                 " id           INTEGER PRIMARY KEY AUTOINCREMENT,"
                 " ship_type_id INTEGER NOT NULL REFERENCES Ship_Type(id) ON DELETE CASCADE,"
                 " name         TEXT    NOT NULL,"
                 " kind         TEXT    NOT NULL,"          // bolt | slug | missile | beam | kamikaze | launch
                 " damage       REAL    NOT NULL DEFAULT 0,"
                 " shots        INTEGER NOT NULL DEFAULT 1,"
                 " cooldown     REAL    NOT NULL DEFAULT 3,"
                 " chance       REAL    NOT NULL DEFAULT 1,"
                 " use_when     TEXT    NOT NULL DEFAULT 'always',"
                 " payload      TEXT,"
                 " special      BOOLEAN NOT NULL DEFAULT 0,"
                 " description  TEXT,"
                 " UNIQUE (ship_type_id, name))"))
        return false;

    db.beginTransaction();
    for (const ClassSeed &c : kClasses) {
        // Existing rows (Destiny, Drone, Glider…) only get the missing data
        QSqlQuery insert = db.query("INSERT INTO Ship_Type (name, type) SELECT :name, :role "
                                    "WHERE NOT EXISTS (SELECT 1 FROM Ship_Type WHERE name = :name2)");
        insert.bindValue(":name", c.name);
        insert.bindValue(":role", c.role);
        insert.bindValue(":name2", c.name);
        QSqlQuery update = db.query("UPDATE Ship_Type SET faction = :faction, description = :desc, hull = :hull, "
                                    "hostile = :hostile, color = :color WHERE name = :name AND faction IS NULL");
        update.bindValue(":faction", c.faction);
        update.bindValue(":desc", c.description);
        update.bindValue(":hull", c.hull);
        update.bindValue(":hostile", c.hostile);
        update.bindValue(":color", c.color);
        update.bindValue(":name", c.name);
        if (!insert.exec() || !update.exec()) {
            qWarning() << "[DB] Ship library seed failed for" << c.name;
            db.rollback();
            return false;
        }
    }
    for (const auto &[ship, picture] : kPictures) {
        QSqlQuery q = db.query("UPDATE Ship_Type SET picture = :picture WHERE name = :name AND picture IS NULL");
        q.bindValue(":picture", picture);
        q.bindValue(":name", ship);
        if (!q.exec()) {
            db.rollback();
            return false;
        }
    }
    for (const AttackSeed &a : kAttacks) {
        QSqlQuery q = db.query(
            "INSERT OR IGNORE INTO Ship_Attack "
            "(ship_type_id, name, kind, damage, shots, cooldown, chance, use_when, payload, special, description) "
            "SELECT id, :name, :kind, :damage, :shots, :cooldown, :chance, :trigger, :payload, :special, :desc "
            "FROM Ship_Type WHERE name = :ship");
        q.bindValue(":name", a.name);
        q.bindValue(":kind", a.kind);
        q.bindValue(":damage", a.damage);
        q.bindValue(":shots", a.shots);
        q.bindValue(":cooldown", a.cooldown);
        q.bindValue(":chance", a.chance);
        q.bindValue(":trigger", a.trigger);
        q.bindValue(":payload", *a.payload ? QVariant(a.payload) : QVariant());
        q.bindValue(":special", a.special);
        q.bindValue(":desc", a.description);
        q.bindValue(":ship", a.ship);
        if (!q.exec()) {
            qWarning() << "[DB] Ship attack seed failed:" << a.ship << a.name << q.lastError().text();
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

QList<ShipClass> ShipLibraryRepository::all()
{
    QList<ShipClass> classes;
    DatabaseManager &db = DatabaseManager::instance();

    QSqlQuery q = db.query(
        "SELECT id, name, COALESCE(type, ''), COALESCE(faction, 'Unknown'), COALESCE(description, ''), "
        "hull, hostile, COALESCE(color, '#808080'), COALESCE(picture, '') FROM Ship_Type "
        "ORDER BY faction, CASE type WHEN 'MotherShip' THEN 0 ELSE 1 END, name");
    if (!q.exec()) {
        qWarning() << "[DB] ShipLibraryRepository::all:" << q.lastError().text();
        return classes;
    }
    QHash<int, int> indexOf;
    while (q.next()) {
        ShipClass c;
        c.id = q.value(0).toInt();
        c.name = q.value(1).toString();
        c.role = q.value(2).toString();
        c.faction = q.value(3).toString();
        c.description = q.value(4).toString();
        c.hull = q.value(5).toInt();
        c.hostile = q.value(6).toBool();
        c.color = q.value(7).toString();
        c.picture = q.value(8).toString();
        indexOf.insert(c.id, int(classes.size()));
        classes << c;
    }

    QSqlQuery a = db.query(
        "SELECT ship_type_id, name, kind, damage, shots, cooldown, chance, use_when, COALESCE(payload, ''), "
        "special, COALESCE(description, '') FROM Ship_Attack ORDER BY ship_type_id, special, id");
    if (!a.exec()) {
        qWarning() << "[DB] ShipLibraryRepository::all (attacks):" << a.lastError().text();
        return classes;
    }
    while (a.next()) {
        const auto it = indexOf.constFind(a.value(0).toInt());
        if (it == indexOf.constEnd())
            continue;
        ShipAttack attack;
        attack.name = a.value(1).toString();
        attack.kind = a.value(2).toString();
        attack.damage = a.value(3).toFloat();
        attack.shots = a.value(4).toInt();
        attack.cooldown = a.value(5).toFloat();
        attack.chance = a.value(6).toFloat();
        attack.trigger = a.value(7).toString();
        attack.payload = a.value(8).toString();
        attack.special = a.value(9).toBool();
        attack.description = a.value(10).toString();
        classes[*it].attacks << attack;
    }
    return classes;
}
