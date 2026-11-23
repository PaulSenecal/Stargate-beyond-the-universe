#include "battleview.h"

#include <QDebug>
#include <QFontDatabase>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QQuaternion>
#include <QRandomGenerator>
#include <QTimer>
#include <QVector4D>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <utility>

// ================================================================
//  Shaders
// ================================================================

namespace {

const char *kLitVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vObjPos;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(uNormalMat * aNormal);
    vObjPos = aPos;
    gl_Position = uViewProj * world;
}
)";

const char *kLitFragmentShader = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vObjPos;
uniform vec3 uColor;
uniform float uEmissive;
uniform float uAlpha;
uniform int uMode;          // 0 hull, 1 planet, 2 shield, 3 glow
uniform vec3 uLightDir;     // direction the light travels
uniform vec3 uCamPos;
uniform float uTime;
// Planet look (mode 1)
uniform vec3 uOcean;
uniform vec3 uGround;
uniform vec3 uGroundAlt;
uniform vec3 uAtmosphere;
uniform float uWaterLevel;
uniform float uCloudCover;
uniform float uLights;
uniform vec3 uPlanetSeed;
out vec4 fragColor;

float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float noise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash(i + vec3(0,0,0)), hash(i + vec3(1,0,0)), f.x),
                   mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
               mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
                   mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}
float fbm(vec3 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { v += a * noise(p); p *= 2.03; a *= 0.5; }
    return v;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCamPos - vWorldPos);

    if (uMode == 3) {                       // additive glow
        fragColor = vec4(uColor * uEmissive, uAlpha);
        return;
    }
    if (uMode == 2) {                       // energy shield
        float fresnel = pow(1.0 - abs(dot(N, V)), 2.0);
        float bands = 0.5 + 0.5 * sin(vObjPos.y * 24.0 + uTime * 3.0);
        float hex = smoothstep(0.35, 0.5, noise(vObjPos * 14.0));
        fragColor = vec4(uColor, uAlpha * (0.25 + fresnel) * (0.75 + 0.25 * bands) * (0.8 + 0.4 * hex));
        return;
    }
    if (uMode == 1) {                       // procedural planet
        vec3 q = normalize(vObjPos) + uPlanetSeed;
        float land = fbm(q * 3.0);
        float clouds = fbm(q * 6.0 + vec3(uTime * 0.01, 0.0, 0.0));
        vec3 ground = mix(uGround, uGroundAlt, fbm(q * 9.0));
        bool isLand = land > uWaterLevel;
        vec3 base = isLand ? ground : uOcean;
        base = mix(base, vec3(0.75), smoothstep(0.62 - 0.12 * uCloudCover, 0.80, clouds) * uCloudCover);
        float diff = max(dot(N, L), 0.0);
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        vec3 col = base * (0.03 + diff) + uAtmosphere * rim * (0.15 + diff);
        // Ancient site: city lights on the night side
        float night = 1.0 - smoothstep(-0.05, 0.15, dot(N, L));
        float city = isLand ? step(0.8, noise(q * 70.0)) : 0.0;
        col += vec3(0.45, 0.75, 1.0) * city * night * uLights;
        fragColor = vec4(col, 1.0);
        return;
    }

    // Hull: diffuse + spec + panel noise + cool back light
    float panel = 0.7 + 0.3 * noise(floor(vObjPos * 5.0));
    float diff = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.35;
    vec3 back = vec3(0.12, 0.18, 0.3) * max(dot(N, -L), 0.0) * 0.5;
    vec3 col = uColor * panel * (0.10 + diff * 0.95) + back + vec3(spec) + uColor * uEmissive;
    fragColor = vec4(col, uAlpha);
}
)";

const char *kStarVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aSize;
uniform mat4 uViewProj;
uniform float uPointScale;
out float vBrightness;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = aSize * uPointScale;
    vBrightness = clamp(aSize / 2.5, 0.35, 1.0);
}
)";

const char *kStarFragmentShader = R"(
#version 330 core
in float vBrightness;
out vec4 fragColor;
void main() {
    float d = length(gl_PointCoord - vec2(0.5));
    if (d > 0.5) discard;
    fragColor = vec4(vec3(0.85, 0.9, 1.0) * vBrightness * (1.0 - d * 1.6), 1.0);
}
)";

// ================================================================
//  Tuning
// ================================================================

const QVector3D kHullColor(0.36f, 0.34f, 0.31f);
const QVector3D kHullDark(0.22f, 0.21f, 0.20f);
const QVector3D kEnemyGold(0.62f, 0.50f, 0.28f);
const QVector3D kEnemyDark(0.30f, 0.25f, 0.16f);
const QVector3D kFighterColor(0.45f, 0.42f, 0.38f);
const QVector3D kDestinyBolt(0.55f, 0.85f, 1.0f);
const QVector3D kEnemyBolt(1.0f, 0.72f, 0.18f);
const QVector3D kExplosion(1.0f, 0.5f, 0.12f);
const QVector3D kShieldColor(0.35f, 0.65f, 1.0f);
const QVector3D kEngineGlow(0.35f, 0.65f, 1.0f);
const QVector3D kLightDir = QVector3D(0.8f, -0.3f, -0.5f).normalized();

const QVector3D kMainTowerLocal(6.6f, 0.75f, 0.0f);
const QVector3D kShieldCenter(1.95f, 0.15f, 0.0f);
const QVector3D kShieldRadii(6.9f, 2.4f, 5.8f);
const QVector2D kFanCenter(1.5f, 0.0f);
const float kFanRadius = 5.6f;

constexpr float kMainCooldown = 4.0f;
constexpr float kPulseCooldown = 8.0f;
constexpr float kSecondaryCooldown = 2.0f;
constexpr size_t kMaxFires = 14;

float randRange(float a, float b)
{
    return a + float(QRandomGenerator::global()->generateDouble()) * (b - a);
}

QVector3D randomHullPoint()
{
    if (randRange(0, 1) < 0.35f) {                  // along the spire
        const float x = randRange(0.0f, 6.5f);
        return {x, 0.35f, randRange(-0.6f, 0.6f) * (1.0f - x / 8.0f)};
    }
    const float a = qDegreesToRadians(randRange(135.0f, 225.0f));
    const float r = randRange(1.0f, kFanRadius - 0.3f);
    return {kFanCenter.x() + r * std::cos(a), 0.3f, r * std::sin(a)};
}

QColor healthColor(float pct)
{
    if (pct > 60) return QColor(70, 230, 100);
    if (pct > 30) return QColor(240, 190, 60);
    return QColor(255, 70, 60);
}

} // namespace

