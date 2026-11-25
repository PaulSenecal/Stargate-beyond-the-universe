#include "interior.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace {

// Display name of a compartment / module (stored in English)
QString shown(const QString &name)
{
    return QCoreApplication::translate("Interior", name.toUtf8().constData());
}

constexpr float kFireGrowth = 0.03f;        // per second, with full air
constexpr float kFireAirUse = 2.0f;         // air burnt per second at full fire
constexpr float kFireSystemDamage = 1.2f;   // % of the system per second at full fire
constexpr float kFireSpread = 0.015f;       // chance per second to reach an open neighbour
constexpr float kBreachLoss = 9.0f;         // air lost per second through a breach
constexpr float kAirRefill = 3.0f;          // life support, per second at 100%
constexpr float kAirMix = 0.25f;            // air shared through open doors, per second
constexpr int kTeamSize = 15;               // crew members per generic repair team
constexpr int kLeakPower = 8;               // power lost per leaking conduit
constexpr float kFatigueWork = 0.45f;       // fatigue per second of work
constexpr float kFatigueRest = 1.2f;
constexpr float kFatigueIdle = 0.03f;

} // namespace

// ================================================================
//  Modules
// ================================================================

const QList<InteriorDef::Module> &InteriorDef::modules()
{
    static const QList<Module> list{
        {"asgard_shield", QT_TRANSLATE_NOOP("Interior", "Asgard shield booster"),
         QT_TRANSLATE_NOOP("Interior", "shield recharge x1.3"), {{"crystal", 3}, {"naquadah", 6}}, 60},
        {"goauld_cannon", QT_TRANSLATE_NOOP("Interior", "Goa'uld plasma cannon"),
         QT_TRANSLATE_NOOP("Interior", "weapon damage +15%"), {{"naquadah", 8}, {"trinium", 3}}, 60},
        {"naquadah_generator", QT_TRANSLATE_NOOP("Interior", "Naquadah generator"),
         QT_TRANSLATE_NOOP("Interior", "+15 power"), {{"naquadah", 10}, {"minerals", 4}}, 50},
        {"hydroponic_rack", QT_TRANSLATE_NOOP("Interior", "Hydroponic rack"),
         QT_TRANSLATE_NOOP("Interior", "+2 food per hour"), {{"minerals", 4}, {"water", 20}}, 35},
        {"water_recycler", QT_TRANSLATE_NOOP("Interior", "Water recycler"),
         QT_TRANSLATE_NOOP("Interior", "+3 water per hour"), {{"minerals", 5}, {"crystal", 1}}, 35},
        {"sensor_suite", QT_TRANSLATE_NOOP("Interior", "Ancient sensor suite"),
         QT_TRANSLATE_NOOP("Interior", "accuracy +10%"), {{"crystal", 2}, {"ancient_alloy", 1}}, 45},
    };
    return list;
}

const InteriorDef::Module *InteriorDef::module(const QString &id)
{
    for (const Module &m : modules())
        if (m.id == id || shown(m.name).compare(id, Qt::CaseInsensitive) == 0
            || m.name.compare(id, Qt::CaseInsensitive) == 0)
            return &m;
    return nullptr;
}

// ================================================================
//  Construction
// ================================================================

