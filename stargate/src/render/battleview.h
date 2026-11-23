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
#include <QVector3D>
#include <vector>

#include "meshbuilder.h"

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

    // --- Reports for the console
    QString destinyReport() const;
    QString weaponReport() const;
    QString sectorReport() const;

signals:
    void logMessage(const QString &text, BattleView::LogLevel level);

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
        QVector3D pos, facing{1, 0, 0};
        bool alive = true;
    };

    struct Projectile {
        bool fromDestiny = true;
        int targetId = -1;          // -1 = the Destiny
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
    };

    // Simulation
    void step(float dt);
    void spawnWave(bool announce);
    void updateEnemy(Enemy &e, float dt);
    void updateProjectiles(float dt);
    void hitEnemy(Enemy &e, const Projectile &p);
    void hitShield(const Projectile &p);
    void hitHull(const Projectile &p);
    void damageRandomSubsystem(float amount);
    void shoot(bool fromDestiny, const QVector3D &from, int targetId, const QVector3D &aimLocal,
               float damage, float speed, const QVector3D &color, float length, float width,
               float delay = 0.0f);

    // Helpers
    std::vector<int> aliveIds() const;
    int indexOfId(int id) const;
    Enemy *findEnemy(int id);
    const Enemy *findEnemy(int id) const;
    const Enemy *pickTarget();
    Subsystem *subsystem(const QString &key);
    float subsystemHealth(const QString &key) const;
    QVector3D destinyWorld(const QVector3D &local) const;
    bool insideShield(const QVector3D &world) const;
    bool shieldProtecting() const;
    QString className(EnemyClass cls) const;

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
    std::vector<Projectile> m_projectiles;
    std::vector<Effect> m_effects;
    std::vector<QVector3D> m_fires;           // burning hull spots (local space)
    std::vector<Subsystem> m_subsystems;
    std::vector<QVector3D> m_turrets;         // local space
    int m_nextEnemyId = 1;
    int m_lockedId = -1;
    int m_kills = 0;
    float m_waveTimer = -1;

    float m_hull = 100;
    float m_shieldStrength = 100;
    bool m_shieldOrdered = true;
    bool m_shieldCollapsed = false;
    float m_shieldFlash = 0;
    float m_lastShieldHit = -10;

    float m_mainCooldown = 0, m_pulseCooldown = 0, m_secondaryCooldown = 0, m_autoFireTimer = 1;
};

#endif // BATTLEVIEW_H