// ================================================================
//  Construction
// ================================================================

BattleView::BattleView(QWidget *parent)
    : QOpenGLWidget(parent)
    , m_timer(new QTimer(this))
{
    setMinimumSize(320, 240);

    m_subsystems = {
        {"engines",     tr("Engines"),      {-3.9f, 0.3f,  0.0f}, { 150,   70}},
        {"shields",     tr("Shields"),      { 0.4f, 1.6f,  0.0f}, {-190, -120}},
        {"weapons",     tr("Weapons"),      { 5.2f, 0.55f, 0.0f}, { 170, -110}},
        {"lifesupport", tr("Life support"), {-1.2f, 0.4f,  3.0f}, {-180,   90}},
    };

    for (float a : {150.0f, 165.0f, 195.0f, 210.0f}) {
        const float r = qDegreesToRadians(a);
        m_turrets.emplace_back(kFanCenter.x() + 4.0f * std::cos(r), 0.4f, 4.0f * std::sin(r));
    }
    m_turrets.emplace_back(3.0f, 0.55f, 0.35f);
    m_turrets.emplace_back(3.0f, 0.55f, -0.35f);

    step(0.0f);
    spawnWave(false);

    connect(m_timer, &QTimer::timeout, this, [this] {
        const float dt = std::min(0.05f, float(m_clock.restart()) / 1000.0f);
        step(dt);
        update();
    });
    m_clock.start();
    m_timer->start(16);
}

BattleView::~BattleView()
{
    makeCurrent();
    for (Mesh *m : meshes()) {
        m->vao.destroy();
        m->vbo.destroy();
    }
    m_starVao.destroy();
    m_starVbo.destroy();
    doneCurrent();
}

std::vector<BattleView::Mesh *> BattleView::meshes()
{
    return {&m_box, &m_sphere, &m_spire, &m_spireRidge, &m_fan, &m_fanPlate, &m_hub,
            &m_ring, &m_pyramid, &m_fighter};
}

// ================================================================
//  Orders
// ================================================================

bool BattleView::lockTarget(int enemyIndex)
{
    const std::vector<int> ids = aliveIds();
    if (enemyIndex < 0 || enemyIndex >= int(ids.size())) {
        emit logMessage(tr("No contact T%1 in range").arg(enemyIndex + 1), LogLevel::Warning);
        return false;
    }
    m_lockedId = ids[enemyIndex];
    const Enemy *e = findEnemy(m_lockedId);
    emit logMessage(tr("Target locked: T%1 %2 — hull %3%")
                        .arg(enemyIndex + 1)
                        .arg(className(e->cls))
                        .arg(qRound(100.0f * e->hp / e->maxHp)),
                    LogLevel::Success);
    return true;
}

bool BattleView::fireMainWeapon(int enemyIndex)
{
    if (enemyIndex >= 0 && !lockTarget(enemyIndex))
        return false;
    if (m_mainCooldown > 0.0f) {
        emit logMessage(tr("Main tower recharging (%1 s)").arg(m_mainCooldown, 0, 'f', 1),
                        LogLevel::Warning);
        return false;
    }
    const Enemy *target = findEnemy(m_lockedId);
    if (!target || !target->alive) {
        target = pickTarget();
        if (!target) {
            emit logMessage(tr("Main tower: no target"), LogLevel::Warning);
            return false;
        }
        m_lockedId = target->id;
    }

    shoot(true, destinyWorld(kMainTowerLocal), target->id, {}, 160.0f, 48.0f,
          QVector3D(0.7f, 0.92f, 1.0f), 3.2f, 0.32f);
    m_mainCooldown = kMainCooldown;
    emit logMessage(tr("Main tower fired at T%1").arg(indexOfId(target->id) + 1), LogLevel::Success);
    return true;
}

bool BattleView::firePulseVolley()
{
    if (m_pulseCooldown > 0.0f) {
        emit logMessage(tr("Pulse weapons recharging (%1 s)").arg(m_pulseCooldown, 0, 'f', 1),
                        LogLevel::Warning);
        return false;
    }
    const std::vector<int> ids = aliveIds();
    if (ids.empty()) {
        emit logMessage(tr("Pulse weapons: no target"), LogLevel::Warning);
        return false;
    }
    for (int id : ids) {
        for (int i = 0; i < 3; ++i) {
            const QVector3D &turret = m_turrets[QRandomGenerator::global()->bounded(int(m_turrets.size()))];
            shoot(true, destinyWorld(turret), id, {}, 22.0f, 40.0f, kDestinyBolt, 1.3f, 0.12f,
                  randRange(0.0f, 0.6f));
        }
    }
    m_pulseCooldown = kPulseCooldown;
    emit logMessage(tr("Pulse weapons: full volley on %1 targets").arg(ids.size()), LogLevel::Success);
    return true;
}

bool BattleView::fireSecondaryBurst()
{
    if (m_secondaryCooldown > 0.0f) {
        emit logMessage(tr("Secondary towers recharging (%1 s)").arg(m_secondaryCooldown, 0, 'f', 1),
                        LogLevel::Warning);
        return false;
    }
    const Enemy *target = pickTarget();
    if (!target) {
        emit logMessage(tr("Secondary towers: no target"), LogLevel::Warning);
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        const QVector3D &turret = m_turrets[i % m_turrets.size()];
        shoot(true, destinyWorld(turret), target->id, {}, 10.0f, 42.0f, kDestinyBolt, 1.1f, 0.1f,
              randRange(0.0f, 0.5f));
    }
    m_secondaryCooldown = kSecondaryCooldown;
    emit logMessage(tr("Secondary towers: burst on T%1").arg(indexOfId(target->id) + 1), LogLevel::Success);
    return true;
}

void BattleView::setShieldActive(bool active)
{
    m_shieldOrdered = active;
    if (active)
        emit logMessage(tr("Shields raised (%1%)").arg(qRound(m_shieldStrength)), LogLevel::Success);
    else
        emit logMessage(tr("Shields lowered"), LogLevel::Warning);
}

QString BattleView::systemKey(const QString &name)
{
    const QString key = name.toLower().remove(' ').remove('_');
    if (key == "engine" || key == "engines") return QStringLiteral("engines");
    if (key == "shield" || key == "shields") return QStringLiteral("shields");
    if (key == "weapon" || key == "weapons") return QStringLiteral("weapons");
    if (key == "life" || key == "lifesupport") return QStringLiteral("lifesupport");
    if (key == "hull") return QStringLiteral("hull");
    return QString();
}

QStringList BattleView::systemKeys()
{
    return {"engines", "shields", "weapons", "lifesupport"};
}