namespace {

struct RoomDef {
    const char *id;
    const char *name;
    QRectF rect;                // x1, z1, width, depth (ship-local, bow toward +x)
    const char *system;
    const char *capability;
    bool locked = false;
    QList<std::pair<QString, int>> cost = {};
    float work = 0;
    int bays = 0;
};

struct CrewDef {
    const char *id;
    const char *name;
    InteriorDef::Role role;
    const char *bonus;
    float skill;
    const char *location;
};

// Plans of the ships, seen from above. Each faction has its own.
QList<RoomDef> roomsFor(const QString &style)
{
    if (style == "Goa'uld" || style == "Lucian Alliance") {
        // Ha'tak: a pyramid on its ring, five sections across, three deep
        const bool lucian = style == "Lucian Alliance";
        return {
            {"gliderbay", QT_TRANSLATE_NOOP("Interior", "Glider Bay"), {-7.5, -3, 3, 2}, "hangar", "hangar"},
            {"rings", QT_TRANSLATE_NOOP("Interior", "Ring Room"), {-4.5, -3, 3, 2}, "", "rings"},
            {"cargo", lucian ? QT_TRANSLATE_NOOP("Interior", "Smuggler Hold") : QT_TRANSLATE_NOOP("Interior", "Cargo Hold"), {-1.5, -3, 3, 2}, "", lucian ? "supplies" : ""},
            {"battery", QT_TRANSLATE_NOOP("Interior", "Staff Cannon Battery"), {1.5, -3, 3, 2}, "weapons", ""},
            {"sensors", QT_TRANSLATE_NOOP("Interior", "Sensor Chamber"), {4.5, -3, 3, 2}, "sensors", ""},
            {"engines", QT_TRANSLATE_NOOP("Interior", "Engine Room"), {-7.5, -1, 3, 2}, "engines", ""},
            {"reactor", QT_TRANSLATE_NOOP("Interior", "Naquadah Reactor"), {-4.5, -1, 3, 2}, "", "power"},
            {"gateroom", QT_TRANSLATE_NOOP("Interior", "Gate Room"), {-1.5, -1, 3, 2}, "", "gate"},
            {"peltak", QT_TRANSLATE_NOOP("Interior", "Peltak"), {1.5, -1, 3, 2}, "", "command"},
            {"throne", lucian ? QT_TRANSLATE_NOOP("Interior", "Command Deck") : QT_TRANSLATE_NOOP("Interior", "Throne Room"), {4.5, -1, 3, 2}, "", "morale"},
            {"shields", QT_TRANSLATE_NOOP("Interior", "Shield Generator"), {-7.5, 1, 3, 2}, "shields", ""},
            {"lifesupport", QT_TRANSLATE_NOOP("Interior", "Life Support"), {-4.5, 1, 3, 2}, "lifesupport", ""},
            {"barracks", QT_TRANSLATE_NOOP("Interior", "Jaffa Barracks"), {-1.5, 1, 3, 2}, "", "rest"},
            {"sarcophagus", lucian ? QT_TRANSLATE_NOOP("Interior", "Infirmary") : QT_TRANSLATE_NOOP("Interior", "Sarcophagus Chamber"), {1.5, 1, 3, 2}, "", "infirmary"},
            {"armory", QT_TRANSLATE_NOOP("Interior", "Armory"), {4.5, 1, 3, 2}, "", "armory"},
        };
    }
    if (style == "Tau'ri") {
        // BC-304: a long hull, a hangar pod on each side
        return {
            {"engineering", QT_TRANSLATE_NOOP("Interior", "Engineering"), {-7, -1, 2.5, 2}, "engines", ""},
            {"hyperdrive", QT_TRANSLATE_NOOP("Interior", "Hyperdrive Room"), {-4.5, -1, 2.5, 2}, "", "power"},
            {"asgardcore", QT_TRANSLATE_NOOP("Interior", "Asgard Core"), {-2, -1, 2.5, 2}, "shields", ""},
            {"lifesupport", QT_TRANSLATE_NOOP("Interior", "Life Support"), {0.5, -1, 2, 2}, "lifesupport", ""},
            {"bridge", QT_TRANSLATE_NOOP("Interior", "Bridge"), {2.5, -1, 2, 2}, "", "command"},
            {"sensors", QT_TRANSLATE_NOOP("Interior", "Sensor Suite"), {4.5, -1, 2.5, 2}, "sensors", ""},
            {"quarters", QT_TRANSLATE_NOOP("Interior", "Crew Quarters"), {-7, -2.5, 2, 1.5}, "", "rest"},
            {"portbay", QT_TRANSLATE_NOOP("Interior", "F-302 Bay (port)"), {-5, -2.5, 3.5, 1.5}, "hangar", "hangar"},
            {"railguns", QT_TRANSLATE_NOOP("Interior", "Railgun Battery"), {-1.5, -2.5, 3.5, 1.5}, "weapons", ""},
            {"infirmary", QT_TRANSLATE_NOOP("Interior", "Infirmary"), {2, -2.5, 2.5, 1.5}, "", "infirmary"},
            {"mess", QT_TRANSLATE_NOOP("Interior", "Mess Hall"), {-7, 1, 2, 1.5}, "", "morale"},
            {"starboardbay", QT_TRANSLATE_NOOP("Interior", "F-302 Bay (starboard)"), {-5, 1, 3.5, 1.5}, "", "hangar"},
            {"missiles", QT_TRANSLATE_NOOP("Interior", "Missile Bay"), {-1.5, 1, 3.5, 1.5}, "", "armory"},
            {"transporter", QT_TRANSLATE_NOOP("Interior", "Asgard Beam Room"), {2, 1, 2.5, 1.5}, "", "beam"},
        };
    }
    if (style == "Wraith") {
        // Hive: rooms grown around the queen's throne
        return {
            {"throne", QT_TRANSLATE_NOOP("Interior", "Queen's Throne"), {-1.5, -1.5, 3, 3}, "", "command"},
            {"dartbay", QT_TRANSLATE_NOOP("Interior", "Dart Bay"), {-1.5, -4, 3, 2.5}, "hangar", "hangar"},
            {"hibernation", QT_TRANSLATE_NOOP("Interior", "Hibernation Pods"), {-1.5, 1.5, 3, 2.5}, "", "rest"},
            {"engines", QT_TRANSLATE_NOOP("Interior", "Organic Engines"), {-5, -1.5, 3.5, 3}, "engines", ""},
            {"batteries", QT_TRANSLATE_NOOP("Interior", "Energy Batteries"), {1.5, -1.5, 3.5, 3}, "weapons", ""},
            {"healing", QT_TRANSLATE_NOOP("Interior", "Healing Chamber"), {-5, -4, 3.5, 2.5}, "", "infirmary"},
            {"neural", QT_TRANSLATE_NOOP("Interior", "Neural Sensors"), {1.5, -4, 3.5, 2.5}, "sensors", ""},
            {"regeneration", QT_TRANSLATE_NOOP("Interior", "Regeneration Node"), {-5, 1.5, 3.5, 2.5}, "", "regeneration"},
            {"feeding", QT_TRANSLATE_NOOP("Interior", "Feeding Hall"), {1.5, 1.5, 3.5, 2.5}, "", "morale"},
            {"membrane", QT_TRANSLATE_NOOP("Interior", "Life Membrane"), {-7, -2, 2, 4}, "lifesupport", ""},
            {"culling", QT_TRANSLATE_NOOP("Interior", "Culling Beam Bay"), {5, -2, 2, 4}, "", "beam"},
        };
    }
    if (style == "Asgard") {
        // O'Neill class: compact, run by its computer core
        return {
            {"engines", QT_TRANSLATE_NOOP("Interior", "Engines"), {-6, -1, 3, 2}, "engines", ""},
            {"core", QT_TRANSLATE_NOOP("Interior", "Power Core"), {-3, -1, 2, 2}, "", "power"},
            {"computer", QT_TRANSLATE_NOOP("Interior", "Computer Core"), {-1, -1, 2, 2}, "", "computer"},
            {"bridge", QT_TRANSLATE_NOOP("Interior", "Bridge"), {1, -1, 2, 2}, "", "command"},
            {"emitters", QT_TRANSLATE_NOOP("Interior", "Beam Emitters"), {3, -1, 3, 2}, "weapons", ""},
            {"scoutbay", QT_TRANSLATE_NOOP("Interior", "Scout Bay"), {-6, -2.5, 3, 1.5}, "hangar", "hangar"},
            {"shieldmatrix", QT_TRANSLATE_NOOP("Interior", "Shield Matrix"), {-3, -2.5, 3, 1.5}, "shields", ""},
            {"transporter", QT_TRANSLATE_NOOP("Interior", "Transport Beam"), {0, -2.5, 3, 1.5}, "", "beam"},
            {"sensors", QT_TRANSLATE_NOOP("Interior", "Sensors"), {-6, 1, 3, 1.5}, "sensors", ""},
            {"lifesupport", QT_TRANSLATE_NOOP("Interior", "Life Support"), {-3, 1, 3, 1.5}, "lifesupport", ""},
            {"cloning", QT_TRANSLATE_NOOP("Interior", "Cloning Lab"), {0, 1, 3, 1.5}, "", "infirmary"},
        };
    }
    if (style == "Ancients") {
        // Aurora class
        return {
            {"engines", QT_TRANSLATE_NOOP("Interior", "Engines"), {-7, -1, 2.5, 2}, "engines", ""},
            {"zpm", QT_TRANSLATE_NOOP("Interior", "ZPM Room"), {-4.5, -1, 2.5, 2}, "", "power"},
            {"chair", QT_TRANSLATE_NOOP("Interior", "Control Chair"), {-2, -1, 2.5, 2}, "weapons", ""},
            {"bridge", QT_TRANSLATE_NOOP("Interior", "Bridge"), {0.5, -1, 2.5, 2}, "", "command"},
            {"sensors", QT_TRANSLATE_NOOP("Interior", "Sensors"), {3, -1, 3, 2}, "sensors", ""},
            {"jumperbay", QT_TRANSLATE_NOOP("Interior", "Jumper Bay"), {-6, -2.5, 4, 1.5}, "hangar", "hangar"},
            {"shields", QT_TRANSLATE_NOOP("Interior", "Shield Emitters"), {-2, -2.5, 4, 1.5}, "shields", ""},
            {"drones", QT_TRANSLATE_NOOP("Interior", "Drone Storage"), {2, -2.5, 3, 1.5}, "", "armory"},
            {"lifesupport", QT_TRANSLATE_NOOP("Interior", "Life Support"), {-6, 1, 4, 1.5}, "lifesupport", ""},
            {"infirmary", QT_TRANSLATE_NOOP("Interior", "Infirmary"), {-2, 1, 4, 1.5}, "", "infirmary"},
            {"transporter", QT_TRANSLATE_NOOP("Interior", "Transporter"), {2, 1, 3, 1.5}, "", "rings"},
        };
    }
    if (style == "Ori") {
        // Ori warship: a ring around the Prior's chamber
        return {
            {"prior", QT_TRANSLATE_NOOP("Interior", "Prior Chamber"), {-1.5, -1.5, 3, 3}, "", "command"},
            {"mainbeam", QT_TRANSLATE_NOOP("Interior", "Main Beam Emitter"), {-1.5, -4, 3, 2.5}, "weapons", ""},
            {"sanctuary", QT_TRANSLATE_NOOP("Interior", "Sanctuary"), {-1.5, 1.5, 3, 2.5}, "", "morale"},
            {"engines", QT_TRANSLATE_NOOP("Interior", "Engines"), {-5, -1.5, 3.5, 3}, "engines", ""},
            {"sensors", QT_TRANSLATE_NOOP("Interior", "Sensors"), {1.5, -1.5, 3.5, 3}, "sensors", ""},
            {"core", QT_TRANSLATE_NOOP("Interior", "Power Core"), {-5, -4, 3.5, 2.5}, "", "power"},
            {"shields", QT_TRANSLATE_NOOP("Interior", "Shield Generator"), {1.5, -4, 3.5, 2.5}, "shields", ""},
            {"fighterbay", QT_TRANSLATE_NOOP("Interior", "Fighter Bay"), {-5, 1.5, 3.5, 2.5}, "hangar", "hangar"},
            {"infirmary", QT_TRANSLATE_NOOP("Interior", "Infirmary"), {1.5, 1.5, 3.5, 2.5}, "", "infirmary"},
            {"lifesupport", QT_TRANSLATE_NOOP("Interior", "Life Support"), {-7, -2, 2, 4}, "lifesupport", ""},
            {"rings", QT_TRANSLATE_NOOP("Interior", "Ring Room"), {5, -2, 2, 4}, "", "rings"},
        };
    }
    if (style == "generic") {
        // Any other mothership (Nakai, Ursini, mods): a simple layout
        return {
            {"hangar", QT_TRANSLATE_NOOP("Interior", "Hangar"), {-4.5, -3, 3, 2}, "hangar", "hangar"},
            {"battery", QT_TRANSLATE_NOOP("Interior", "Weapons Battery"), {-1.5, -3, 3, 2}, "weapons", ""},
            {"sensors", QT_TRANSLATE_NOOP("Interior", "Sensors"), {1.5, -3, 3, 2}, "sensors", ""},
            {"engines", QT_TRANSLATE_NOOP("Interior", "Engines"), {-4.5, -1, 3, 2}, "engines", ""},
            {"bridge", QT_TRANSLATE_NOOP("Interior", "Bridge"), {-1.5, -1, 3, 2}, "", "command"},
            {"shields", QT_TRANSLATE_NOOP("Interior", "Shield Generator"), {1.5, -1, 3, 2}, "shields", ""},
            {"reactor", QT_TRANSLATE_NOOP("Interior", "Reactor"), {-4.5, 1, 3, 2}, "", "power"},
            {"lifesupport", QT_TRANSLATE_NOOP("Interior", "Life Support"), {-1.5, 1, 3, 2}, "lifesupport", ""},
            {"quarters", QT_TRANSLATE_NOOP("Interior", "Crew Quarters"), {1.5, 1, 3, 2}, "", "rest"},
        };
    }
    // The Destiny: spire forward (x > 1.2), fan behind; see the status schematic
    return {
        {"maintower", QT_TRANSLATE_NOOP("Interior", "Main Tower"), {6.0, -0.6, 2.0, 1.2}, "weapons", ""},
        {"bridge", QT_TRANSLATE_NOOP("Interior", "Bridge"), {4.2, -0.7, 1.8, 1.4}, "", "command"},
        {"sensors", QT_TRANSLATE_NOOP("Interior", "Sensor Room"), {2.8, -0.8, 1.4, 0.8}, "sensors", ""},
        {"observation", QT_TRANSLATE_NOOP("Interior", "Observation Deck"), {2.8, 0.0, 1.4, 0.8}, "", "observation"},
        {"control", QT_TRANSLATE_NOOP("Interior", "Control Interface Room"), {1.2, -0.9, 1.6, 1.8}, "", "ancient"},
        {"gateroom", QT_TRANSLATE_NOOP("Interior", "Gate Room"), {-0.6, -1.2, 1.8, 2.4}, "", "gate"},
        {"shields", QT_TRANSLATE_NOOP("Interior", "Shield Emitters"), {-0.6, 1.2, 1.8, 1.8}, "shields", ""},
        {"infirmary", QT_TRANSLATE_NOOP("Interior", "Infirmary"), {-0.6, -3.0, 1.8, 1.8}, "", "infirmary"},
        {"lifesupport", QT_TRANSLATE_NOOP("Interior", "Life Support"), {-2.0, 2.0, 1.4, 2.0}, "lifesupport", ""},
        {"mess", QT_TRANSLATE_NOOP("Interior", "Mess Hall"), {-2.0, -2.0, 1.4, 2.0}, "", "morale"},
        {"quarters", QT_TRANSLATE_NOOP("Interior", "Crew Quarters"), {-2.0, 0.0, 1.4, 2.0}, "", "rest"},
        {"shuttlebay", QT_TRANSLATE_NOOP("Interior", "Shuttle Bay"), {-2.0, -4.0, 1.4, 2.0}, "", "hangar"},
        {"engines", QT_TRANSLATE_NOOP("Interior", "Engine Room"), {-4.6, -1.5, 1.6, 3.0}, "engines", ""},
        {"core", QT_TRANSLATE_NOOP("Interior", "Power Core"), {-3.0, -1.5, 1.0, 3.0}, "", "power"},
        {"hydroponics", QT_TRANSLATE_NOOP("Interior", "Hydroponics"), {-3.0, 1.5, 1.0, 2.5}, "", "food", true, {{"minerals", 6}, {"water", 30}}, 70},
        {"reclamation", QT_TRANSLATE_NOOP("Interior", "Water Reclamation"), {-3.0, -4.0, 1.0, 2.5}, "", "water", true, {{"minerals", 6}, {"crystal", 1}}, 70},
        {"archives", QT_TRANSLATE_NOOP("Interior", "Ancient Archives"), {-4.6, 1.5, 1.6, 2.5}, "", "archives", true, {{"crystal", 2}, {"ancient_alloy", 1}}, 90},
        {"armory", QT_TRANSLATE_NOOP("Interior", "Armory"), {-4.6, -4.0, 1.6, 2.5}, "", "armory", true, {{"trinium", 3}, {"minerals", 4}}, 60},
        {"upperdeck", QT_TRANSLATE_NOOP("Interior", "Upper Module Bay"), {-0.6, 3.0, 1.8, 2.0}, "", "modules", true, {{"trinium", 2}, {"minerals", 6}}, 60, 2},
        {"lowerdeck", QT_TRANSLATE_NOOP("Interior", "Lower Module Bay"), {-0.6, -5.0, 1.8, 2.0}, "", "modules", true, {{"trinium", 2}, {"minerals", 6}}, 60, 2},
    };
}

// Key characters of each ship (an enemy ship has only its generic teams)
QList<CrewDef> crewFor(const QString &style)
{
    using InteriorDef::Role;
    if (style == "Tau'ri")
        return {{"caldwell", "Steven Caldwell", Role::Military, "combat", 1.0f, "bridge"},
                {"hermiod", "Hermiod", Role::Scientist, "energy", 2.2f, "asgardcore"},
                {"novak", "Lindsey Novak", Role::Scientist, "ancient", 1.6f, "hyperdrive"},
                {"marks", "Kevin Marks", Role::Pilot, "pilot", 1.0f, "bridge"},
                {"lam", "Carolyn Lam", Role::Medic, "medicine", 1.0f, "infirmary"},
                {"sgteam", "SG-1", Role::Military, "sabotage", 1.0f, "transporter"}};
    if (style == "Goa'uld")
        return {{"firstprime", "First Prime Kar'yn", Role::Military, "boarding", 1.0f, "peltak"},
                {"lotaur", "Lo'taur Nerus", Role::Civilian, "supplies", 1.0f, "throne"},
                {"artisan", "Tor'el the artisan", Role::Engineer, "energy", 1.8f, "reactor"},
                {"healer", "Healer Ka'ra", Role::Medic, "medicine", 1.0f, "sarcophagus"}};
    if (style == "Lucian Alliance")
        return {{"kiva", "Kiva", Role::Military, "combat", 1.0f, "throne"},
                {"varro", "Varro", Role::Military, "boarding", 1.0f, "rings"},
                {"simeon", "Simeon", Role::Engineer, "energy", 1.6f, "reactor"},
                {"ginn", "Ginn", Role::Scientist, "ancient", 1.8f, "sensors"}};
    if (style == "Wraith")
        return {{"commander", "Commander Todd", Role::Military, "combat", 1.0f, "throne"},
                {"keeper", "Hive Keeper", Role::Medic, "medicine", 1.2f, "healing"},
                {"scientist", "Wraith Scientist", Role::Scientist, "energy", 1.9f, "engines"},
                {"blade", "Blade", Role::Military, "boarding", 1.0f, "dartbay"}};
    if (style == "Asgard")
        return {{"thor", "Thor", Role::Military, "combat", 1.2f, "bridge"},
                {"heimdall", "Heimdall", Role::Scientist, "ancient", 2.4f, "computer"},
                {"kvasir", "Kvasir", Role::Engineer, "energy", 2.2f, "core"}};
    if (style == "Ancients")
        return {{"helia", "Helia", Role::Military, "combat", 1.0f, "bridge"},
                {"janus", "Janus", Role::Scientist, "ancient", 2.4f, "chair"},
                {"moros", "Moros", Role::Scientist, "energy", 2.0f, "zpm"},
                {"healer", "Ancient healer", Role::Medic, "medicine", 1.2f, "infirmary"}};
    if (style == "Ori")
        return {{"adria", "Adria", Role::Civilian, "morale", 1.0f, "prior"},
                {"tomin", "Tomin", Role::Military, "combat", 1.0f, "mainbeam"},
                {"damaris", "Prior Damaris", Role::Medic, "medicine", 1.2f, "infirmary"}};
    if (style == "enemy")
        return {};
    return {{"young", "Everett Young", Role::Military, "combat", 1.0f, "bridge"},
            {"rush", "Nicholas Rush", Role::Scientist, "energy", 2.0f, "control"},
            {"eli", "Eli Wallace", Role::Scientist, "ancient", 1.8f, "control"},
            {"greer", "Ronald Greer", Role::Military, "boarding", 1.0f, "gateroom"},
            {"tj", "Tamara Johansen", Role::Medic, "medicine", 1.0f, "infirmary"},
            {"scott", "Matthew Scott", Role::Pilot, "pilot", 1.0f, "shuttlebay"},
            {"chloe", "Chloe Armstrong", Role::Civilian, "morale", 1.0f, "mess"},
            {"wray", "Camile Wray", Role::Civilian, "supplies", 1.0f, "quarters"},
            {"brody", "Adam Brody", Role::Engineer, "", 2.0f, "engines"},
            {"volker", "Dale Volker", Role::Scientist, "", 1.6f, "sensors"},
            {"park", "Lisa Park", Role::Scientist, "", 1.6f, "lifesupport"}};
}

} // namespace

