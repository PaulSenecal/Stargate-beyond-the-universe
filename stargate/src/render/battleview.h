#ifndef BATTLEVIEW_H
#define BATTLEVIEW_H

#include <map>
#include <memory>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include "modmesh.h"
#include "src/core/balance.h"
#include <QPoint>
#include <QPointF>
#include <QStringList>
#include <QVector3D>
#include <utility>
#include <vector>

#include "meshbuilder.h"
#include "planetlook.h"
#include "src/audio/soundcue.h"
#include "src/core/interior.h"
#include "src/core/shipclass.h"

class QPainter;
class QTimer;


// ================================================================
//  BattleView
//  Real-time 3D view of the space battle shown in the center of the
//  game menu: the Destiny, enemy ships, weapon fire, shield, planet
//  and a tactical HUD (reticles, health bars, subsystem callouts).
//
//  Plain OpenGL 3.3 core (QOpenGLWidget) with procedural meshes, so it
//  needs no extra Qt module. Orders typed in the command console are
//  forwarded through the public methods below; everything worth
//  reporting is emitted through logMessage().
//
//  Mouse: drag = orbit the camera, wheel = zoom.
// ================================================================

// A Destiny weapon as described in the database (table Component)
struct WeaponProfile {
    QString name;
    QString munition;           // kinetic | energy | missile
    float damage = 0;
    float penetration = 0;
    float reach = 0;            // world units
};

// Weapons state for the weapon panel
struct WeaponReadiness {
    float mainCooldown = 0, mainMax = 1;
    float pulseCooldown = 0, pulseMax = 1;
    float burstCooldown = 0, burstMax = 1;
    float efficiency = 100;     // weapons system health (auto-fire)
    bool hasTarget = false;
    int targetIndex = -1;       // 0-based, shown as T1…
    QString targetClass;
    float targetHealth = 0;     // percent
    int hostiles = 0;
    int kills = 0;
    // Hangar fighters and friendly ships
    int hangar = 0, hangarMax = 0;
    int drones = 0, dronesMax = 0;      // drone salvos (Ancient flagship)
    // Weapons of the flagship (Balance::flagshipLoadout)
    QString mainName;                   // untranslated, context "Balance"
    bool hasPulse = true, hasTowers = true, hasRailguns = true;
    int fightersOut = 0;
    int allies = 0;
    // Energy
    bool turretsOn = true, railgunsOn = true;
    int freePower = 0;
    QString priority;           // balanced | shields | weapons
    // Railguns (point defense)
    float railgunReach = 0;
    int interceptions = 0;
    int incomingMissiles = 0;
    float railgunHeat = 0;              // 0..100, overheated at 100
    bool railgunOverheated = false;
    int railgunAmmo = 0, railgunAmmoMax = 0;
    int missiles = 0, missilesMax = 0;  // the player's missiles
    // Tactics
    QString aimSystem;                  // enemy subsystem aimed at ("" = hull)
    bool evading = false;
    float accuracy = 100;               // percent, on the current target
    float ftlCountdown = -1;            // seconds before the FTL jump, -1 = none
};

// One moment of a recorded battle, seen from above (world x, z)
struct ReplayFrame {
    struct Ship { QPointF pos; QPointF facing; bool mothership; int side; float health; };  // side: 0 you, 1 enemy, 2 ally
    struct Shot { QPointF pos; bool friendly; bool missile; bool beam; };
    float time = 0;
    float hull = 100, shield = 100;
    QList<Ship> ships;
    QList<Shot> shots;
    QStringList events;         // log lines of that moment
};

// Statistics of one battle (summary at the end, achievements)
struct BattleStats {
    float seconds = 0;
    int shots = 0, hits = 0;
    int kills = 0, motherships = 0;
    float damageDealt = 0;
    float hullLost = 0, shieldLost = 0;
    int interceptions = 0;
    int alliesLost = 0;
    QString faction;
};