void BattleView::setSubsystemHealth(const QString &key, float health)
{
    if (Subsystem *s = subsystem(key))
        s->health = std::clamp(health, 0.0f, 100.0f);
}

void BattleView::restoreDefenses(float hull, float shield, bool shieldOn)
{
    m_hull = hull > 0.0f ? std::min(hull, 100.0f) : 35.0f;
    m_shieldStrength = std::clamp(shield, 0.0f, 100.0f);
    m_shieldOrdered = shieldOn;
    m_shieldCollapsed = m_shieldStrength <= 0.0f;
}

void BattleView::clearBattlefield(float calmSeconds)
{
    for (Enemy &e : m_enemies)
        e.alive = false;
    m_enemies.clear();
    m_projectiles.clear();
    m_effects.clear();
    m_lockedId = -1;
    m_waveTimer = calmSeconds;
}

void BattleView::setPlanetLook(const PlanetLook &look)
{
    m_planetLook = look;
}

void BattleView::setNavigationLines(const QStringList &lines)
{
    m_navigationLines = lines;
}

float BattleView::systemHealth(const QString &key) const
{
    return key == "hull" ? m_hull : subsystemHealth(key);
}

bool BattleView::repairSubsystem(const QString &name)
{
    const QString key = systemKey(name);

    auto removeFires = [this](int count) {
        for (int i = 0; i < count && !m_fires.empty(); ++i)
            m_fires.erase(m_fires.begin() + QRandomGenerator::global()->bounded(int(m_fires.size())));
    };

    if (key == "hull") {
        m_hull = std::min(100.0f, m_hull + 20.0f);
        removeFires(4);
        emit logMessage(tr("Hull repaired: %1%").arg(qRound(m_hull)), LogLevel::Success);
        return true;
    }
    if (Subsystem *s = subsystem(key)) {
        s->health = std::min(100.0f, s->health + 35.0f);
        removeFires(2);
        emit subsystemChanged(s->key, s->health);
        emit logMessage(tr("%1 repaired: %2%").arg(s->name).arg(qRound(s->health)), LogLevel::Success);
        return true;
    }
    emit logMessage(tr("Unknown system '%1' (engines, shields, weapons, lifesupport, hull)").arg(name),
                    LogLevel::Warning);
    return false;
}

QString BattleView::destinyReport() const
{
    QString shield = m_shieldCollapsed ? tr("collapsed")
                   : m_shieldOrdered   ? tr("online")
                                       : tr("offline");
    QString report = tr("Hull %1% | Shields %2% (%3) | Fires on board: %4")
                         .arg(qRound(m_hull)).arg(qRound(m_shieldStrength)).arg(shield).arg(m_fires.size());
    for (const Subsystem &s : m_subsystems)
        report += QStringLiteral("\n%1: %2%").arg(s.name).arg(qRound(s.health));
    return report;
}

QString BattleView::weaponReport() const
{
    auto state = [](float cooldown) {
        return cooldown > 0.0f ? tr("recharging %1 s").arg(cooldown, 0, 'f', 1) : tr("ready");
    };
    QString target = tr("none");
    if (const Enemy *e = findEnemy(m_lockedId); e && e->alive)
        target = tr("T%1 %2 (%3%)").arg(indexOfId(e->id) + 1).arg(className(e->cls))
                     .arg(qRound(100.0f * e->hp / e->maxHp));

    return tr("Main tower: %1\nPulse weapons: %2\nSecondary towers: %3 (auto-fire, %4% efficiency)\n"
              "Current target: %5 | Kills: %6")
        .arg(state(m_mainCooldown), state(m_pulseCooldown), state(m_secondaryCooldown))
        .arg(qRound(subsystemHealth("weapons")))
        .arg(target)
        .arg(m_kills);
}

QString BattleView::sectorReport() const
{
    const std::vector<int> ids = aliveIds();
    if (ids.empty())
        return tr("Sector clear — no hostile contact");

    QString report = tr("%1 hostile contact(s):").arg(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        const Enemy *e = findEnemy(ids[i]);
        report += tr("\nT%1  %2  hull %3%  distance %4 km")
                      .arg(i + 1)
                      .arg(className(e->cls))
                      .arg(qRound(100.0f * e->hp / e->maxHp))
                      .arg(qRound(e->pos.length() * 10.0f));
    }
    return report;
}

// ================================================================
//  Simulation
// ================================================================

void BattleView::step(float dt)
{
    m_time += dt;

    m_destinyModel.setToIdentity();
    m_destinyModel.translate(0.0f, 0.25f * std::sin(m_time * 0.5f), 0.0f);
    m_destinyModel.rotate(30.0f, 0, 1, 0);
    m_destinyModel.rotate(3.0f * std::sin(m_time * 0.3f), 1, 0, 0);
    m_destinyInverse = m_destinyModel.inverted();

    if (dt <= 0.0f)
        return;

    for (Enemy &e : m_enemies)
        if (e.alive)
            updateEnemy(e, dt);

    // Secondary towers auto-fire, slower when the weapons are damaged
    m_autoFireTimer -= dt;
    if (m_autoFireTimer <= 0.0f) {
        const float efficiency = std::max(0.25f, subsystemHealth("weapons") / 100.0f);
        m_autoFireTimer = 0.45f / efficiency;
        if (const Enemy *t = pickTarget()) {
            const QVector3D &turret = m_turrets[QRandomGenerator::global()->bounded(int(m_turrets.size()))];
            shoot(true, destinyWorld(turret), t->id, {}, 7.0f * efficiency, 38.0f, kDestinyBolt, 1.0f, 0.09f);
        }
    }

    m_mainCooldown = std::max(0.0f, m_mainCooldown - dt);
    m_pulseCooldown = std::max(0.0f, m_pulseCooldown - dt);
    m_secondaryCooldown = std::max(0.0f, m_secondaryCooldown - dt);
    m_shieldFlash = std::max(0.0f, m_shieldFlash - dt * 2.0f);

    // Shield regeneration (slower while under fire)
    if (m_shieldOrdered) {
        const float base = m_time - m_lastShieldHit > 1.5f ? 6.0f : 2.0f;
        const float rate = base * std::max(0.2f, subsystemHealth("shields") / 100.0f);
        m_shieldStrength = std::min(100.0f, m_shieldStrength + rate * dt);
        if (m_shieldCollapsed && m_shieldStrength >= 35.0f) {
            m_shieldCollapsed = false;
            emit logMessage(tr("Shields restored (%1%)").arg(qRound(m_shieldStrength)), LogLevel::Success);
        }
    }

    updateProjectiles(dt);

    for (Effect &fx : m_effects)
        fx.age += dt;
    m_effects.erase(std::remove_if(m_effects.begin(), m_effects.end(),
                                   [](const Effect &fx) { return fx.age >= fx.duration; }),
                    m_effects.end());

    // New wave a few seconds after the sector is cleared
    if (aliveIds().empty()) {
        if (m_waveTimer < 0.0f) {
            m_waveTimer = 7.0f;
            emit logMessage(tr("Sector clear"), LogLevel::Success);
        } else {
            m_waveTimer -= dt;
            if (m_waveTimer <= 0.0f) {
                m_waveTimer = -1.0f;
                spawnWave(true);
            }
        }
    }
}