Interior::Interior(const QString &style, const QString &faction)
    : m_style(style)
    , m_faction(faction.isEmpty() ? style : faction)
{
    // Asgard ships have very few people aboard
    if (m_faction == "Asgard")
        m_crewCount = 12;
    buildShip();
    buildCrew();
}

void Interior::buildShip()
{
    const QList<RoomDef> defs = roomsFor(m_faction);
    m_compartments.clear();
    for (const RoomDef &d : defs) {
        Compartment c;
        c.id = d.id;
        c.name = d.name;
        c.rect = d.rect;
        c.system = d.system;
        c.capability = d.capability;
        c.locked = d.locked;
        c.unlockCost = d.cost;
        c.unlockWork = d.work;
        c.bays = d.bays;
        m_compartments << c;
    }
    // Doors between compartments that share a wall
    for (int i = 0; i < m_compartments.size(); ++i)
        for (int j = i + 1; j < m_compartments.size(); ++j) {
            const QRectF a = m_compartments[i].rect.adjusted(-0.05, -0.05, 0.05, 0.05);
            const QRectF b = m_compartments[j].rect;
            const QRectF overlap = a.intersected(b);
            if (!overlap.isEmpty() && std::max(overlap.width(), overlap.height()) > 0.3) {
                m_compartments[i].neighbours << j;
                m_compartments[j].neighbours << i;
            }
        }
}