// A recent impact on the Destiny, in ship-local coordinates
struct HitMark {
    QVector3D local;
    float age = 0;          // seconds
    bool shield = false;    // absorbed by the shield
};

class BattleView : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    enum class LogLevel { Info, Success, Warning, Critical };
    Q_ENUM(LogLevel)

    explicit BattleView(QWidget *parent = nullptr);
    ~BattleView() override;

    // --- Orders (targets are 1-based "T1, T2…" on screen, 0-based here)
    bool fireMainWeapon(int enemyIndex = -1);   // -1 = current target
    bool firePulseVolley();
    bool fireSecondaryBurst();
    void setShieldActive(bool active);
    // Flagship without shield (Wraith hive) and how much its hull resists
    // (damage taken x armor)
    void setShipEquipment(bool shield, float armor);
    bool hasShield() const { return m_hasShield; }
    bool lockTarget(int enemyIndex);
    // Aims at a subsystem of an enemy: weapons, shields, engines, hangar
    // ("" or "hull" = the hull). Harder to hit, but disables it.
    bool aimAt(int enemyIndex, const QString &system);
    static QStringList enemySystems();
    bool fireMissiles(int enemyIndex = -1);     // the player's missile bay
    void setEvasion(bool on);                   // evasive maneuvers (engines, power)
    bool evading() const { return m_evading; }
    bool repairSubsystem(const QString &name);
    // Repairs a system by `amount` percent (crew repair jobs)
    void repairBy(const QString &key, float amount);

    // --- Ship systems: engines, shields, weapons, lifesupport (+ hull)
    // Normalizes a typed name ("engine", "Life Support"…) to a system
    // key; returns an empty string if unknown.
    static QString systemKey(const QString &name);
    static QStringList systemKeys();
    void setSubsystemHealth(const QString &key, float health);
    float systemHealth(const QString &key) const;   // also accepts "hull"

    // --- Supplies (finite): railgun slugs, missiles, drone salvos.
    // They are only restocked on planets (campaign) or worlds (conquest).
    int railgunAmmo() const { return m_railgunAmmo; }
    void setRailgunAmmo(int ammo) { m_railgunAmmo = std::clamp(ammo, 0, kRailgunAmmoMax()); }
    static int kRailgunAmmoMax() { return Balance::kRailgunAmmo; }
    int missiles() const { return m_missiles; }
    int missileCapacity() const { return m_missileBay; }
    void setMissileBay(int capacity, int stock);
    int drones() const { return m_drones; }
    int droneCapacity() const { return m_droneBay; }
    void setDrones(int salvos) { m_drones = std::clamp(salvos, 0, m_droneBay); }

    // --- FTL jump under fire: the drive spools for `seconds`, hangar
    // fighters fly back; jumpReady() when it is time to jump
    void startJumpSequence(float seconds);
    void abortJumpSequence();
    bool jumpPending() const { return m_jumpCountdown >= 0.0f; }
    // Hangar fighters left too far: lost at the jump. Returns how many.
    int dockForJump();
    // Fighters launched by order before a jump take off again after it
    void relaunchAfterJump();
    const BattleStats &battleStats() const { return m_stats; }
    // Replay of the last finished battle (10 frames per second)
    const QList<ReplayFrame> &lastReplay() const { return m_lastReplay; }
    QString lastReplayFaction() const { return m_lastReplayFaction; }

    // --- Interior of the ship (campaign)
    void setPowerLoss(int power) { m_powerLoss = std::max(0, power); }  // leaking conduits
    void setPowerBonus(int power) { m_powerBonus = std::max(0, power); } // naquadah generator
    void setAccuracyBonus(float factor) { m_accuracyBonus = factor; }   // sensor suite, Eli
    // Red alert lights (fires, boarders aboard) on top of the low hull alarm
    void setRedAlert(bool on) { m_redAlert = on; }
    // Observation deck: a slow wide view of the ship, no tactical HUD
    void setObservation(bool on);
    bool observing() const { return m_observing; }
    // Burning compartments shown on the hull (replaces the random fires)
    void setFireSpots(const QList<QVector3D> &spots);
    // Boarding a crippled enemy mothership (shield down, hull < 35%)
    bool canBoard(int enemyIndex, QString *why) const;
    // The boarded ship is taken: it leaves the battle. Returns its faction.
    QString captureEnemy(int enemyIndex);

    // --- Inside the enemy motherships (conquest and campaign)
    std::shared_ptr<Interior> enemyInterior(int enemyIndex) const;
    QString enemyClass(int enemyIndex) const;
    QString enemyFaction(int enemyIndex) const;
    bool enemyShieldDown(int enemyIndex) const;
    // A team or a fire in a room of an enemy ship damages the system there
    void damageEnemyRoom(int enemyIndex, const QString &room, float damage);
    // A hangar fighter flies to an enemy ship and latches on it: a team
    // can then go aboard through it
    bool dockShuttle(int enemyIndex, QString *error);
    bool shuttleDocked(int enemyIndex) const;
    // Damage of the enemy motherships still alive (0..1), and damage they
    // start with (a fleet that fled comes back hurt)
    float enemyDamage() const;
    void setIncomingDamage(float damage) { m_incomingDamage = std::clamp(damage, 0.0f, 0.9f); }

    // Advances the battle by `seconds` at 60 steps per second, without
    // rendering (balance measurements, tests)
    void simulate(float seconds);
    // Stops the real-time battle (pause menu)
    void setPaused(bool paused);

    // Destiny weapons from the database: "Railgun" (point defense,
    // anti-fighter) and "Energy Cannon" (tower auto-fire)
    void setWeaponProfiles(const QList<WeaponProfile> &profiles);

    // Ship classes from the ship library (Ship_Type + Ship_Attack).
    // Enemy waves are built from the hostile ones, one faction per
    // wave. Replaces the current wave.
    void setShipClasses(const QList<ShipClass> &classes);
    QString waveFaction() const { return m_waveFaction; }

    // --- Save / restore
    float hull() const { return m_hull; }
    float shieldStrength() const { return m_shieldStrength; }
    bool shieldActive() const { return m_shieldOrdered; }
    int hostileCount() const { return int(aliveIds().size()); }

    // --- Damage feed for the ship status panel
    QList<HitMark> recentHits() const;
    QList<QVector3D> fireSpots() const { return QList<QVector3D>(m_fires.begin(), m_fires.end()); }
    float secondsSinceHit(const QString &key) const;     // system key, large if never hit
    float subsystemHealth(const QString &key) const;     // 0..100, 100 if unknown
    WeaponReadiness weaponReadiness() const;

    // Radar feed, in ship-local coordinates (x forward, z starboard)
    struct RadarBlip { QVector3D local; int index; bool mothership; bool locked; float health; bool ally; };
    struct RadarShot { QVector3D local; bool fromDestiny; bool missile; };
    QList<RadarBlip> radarContacts() const;
    QList<RadarShot> radarShots() const;
    void restoreDefenses(float hull, float shield, bool shieldOn);

    // --- Navigation
    // `faction` empty = a random hostile faction
    void spawnWave(bool announce, const QString &faction = QString());
    // FTL jump: every enemy is left behind; new contacts may show up
    // after `calmSeconds`
    void clearBattlefield(float calmSeconds, bool keepAllies = false);
    // Conquest: no automatic wave; fleets come from spawnFleet() and
    // sectorCleared() tells when one is destroyed
    void setAutoWaves(bool on) { m_autoWaves = on; }
    // Conquest technologies: damage of the Destiny's shots, shield recharge
    void setDamageBonus(float factor) { m_damageBonus = factor; }
    void setShieldRegenBonus(float factor) { m_shieldRegenBonus = factor; }

    // --- Energy: the power core gives 100; every active system draws its
    // share, the rest boosts the shields or the weapon recharge
    enum class PowerPriority { Balanced, Shields, Weapons };
    void setTurretsActive(bool on);             // secondary towers auto-fire
    void setRailgunsActive(bool on);            // point defense
    void setPowerPriority(PowerPriority priority);
    bool turretsActive() const { return m_turretsOn; }
    bool railgunsActive() const { return m_railgunsOn; }
    int freePower() const;
    float shieldPowerBoost() const;
    float weaponPowerBoost() const;
    QString powerReport() const;
    // Fleet of any faction of the library (bosses excluded), even ships
    // that never come in survival waves (Tau'ri…)
    bool spawnFleet(const QString &faction, int motherships, int fighters);

    // --- Friendly ships: allied or own fleets and the Destiny's hangar
    // fighters. They fly and fight at the Destiny's side; `group` tags
    // them (a conquest fleet id) to report the survivors, -1 = hangar.
    bool spawnAllies(const QString &faction, int motherships, int fighters, int group, bool announce = true);
    void removeAllyGroup(int group, bool jumpOut);  // a fleet leaves the orbit
    void setHangar(const QString &fighterClass, int capacity);
    void setDroneBay(int salvos);               // 0 = no drones (only the Ancients have some)
    void setLoadout(const Balance::FlagshipLoadout &loadout);   // weapons of the flagship
    bool fireDrones(int enemyIndex = -1);       // a salvo through the shields, -1 = current target
    int launchFighters(int count = -1);         // how many took off
    int hangarFighters() const { return m_hangar; }
    int hangarCapacity() const { return m_hangarCapacity; }
    struct AllySurvivors { int group; int motherships; int fighters; };
    QList<AllySurvivors> allySurvivors() const;
    void clearAllies();                         // fleets leave, hangar fighters land
    int allyCount() const;
    // Flash of the Destiny entering or leaving hyperspace
    void hyperspaceFlash();
    // Look of the player's ship: "" = the Destiny (campaign), else the
    // conquest faction: Tau'ri (BC-304), Goa'uld / Lucian Alliance (Ha'tak),
    // Wraith (hive ship). Same size, shield and hit points as the Destiny.
    void setFlagshipStyle(const QString &faction) { m_flagshipStyle = faction; update(); }
    // Lines drawn in the bottom-right corner of the view
    void setNavigationLines(const QStringList &lines);
    void setPlanetLook(const PlanetLook &look);
    // Current quest objectives, drawn at the top of the view
    void setObjectiveLines(const QStringList &lines);

    // --- Reports for the console
    QString destinyReport() const;
    QString weaponReport() const;
    QString sectorReport() const;