void BattleView::spawnWave(bool announce)
{
    m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(),
                                   [](const Enemy &e) { return !e.alive; }),
                    m_enemies.end());

    m_waveTimer = -1.0f;
    const int motherships = 2 + QRandomGenerator::global()->bounded(2);
    const int fighters = 4 + QRandomGenerator::global()->bounded(3);

    auto spawn = [this](EnemyClass cls) {
        Enemy e;
        e.id = m_nextEnemyId++;
        e.cls = cls;
        e.phase = randRange(0.0f, 2.0f * float(M_PI));
        e.fireCooldown = randRange(1.0f, 3.5f);
        if (cls == EnemyClass::Mothership) {
            e.hp = e.maxHp = 420.0f;
            e.radius = 3.6f;
            e.orbitRadius = randRange(27.0f, 34.0f);
            e.baseAngle = qDegreesToRadians(randRange(205.0f, 335.0f));
            e.angleAmp = qDegreesToRadians(randRange(6.0f, 14.0f));
            e.angleSpeed = randRange(0.08f, 0.15f);
            e.height = randRange(5.0f, 10.0f);
        } else {
            e.hp = e.maxHp = 60.0f;
            e.radius = 1.0f;
            e.orbitRadius = randRange(13.0f, 20.0f);
            e.baseAngle = qDegreesToRadians(randRange(190.0f, 350.0f));
            e.angleAmp = qDegreesToRadians(randRange(25.0f, 45.0f));
            e.angleSpeed = randRange(0.35f, 0.6f);
            e.height = randRange(-3.0f, 7.0f);
        }
        updateEnemy(e, 0.0f);
        m_enemies.push_back(e);
    };

    for (int i = 0; i < motherships; ++i)
        spawn(EnemyClass::Mothership);
    for (int i = 0; i < fighters; ++i)
        spawn(EnemyClass::Fighter);

    if (announce)
        emit logMessage(tr("New hostile contacts: %1 motherships, %2 fighters").arg(motherships).arg(fighters),
                        LogLevel::Warning);
}

void BattleView::updateEnemy(Enemy &e, float dt)
{
    const QVector3D previous = e.pos;
    const float a = e.baseAngle + e.angleAmp * std::sin(m_time * e.angleSpeed + e.phase);
    e.pos = QVector3D(e.orbitRadius * std::cos(a),
                      e.height + 0.8f * std::sin(m_time * 0.7f + e.phase),
                      e.orbitRadius * std::sin(a));

    const QVector3D motion = e.pos - previous;
    if (e.cls == EnemyClass::Fighter && dt > 0.0f && motion.lengthSquared() > 1e-6f)
        e.facing = motion.normalized();
    else
        e.facing = QVector3D(-e.pos.x(), 0.0f, -e.pos.z()).normalized();

    e.hitFlash = std::max(0.0f, e.hitFlash - dt * 4.0f);

    if (dt <= 0.0f)
        return;
    e.fireCooldown -= dt;
    if (e.fireCooldown <= 0.0f) {
        const bool big = e.cls == EnemyClass::Mothership;
        for (int i = 0; i < (big ? 2 : 1); ++i) {
            const QVector3D from = e.pos + QVector3D(randRange(-1, 1), randRange(-0.3f, 0.3f), randRange(-1, 1)) * (e.radius * 0.5f);
            shoot(false, from, -1, randomHullPoint(), big ? 2.5f : 1.2f, big ? 22.0f : 28.0f,
                  kEnemyBolt, big ? 1.6f : 1.1f, big ? 0.14f : 0.1f, i * 0.15f);
        }
        e.fireCooldown = big ? randRange(2.5f, 4.0f) : randRange(1.8f, 3.0f);
    }
}

void BattleView::shoot(bool fromDestiny, const QVector3D &from, int targetId, const QVector3D &aimLocal,
                       float damage, float speed, const QVector3D &color, float length, float width,
                       float delay)
{
    Projectile p;
    p.fromDestiny = fromDestiny;
    p.targetId = targetId;
    p.pos = from;
    p.aimLocal = aimLocal;
    p.damage = damage;
    p.color = color;
    p.length = length;
    p.width = width;
    p.delay = delay;

    QVector3D target = targetId < 0 ? destinyWorld(aimLocal) : from + QVector3D(1, 0, 0);
    if (const Enemy *e = findEnemy(targetId))
        target = e->pos;
    p.vel = (target - from).normalized() * speed;
    m_projectiles.push_back(p);
}

void BattleView::updateProjectiles(float dt)
{
    // Impacts may spawn effects / logs but never new projectiles,
    // so iterating while erasing is safe here.
    for (auto it = m_projectiles.begin(); it != m_projectiles.end();) {
        Projectile &p = *it;
        if (p.delay > 0.0f) {
            p.delay -= dt;
            ++it;
            continue;
        }

        p.life -= dt;
        bool remove = p.life <= 0.0f;

        Enemy *enemy = nullptr;
        bool hasTarget = false;
        QVector3D target;
        float hitRadius = 0.7f;
        if (p.targetId < 0) {
            target = destinyWorld(p.aimLocal);
            hasTarget = true;
        } else if ((enemy = findEnemy(p.targetId)) && enemy->alive) {
            target = enemy->pos;
            hitRadius = enemy->radius * 0.8f;
            hasTarget = true;
        }

        const float speed = p.vel.length();
        float distance = 1e9f;
        if (hasTarget) {
            const QVector3D toTarget = target - p.pos;
            distance = toTarget.length();
            if (distance > 1e-3f)
                p.vel = toTarget / distance * speed;        // homing
        }
        p.pos += p.vel * dt;

        if (!remove && hasTarget) {
            if (p.targetId < 0 && shieldProtecting() && insideShield(p.pos)) {
                hitShield(p);
                remove = true;
            } else if (distance <= speed * dt + hitRadius) {
                if (p.targetId < 0)
                    hitHull(p);
                else
                    hitEnemy(*enemy, p);
                remove = true;
            }
        }

        it = remove ? m_projectiles.erase(it) : it + 1;
    }
}