void Interior::buildCrew()
{
    m_people.clear();
    for (const CrewDef &d : crewFor(m_style == "enemy" ? m_style : m_faction)) {
        Person p;
        p.id = d.id;
        p.name = QString::fromUtf8(d.name);
        p.role = d.role;
        p.bonus = d.bonus;
        p.skill = d.skill;
        p.location = d.location;
        m_people << p;
    }
    // Soldier squads guard the ships of the conquest (Jaffa: more of them)
    if (!m_style.isEmpty()) {
        const int squads = m_faction == "Asgard" ? 0 : m_faction == "Goa'uld" || m_faction == "Lucian Alliance" ? 3 : 2;
        QString post;
        for (const Compartment &c : m_compartments)
            if (c.capability == "command")
                post = c.id;
        for (int i = 0; i < squads; ++i) {
            Person p;
            p.generic = true;
            p.id = QStringLiteral("squad%1").arg(i + 1);
            p.name = QCoreApplication::translate("Interior", "Soldier squad %1").arg(i + 1);
            p.role = InteriorDef::Role::Military;
            p.skill = 1.0f;
            p.location = post;
            m_people << p;
        }
    }
    setCrewCount(m_crewCount);
}

void Interior::setCrewCount(int crew)
{
    m_crewCount = std::max(0, crew);
    const int named = int(std::count_if(m_people.begin(), m_people.end(), [](const Person &p) { return !p.generic; }));
    const int teams = std::max(1, (m_crewCount - named) / kTeamSize);
    auto isTeam = [](const Person &p) { return p.generic && p.role == InteriorDef::Role::Engineer; };
    int current = int(std::count_if(m_people.begin(), m_people.end(), isTeam));
    while (current < teams) {
        Person p;
        p.generic = true;
        p.id = QStringLiteral("team%1").arg(current + 1);
        p.name = QCoreApplication::translate("Interior", "Repair team %1").arg(current + 1);
        p.role = InteriorDef::Role::Engineer;
        p.skill = 1.0f;
        m_people << p;
        ++current;
    }
    // Fewer people: the last teams are disbanded (their job is freed)
    while (current > teams) {
        for (int i = int(m_people.size()) - 1; i >= 0; --i)
            if (isTeam(m_people[i])) {
                for (Job &j : m_jobs)
                    j.workers.removeAll(i);
                m_people.removeAt(i);
                break;
            }
        --current;
    }
}

// ================================================================
//  Lookups
// ================================================================

int Interior::find(const QString &nameOrId) const
{
    const QString key = nameOrId.trimmed().toLower().remove(' ');
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_compartments.size(); ++i) {
        const Compartment &c = m_compartments[i];
        if (c.id == key || QString(c.name).toLower().remove(' ') == key
            || shown(c.name).toLower().remove(' ') == key)
            return i;
    }
    for (int i = 0; i < m_compartments.size(); ++i)      // prefix
        if (m_compartments[i].id.startsWith(key) || QString(m_compartments[i].name).toLower().remove(' ').startsWith(key))
            return i;
    return -1;
}

int Interior::findPerson(const QString &name) const
{
    const QString key = name.trimmed().toLower();
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_people.size(); ++i)
        if (m_people[i].id == key || m_people[i].name.toLower() == key
            || m_people[i].name.toLower().split(' ').contains(key))
            return i;
    return -1;
}

int Interior::compartmentOfSystem(const QString &system) const
{
    for (int i = 0; i < m_compartments.size(); ++i)
        if (m_compartments[i].system == system)
            return i;
    return -1;
}