signals:
    void logMessage(const QString &text, BattleView::LogLevel level);
    void subsystemChanged(const QString &key, float health);
    void enemyDestroyed(bool mothership);
    void sectorCleared();               // last enemy destroyed (no auto waves)
    void hullBreached();                // hull reached 0 (emergency repairs follow)
    void jumpReady();                   // the FTL drive is spooled (startJumpSequence)
    void hullHit(const QVector3D &local, float damage);    // ship-local impact (interior damage)
    // Enemies come aboard: via = "rings" (a ship latched or close), "beam"
    void boardingParty(int strength, const QString &faction, const QString &via);
    void battleEnded(const BattleStats &stats);     // last enemy of a battle destroyed
    // delay: seconds before the sound starts (salvos)
    void sound(SoundCue cue, const QString &faction, float delay);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum ShaderMode { ModeHull = 0, ModePlanet = 1, ModeShield = 2, ModeGlow = 3, ModeSprite = 4 };
    // Textures of the shots, from the mod (0 = missing: plain shapes)
    enum Sprite { SpriteNone = -1, SpritePlasma, SpriteWraith, SpriteIon, SpriteAsgardLaser, SpriteAncientLaser,
                  SpriteBeamCore, SpriteBeamGlow, SpriteCount };
    static int spriteFor(const QString &faction);
    GLuint spriteTexture(int sprite);
    void drawSprite(int sprite, const QVector3D &center, const QVector3D &direction, float length, float width,
                    const QVector3D &tint, float tintAmount, float alpha, float uvRepeat = 1.0f);
    enum class EnemyClass { Mothership, Fighter };

    struct Mesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo;
        int vertexCount = 0;
    };

    // A mod model and its color texture (decoded in the background)
    struct TextureJob;
    // Textures of a mod model: color, mask (lights in blue), occlusion-
    // roughness-metallic, normal map; 0 = missing
    struct ShipTextures {
        GLuint color = 0, mask = 0, orm = 0, normal = 0;
    };
    struct ModModel {
        Mesh mesh;
        QString texturePaths[4];                // color, mask, orm, normal
        std::vector<QVector3D> engines;         // exhaust spots (normalized model space)
        std::vector<ModMesh::AttachPoint> points;
        QVector3D halfExtents;
        QVector3D center;                       // normalization of the file (for its shield model)
        float length = 0;
        QImage silhouette;
        QRectF silhouetteRect;
        ShipTextures textures;
        int quality = -1;                       // texture quality it was decoded for
        std::shared_ptr<TextureJob> job;        // decoding in progress
    };

    struct Enemy {
        int id = 0;
        EnemyClass cls = EnemyClass::Fighter;
        float hp = 0, maxHp = 0, radius = 1;
        float orbitRadius = 0, baseAngle = 0, angleAmp = 0, angleSpeed = 0;
        float height = 0, phase = 0;
        float fireCooldown = 0, hitFlash = 0;
        float shield = 0, maxShield = 0;    // energy shield (0 = none, see Balance)
        float shieldFlash = 0;      // bubble visible after a hit
        float lastHit = -1000;      // m_time of the last hit (shield recharge delay)
        std::vector<int> ammo;      // salvos left per attack, -1 = unlimited (missiles, drones)
        int classIndex = 0;         // in m_classes
        std::vector<float> attackTimers;    // one per attack of its class
        bool diving = false;        // kamikaze run on the Destiny
        QVector3D diveAim;          // ship-local impact point
        QVector3D pos, facing{1, 0, 0};
        bool alive = true;
        // Friendly ships only
        int group = -1;             // conquest fleet id, -1 = hangar fighter
        int targetId = -1;          // enemy it attacks
        QVector3D vel;
        float idle = 0;             // seconds without enemy (hangar fighters land)
        // Subsystems of the ship (percent): weapons, shields, engines, hangar
        float systems[4] = {100, 100, 100, 100};
        float retreat = 0;          // seconds left falling back to recharge (motherships)
        // Motherships: the inside of the ship, run by the AI (see Interior)
        std::shared_ptr<Interior> interior;
        float interiorClock = 0, lastInteriorLog = -100;
        // Fighters: latched on a ship to bring a team over (rings, airlock)
        int latchedOn = -1;         // enemy id (friendly fighter) or 0 = the player's ship
        float latchTime = 0;        // seconds latched
        QVector3D latchLocal;       // where on the player's hull
    };
    enum EnemySystem { SysWeapons = 0, SysShields = 1, SysEngines = 2, SysHangar = 3 };
    // Which weapon fired a shot (adaptive drones, statistics)
    enum class Weapon { None, Main, Pulse, Towers, Railgun, Drones, Missiles, Ally };

    struct Projectile {
        enum class Kind { Bolt, Missile, Slug, Beam };
        Kind kind = Kind::Bolt;
        int id = 0;
        int targetShot = -1;        // railgun slug: id of the missile it chases
        bool dead = false;
        bool fromDestiny = true;
        int targetId = -1;          // -1 = the Destiny
        int targetAlly = -1;        // enemy shot at a friendly ship
        bool fromAlly = false;
        bool drone = false;         // Ancient drone: goes through the shields
        bool miss = false;          // flies past its target (distance, sensors, evasion)
        bool rolled = false;        // hit chance already rolled
        int sprite = -1;            // Sprite of the mod for this shot (-1 = plain shape)
        int aimSystem = -1;         // enemy subsystem aimed at (EnemySystem)
        Weapon weapon = Weapon::None;
        QVector3D pos, vel;
        QVector3D aimLocal;         // aim point on the Destiny hull (local space)
        QVector3D color;
        float damage = 0, life = 5, delay = 0, length = 1, width = 0.1f;
    };

    struct Effect {
        QVector3D pos, color;
        float age = 0, duration = 0.5f, maxRadius = 1;
    };

    // Continuous beam (Asgard, Ori…): a ray from its ship to the target
    // for kBeamDuration, its damage delivered in ticks
    struct Ray {
        int sourceEnemy = -1, sourceAlly = -1;  // -1 / -1 = the player's ship
        QVector3D sourceLocal;                  // on the player's ship
        int targetEnemy = -1, targetAlly = -1;  // -1 / -1 = the player's ship
        QVector3D aimLocal;
        QVector3D missOffset;                   // non-zero: it misses
        QVector3D color;
        float width = 0.3f, age = 0, damageLeft = 0, tick = 0;
        bool fromDestiny = false;
        Weapon weapon = Weapon::None;
        QString faction;            // look of the beam
    };

    struct Subsystem {
        QString key;
        QString name;
        QVector3D localPos;
        QPointF labelOffset;        // screen-space callout direction (px)
        float health = 100;
        float lastHit = -1000;      // m_time of the last damage
    };

    // Simulation
    void step(float dt);
    void updateEnemy(Enemy &e, float dt);
    void updateProjectiles(float dt);
    void fireRailguns(float dt);
    void performAttack(Enemy &e, const ShipAttack &attack);
    void ram(Enemy &e);
    // fromHyperspace: appears far away in a flash, then flies to its orbit
    Enemy &spawnEnemy(int classIndex, bool fromHyperspace = false);
    void hyperspaceWindow(const QVector3D &pos, float size);
    void preferLineShips(QList<int> &motherships) const;
    int classIndexOf(const QString &name) const;
    const ShipClass &classOf(const Enemy &e) const { return m_classes[e.classIndex]; }
    QStringList hostileFactions() const;
    Projectile *findShot(int id);
    void hitEnemy(Enemy &e, const Projectile &p);
    float absorbByShield(Enemy &ship, const Projectile &p, float damage);
    void setupShip(Enemy &ship);
    void regenShield(Enemy &ship, float dt);
    void drawShipShield(const Enemy &ship, const QMatrix4x4 &model);
    void hitShield(const Projectile &p);
    void hitHull(const Projectile &p);
    // Damages the system closest to the impact point (ship-local)
    void damageSubsystemNear(const QVector3D &local, float amount);
    void shoot(bool fromDestiny, const QVector3D &from, int targetId, const QVector3D &aimLocal,
               float damage, float speed, const QVector3D &color, float length, float width,
               float delay = 0.0f);
    // Rolls the hit chance of the last shot (distance, sensors, evasion…)
    void rollAccuracy(Projectile &p, const QVector3D &target, float floor = 0.0f);
    float hitChance(const QVector3D &from, const QVector3D &to, bool fromPlayer, const Enemy *target) const;
    void fireRay(Ray ray, float damage);
    void updateRays(float dt);
    void drawRays();
    QVector3D rayStart(const Ray &r) const;
    QVector3D rayEnd(const Ray &r) const;
    float adaptedDamage(const Enemy &e, Weapon weapon, float damage);
    void damageEnemySystem(Enemy &e, int system, float hullDamage);
    void finishBattle();

    // Helpers
    std::vector<int> aliveIds() const;
    int indexOfId(int id) const;
    Enemy *findEnemy(int id);
    Enemy *findAlly(int id);
    Enemy *nearestEnemy(const QVector3D &from, bool fighterFirst);
    Enemy *nearestAlly(const QVector3D &from);
    Enemy &spawnAlly(int classIndex, const QVector3D &from, int group);
    void updateAlly(Enemy &a, float dt);
    void allyAttack(Enemy &a, const ShipAttack &attack, const Enemy &target);
    void hitAlly(Enemy &a, const Projectile &p);
    void drawAllies();
    // Ship models of the mod (local assets), loaded once on first use
    ModModel *modModel(const QString &key, const QString &frameKey = QString());
    bool useAmmo(Enemy &ship, int attack);
    static bool isDrone(const ShipAttack &attack);
    void drawEngineSpots(const QString &key, const QMatrix4x4 &model, float length, const QVector3D &color,
                         float strength);
    QVector3D m_shieldCenter, m_shieldRadii;    // Destiny / flagship shield (fits its model)
    void updateTexture(ModModel &model);
    static QString meshKeyOf(const ShipClass &shipClass);
    QString flagshipMeshKey() const;
    bool drawModShip(const QString &key, const QMatrix4x4 &model, float length, const QVector3D &color, float flash,
                     const QVector3D &lights);
    static QVector3D factionLights(const QString &faction);
    const Enemy *findEnemy(int id) const;
    const Enemy *pickTarget();
    Subsystem *subsystem(const QString &key);
    QVector3D destinyWorld(const QVector3D &local) const;
    bool insideShield(const QVector3D &world) const;
    bool shieldProtecting() const;
    QString className(const Enemy &e) const;

    // Rendering
    void upload(Mesh &mesh, const MeshBuilder::VertexData &data);
    void updateCamera();
    void drawMesh(Mesh &mesh, const QMatrix4x4 &model, const QVector3D &color, float emissive, float alpha,
                  ShaderMode mode, const ShipTextures *textures);
    void drawMesh(Mesh &mesh, const QMatrix4x4 &model, const QVector3D &color,
                  float emissive, float alpha, ShaderMode mode);
    void drawGlow(const QVector3D &pos, float radius, const QVector3D &color, float alpha);
    void drawDestiny();
    void drawEnemies();
    void drawEffects();
    void drawOverlay();
    void drawStatusBar(QPainter &p, const QRectF &rect, const QString &label, float value,
                       const QColor &color, const QString &suffix);
    bool project(const QVector3D &world, QPointF *screen) const;
    std::vector<Mesh *> meshes();

    // GL resources
    bool m_glReady = false;
    QOpenGLShaderProgram m_lit;
    QOpenGLShaderProgram m_stars;
    Mesh m_box, m_sphere, m_spire, m_spireRidge, m_fan, m_fanPlate, m_hub;
    Mesh m_ring, m_pyramid, m_fighter;
    // Quad with texture coordinates for the shot sprites
    QOpenGLVertexArrayObject m_quadVao;
    QOpenGLBuffer m_quadVbo;
    GLuint m_sprites[SpriteCount] = {};
    bool m_spritesTried[SpriteCount] = {};
    QOpenGLVertexArrayObject m_starVao;
    QOpenGLBuffer m_starVbo;
    int m_starCount = 0;

    // Camera
    QMatrix4x4 m_projection, m_view, m_viewProj;
    QVector3D m_eye, m_camRight{1, 0, 0};
    float m_yawOffset = 0, m_pitch = 20, m_distance = 24;
    QPoint m_lastMouse;

    // World state
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    float m_time = 0;
    QMatrix4x4 m_destinyModel, m_destinyInverse;
    std::vector<Enemy> m_enemies;
    std::vector<Enemy> m_allies;
    std::map<QString, std::unique_ptr<ModModel>> m_modModels;  // nullptr: no model for that key
    std::vector<std::pair<int, QVector3D>> m_allyLaunches;
    int m_nextAllyId = 100000;
    QString m_hangarClass;
    int m_hangar = 0, m_hangarCapacity = 0;
    int m_drones = 0, m_droneBay = 0;
    Balance::FlagshipLoadout m_loadout = Balance::flagshipLoadout(QString());
    float m_droneRefill = 0;
    float m_hangarRefill = 0;
    std::vector<Projectile> m_projectiles;
    std::vector<Effect> m_effects;
    std::vector<QVector3D> m_fires;           // burning hull spots (local space)
    std::vector<std::pair<QVector3D, std::pair<float, bool>>> m_hits;   // local pos, (time, shield)
    std::vector<Subsystem> m_subsystems;
    std::vector<QVector3D> m_turrets;         // local space
    int m_nextEnemyId = 1;
    int m_lockedId = -1;
    int m_kills = 0;
    float m_waveTimer = -1;
    bool m_autoWaves = true;
    float m_damageBonus = 1.0f;
    float m_shieldRegenBonus = 1.0f;