void BattleView::hitEnemy(Enemy &e, const Projectile &p)
{
    e.hp -= p.damage;
    e.hitFlash = 1.0f;
    m_effects.push_back({p.pos, kExplosion, 0.0f, 0.35f, 0.5f + p.width * 2.0f});

    if (e.hp > 0.0f)
        return;

    const int index = indexOfId(e.id);
    e.alive = false;
    ++m_kills;
    const bool big = e.cls == EnemyClass::Mothership;
    m_effects.push_back({e.pos, kExplosion, 0.0f, big ? 1.6f : 0.9f, big ? 6.0f : 2.2f});
    m_effects.push_back({e.pos, QVector3D(1.0f, 0.9f, 0.7f), 0.0f, big ? 0.6f : 0.35f, big ? 3.0f : 1.2f});
    if (m_lockedId == e.id)
        m_lockedId = -1;
    emit logMessage(tr("T%1 %2 destroyed").arg(index + 1).arg(className(e.cls)), LogLevel::Success);
    emit enemyDestroyed(big);
}

void BattleView::hitShield(const Projectile &p)
{
    const float absorb = 1.0f / std::max(0.4f, subsystemHealth("shields") / 100.0f);
    m_shieldStrength = std::max(0.0f, m_shieldStrength - p.damage * absorb);
    m_shieldFlash = 1.0f;
    m_lastShieldHit = m_time;
    m_effects.push_back({p.pos, kShieldColor, 0.0f, 0.3f, 0.9f});

    if (m_shieldStrength <= 0.0f && !m_shieldCollapsed) {
        m_shieldCollapsed = true;
        emit logMessage(tr("Shields collapsed!"), LogLevel::Critical);
    }
}

void BattleView::hitHull(const Projectile &p)
{
    const float before = m_hull;
    m_hull = std::max(0.0f, m_hull - p.damage);
    m_effects.push_back({p.pos, kExplosion, 0.0f, 0.45f, 0.9f});
    damageRandomSubsystem(p.damage * 1.6f);
    if (m_fires.size() < kMaxFires && randRange(0, 1) < 0.35f)
        m_fires.push_back(p.aimLocal);

    for (float threshold : {75.0f, 50.0f, 25.0f}) {
        if (before > threshold && m_hull <= threshold)
            emit logMessage(tr("Hull integrity %1%").arg(qRound(m_hull)),
                            threshold > 30.0f ? LogLevel::Warning : LogLevel::Critical);
    }
    if (m_hull <= 0.0f) {
        m_hull = 35.0f;
        emit logMessage(tr("Hull breach! Emergency repair teams deployed"), LogLevel::Critical);
    }
}

void BattleView::damageRandomSubsystem(float amount)
{
    Subsystem &s = m_subsystems[QRandomGenerator::global()->bounded(int(m_subsystems.size()))];
    const float before = s.health;
    s.health = std::max(0.0f, s.health - amount);
    emit subsystemChanged(s.key, s.health);
    if (before >= 50.0f && s.health < 50.0f)
        emit logMessage(tr("%1 damaged (%2%)").arg(s.name).arg(qRound(s.health)), LogLevel::Warning);
    if (before > 0.0f && s.health <= 0.0f)
        emit logMessage(tr("%1 : Error — system offline").arg(s.name), LogLevel::Critical);
}

// ================================================================
//  Helpers
// ================================================================

std::vector<int> BattleView::aliveIds() const
{
    std::vector<int> ids;
    for (const Enemy &e : m_enemies)
        if (e.alive)
            ids.push_back(e.id);
    return ids;
}

int BattleView::indexOfId(int id) const
{
    const std::vector<int> ids = aliveIds();
    const auto it = std::find(ids.begin(), ids.end(), id);
    return it == ids.end() ? -1 : int(it - ids.begin());
}

BattleView::Enemy *BattleView::findEnemy(int id)
{
    return const_cast<Enemy *>(std::as_const(*this).findEnemy(id));
}

const BattleView::Enemy *BattleView::findEnemy(int id) const
{
    for (const Enemy &e : m_enemies)
        if (e.id == id)
            return &e;
    return nullptr;
}

const BattleView::Enemy *BattleView::pickTarget()
{
    if (const Enemy *locked = findEnemy(m_lockedId); locked && locked->alive)
        return locked;
    const std::vector<int> ids = aliveIds();
    if (ids.empty())
        return nullptr;
    return findEnemy(ids[QRandomGenerator::global()->bounded(int(ids.size()))]);
}

BattleView::Subsystem *BattleView::subsystem(const QString &key)
{
    for (Subsystem &s : m_subsystems)
        if (s.key == key)
            return &s;
    return nullptr;
}

float BattleView::subsystemHealth(const QString &key) const
{
    for (const Subsystem &s : m_subsystems)
        if (s.key == key)
            return s.health;
    return 100.0f;
}

QVector3D BattleView::destinyWorld(const QVector3D &local) const
{
    return m_destinyModel.map(local);
}

bool BattleView::insideShield(const QVector3D &world) const
{
    const QVector3D l = (m_destinyInverse.map(world) - kShieldCenter) / kShieldRadii;
    return l.lengthSquared() < 1.0f;
}

bool BattleView::shieldProtecting() const
{
    return m_shieldOrdered && !m_shieldCollapsed && m_shieldStrength > 0.0f;
}

QString BattleView::className(EnemyClass cls) const
{
    return cls == EnemyClass::Mothership ? tr("Mothership") : tr("Fighter");
}

// ================================================================
//  OpenGL setup
// ================================================================