int Interior::compartmentAt(const QPointF &local) const
{
    int best = -1;
    double bestDistance = 1e9;
    for (int i = 0; i < m_compartments.size(); ++i) {
        const QRectF &r = m_compartments[i].rect;
        if (r.contains(local))
            return i;
        const QPointF d(std::max({r.left() - local.x(), 0.0, local.x() - r.right()}),
                        std::max({r.top() - local.y(), 0.0, local.y() - r.bottom()}));
        const double distance = std::hypot(d.x(), d.y());
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

QRectF Interior::bounds() const
{
    QRectF r;
    for (const Compartment &c : m_compartments)
        r = r.united(c.rect);
    return r;
}

bool Interior::hasCapability(const QString &capability) const
{
    for (const Compartment &c : m_compartments)
        if (c.capability == capability)
            return !c.locked && !c.vented && c.fire < 0.5f && c.integrity > 15.0f && c.boarders == 0;
    return false;
}

bool Interior::hasModule(const QString &moduleId) const
{
    for (const Compartment &c : m_compartments)
        if (c.modules.contains(moduleId))
            return !c.vented && c.fire < 0.5f && c.integrity > 15.0f;
    return false;
}

bool Interior::bonusActive(const QString &bonus) const
{
    for (const Person &p : m_people)
        if (p.bonus == bonus)
            return p.injury <= 0.0f;
    return false;
}

int Interior::powerLoss() const
{
    int loss = 0;
    for (const Compartment &c : m_compartments)
        if (c.leak)
            loss += kLeakPower;
    return loss;
}

float Interior::workSpeed() const
{
    // The Asgard computer core guides every repair
    const float computer = hasCapability("computer") ? 1.4f : 1.0f;
    return (0.6f + 0.6f * m_morale / 100.0f) * computer;      // 0.6 .. 1.2 (x1.4 Asgard)
}

int Interior::freeWorkers() const
{
    const bool doctor = bonusActive("medicine");
    return int(std::count_if(m_people.begin(), m_people.end(), [doctor](const Person &p) {
        return p.job < 0 && p.injury <= 0.0f && !p.resting && p.role != InteriorDef::Role::Military
               && !(doctor && p.role == InteriorDef::Role::Medic);
    }));
}

int Interior::boarders() const
{
    int count = 0;
    for (const Compartment &c : m_compartments)
        count += c.boarders;
    return count;
}

void Interior::changeMorale(float delta)
{
    m_morale = std::clamp(m_morale + delta, 0.0f, 100.0f);
}

int Interior::injureRandom(int count, float seconds)
{
    int done = 0;
    for (int tries = 0; done < count && tries < 40; ++tries) {
        const int i = int(m_rng.bounded(int(m_people.size())));
        if (m_people[i].injury > 0.0f)
            continue;
        injure(i, seconds);
        ++done;
    }
    return done;
}

void Interior::dockRepair(float seconds)
{
    for (Compartment &c : m_compartments) {
        c.fire = std::max(0.0f, c.fire - 0.1f * seconds);
        c.integrity = std::min(100.0f, c.integrity + 2.0f * seconds);
        if (c.integrity > 80.0f) {
            c.breach = false;
            c.leak = false;
        }
        if (c.vented && c.fire <= 0.0f) {
            c.vented = false;
            c.sealed = false;
        }
        c.boarders = 0;
    }
    heal(seconds * 2.0f);
}

void Interior::heal(float seconds)
{
    for (Person &p : m_people)
        if (p.injury > 0.0f)
            p.injury = std::max(1.0f, p.injury - seconds);
}

void Interior::say(const QString &text, int tone)
{
    m_messages.append({text, tone});
}

QList<Interior::Message> Interior::takeMessages()
{
    QList<Message> messages;
    messages.swap(m_messages);
    return messages;
}

// ================================================================
//  Damage
// ================================================================

void Interior::startFire(int compartment, float intensity)
{
    if (compartment < 0 || compartment >= m_compartments.size())
        return;
    Compartment &c = m_compartments[compartment];
    if (c.vented || c.air < 15.0f)
        return;
    const bool wasBurning = c.fire > 0.0f;
    c.fire = std::min(1.0f, c.fire + intensity);
    if (!wasBurning)
        say(QCoreApplication::translate("Interior", "Fire in %1!").arg(shown(c.name)), 3);
}

void Interior::damageAt(const QPointF &local, float damage)
{
    const int index = compartmentAt(local);
    if (index < 0 || damage <= 0.0f)
        return;
    Compartment &c = m_compartments[index];
    c.integrity = std::max(0.0f, c.integrity - damage * 0.6f);
    if (rand01() < 0.2f + damage / 120.0f)
        startFire(index, 0.15f + damage / 200.0f);
    if (!c.breach && c.integrity < 60.0f && rand01() < damage / 70.0f) {
        c.breach = true;
        say(QCoreApplication::translate("Interior", "Hull breach in %1 - the air is escaping").arg(shown(c.name)), 3);
    }
    if (!c.leak && (c.capability == "power" || !c.system.isEmpty()) && rand01() < 0.12f + damage / 250.0f) {
        c.leak = true;
        say(QCoreApplication::translate("Interior", "Power conduit leaking in %1 (-%2 power)").arg(shown(c.name)).arg(kLeakPower), 2);
    }
    injureIn(index, damage / 60.0f);
}

void Interior::injure(int person, float seconds)
{
    Person &p = m_people[person];
    if (p.injury > 0.0f)
        return;
    p.injury = seconds;
    if (p.job >= 0) {
        for (Job &j : m_jobs)
            if (j.id == p.job)
                j.workers.removeAll(person);
        p.job = -1;
    }
    p.resting = false;
    p.location = QStringLiteral("infirmary");
    changeMorale(-2.0f);
    say(p.bonus.isEmpty() ? QCoreApplication::translate("Interior", "%1 injured - sent to the infirmary").arg(p.name)
                          : QCoreApplication::translate("Interior", "%1 injured - sent to the infirmary, bonus lost until healed").arg(p.name),
        2);
}

void Interior::injureIn(int compartment, float chance)
{
    const QString id = m_compartments[compartment].id;
    for (int i = 0; i < m_people.size(); ++i)
        if (m_people[i].location == id && m_people[i].injury <= 0.0f && rand01() < chance)
            injure(i, 120.0f + rand01() * 180.0f);
}

// ================================================================
//  Orders
// ================================================================

int Interior::addJob(Job job)
{
    job.id = m_nextJob++;
    job.work = job.total;
    m_jobs << job;
    assignWorkers();
    return job.id;
}

int Interior::orderRepair(const QString &what, QString *error)
{
    const QString key = what.trimmed().toLower();
    auto existing = [this](const QString &kind, const QString &target) {
        for (const Job &j : m_jobs)
            if (j.kind == kind && j.target == target)
                return j.id;
        return -1;
    };
    Job job;
    // A ship system: its compartment's job
    static const QStringList systems{"engines", "shields", "weapons", "lifesupport", "sensors", "hangar"};
    QString system = key;
    if (system == "engine") system = "engines";
    if (system == "shield") system = "shields";
    if (system == "weapon") system = "weapons";
    if (system == "life" || system == "life support") system = "lifesupport";
    if (system == "sensor") system = "sensors";
    if (systems.contains(system)) {
        if (existing("system", system) >= 0) {
            if (error) *error = QCoreApplication::translate("Interior", "a repair of %1 is already under way").arg(system);
            return -1;
        }
        job.kind = "system";
        job.target = system;
        const int c = compartmentOfSystem(system);
        job.compartment = c >= 0 ? m_compartments[c].id : QString();
        job.total = 25.0f;
        job.wanted = 2;
        return addJob(job);
    }
    if (key == "hull") {
        if (existing("hull", "hull") >= 0) {
            if (error) *error = QCoreApplication::translate("Interior", "the hull is already being repaired");
            return -1;
        }
        job.kind = "hull";
        job.target = "hull";
        job.total = 30.0f;
        job.wanted = 3;
        return addJob(job);
    }
    const int index = find(key);
    if (index < 0) {
        if (error) *error = QCoreApplication::translate("Interior", "unknown system or compartment '%1' (see 'plan')").arg(what);
        return -1;
    }
    const Compartment &c = m_compartments[index];
    if (c.locked) {
        if (error) *error = QCoreApplication::translate("Interior", "%1 is closed - 'unlock %2' to repair the section").arg(shown(c.name), c.id);
        return -1;
    }
    // Worst problem first: fire, breach, leak, structure
    if (c.fire > 0.0f) {
        job.kind = "fire";
        job.total = 8.0f + 20.0f * c.fire;
    } else if (c.breach) {
        job.kind = "breach";
        job.total = 20.0f;
    } else if (c.leak) {
        job.kind = "leak";
        job.total = 15.0f;
    } else if (c.integrity < 99.0f) {
        job.kind = "integrity";
        job.total = 10.0f + (100.0f - c.integrity) * 0.3f;
    } else if (!c.system.isEmpty()) {
        return orderRepair(c.system, error);
    } else {
        if (error) *error = QCoreApplication::translate("Interior", "%1 needs no repair").arg(shown(c.name));
        return -1;
    }
    if (existing(job.kind, c.id) >= 0) {
        if (error) *error = QCoreApplication::translate("Interior", "a team is already on it in %1").arg(shown(c.name));
        return -1;
    }
    if (c.vented && job.kind != "breach") {
        if (error) *error = QCoreApplication::translate("Interior", "%1 is open to space - seal it first ('seal %2')").arg(shown(c.name), c.id);
        return -1;
    }
    job.target = c.id;
    job.compartment = c.id;
    job.wanted = job.kind == "fire" ? 3 : 2;
    return addJob(job);
}

bool Interior::setSealed(const QString &compartment, bool sealed, QString *error)
{
    const int index = find(compartment);
    if (index < 0) {
        if (error) *error = QCoreApplication::translate("Interior", "unknown compartment '%1'").arg(compartment);
        return false;
    }
    Compartment &c = m_compartments[index];
    c.sealed = sealed;
    if (!sealed && c.vented) {
        c.vented = false;           // doors closed to space again, life support refills it
        say(QCoreApplication::translate("Interior", "%1 repressurized").arg(shown(c.name)), 1);
    }
    say(sealed ? QCoreApplication::translate("Interior", "%1 sealed - nothing goes through its doors").arg(shown(c.name))
               : QCoreApplication::translate("Interior", "%1 doors open").arg(shown(c.name)),
        0);
    return true;
}

bool Interior::vent(const QString &compartment, QString *error)
{
    const int index = find(compartment);
    if (index < 0) {
        if (error) *error = QCoreApplication::translate("Interior", "unknown compartment '%1'").arg(compartment);
        return false;
    }
    Compartment &c = m_compartments[index];
    c.sealed = true;
    c.vented = true;
    c.air = 0.0f;
    c.fire = 0.0f;
    say(QCoreApplication::translate("Interior", "%1 vented to space - fire out. Whoever was inside is hurt").arg(shown(c.name)), 2);
    injureIn(index, 0.8f);
    // Boarders without suits do not survive it
    if (c.boarders > 0) {
        say(QCoreApplication::translate("Interior", "%1 boarder(s) blown into space").arg(c.boarders), 1);
        c.boarders = 0;
    }
    return true;
}

int Interior::orderUnlock(const QString &compartment, QString *error)
{
    const int index = find(compartment);
    if (index < 0) {
        if (error) *error = QCoreApplication::translate("Interior", "unknown compartment '%1'").arg(compartment);
        return -1;
    }
    const Compartment &c = m_compartments[index];
    if (!c.locked) {
        if (error) *error = QCoreApplication::translate("Interior", "%1 is already open").arg(shown(c.name));
        return -1;
    }
    for (const Job &j : m_jobs)
        if (j.kind == "unlock" && j.target == c.id) {
            if (error) *error = QCoreApplication::translate("Interior", "a team is already working on %1").arg(shown(c.name));
            return -1;
        }
    Job job;
    job.kind = "unlock";
    job.target = c.id;
    job.compartment = c.id;
    job.total = c.unlockWork;
    job.wanted = 3;
    return addJob(job);
}

int Interior::orderInstall(const QString &moduleId, const QString &compartment, QString *error)
{
    const InteriorDef::Module *m = InteriorDef::module(moduleId);
    if (!m) {
        if (error) *error = QCoreApplication::translate("Interior", "unknown module '%1' (see 'blueprints')").arg(moduleId);
        return -1;
    }
    if (!m_blueprints.contains(m->id)) {
        if (error) *error = QCoreApplication::translate("Interior", "no blueprint for %1 yet").arg(shown(m->name));
        return -1;
    }
    int index = compartment.isEmpty() ? -1 : find(compartment);
    if (compartment.isEmpty()) {
        for (int i = 0; i < m_compartments.size() && index < 0; ++i) {
            const Compartment &c = m_compartments[i];
            int pending = 0;
            for (const Job &j : m_jobs)
                if (j.kind == "install" && j.compartment == c.id)
                    ++pending;
            if (!c.locked && c.bays > int(c.modules.size()) + pending)
                index = i;
        }
    }
    if (index < 0) {
        if (error) *error = QCoreApplication::translate("Interior", "no free module bay - unlock 'upperdeck' or 'lowerdeck'");
        return -1;
    }
    const Compartment &c = m_compartments[index];
    int pending = 0;
    for (const Job &j : m_jobs)
        if (j.kind == "install" && j.compartment == c.id)
            ++pending;
    if (c.locked || c.bays <= int(c.modules.size()) + pending) {
        if (error) *error = QCoreApplication::translate("Interior", "%1 has no free module bay").arg(shown(c.name));
        return -1;
    }
    Job job;
    job.kind = "install";
    job.target = m->id;
    job.compartment = c.id;
    job.total = m->work;
    job.wanted = 2;
    return addJob(job);
}

bool Interior::rest(const QString &person, QString *error)
{
    const int index = findPerson(person);
    if (index < 0) {
        if (error) *error = QCoreApplication::translate("Interior", "nobody called '%1' aboard ('crew' for the list)").arg(person);
        return false;
    }
    Person &p = m_people[index];
    if (p.injury > 0.0f) {
        if (error) *error = QCoreApplication::translate("Interior", "%1 is in the infirmary").arg(p.name);
        return false;
    }
    if (p.job >= 0)
        for (Job &j : m_jobs)
            if (j.id == p.job)
                j.workers.removeAll(index);
    p.job = -1;
    p.resting = true;
    p.location = QStringLiteral("quarters");
    return true;
}

int Interior::rotate()
{
    int count = 0;
    for (int i = 0; i < m_people.size(); ++i)
        if (!m_people[i].resting && m_people[i].injury <= 0.0f && m_people[i].fatigue >= 50.0f) {
            rest(m_people[i].id, nullptr);
            ++count;
        }
    assignWorkers();
    return count;
}

bool Interior::cancelJob(int id)
{
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs[i].id == id) {
            for (int w : m_jobs[i].workers)
                m_people[w].job = -1;
            m_jobs.removeAt(i);
            assignWorkers();
            return true;
        }
    return false;
}

// ================================================================
//  Boarding
// ================================================================

void Interior::board(int strength)
{
    QList<int> candidates;
    for (int i = 0; i < m_compartments.size(); ++i)
        if (!m_compartments[i].locked && !m_compartments[i].vented)
            candidates << i;
    if (candidates.isEmpty())
        return;
    Compartment &c = m_compartments[candidates[int(m_rng.bounded(int(candidates.size())))]];
    c.boarders += std::max(1, strength);
    c.boarderTimer = 30.0f;
    say(QCoreApplication::translate("Interior", "Boarding party in %1 (%2 hostiles) - 'defend %3' or vent it").arg(shown(c.name)).arg(c.boarders).arg(c.id), 3);
}

bool Interior::defend(const QString &compartment, QString *error)
{
    const int index = find(compartment);
    if (index < 0) {
        if (error) *error = QCoreApplication::translate("Interior", "unknown compartment '%1'").arg(compartment);
        return false;
    }
    const QString id = m_compartments[index].id;
    if (!m_defenders.contains(id))
        m_defenders << id;
    for (Person &p : m_people)
        if (p.role == InteriorDef::Role::Military && p.injury <= 0.0f) {
            p.location = id;
            p.resting = false;
        }
    say(QCoreApplication::translate("Interior", "Soldiers sent to %1").arg(shown(m_compartments[index].name)), 0);
    return true;
}

float Interior::boardingChance() const
{
    float chance = 0.35f + std::max(0, m_crewCount - 40) * 0.004f;
    if (bonusActive("boarding"))
        chance += 0.2f;                         // Greer leads the team
    if (bonusActive("combat"))
        chance += 0.05f;
    if (hasCapability("armory"))
        chance += 0.15f;
    return std::clamp(chance * workSpeed(), 0.05f, 0.9f);
}

bool Interior::boardEnemy()
{
    const bool success = rand01() < boardingChance();
    if (success) {
        changeMorale(5.0f);
    } else {
        // The team pays for it
        for (int i = 0; i < m_people.size(); ++i)
            if (m_people[i].role == InteriorDef::Role::Military && rand01() < 0.5f)
                injure(i, 200.0f);
        changeMorale(-4.0f);
    }
    return success;
}

// ================================================================
//  Blueprints
// ================================================================

bool Interior::learnBlueprint(const QString &moduleId)
{
    const InteriorDef::Module *m = InteriorDef::module(moduleId);
    if (!m || m_blueprints.contains(m->id))
        return false;
    m_blueprints << m->id;
    say(QCoreApplication::translate("Interior", "New blueprint: %1 (%2) - 'install %3'").arg(shown(m->name), shown(m->effect), m->id), 1);
    return true;
}

QString Interior::randomUnknownBlueprint()
{
    QStringList unknown;
    for (const InteriorDef::Module &m : InteriorDef::modules())
        if (!m_blueprints.contains(m.id))
            unknown << m.id;
    return unknown.isEmpty() ? QString() : unknown[int(m_rng.bounded(int(unknown.size())))];
}

// ================================================================
//  Simulation
// ================================================================

float Interior::personSpeed(const Person &p, const Job &job) const
{
    float speed = p.skill * (p.fatigue > 80.0f ? 0.5f : 1.0f - p.fatigue / 300.0f);
    // Key characters in their field
    if (p.bonus == "energy" && (job.kind == "leak" || job.target == "engines" || job.compartment == "core"))
        speed *= 2.0f;
    if (p.bonus == "ancient" && (job.kind == "unlock" || job.kind == "install" || job.target == "sensors"))
        speed *= 2.0f;
    return speed;
}

void Interior::assignWorkers()
{
    // Jobs in order: each one takes the best free workers it can use
    for (Job &job : m_jobs) {
        while (job.workers.size() < job.wanted) {
            int best = -1;
            float bestSpeed = 0;
            for (int i = 0; i < m_people.size(); ++i) {
                const Person &p = m_people[i];
                if (p.job >= 0 || p.injury > 0.0f || p.resting || p.role == InteriorDef::Role::Military)
                    continue;
                if (p.role == InteriorDef::Role::Medic && bonusActive("medicine"))
                    continue;           // the doctor stays in the infirmary
                const float speed = personSpeed(p, job);
                if (speed > bestSpeed) {
                    bestSpeed = speed;
                    best = i;
                }
            }
            if (best < 0)
                break;
            job.workers << best;
            m_people[best].job = job.id;
            if (!job.compartment.isEmpty())
                m_people[best].location = job.compartment;
        }
    }
}

void Interior::finishJob(const Job &job, Effects &effects)
{
    const int index = find(job.compartment);
    Compartment *c = index >= 0 ? &m_compartments[index] : nullptr;
    if (job.kind == "system") {
        effects.systemRepair.append({job.target, 35.0f});
        say(QCoreApplication::translate("Interior", "Repair of %1 complete (+35%)").arg(job.target), 1);
    } else if (job.kind == "hull") {
        effects.hullRepair += 20.0f;
        say(QCoreApplication::translate("Interior", "Hull patched (+20%)"), 1);
    } else if (c && job.kind == "fire") {
        c->fire = 0.0f;
        ++effects.firesOut;
        say(QCoreApplication::translate("Interior", "Fire in %1 put out").arg(shown(c->name)), 1);
    } else if (c && job.kind == "breach") {
        c->breach = false;
        c->vented = false;
        say(QCoreApplication::translate("Interior", "Breach in %1 sealed").arg(shown(c->name)), 1);
    } else if (c && job.kind == "leak") {
        c->leak = false;
        say(QCoreApplication::translate("Interior", "Power conduit in %1 repaired").arg(shown(c->name)), 1);
    } else if (c && job.kind == "integrity") {
        c->integrity = 100.0f;
        say(QCoreApplication::translate("Interior", "Structure of %1 reinforced").arg(shown(c->name)), 1);
    } else if (c && job.kind == "unlock") {
        c->locked = false;
        c->integrity = 100.0f;
        changeMorale(4.0f);
        say(QCoreApplication::translate("Interior", "%1 is open! New capability aboard").arg(shown(c->name)), 1);
        if (c->capability == "archives") {
            const QString blueprint = randomUnknownBlueprint();
            if (!blueprint.isEmpty())
                learnBlueprint(blueprint);
        }
    } else if (c && job.kind == "install") {
        c->modules << job.target;
        const InteriorDef::Module *m = InteriorDef::module(job.target);
        say(QCoreApplication::translate("Interior", "%1 installed in %2 (%3)").arg(m ? shown(m->name) : job.target, shown(c->name),
                                                m ? shown(m->effect) : QString()),
            1);
    }
}

Interior::Effects Interior::tick(float seconds, float lifeSupport, bool inBattle)
{
    Effects effects;
    if (seconds <= 0.0f)
        return effects;

    // --- Compartments: fire, air, breaches
    QList<float> air;
    for (const Compartment &c : m_compartments)
        air << c.air;
    for (int i = 0; i < m_compartments.size(); ++i) {
        Compartment &c = m_compartments[i];
        if (c.locked)
            continue;
        if (c.fire > 0.0f) {
            if (c.air < 10.0f) {
                c.fire = std::max(0.0f, c.fire - 0.1f * seconds);     // no air: it dies down
                if (c.fire <= 0.0f)
                    say(QCoreApplication::translate("Interior", "The fire in %1 died out for lack of air").arg(shown(c.name)), 1);
            } else {
                // A Wraith hive is organic: fires spread badly in it
                const float growth = kFireGrowth * (m_faction == "Wraith" ? 0.6f : 1.0f);
                c.fire = std::min(1.0f, c.fire + growth * c.air / 100.0f * seconds);
            }
            c.air = std::max(0.0f, c.air - kFireAirUse * c.fire * seconds);
            c.integrity = std::max(0.0f, c.integrity - 0.3f * c.fire * seconds);
            if (!c.system.isEmpty())
                effects.systemDamage.append({c.system, kFireSystemDamage * c.fire * seconds});
            injureIn(i, 0.01f * c.fire * seconds);
            // Spreads through open doors
            if (!c.sealed)
                for (int n : c.neighbours)
                    if (!m_compartments[n].sealed && !m_compartments[n].locked && m_compartments[n].fire <= 0.0f
                        && rand01() < kFireSpread * c.fire * seconds)
                        startFire(n, 0.15f);
        }
        if (c.breach || c.vented)
            c.air = std::max(0.0f, c.air - kBreachLoss * seconds);
        else if (lifeSupport > 0.0f && c.fire <= 0.0f)          // smoke: no fresh air while it burns
            c.air = std::min(100.0f, c.air + kAirRefill * lifeSupport / 100.0f * seconds);
        // Air flows through open doors
        if (!c.sealed)
            for (int n : c.neighbours)
                if (!m_compartments[n].sealed && n > i) {
                    Compartment &o = m_compartments[n];
                    const float flow = (air[i] - air[n]) * std::min(1.0f, kAirMix * seconds) * 0.5f;
                    c.air = std::clamp(c.air - flow, 0.0f, 100.0f);
                    o.air = std::clamp(o.air + flow, 0.0f, 100.0f);
                }
        if (c.air < 25.0f)
            injureIn(i, 0.004f * seconds);
    }

    // --- Organic hull (Wraith): the hive heals its structure and closes
    // its own breaches little by little (twice faster with its
    // regeneration node intact)
    if (m_faction == "Wraith")
        for (Compartment &c : m_compartments) {
            c.integrity = std::min(100.0f, c.integrity + (hasCapability("regeneration") ? 0.8f : 0.4f) * seconds);
            if (c.breach && c.integrity > 70.0f) {
                c.breach = false;
                say(QCoreApplication::translate("Interior", "The hive closed the breach in %1").arg(shown(c.name)), 0);
            }
        }

    // --- Boarders: they sabotage, fight the soldiers, move on
    for (int i = 0; i < m_compartments.size(); ++i) {
        Compartment &c = m_compartments[i];
        if (c.boarders <= 0)
            continue;
        if (!c.system.isEmpty())
            effects.systemDamage.append({c.system, 0.8f * c.boarders * seconds});
        int soldiers = 0;
        for (const Person &p : m_people)
            if (p.role == InteriorDef::Role::Military && p.injury <= 0.0f && p.location == c.id)
                soldiers += p.bonus == "boarding" ? 3 : 2;
        if (m_defenders.contains(c.id))
            soldiers += std::max(1, m_crewCount / 25);     // armed crew members
        if (hasCapability("armory"))
            soldiers = int(soldiers * 1.3f);
        if (soldiers > 0) {
            if (rand01() < 0.12f * soldiers / float(soldiers + c.boarders) * seconds * 3.0f) {
                --c.boarders;
                if (c.boarders == 0) {
                    say(QCoreApplication::translate("Interior", "Boarders in %1 neutralized").arg(shown(c.name)), 1);
                    changeMorale(3.0f);
                    m_defenders.removeAll(c.id);
                }
            }
            if (c.boarders > 0 && rand01() < 0.05f * c.boarders / float(soldiers + c.boarders) * seconds) {
                ++effects.crewLost;
                say(QCoreApplication::translate("Interior", "A crew member was killed fighting the boarders in %1").arg(shown(c.name)), 3);
                changeMorale(-5.0f);
            }
        } else {
            injureIn(i, 0.02f * seconds);
        }
        c.boarderTimer -= seconds;
        if (c.boarders > 0 && c.boarderTimer <= 0.0f && !c.neighbours.isEmpty()) {
            const int next = c.neighbours[int(m_rng.bounded(int(c.neighbours.size())))];
            Compartment &n = m_compartments[next];
            if (!n.locked && !n.sealed && !n.vented) {
                n.boarders += c.boarders;
                n.boarderTimer = 30.0f;
                say(QCoreApplication::translate("Interior", "The boarders move from %1 to %2").arg(shown(c.name), shown(n.name)), 2);
                c.boarders = 0;
            } else {
                c.boarderTimer = 10.0f;     // sealed doors hold them back
            }
        }
    }

    // --- Repair jobs
    assignWorkers();
    const float speed = workSpeed();
    QList<Job> done;
    for (Job &job : m_jobs) {
        float rate = 0;
        for (int w : job.workers)
            rate += personSpeed(m_people[w], job);
        job.work -= rate * speed * seconds;
        if (job.work <= 0.0f)
            done << job;
    }
    for (const Job &job : done) {
        for (int w : job.workers)
            if (w < m_people.size())
                m_people[w].job = -1;
        m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(), [&job](const Job &j) { return j.id == job.id; }),
                     m_jobs.end());
        finishJob(job, effects);
    }
    assignWorkers();

    // --- People: fatigue, rest, injuries
    const bool infirmary = hasCapability("infirmary");
    // A Goa'uld sarcophagus brings anyone back quickly
    const float sarcophagus = m_faction == "Goa'uld" && infirmary ? 1.5f : 1.0f;
    const float healing = (infirmary ? 2.0f : 1.0f) * (bonusActive("medicine") ? 1.5f : 1.0f) * sarcophagus;
    for (int i = 0; i < m_people.size(); ++i) {
        Person &p = m_people[i];
        if (p.injury > 0.0f) {
            p.injury = std::max(0.0f, p.injury - healing * seconds);
            if (p.injury <= 0.0f) {
                p.location = QStringLiteral("quarters");
                say(QCoreApplication::translate("Interior", "%1 is back on duty").arg(p.name), 1);
            }
            continue;
        }
        if (p.job >= 0)
            p.fatigue = std::min(100.0f, p.fatigue + kFatigueWork * seconds * (inBattle ? 1.3f : 1.0f));
        else if (p.resting) {
            p.fatigue = std::max(0.0f, p.fatigue - kFatigueRest * seconds * (hasCapability("rest") ? 1.0f : 0.5f));
            if (p.fatigue <= 5.0f) {
                p.resting = false;
                if (!p.generic)
                    say(QCoreApplication::translate("Interior", "%1 is rested").arg(p.name), 0);
            }
        } else
            p.fatigue = std::max(0.0f, p.fatigue - kFatigueIdle * seconds);
        // Exhausted people make mistakes
        if (p.fatigue >= 95.0f && rand01() < 0.003f * seconds)
            injure(i, 90.0f);
    }

    // --- Morale drifts toward what the ship offers
    float target = 55.0f;
    if (hasCapability("morale")) target += 10.0f;
    if (hasCapability("observation")) target += 5.0f;
    if (bonusActive("morale")) target += 5.0f;
    if (hasCapability("food")) target += 5.0f;
    if (inBattle) target -= 10.0f;
    for (const Compartment &c : m_compartments)
        if (c.fire > 0.0f || c.boarders > 0)
            target -= 5.0f;
    m_morale += (std::clamp(target, 10.0f, 95.0f) - m_morale) * std::min(1.0f, 0.004f * seconds);

    // --- Production of the unlocked sections and modules (per game hour = 4 s)
    m_productionClock += seconds;
    while (m_productionClock >= 4.0f) {
        m_productionClock -= 4.0f;
        if (hasCapability("food")) effects.food += 1.0f;
        if (hasCapability("water")) effects.water += 1.5f;
        if (hasModule("hydroponic_rack")) effects.food += 2.0f;
        if (hasModule("water_recycler")) effects.water += 3.0f;
    }
    return effects;
}

