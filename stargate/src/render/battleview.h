#ifndef BATTLEVIEW_H
#define BATTLEVIEW_H

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QPointF>
#include <QStringList>
#include <QVector3D>
#include <utility>
#include <vector>

#include "meshbuilder.h"
#include "planetlook.h"
#include "src/audio/soundcue.h"
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
    bool lockTarget(int enemyIndex);
    bool repairSubsystem(const QString &name);

    // --- Ship systems: engines, shields, weapons, lifesupport (+ hull)
    // Normalizes a typed name ("engine", "Life Support"…) to a system
    // key; returns an empty string if unknown.
    static QString systemKey(const QString &name);
    static QStringList systemKeys();
    void setSubsystemHealth(const QString &key, float health);
    float systemHealth(const QString &key) const;   // also accepts "hull"

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
    void clearBattlefield(float calmSeconds);
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
    bool spawnAllies(const QString &faction, int motherships, int fighters, int group);
    void setHangar(const QString &fighterClass, int capacity);
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
    enum ShaderMode { ModeHull = 0, ModePlanet = 1, ModeShield = 2, ModeGlow = 3 };
    enum class EnemyClass { Mothership, Fighter };

    struct Mesh {
        QOpenGLVertexArrayObject vao;
        QOpenGLBuffer vbo;
        int vertexCount = 0;
    };

    struct Enemy {
        int id = 0;
        EnemyClass cls = EnemyClass::Fighter;
        float hp = 0, maxHp = 0, radius = 1;
        float orbitRadius = 0, baseAngle = 0, angleAmp = 0, angleSpeed = 0;
        float height = 0, phase = 0;
        float fireCooldown = 0, hitFlash = 0;
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
    };

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
        QVector3D pos, vel;
        QVector3D aimLocal;         // aim point on the Destiny hull (local space)
        QVector3D color;
        float damage = 0, life = 5, delay = 0, length = 1, width = 0.1f;
    };

    struct Effect {
        QVector3D pos, color;
        float age = 0, duration = 0.5f, maxRadius = 1;
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
    void hitShield(const Projectile &p);
    void hitHull(const Projectile &p);
    // Damages the system closest to the impact point (ship-local)
    void damageSubsystemNear(const QVector3D &local, float amount);
    void shoot(bool fromDestiny, const QVector3D &from, int targetId, const QVector3D &aimLocal,
               float damage, float speed, const QVector3D &color, float length, float width,
               float delay = 0.0f);

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
    std::vector<std::pair<int, QVector3D>> m_allyLaunches;
    int m_nextAllyId = 100000;
    QString m_hangarClass;
    int m_hangar = 0, m_hangarCapacity = 0;
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
};

#endif // BATTLEVIEW_H