void BattleView::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        qWarning() << "[BattleView] OpenGL 3.3 core profile is not available";
        return;
    }

    auto build = [](QOpenGLShaderProgram &program, const char *vs, const char *fs) {
        if (!program.addShaderFromSourceCode(QOpenGLShader::Vertex, vs)
            || !program.addShaderFromSourceCode(QOpenGLShader::Fragment, fs)
            || !program.link()) {
            qWarning() << "[BattleView] Shader error:" << program.log();
            return false;
        }
        return true;
    };
    if (!build(m_lit, kLitVertexShader, kLitFragmentShader)
        || !build(m_stars, kStarVertexShader, kStarFragmentShader))
        return;

    using namespace MeshBuilder;
    upload(m_box, box({1, 1, 1}));
    upload(m_sphere, sphere(24, 36));
    upload(m_spire, prism({{0.0f, -0.9f}, {8.0f, 0.0f}, {0.0f, 0.9f}}, 0.5f));
    upload(m_spireRidge, prism({{0.3f, -0.45f}, {6.6f, 0.0f}, {0.3f, 0.45f}}, 0.4f));
    upload(m_fan, prism(sector(kFanCenter, kFanRadius, 130.0f, 230.0f, 20), 0.55f));
    upload(m_fanPlate, prism(sector(kFanCenter, 4.2f, 140.0f, 220.0f, 16), 0.35f));
    upload(m_hub, frustum(6, 1.3f, 0.55f, 1.3f));
    upload(m_pyramid, frustum(4, 2.4f, 0.05f, 2.6f));
    upload(m_fighter, prism({{1.2f, 0.0f}, {-0.8f, 0.7f}, {-0.45f, 0.0f}, {-0.8f, -0.7f}}, 0.28f));

    std::vector<QVector2D> octagon;
    for (int i = 0; i < 8; ++i) {
        const float a = qDegreesToRadians(22.5f + 45.0f * float(i));
        octagon.emplace_back(3.6f * std::cos(a), 3.6f * std::sin(a));
    }
    upload(m_ring, prism(octagon, 0.45f));

    // Starfield: points on a large sphere, drawn without camera translation
    std::vector<float> stars;
    m_starCount = 2500;
    for (int i = 0; i < m_starCount; ++i) {
        QVector3D dir;
        do {
            dir = QVector3D(randRange(-1, 1), randRange(-1, 1), randRange(-1, 1));
        } while (dir.lengthSquared() > 1.0f || dir.lengthSquared() < 0.01f);
        dir = dir.normalized() * 400.0f;
        const float size = 1.0f + 2.2f * std::pow(randRange(0, 1), 6.0f);
        stars.insert(stars.end(), {dir.x(), dir.y(), dir.z(), size});
    }
    m_starVao.create();
    m_starVao.bind();
    m_starVbo.create();
    m_starVbo.bind();
    m_starVbo.allocate(stars.data(), int(stars.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    m_starVao.release();
    m_starVbo.release();

    m_glReady = true;
}

void BattleView::upload(Mesh &mesh, const MeshBuilder::VertexData &data)
{
    const int stride = MeshBuilder::kFloatsPerVertex * sizeof(float);
    mesh.vao.create();
    mesh.vao.bind();
    mesh.vbo.create();
    mesh.vbo.bind();
    mesh.vbo.allocate(data.data(), int(data.size() * sizeof(float)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(3 * sizeof(float)));
    mesh.vao.release();
    mesh.vbo.release();
    mesh.vertexCount = int(data.size()) / MeshBuilder::kFloatsPerVertex;
}

void BattleView::resizeGL(int w, int h)
{
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, float(w) / float(std::max(h, 1)), 0.1f, 1000.0f);
}

void BattleView::updateCamera()
{
    const float yaw = qDegreesToRadians(m_yawOffset + 28.0f * std::sin(m_time * 0.07f));
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D target(0.8f, 0.5f, 0.0f);

    m_eye = target + m_distance * QVector3D(std::cos(pitch) * std::sin(yaw),
                                            std::sin(pitch),
                                            std::cos(pitch) * std::cos(yaw));
    m_view.setToIdentity();
    m_view.lookAt(m_eye, target, {0, 1, 0});
    m_viewProj = m_projection * m_view;
    m_camRight = QVector3D::crossProduct((target - m_eye).normalized(), {0, 1, 0}).normalized();
}

// ================================================================
//  Rendering
// ================================================================

void BattleView::paintGL()
{
    if (!m_glReady) {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(QColor(255, 80, 60));
        p.drawText(rect(), Qt::AlignCenter, tr("OpenGL 3.3 unavailable — 3D view disabled"));
        return;
    }

    updateCamera();

    glClearColor(0.0f, 0.0f, 0.012f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);

    // Stars (infinitely far: rotation only)
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    QMatrix4x4 rotationOnly = m_view;
    rotationOnly.setColumn(3, QVector4D(0, 0, 0, 1));
    m_stars.bind();
    m_stars.setUniformValue("uViewProj", m_projection * rotationOnly);
    m_stars.setUniformValue("uPointScale", float(devicePixelRatioF()));
    m_starVao.bind();
    glDrawArrays(GL_POINTS, 0, m_starCount);
    m_starVao.release();
    m_stars.release();

    // Opaque geometry
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    m_lit.bind();
    m_lit.setUniformValue("uViewProj", m_viewProj);
    m_lit.setUniformValue("uLightDir", kLightDir);
    m_lit.setUniformValue("uCamPos", m_eye);
    m_lit.setUniformValue("uTime", m_time);

    QMatrix4x4 planet;
    planet.translate(30.0f, -48.0f, -175.0f);
    planet.rotate(18.0f, 0, 0, 1);
    planet.rotate(m_time * 0.6f, 0, 1, 0);
    planet.scale(100.0f);
    m_lit.setUniformValue("uOcean", m_planetLook.ocean);
    m_lit.setUniformValue("uGround", m_planetLook.ground);
    m_lit.setUniformValue("uGroundAlt", m_planetLook.groundAlt);
    m_lit.setUniformValue("uAtmosphere", m_planetLook.atmosphere);
    m_lit.setUniformValue("uWaterLevel", m_planetLook.waterLevel);
    m_lit.setUniformValue("uCloudCover", m_planetLook.cloudCover);
    m_lit.setUniformValue("uLights", m_planetLook.lights);
    m_lit.setUniformValue("uPlanetSeed", m_planetLook.seed);
    drawMesh(m_sphere, planet, {}, 0.0f, 1.0f, ModePlanet);

    drawDestiny();
    drawEnemies();

    // Additive effects
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    drawEffects();

    // Shield bubble (alpha blended, on top of the hull)
    const float shieldAlpha = shieldProtecting()
        ? (0.05f + 0.45f * m_shieldFlash) * (0.4f + 0.6f * m_shieldStrength / 100.0f)
        : 0.0f;
    if (shieldAlpha > 0.003f) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        QMatrix4x4 shield = m_destinyModel;
        shield.translate(kShieldCenter);
        shield.scale(kShieldRadii);
        drawMesh(m_sphere, shield, kShieldColor, 0.0f, shieldAlpha, ModeShield);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    m_lit.release();

    drawOverlay();
}

void BattleView::drawMesh(Mesh &mesh, const QMatrix4x4 &model, const QVector3D &color,
                          float emissive, float alpha, ShaderMode mode)
{
    m_lit.setUniformValue("uModel", model);
    m_lit.setUniformValue("uNormalMat", model.normalMatrix());
    m_lit.setUniformValue("uColor", color);
    m_lit.setUniformValue("uEmissive", emissive);
    m_lit.setUniformValue("uAlpha", alpha);
    m_lit.setUniformValue("uMode", int(mode));
    mesh.vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    mesh.vao.release();
}

void BattleView::drawGlow(const QVector3D &pos, float radius, const QVector3D &color, float alpha)
{
    QMatrix4x4 m;
    m.translate(pos);
    m.scale(radius);
    drawMesh(m_sphere, m, color, 1.4f, alpha, ModeGlow);
}

void BattleView::drawDestiny()
{
    auto part = [this](Mesh &mesh, const QVector3D &offset, const QVector3D &scale,
                       float rotY, const QVector3D &color) {
        QMatrix4x4 m = m_destinyModel;
        m.translate(offset);
        if (rotY != 0.0f)
            m.rotate(rotY, 0, 1, 0);
        m.scale(scale);
        drawMesh(mesh, m, color, 0.0f, 1.0f, ModeHull);
    };

    const QVector3D one(1, 1, 1);
    part(m_spire, {}, one, 0, kHullColor);
    part(m_spireRidge, {0, 0.42f, 0}, one, 0, kHullDark);
    part(m_fan, {}, one, 0, kHullColor);
    part(m_fanPlate, {0, 0.42f, 0}, one, 0, kHullDark);
    part(m_hub, {0.4f, 0.25f, 0}, one, 0, kHullDark);
    part(m_box, {0.4f, 1.9f, 0}, {0.35f, 0.9f, 0.35f}, 0, kHullColor);

    for (float angle = 160.0f; angle <= 200.0f; angle += 10.0f) {
        const float a = qDegreesToRadians(angle);
        const QVector3D pos(kFanCenter.x() + 5.45f * std::cos(a), 0.0f, 5.45f * std::sin(a));
        part(m_box, pos, {0.8f, 0.42f, 0.55f}, -angle, kHullDark);
    }
    for (const QVector3D &t : m_turrets)
        part(m_box, t, {0.28f, 0.22f, 0.28f}, 0, kHullDark);
}

void BattleView::drawEnemies()
{
    for (const Enemy &e : m_enemies) {
        if (!e.alive)
            continue;
        QMatrix4x4 m;
        m.translate(e.pos);
        m.rotate(QQuaternion::rotationTo({1, 0, 0}, e.facing));
        const float flash = e.hitFlash * 0.5f;

        if (e.cls == EnemyClass::Mothership) {
            drawMesh(m_ring, m, kEnemyDark, flash, 1.0f, ModeHull);
            QMatrix4x4 top = m;
            top.translate(0, 0.1f, 0);
            top.rotate(45, 0, 1, 0);
            drawMesh(m_pyramid, top, kEnemyGold, flash, 1.0f, ModeHull);
            QMatrix4x4 bottom = m;
            bottom.translate(0, -0.1f, 0);
            bottom.scale(1.0f, -0.45f, 1.0f);
            bottom.rotate(45, 0, 1, 0);
            drawMesh(m_pyramid, bottom, kEnemyDark, flash, 1.0f, ModeHull);
        } else {
            drawMesh(m_fighter, m, kFighterColor, flash, 1.0f, ModeHull);
        }
    }
}

void BattleView::drawEffects()
{
    // Destiny engines
    const float engines = subsystemHealth("engines") / 100.0f;
    for (float angle = 160.0f; angle <= 200.0f; angle += 10.0f) {
        const float a = qDegreesToRadians(angle);
        const QVector3D pos = destinyWorld({kFanCenter.x() + 6.0f * std::cos(a), 0.0f, 6.0f * std::sin(a)});
        const float pulse = 0.3f + 0.04f * std::sin(m_time * 9.0f + angle);
        drawGlow(pos, pulse, kEngineGlow * (0.3f + 0.7f * engines), 0.9f);
        drawGlow(pos, pulse * 2.6f, kEngineGlow, 0.12f * engines);
    }

    // Enemy engines
    for (const Enemy &e : m_enemies) {
        if (!e.alive)
            continue;
        const QVector3D back = e.pos - e.facing * e.radius * 0.95f;
        drawGlow(back, e.radius * 0.18f, QVector3D(1.0f, 0.6f, 0.2f), 0.8f);
    }

    // Fires on the Destiny hull
    for (size_t i = 0; i < m_fires.size(); ++i) {
        const QVector3D pos = destinyWorld(m_fires[i] + QVector3D(0, 0.15f, 0));
        const float flicker = 0.22f + 0.1f * std::sin(m_time * 13.0f + float(i) * 1.7f);
        drawGlow(pos, flicker, QVector3D(1.0f, 0.45f, 0.1f), 0.9f);
        drawGlow(pos, flicker * 2.5f, QVector3D(1.0f, 0.35f, 0.05f), 0.2f);
    }

    // Projectiles: elongated boxes along their velocity
    for (const Projectile &p : m_projectiles) {
        if (p.delay > 0.0f)
            continue;
        const QQuaternion orient = QQuaternion::rotationTo({0, 0, 1}, p.vel.normalized());
        QMatrix4x4 core;
        core.translate(p.pos);
        core.rotate(orient);
        QMatrix4x4 halo = core;
        core.scale(p.width, p.width, p.length);
        halo.scale(p.width * 3.0f, p.width * 3.0f, p.length * 1.2f);
        drawMesh(m_box, core, QVector3D(1, 1, 1) * 0.6f + p.color * 0.6f, 1.3f, 1.0f, ModeGlow);
        drawMesh(m_box, halo, p.color, 1.2f, 0.25f, ModeGlow);
    }

    // Explosions / impacts
    for (const Effect &fx : m_effects) {
        const float t = fx.age / fx.duration;
        const float radius = fx.maxRadius * (1.0f - std::pow(1.0f - t, 3.0f));
        const float alpha = 1.0f - t;
        drawGlow(fx.pos, radius * 0.6f, QVector3D(1.0f, 0.95f, 0.8f) * (1.0f - t) + fx.color * t, alpha);
        drawGlow(fx.pos, radius, fx.color, alpha * 0.5f);
    }
}

// ================================================================
//  HUD overlay (QPainter on top of the GL scene)
// ================================================================

bool BattleView::project(const QVector3D &world, QPointF *screen) const
{
    const QVector4D clip = m_viewProj * QVector4D(world, 1.0f);
    if (clip.w() <= 0.01f)
        return false;
    const QVector3D ndc = clip.toVector3DAffine();
    *screen = QPointF((ndc.x() * 0.5f + 0.5f) * width(), (0.5f - ndc.y() * 0.5f) * height());
    return true;
}

void BattleView::drawStatusBar(QPainter &p, const QRectF &rect, const QString &label, float value,
                               const QColor &color, const QString &suffix)
{
    p.setPen(QColor(220, 220, 210));
    p.drawText(QRectF(rect.left(), rect.top(), 64, rect.height()), Qt::AlignVCenter, label);
    const QRectF bar(rect.left() + 66, rect.center().y() - 4, rect.width() - 130, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(60, 60, 60, 200));
    p.drawRect(bar);
    p.setBrush(color);
    p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * std::clamp(value, 0.0f, 100.0f) / 100.0f, bar.height()));
    p.setPen(QColor(220, 220, 210));
    p.drawText(QRectF(bar.right() + 6, rect.top(), 60, rect.height()), Qt::AlignVCenter,
               QStringLiteral("%1%%2").arg(qRound(value)).arg(suffix));
}