// ================================================================
//  Save
// ================================================================

QJsonObject Interior::toJson() const
{
    QJsonArray compartments;
    for (const Compartment &c : m_compartments)
        compartments.append(QJsonObject{{"id", c.id}, {"locked", c.locked}, {"air", double(c.air)},
                                        {"fire", double(c.fire)}, {"integrity", double(c.integrity)},
                                        {"breach", c.breach}, {"leak", c.leak}, {"sealed", c.sealed},
                                        {"vented", c.vented}, {"boarders", c.boarders},
                                        {"modules", QJsonArray::fromStringList(c.modules)}});
    QJsonArray people;
    for (const Person &p : m_people)
        if (!p.generic)
            people.append(QJsonObject{{"id", p.id}, {"fatigue", double(p.fatigue)}, {"injury", double(p.injury)},
                                      {"resting", p.resting}, {"location", p.location}});
    QJsonArray jobs;
    for (const Job &j : m_jobs)
        jobs.append(QJsonObject{{"kind", j.kind}, {"target", j.target}, {"compartment", j.compartment},
                                {"work", double(j.work)}, {"total", double(j.total)}, {"wanted", j.wanted}});
    return QJsonObject{{"faction", m_faction}, {"compartments", compartments}, {"people", people}, {"jobs", jobs},
                       {"blueprints", QJsonArray::fromStringList(m_blueprints)},
                       {"morale", double(m_morale)}, {"crew", m_crewCount}};
}