public:
    // Schematic of the flagship for the status panel (empty without a mod model)
    struct Schematic {
        QImage silhouette;
        QRectF rect;                // (x, z) in ship-local units
        QPointF shieldCenter;
        QSizeF shieldRadii;
    };
    Schematic schematic() const;
    // Combat assist (settings): health bars on the ships, callouts on the hull
    static bool assistSetting();
    static void setAssistSetting(bool on);
private:
    QVector3D m_shipLights;                 // light color of the mod model being drawn
    QString m_flagshipStyle;
    bool m_turretsOn = true;
    bool m_railgunsOn = true;
    PowerPriority m_priority = PowerPriority::Balanced;
    QStringList m_navigationLines;
    PlanetLook m_planetLook;
    QStringList m_objectiveLines;

    float m_hull = 100;
    float m_shieldStrength = 100;
    bool m_shieldOrdered = true;
    bool m_shieldCollapsed = false;
    float m_shieldFlash = 0;
    float m_lastShieldHit = -10;

    float m_mainCooldown = 0, m_pulseCooldown = 0, m_secondaryCooldown = 0, m_autoFireTimer = 1;

    QList<ShipClass> m_classes = ShipClass::fallbackRoster();
    QString m_waveFaction;
    std::vector<std::pair<int, QVector3D>> m_launches;     // class index, hangar position

    WeaponProfile m_railgun{"Railgun", "kinetic", 80, 60, 22};
    WeaponProfile m_cannon{"Energy Cannon", "energy", 120, 40, 45};
    float m_railgunTimer = 0;
    int m_nextShotId = 1;
    int m_interceptions = 0;
    float m_lastMissileLog = -100;
    // Tactics
    std::vector<Ray> m_rays;
    int m_aimSystem = -1;                   // subsystem aimed on the locked target
    bool m_evading = false;
    float m_evadeBlend = 0;                 // 0..1, smooth start of the maneuvers
    float m_railgunHeat = 0;
    bool m_railgunOverheated = false;
    int m_railgunAmmo = Balance::kRailgunAmmo;
    int m_missiles = 0, m_missileBay = 0;
    int m_dronesInFlight = 0;
    float m_jumpCountdown = -1;
    int m_relaunch = 0;                     // fighters to launch again after a jump
    int m_launchedByOrder = 0;
    std::map<int, int> m_weaponHits;        // Weapon -> hits on the drones this battle
    int m_adaptedWeapon = -1;               // weapon the drones resist (-1 = none)
    BattleStats m_stats;
    bool m_inBattle = false;
    int m_powerLoss = 0, m_powerBonus = 0;
    float m_accuracyBonus = 1.0f;
    bool m_externalFires = false;
    bool m_redAlert = false;
    bool m_hasShield = true;
    float m_armor = 1.0f;
    float m_incomingDamage = 0;
    void updateEnemyInterior(Enemy &e, float dt);
    void enemyBoarding(Enemy &e, float dt);
    static QString layoutOf(const QString &faction);
    QList<ReplayFrame> m_replay, m_lastReplay;
    QString m_lastReplayFaction;
    float m_replayClock = 0;
    QStringList m_replayEvents;
    void recordFrame();
    bool m_observing = false;
    float m_shake = 0;                      // camera shake after heavy hits
    float m_shedLogged = -100;
};

#endif // BATTLEVIEW_H