void BattleView::drawOverlay()
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(12);
    font.setBold(true);
    p.setFont(font);

    // --- Enemy reticles + health bars
    const QColor red(255, 70, 60);
    const std::vector<int> ids = aliveIds();
    for (size_t i = 0; i < ids.size(); ++i) {
        const Enemy *e = findEnemy(ids[i]);
        QPointF center, edge;
        if (!project(e->pos, &center) || !project(e->pos + m_camRight * e->radius, &edge))
            continue;
        const qreal r = std::clamp(QLineF(center, edge).length(), 9.0, 70.0);
        const bool locked = e->id == m_lockedId;

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(red, locked ? 2.5 : 1.5));
        p.drawEllipse(center, r, r);
        p.drawLine(QPointF(center.x() - r - 6, center.y()), QPointF(center.x() - r + 4, center.y()));
        p.drawLine(QPointF(center.x() + r - 4, center.y()), QPointF(center.x() + r + 6, center.y()));
        p.drawLine(QPointF(center.x(), center.y() - r - 6), QPointF(center.x(), center.y() - r + 4));
        p.drawLine(QPointF(center.x(), center.y() + r - 4), QPointF(center.x(), center.y() + r + 6));
        if (locked)
            p.drawEllipse(center, r + 6, r + 6);

        const qreal w = std::max(44.0, r * 2.2);
        const QRectF bar(center.x() - w / 2, center.y() - r - 16, w, 5);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 90, 90, 200));
        p.drawRect(bar);
        p.setBrush(QColor(60, 230, 90));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * e->hp / e->maxHp, bar.height()));

        p.setPen(red);
        p.drawText(QPointF(center.x() + r + 8, center.y() - r + 4),
                   locked ? tr("T%1 LOCK").arg(i + 1) : tr("T%1").arg(i + 1));
    }

    // --- Destiny subsystem callouts
    for (const Subsystem &s : m_subsystems) {
        QPointF a;
        if (!project(destinyWorld(s.localPos), &a))
            continue;
        const QPointF elbow(a.x() + s.labelOffset.x() * 0.45, a.y() + s.labelOffset.y());
        const QPointF end(a.x() + s.labelOffset.x(), a.y() + s.labelOffset.y());
        const QColor line(70, 255, 110, 210);

        p.setPen(QPen(line, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(a, 6, 6);
        p.drawPolyline(QPolygonF{a, elbow, end});

        const bool right = s.labelOffset.x() > 0;
        const QRectF bar(right ? end.x() : end.x() - 72, end.y() - 4, 72, 7);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 40, 40, 200));
        p.drawRect(bar);
        p.setBrush(healthColor(s.health));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * s.health / 100.0f, bar.height()));
        p.setPen(QColor(225, 230, 225));
        p.drawText(QRectF(bar.left(), bar.bottom() + 2, 140, 16), Qt::AlignLeft, s.name);
    }

    // --- Status block (top left)
    const QRectF panel(12, 12, 270, 62);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 150));
    p.drawRoundedRect(panel, 4, 4);
    drawStatusBar(p, QRectF(22, 18, 250, 22), tr("HULL"), m_hull, healthColor(m_hull), QString());
    const QString shieldSuffix = m_shieldCollapsed ? tr(" DOWN") : (m_shieldOrdered ? QString() : tr(" OFF"));
    drawStatusBar(p, QRectF(22, 44, 250, 22), tr("SHIELD"), m_shieldStrength,
                  shieldProtecting() ? QColor(90, 170, 255) : QColor(110, 110, 110), shieldSuffix);

    // --- Contacts (top right) and hint (bottom left)
    p.setPen(ids.empty() ? QColor(70, 230, 100) : red);
    p.drawText(QRectF(0, 14, width() - 14, 18), Qt::AlignRight,
               ids.empty() ? tr("NO CONTACT") : tr("HOSTILES: %1").arg(ids.size()));
    // --- Navigation block (bottom right)
    if (!m_navigationLines.isEmpty()) {
        const qreal lineHeight = 16;
        const QRectF nav(width() - 300, height() - 14 - lineHeight * m_navigationLines.size() - 10,
                         288, lineHeight * m_navigationLines.size() + 10);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 150));
        p.drawRoundedRect(nav, 4, 4);
        p.setPen(QColor(150, 210, 255));
        for (int i = 0; i < m_navigationLines.size(); ++i)
            p.drawText(QRectF(nav.left() + 10, nav.top() + 5 + i * lineHeight, nav.width() - 20, lineHeight),
                       Qt::AlignLeft | Qt::AlignVCenter, m_navigationLines.at(i));
    }

    p.setPen(QColor(150, 150, 150));
    font.setBold(false);
    font.setPixelSize(11);
    p.setFont(font);
    p.drawText(QRectF(12, height() - 22, width() - 24, 16), Qt::AlignLeft,
               tr("drag: rotate camera  •  wheel: zoom"));
}

// ================================================================
//  Camera controls
// ================================================================

void BattleView::mousePressEvent(QMouseEvent *event)
{
    m_lastMouse = event->position().toPoint();
}

void BattleView::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;
    const QPoint pos = event->position().toPoint();
    const QPoint delta = pos - m_lastMouse;
    m_lastMouse = pos;
    m_yawOffset -= delta.x() * 0.3f;
    m_pitch = std::clamp(m_pitch + delta.y() * 0.2f, 5.0f, 75.0f);
}

void BattleView::wheelEvent(QWheelEvent *event)
{
    m_distance = std::clamp(m_distance * std::pow(0.999f, float(event->angleDelta().y())), 12.0f, 60.0f);
    event->accept();
}