void Interior::fromJson(const QJsonObject &json)
{
    if (json.isEmpty())
        return;
    if (json.contains("faction"))
        m_faction = json.value("faction").toString();
    buildShip();
    buildCrew();
    m_jobs.clear();
    for (const QJsonValue &v : json.value("compartments").toArray()) {
        const QJsonObject o = v.toObject();
        const int i = find(o.value("id").toString());
        if (i < 0)
            continue;
        Compartment &c = m_compartments[i];
        c.locked = o.value("locked").toBool(c.locked);
        c.air = float(o.value("air").toDouble(100));
        c.fire = float(o.value("fire").toDouble(0));
        c.integrity = float(o.value("integrity").toDouble(100));
        c.breach = o.value("breach").toBool();
        c.leak = o.value("leak").toBool();
        c.sealed = o.value("sealed").toBool();
        c.vented = o.value("vented").toBool();
        c.boarders = o.value("boarders").toInt();
        c.boarderTimer = 30.0f;
        c.modules.clear();
        for (const QJsonValue &m : o.value("modules").toArray())
            c.modules << m.toString();
    }
    for (const QJsonValue &v : json.value("people").toArray()) {
        const QJsonObject o = v.toObject();
        const int i = findPerson(o.value("id").toString());
        if (i < 0)
            continue;
        Person &p = m_people[i];
        p.fatigue = float(o.value("fatigue").toDouble());
        p.injury = float(o.value("injury").toDouble());
        p.resting = o.value("resting").toBool();
        p.location = o.value("location").toString(p.location);
    }
    m_blueprints.clear();
    for (const QJsonValue &v : json.value("blueprints").toArray())
        m_blueprints << v.toString();
    m_morale = float(json.value("morale").toDouble(70));
    setCrewCount(json.value("crew").toInt(m_crewCount));
    for (const QJsonValue &v : json.value("jobs").toArray()) {
        const QJsonObject o = v.toObject();
        Job j;
        j.kind = o.value("kind").toString();
        j.target = o.value("target").toString();
        j.compartment = o.value("compartment").toString();
        j.total = float(o.value("total").toDouble());
        j.wanted = o.value("wanted").toInt(1);
        j.id = m_nextJob++;
        j.work = float(o.value("work").toDouble(j.total));
        m_jobs << j;
    }
    assignWorkers();
}
