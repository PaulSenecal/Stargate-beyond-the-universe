#include "battleview.h"
#include "src/db/dbtext.h"

#include <QDebug>
#include <QFontDatabase>
#include <QLineF>
#include <QMap>
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
const QVector3D kDestinyBolt(0.55f, 0.85f, 1.0f);
const QVector3D kEnemyBolt(1.0f, 0.72f, 0.18f);
const QVector3D kBeamColor(0.75f, 0.95f, 1.0f);
const QVector3D kMissileExhaust(1.0f, 0.35f, 0.15f);
const QVector3D kSlug(1.0f, 0.92f, 0.65f);
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
constexpr float kWaveDelay = 15.0f;            // seconds between cleared sector and next wave
constexpr float kDiveSpeed = 9.0f;
constexpr int kMaxContacts = 12;               // launches stop above this
constexpr int kMaxAllies = 12;                  // friendly ships at once
constexpr float kFireOnAllies = 0.45f;          // share of enemy fire aimed at friendly ships
constexpr float kHangarRefill = 20.0f;          // seconds per fighter rebuilt out of battle
constexpr float kAllyFighterSpeed = 15.0f;
constexpr float kAllyMothershipSpeed = 5.0f;
constexpr float kMissileSpeed = 8.0f;
constexpr float kSlugSpeed = 70.0f;
constexpr float kSlugVsFighter = 2.5f;         // railguns shred small ships…
constexpr float kSlugVsMothership = 0.3f;      // …but barely scratch motherships

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

QVector3D colorOf(const QString &hex)
{
    const QColor c(hex);
    return c.isValid() ? QVector3D(c.redF(), c.greenF(), c.blueF()) : QVector3D(0.5f, 0.5f, 0.5f);
}

// Bolt color of a faction, always distinct from the Destiny's blue
QVector3D boltColor(const QString &faction)
{
    if (faction == "Wraith") return {0.8f, 0.45f, 1.0f};
    if (faction == "Drones") return {1.0f, 0.3f, 0.25f};
    if (faction == "Tau'ri") return {1.0f, 0.95f, 0.7f};
    return kEnemyBolt;
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

void BattleView::setPaused(bool paused)
{
    if (paused) {
        m_timer->stop();
    } else if (!m_timer->isActive()) {
        m_clock.restart();
        m_timer->start(16);
    }
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
                        .arg(className(*e))
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
    emit sound(SoundCue::DestinyMainGun, {}, 0.0f);
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
    for (int i = 0; i < 4; ++i)
        emit sound(SoundCue::DestinyTurret, {}, i * 0.15f);
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
    for (int i = 0; i < 4; ++i)
        emit sound(SoundCue::DestinyTurret, {}, i * 0.12f);
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

void BattleView::setWeaponProfiles(const QList<WeaponProfile> &profiles)
{
    for (const WeaponProfile &w : profiles) {
        WeaponProfile profile = w;
        if (profile.reach <= 0.0f)
            profile.reach = w.name == "Railgun" ? 22.0f : 45.0f;
        if (w.name == "Railgun")
            m_railgun = profile;
        else if (w.name == "Energy Cannon")
            m_cannon = profile;
    }
}

void BattleView::setShipClasses(const QList<ShipClass> &classes)
{
    QList<ShipClass> usable;
    for (const ShipClass &c : classes)
        if (c.hull > 0)
            usable << c;
    const QList<ShipClass> previous = m_classes;
    m_classes = usable;
    if (hostileFactions().isEmpty()) {
        qWarning() << "[Battle] No hostile faction in the ship library, using the built-in roster";
        m_classes = previous;
        return;
    }
    // Enemies point into the class list: start a fresh wave
    m_enemies.clear();
    m_projectiles.erase(std::remove_if(m_projectiles.begin(), m_projectiles.end(),
                                       [](const Projectile &p) { return !p.fromDestiny || p.targetId >= 0; }),
                        m_projectiles.end());
    m_lockedId = -1;
    if (m_autoWaves)
        spawnWave(false);
}

int BattleView::classIndexOf(const QString &name) const
{
    for (int i = 0; i < m_classes.size(); ++i)
        if (m_classes[i].name.compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

// Factions with at least one hostile mothership and one hostile small ship
QStringList BattleView::hostileFactions() const
{
    QStringList factions;
    for (const ShipClass &big : m_classes) {
        if (!big.hostile || !big.isMothership() || factions.contains(big.faction))
            continue;
        for (const ShipClass &small : m_classes)
            if (small.hostile && !small.isMothership() && small.faction == big.faction) {
                factions << big.faction;
                break;
            }
    }
    return factions;
}

void BattleView::simulate(float seconds)
{
    constexpr float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds; t += dt)
        step(dt);
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
    clearAllies();
    m_projectiles.clear();
    m_effects.clear();
    m_lockedId = -1;
    m_waveTimer = calmSeconds;
}

void BattleView::setObjectiveLines(const QStringList &lines)
{
    m_objectiveLines = lines;
    update();
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
        target = tr("T%1 %2 (%3%)").arg(indexOfId(e->id) + 1).arg(className(*e))
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

    QString report = tr("%1 hostile contact(s), %2:").arg(ids.size()).arg(m_waveFaction);
    for (size_t i = 0; i < ids.size(); ++i) {
        const Enemy *e = findEnemy(ids[i]);
        report += tr("\nT%1  %2  hull %3%  distance %4 km")
                      .arg(i + 1)
                      .arg(className(*e))
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
    for (const auto &[classIndex, origin] : m_launches)
        spawnEnemy(classIndex).pos = origin;    // leaves the hangar, then joins its orbit
    m_launches.clear();

    // Friendly ships fly and fight; hangar fighters land after the battle
    for (Enemy &a : m_allies)
        if (a.alive)
            updateAlly(a, dt);
    for (const auto &[classIndex, origin] : m_allyLaunches)
        if (allyCount() < kMaxAllies)
            spawnAlly(classIndex, origin, -2);
    m_allyLaunches.clear();
    m_allies.erase(std::remove_if(m_allies.begin(), m_allies.end(), [](const Enemy &a) { return !a.alive; }),
                   m_allies.end());
    if (aliveIds().empty() && m_hangarCapacity > 0) {
        const int out = int(std::count_if(m_allies.begin(), m_allies.end(), [](const Enemy &a) { return a.group == -1; }));
        m_hangarRefill += dt;
        if (m_hangarRefill >= kHangarRefill) {
            m_hangarRefill = 0;
            if (m_hangar + out < m_hangarCapacity)
                ++m_hangar;             // new fighter assembled in the hangar
        }
    }

    // Secondary towers auto-fire, slower when the weapons are damaged
    m_autoFireTimer -= dt;
    if (m_autoFireTimer <= 0.0f) {
        const float efficiency = std::max(0.25f, subsystemHealth("weapons") / 100.0f);
        m_autoFireTimer = 0.45f / efficiency;
        if (const Enemy *t = pickTarget()) {
            const QVector3D &turret = m_turrets[QRandomGenerator::global()->bounded(int(m_turrets.size()))];
            shoot(true, destinyWorld(turret), t->id, {}, m_cannon.damage / 17.0f * efficiency, 38.0f,
                  kDestinyBolt, 1.0f, 0.09f);
            emit sound(SoundCue::DestinyTurret, {}, 0.0f);
        }
    }

    fireRailguns(dt);

    m_mainCooldown = std::max(0.0f, m_mainCooldown - dt);
    m_pulseCooldown = std::max(0.0f, m_pulseCooldown - dt);
    m_secondaryCooldown = std::max(0.0f, m_secondaryCooldown - dt);
    m_shieldFlash = std::max(0.0f, m_shieldFlash - dt * 2.0f);

    // Shield regeneration (slower while under fire)
    if (m_shieldOrdered) {
        const float base = m_time - m_lastShieldHit > 1.5f ? 6.0f : 3.0f;
        const float rate = base * m_shieldRegenBonus * std::max(0.2f, subsystemHealth("shields") / 100.0f);
        m_shieldStrength = std::min(100.0f, m_shieldStrength + rate * dt);
        if (m_shieldCollapsed && m_shieldStrength >= 35.0f) {
            m_shieldCollapsed = false;
            emit logMessage(tr("Shields restored (%1%)").arg(qRound(m_shieldStrength)), LogLevel::Success);
        }
    }

    updateProjectiles(dt);

    m_hits.erase(std::remove_if(m_hits.begin(), m_hits.end(),
                                [this](const auto &h) { return m_time - h.second.first > 1.5f; }),
                 m_hits.end());

    for (Effect &fx : m_effects)
        fx.age += dt;
    m_effects.erase(std::remove_if(m_effects.begin(), m_effects.end(),
                                   [](const Effect &fx) { return fx.age >= fx.duration; }),
                    m_effects.end());

    // New wave a few seconds after the sector is cleared
    if (aliveIds().empty() && !m_autoWaves) {
        if (m_waveTimer < 0.0f) {
            m_waveTimer = 0.0f;             // reported once per fleet
            emit sectorCleared();
        }
    } else if (aliveIds().empty()) {
        if (m_waveTimer < 0.0f) {
            m_waveTimer = kWaveDelay;
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

void BattleView::spawnWave(bool announce, const QString &faction)
{
    m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(),
                                   [](const Enemy &e) { return !e.alive; }),
                    m_enemies.end());
    m_waveTimer = -1.0f;

    const QStringList factions = hostileFactions();
    if (factions.isEmpty())
        return;
    m_waveFaction = factions.contains(faction) ? faction
                  : factions[QRandomGenerator::global()->bounded(int(factions.size()))];

    QList<int> big, small;
    for (int i = 0; i < m_classes.size(); ++i) {
        const ShipClass &c = m_classes[i];
        if (c.hostile && c.faction == m_waveFaction)
            (c.isMothership() ? big : small) << i;
    }
    auto pick = [](const QList<int> &list) { return list[QRandomGenerator::global()->bounded(int(list.size()))]; };

    // Heavier motherships come in smaller numbers
    int heaviest = 0;
    for (int i : big)
        heaviest = std::max(heaviest, m_classes[i].hull);
    const int motherships = (heaviest >= 500 ? 1 : 2) + QRandomGenerator::global()->bounded(2);
    const int fighters = 4 + QRandomGenerator::global()->bounded(3);
    QMap<QString, int> counts;
    for (int i = 0; i < motherships; ++i)
        ++counts[classOf(spawnEnemy(pick(big), true)).name];
    for (int i = 0; i < fighters; ++i)
        ++counts[classOf(spawnEnemy(pick(small), true)).name];

    if (announce) {
        QStringList parts;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            parts << QStringLiteral("%1 %2").arg(it.value()).arg(it.key());
        emit logMessage(tr("New hostile contacts (%1): %2").arg(m_waveFaction, parts.join(", ")),
                        LogLevel::Warning);
    }
}

bool BattleView::spawnFleet(const QString &faction, int motherships, int fighters)
{
    constexpr int kBossHull = 900;
    QList<int> big, small;
    for (int pass = 0; pass < 2 && (big.isEmpty() || small.isEmpty()); ++pass) {
        // Ships of survival waves first, then the others of the faction
        for (int i = 0; i < m_classes.size(); ++i) {
            const ShipClass &c = m_classes[i];
            if (c.faction != faction || c.hull >= kBossHull || (pass == 0 && !c.hostile))
                continue;
            QList<int> &list = c.isMothership() ? big : small;
            if (!list.contains(i))
                list << i;
        }
    }
    if (big.isEmpty() && small.isEmpty())
        return false;
    if (big.isEmpty())
        big = small;
    if (small.isEmpty())
        small = big;

    m_enemies.erase(std::remove_if(m_enemies.begin(), m_enemies.end(), [](const Enemy &e) { return !e.alive; }),
                    m_enemies.end());
    m_waveFaction = faction;
    m_waveTimer = -1.0f;
    auto pick = [](const QList<int> &list) { return list[QRandomGenerator::global()->bounded(int(list.size()))]; };
    for (int i = 0; i < motherships; ++i)
        spawnEnemy(pick(big), true);
    for (int i = 0; i < fighters; ++i)
        spawnEnemy(pick(small), true);
    return true;
}

// ================================================================
//  Friendly ships
// ================================================================

BattleView::Enemy *BattleView::findAlly(int id)
{
    for (Enemy &a : m_allies)
        if (a.id == id)
            return &a;
    return nullptr;
}

BattleView::Enemy *BattleView::nearestEnemy(const QVector3D &from, bool fighterFirst)
{
    Enemy *best = nullptr;
    float bestScore = 1e9f;
    for (Enemy &e : m_enemies) {
        if (!e.alive)
            continue;
        // Diving kamikazes first, then (for fighters) the small ships
        float score = (e.pos - from).length();
        if (e.diving)
            score *= 0.2f;
        else if (fighterFirst && e.cls == EnemyClass::Mothership)
            score *= 2.5f;
        if (score < bestScore) {
            bestScore = score;
            best = &e;
        }
    }
    return best;
}

BattleView::Enemy *BattleView::nearestAlly(const QVector3D &from)
{
    Enemy *best = nullptr;
    float bestDistance = 1e9f;
    for (Enemy &a : m_allies)
        if (a.alive && (a.pos - from).length() < bestDistance) {
            bestDistance = (a.pos - from).length();
            best = &a;
        }
    return best;
}

int BattleView::allyCount() const
{
    return int(std::count_if(m_allies.begin(), m_allies.end(), [](const Enemy &a) { return a.alive; }));
}

BattleView::Enemy &BattleView::spawnAlly(int classIndex, const QVector3D &from, int group)
{
    const ShipClass &sc = m_classes[classIndex];
    Enemy a;
    a.id = m_nextAllyId++;
    a.classIndex = classIndex;
    a.group = group;
    a.cls = sc.isMothership() ? EnemyClass::Mothership : EnemyClass::Fighter;
    a.phase = randRange(0.0f, 2.0f * float(M_PI));
    a.hp = a.maxHp = float(sc.hull);
    for (const ShipAttack &attack : sc.attacks)
        a.attackTimers.push_back(attack.cooldown * randRange(0.3f, 1.0f));
    a.radius = a.cls == EnemyClass::Mothership ? 3.6f : 1.0f;
    // Formation on the Destiny's side (enemies come from the other one)
    a.baseAngle = qDegreesToRadians(randRange(25.0f, 155.0f));
    a.orbitRadius = a.cls == EnemyClass::Mothership ? randRange(20.0f, 26.0f) : randRange(9.0f, 14.0f);
    a.height = a.cls == EnemyClass::Mothership ? randRange(3.0f, 8.0f) : randRange(-2.0f, 5.0f);
    a.pos = from;
    a.facing = QVector3D(std::cos(a.baseAngle), 0.0f, std::sin(a.baseAngle));
    m_allies.push_back(a);
    return m_allies.back();
}

bool BattleView::spawnAllies(const QString &faction, int motherships, int fighters, int group)
{
    constexpr int kBossHull = 900;
    QList<int> big, small;
    for (int i = 0; i < m_classes.size(); ++i) {
        const ShipClass &c = m_classes[i];
        if (c.faction == faction && c.hull < kBossHull)
            (c.isMothership() ? big : small) << i;
    }
    if (big.isEmpty())
        big = small;
    if (small.isEmpty())
        small = big;
    if (big.isEmpty())
        return false;
    auto pick = [](const QList<int> &list) { return list[QRandomGenerator::global()->bounded(int(list.size()))]; };
    // They arrive from deep space on the Destiny's side
    for (int i = 0; i < motherships && allyCount() < kMaxAllies; ++i) {
        const float angle = qDegreesToRadians(randRange(40.0f, 140.0f));
        hyperspaceWindow(spawnAlly(pick(big), QVector3D(60.0f * std::cos(angle), randRange(4.0f, 10.0f), 60.0f * std::sin(angle)), group).pos, 1.6f);
    }
    for (int i = 0; i < fighters && allyCount() < kMaxAllies; ++i) {
        const float angle = qDegreesToRadians(randRange(40.0f, 140.0f));
        hyperspaceWindow(spawnAlly(pick(small), QVector3D(50.0f * std::cos(angle), randRange(0.0f, 8.0f), 50.0f * std::sin(angle)), group).pos, 0.7f);
    }
    emit logMessage(tr("Friendly %1 ships join the battle").arg(faction), LogLevel::Success);
    return true;
}

void BattleView::setHangar(const QString &fighterClass, int capacity)
{
    m_hangarClass = fighterClass;
    m_hangarCapacity = classIndexOf(fighterClass) >= 0 ? std::max(0, capacity) : 0;
    m_hangar = m_hangarCapacity;
}

int BattleView::launchFighters(int count)
{
    const int classIndex = classIndexOf(m_hangarClass);
    if (classIndex < 0 || m_hangarCapacity == 0) {
        emit logMessage(tr("No hangar aboard"), LogLevel::Warning);
        return 0;
    }
    if (m_hangar == 0) {
        emit logMessage(tr("Hangar empty - fighters are rebuilt out of battle"), LogLevel::Warning);
        return 0;
    }
    int launched = 0;
    const int wanted = count < 0 ? m_hangar : std::min(count, m_hangar);
    for (int i = 0; i < wanted && allyCount() < kMaxAllies; ++i) {
        // Out of the hangar bay, under the Destiny
        Enemy &a = spawnAlly(classIndex, destinyWorld({randRange(-2.0f, 2.0f), -0.6f, randRange(-1.0f, 1.0f)}), -1);
        a.vel = QVector3D(randRange(-2.0f, 2.0f), -1.0f, randRange(2.0f, 5.0f));
        ++launched;
    }
    m_hangar -= launched;
    if (launched > 0) {
        emit logMessage(tr("%1 %2 launched").arg(launched).arg(m_hangarClass), LogLevel::Success);
        emit sound(SoundCue::Launch, m_classes[classIndex].faction, 0.0f);
    }
    return launched;
}

QList<BattleView::AllySurvivors> BattleView::allySurvivors() const
{
    QList<AllySurvivors> result;
    for (const Enemy &a : m_allies) {
        if (!a.alive || a.group < 0)
            continue;
        auto it = std::find_if(result.begin(), result.end(), [&a](const AllySurvivors &s) { return s.group == a.group; });
        if (it == result.end()) {
            result.append({a.group, 0, 0});
            it = result.end() - 1;
        }
        (a.cls == EnemyClass::Mothership ? it->motherships : it->fighters) += 1;
    }
    return result;
}

void BattleView::clearAllies()
{
    for (const Enemy &a : m_allies)
        if (a.alive && a.group == -1)
            ++m_hangar;                 // hangar fighters land
    m_hangar = std::min(m_hangar, m_hangarCapacity);
    m_allies.clear();
    m_allyLaunches.clear();
}

// Fighters circle their target and strafe it; motherships hold a line
// between the Destiny and the enemies and fire their batteries
void BattleView::updateAlly(Enemy &a, float dt)
{
    a.hitFlash = std::max(0.0f, a.hitFlash - dt * 4.0f);
    Enemy *target = findEnemy(a.targetId);
    if (!target || !target->alive) {
        target = nearestEnemy(a.pos, a.cls == EnemyClass::Fighter);
        a.targetId = target ? target->id : -1;
    }
    // Always switch to a kamikaze diving at the Destiny
    if (a.cls == EnemyClass::Fighter && target && !target->diving)
        if (Enemy *diver = nearestEnemy(a.pos, true); diver && diver->diving) {
            target = diver;
            a.targetId = diver->id;
        }

    const bool fighter = a.cls == EnemyClass::Fighter;
    QVector3D goal;
    float speed = fighter ? kAllyFighterSpeed : kAllyMothershipSpeed;
    if (target) {
        a.idle = 0;
        if (fighter) {
            const float angle = m_time * 1.4f + a.phase;
            goal = target->pos + QVector3D(std::cos(angle) * 6.0f, 1.5f * std::sin(angle * 0.7f), std::sin(angle) * 6.0f);
        } else {
            // Front line: halfway to the enemy centroid, spread sideways
            QVector3D centroid;
            int count = 0;
            for (const Enemy &e : m_enemies)
                if (e.alive) {
                    centroid += e.pos;
                    ++count;
                }
            centroid /= float(std::max(1, count));
            const QVector3D side = QVector3D(-centroid.z(), 0.0f, centroid.x()).normalized();
            goal = centroid * 0.45f + side * ((a.phase - float(M_PI)) * 4.0f) + QVector3D(0, a.height, 0);
        }
    } else if (a.group == -1) {
        // Hangar fighter: back to the Destiny, then lands
        a.idle += dt;
        goal = destinyWorld({0.0f, -0.6f, 0.0f});
        if (a.idle > 3.0f && (a.pos - goal).length() < 3.0f) {
            a.alive = false;
            m_hangar = std::min(m_hangarCapacity, m_hangar + 1);
            return;
        }
    } else {
        // Formation on the Destiny's side
        const float angle = a.baseAngle + 0.15f * std::sin(m_time * 0.2f + a.phase);
        goal = QVector3D(a.orbitRadius * std::cos(angle), a.height, a.orbitRadius * std::sin(angle));
        speed *= 0.6f;
    }

    // Steering: smooth acceleration toward the goal
    const QVector3D toGoal = goal - a.pos;
    const float distance = toGoal.length();
    const QVector3D wanted = distance > 0.1f ? toGoal / distance * std::min(speed, distance * 1.5f) : QVector3D();
    a.vel += (wanted - a.vel) * std::min(1.0f, dt * (fighter ? 3.0f : 1.5f));
    a.pos += a.vel * dt;
    if (fighter && a.vel.lengthSquared() > 0.25f)
        a.facing = a.vel.normalized();
    else if (target)
        a.facing = QVector3D(target->pos.x() - a.pos.x(), 0.0f, target->pos.z() - a.pos.z()).normalized();

    if (!target)
        return;
    const float reach = fighter ? 30.0f : 70.0f;
    if ((target->pos - a.pos).length() > reach)
        return;
    const QList<ShipAttack> &attacks = classOf(a).attacks;
    for (int i = 0; i < attacks.size() && i < int(a.attackTimers.size()); ++i) {
        float &timer = a.attackTimers[size_t(i)];
        timer -= dt;
        if (timer > 0.0f)
            continue;
        timer = attacks[i].cooldown * randRange(0.85f, 1.15f);
        if (randRange(0.0f, 1.0f) <= attacks[i].chance)
            allyAttack(a, attacks[i], *target);
    }
}

void BattleView::allyAttack(Enemy &a, const ShipAttack &attack, const Enemy &target)
{
    const ShipClass &sc = classOf(a);
    const bool big = a.cls == EnemyClass::Mothership;
    auto fired = [this, &attack](Projectile::Kind kind) {
        Projectile &p = m_projectiles.back();
        p.kind = kind;
        p.fromAlly = true;
        p.damage = attack.damage;           // no Destiny technology bonus
    };
    if (attack.kind == "bolt" || attack.kind == "slug") {
        const bool slug = attack.kind == "slug";
        for (int i = 0; i < attack.shots; ++i) {
            shoot(true, a.pos, target.id, {}, attack.damage, slug ? 45.0f : 30.0f, slug ? kSlug : boltColor(sc.faction),
                  big ? 1.6f : 1.1f, slug ? 0.05f : 0.1f, i * 0.15f);
            fired(slug ? Projectile::Kind::Slug : Projectile::Kind::Bolt);
        }
        emit sound(big ? SoundCue::EnemyHeavyFire : SoundCue::EnemyLightFire, sc.faction, 0.0f);
    } else if (attack.kind == "missile") {
        for (int i = 0; i < attack.shots; ++i) {
            shoot(true, a.pos, target.id, {}, attack.damage, kMissileSpeed * 1.3f, kMissileExhaust, 0.6f, 0.16f, i * 0.4f);
            fired(Projectile::Kind::Missile);
            m_projectiles.back().life = 12.0f;
        }
        emit sound(SoundCue::EnemyMissile, sc.faction, 0.0f);
    } else if (attack.kind == "beam") {
        shoot(true, a.pos, target.id, {}, attack.damage, 90.0f, kBeamColor, 6.0f, 0.3f);
        fired(Projectile::Kind::Beam);
        emit sound(SoundCue::EnemyBeam, sc.faction, 0.0f);
    } else if (attack.kind == "launch") {
        const int payload = classIndexOf(attack.payload);
        for (int i = 0; payload >= 0 && i < attack.shots; ++i)
            m_allyLaunches.push_back({payload, a.pos});
    }
    // kamikaze: friendly ships do not sacrifice themselves
}

void BattleView::hitAlly(Enemy &a, const Projectile &p)
{
    a.hp -= p.damage;
    a.hitFlash = 1.0f;
    m_effects.push_back({p.pos, kExplosion, 0.0f, 0.3f, 0.5f + p.width * 2.0f});
    if (a.hp > 0.0f)
        return;
    a.alive = false;
    const bool big = a.cls == EnemyClass::Mothership;
    m_effects.push_back({a.pos, kExplosion, 0.0f, big ? 1.6f : 0.9f, big ? 6.0f : 2.2f});
    emit sound(big ? SoundCue::BigExplosion : SoundCue::Explosion, {}, 0.0f);
    emit logMessage(a.group == -1 ? tr("Your %1 is shot down").arg(className(a))
                                  : tr("Friendly %1 destroyed").arg(className(a)),
                    LogLevel::Warning);
}

void BattleView::drawAllies()
{
    for (const Enemy &a : m_allies) {
        if (!a.alive)
            continue;
        QMatrix4x4 m;
        m.translate(a.pos);
        m.rotate(QQuaternion::rotationTo({1, 0, 0}, a.facing));
        const float flash = a.hitFlash * 0.5f;
        const QVector3D tint = colorOf(classOf(a).color);
        const QVector3D dark = tint * 0.5f;
        if (a.cls == EnemyClass::Mothership) {
            drawMesh(m_ring, m, dark, flash, 1.0f, ModeHull);
            QMatrix4x4 top = m;
            top.translate(0, 0.1f, 0);
            top.rotate(45, 0, 1, 0);
            drawMesh(m_pyramid, top, tint, flash, 1.0f, ModeHull);
            QMatrix4x4 bottom = m;
            bottom.translate(0, -0.1f, 0);
            bottom.scale(1.0f, -0.45f, 1.0f);
            bottom.rotate(45, 0, 1, 0);
            drawMesh(m_pyramid, bottom, dark, flash, 1.0f, ModeHull);
        } else {
            drawMesh(m_fighter, m, tint, flash, 1.0f, ModeHull);
        }
        // Engine glow in friendly blue
        drawGlow(a.pos - a.facing * a.radius * 0.95f, a.radius * 0.35f, QVector3D(0.35f, 0.7f, 1.0f), 0.8f);
    }
}

// Blue-white window of a ship dropping out of (or entering) hyperspace
void BattleView::hyperspaceWindow(const QVector3D &pos, float size)
{
    m_effects.push_back({pos, QVector3D(0.45f, 0.7f, 1.0f), 0.0f, 1.0f, 4.5f * size});
    m_effects.push_back({pos, QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 0.35f, 1.8f * size});
}

void BattleView::hyperspaceFlash()
{
    const QVector3D center = destinyWorld({0.0f, 0.0f, 0.0f});
    m_effects.push_back({center, QVector3D(0.45f, 0.7f, 1.0f), 0.0f, 1.3f, 26.0f});
    m_effects.push_back({center, QVector3D(1.0f, 1.0f, 1.0f), 0.0f, 0.5f, 12.0f});
    update();
}

BattleView::Enemy &BattleView::spawnEnemy(int classIndex, bool fromHyperspace)
{
    const ShipClass &sc = m_classes[classIndex];
    Enemy e;
    e.id = m_nextEnemyId++;
    e.classIndex = classIndex;
    e.cls = sc.isMothership() ? EnemyClass::Mothership : EnemyClass::Fighter;
    e.phase = randRange(0.0f, 2.0f * float(M_PI));
    e.hp = e.maxHp = float(sc.hull);
    for (const ShipAttack &a : sc.attacks)
        e.attackTimers.push_back(a.cooldown * randRange(0.3f, 1.0f));
    if (e.cls == EnemyClass::Mothership) {
        e.radius = 3.6f;
        e.orbitRadius = randRange(27.0f, 34.0f);
        e.baseAngle = qDegreesToRadians(randRange(205.0f, 335.0f));
        e.angleAmp = qDegreesToRadians(randRange(6.0f, 14.0f));
        e.angleSpeed = randRange(0.08f, 0.15f);
        e.height = randRange(5.0f, 10.0f);
    } else {
        e.radius = 1.0f;
        e.orbitRadius = randRange(13.0f, 20.0f);
        e.baseAngle = qDegreesToRadians(randRange(190.0f, 350.0f));
        e.angleAmp = qDegreesToRadians(randRange(25.0f, 45.0f));
        e.angleSpeed = randRange(0.35f, 0.6f);
        e.height = randRange(-3.0f, 7.0f);
    }
    updateEnemy(e, 0.0f);
    if (fromHyperspace) {
        // Drops out of hyperspace far out, then flies in to its orbit
        e.pos += QVector3D(e.pos.x(), 0.0f, e.pos.z()).normalized() * 35.0f;
        hyperspaceWindow(e.pos, e.cls == EnemyClass::Mothership ? 1.6f : 0.7f);
    }
    m_enemies.push_back(e);
    return m_enemies.back();
}

void BattleView::updateEnemy(Enemy &e, float dt)
{
    e.hitFlash = std::max(0.0f, e.hitFlash - dt * 4.0f);

    // Kamikaze run: straight at the impact point, no more firing
    if (e.diving) {
        const QVector3D toTarget = destinyWorld(e.diveAim) - e.pos;
        const float distance = toTarget.length();
        if (distance > 1e-3f)
            e.facing = toTarget / distance;
        e.pos += e.facing * std::min(distance, kDiveSpeed * dt);
        if ((shieldProtecting() && insideShield(e.pos)) || distance <= 0.8f)
            ram(e);
        return;
    }

    const QVector3D previous = e.pos;
    const float a = e.baseAngle + e.angleAmp * std::sin(m_time * e.angleSpeed + e.phase);
    QVector3D orbit(e.orbitRadius * std::cos(a), e.height + 0.8f * std::sin(m_time * 0.7f + e.phase),
                    e.orbitRadius * std::sin(a));
    // Freshly launched ships fly from their hangar to their orbit
    const float gap = (orbit - previous).length();
    if (dt > 0.0f && gap > 2.0f)
        orbit = previous + (orbit - previous) / gap * std::min(gap, 12.0f * dt);
    e.pos = orbit;

    const QVector3D motion = e.pos - previous;
    if (e.cls == EnemyClass::Fighter && dt > 0.0f && motion.lengthSquared() > 1e-6f)
        e.facing = motion.normalized();
    else
        e.facing = QVector3D(-e.pos.x(), 0.0f, -e.pos.z()).normalized();

    if (dt <= 0.0f)
        return;

    // Attacks from the ship library: each one has its own cooldown and a
    // chance to be used when ready
    const QList<ShipAttack> &attacks = classOf(e).attacks;
    for (int i = 0; i < attacks.size() && i < int(e.attackTimers.size()); ++i) {
        const ShipAttack &attack = attacks[i];
        float &timer = e.attackTimers[size_t(i)];
        timer -= dt;
        if (timer > 0.0f)
            continue;
        timer = attack.cooldown * randRange(0.85f, 1.15f);
        if (attack.trigger == "damaged" && e.hp > 0.5f * e.maxHp)
            continue;
        if (randRange(0.0f, 1.0f) > attack.chance)
            continue;
        performAttack(e, attack);
        if (e.diving)
            break;
    }
}

void BattleView::performAttack(Enemy &e, const ShipAttack &attack)
{
    const ShipClass &sc = classOf(e);
    const bool big = e.cls == EnemyClass::Mothership;

    // Part of the fire goes to the friendly ships, when there are some
    const Enemy *victim = !m_allies.empty() && randRange(0.0f, 1.0f) < kFireOnAllies ? nearestAlly(e.pos) : nullptr;
    auto aimAtVictim = [this, victim] {
        if (victim)
            m_projectiles.back().targetAlly = victim->id;
    };

    if (attack.kind == "bolt" || attack.kind == "slug") {
        const bool slug = attack.kind == "slug";
        for (int i = 0; i < attack.shots; ++i) {
            const QVector3D from = e.pos + QVector3D(randRange(-1, 1), randRange(-0.3f, 0.3f), randRange(-1, 1)) * (e.radius * 0.5f);
            shoot(false, from, -1, randomHullPoint(), attack.damage, slug ? 45.0f : big ? 22.0f : 28.0f,
                  slug ? kSlug : boltColor(sc.faction), big ? 1.6f : 1.1f, slug ? 0.05f : big ? 0.14f : 0.1f,
                  i * 0.15f);
            aimAtVictim();
            if (i < 3)
                emit sound(slug ? SoundCue::DestinyRailgun : big ? SoundCue::EnemyHeavyFire : SoundCue::EnemyLightFire,
                           sc.faction, i * 0.15f);
        }
    } else if (attack.kind == "missile") {
        for (int i = 0; i < attack.shots; ++i) {
            shoot(false, e.pos, -1, randomHullPoint(), attack.damage, kMissileSpeed, kMissileExhaust, 0.6f, 0.16f,
                  i * 0.4f);
            m_projectiles.back().kind = Projectile::Kind::Missile;
            m_projectiles.back().life = 14.0f;
            aimAtVictim();
            emit sound(SoundCue::EnemyMissile, sc.faction, i * 0.4f);
        }
        if (attack.shots > 1)
            emit logMessage(tr("%1 fires a salvo of %2 missiles").arg(sc.name).arg(attack.shots), LogLevel::Warning);
    } else if (attack.kind == "beam") {
        shoot(false, e.pos, -1, randomHullPoint(), attack.damage, 90.0f, kBeamColor, 6.0f, 0.3f);
        m_projectiles.back().kind = Projectile::Kind::Beam;
        aimAtVictim();
        emit sound(SoundCue::EnemyBeam, sc.faction, 0.0f);
        emit logMessage(tr("%1: %2 fired").arg(sc.name, DbText::tr(attack.name)), LogLevel::Warning);
    } else if (attack.kind == "kamikaze") {
        e.diving = true;
        e.diveAim = randomHullPoint();
        emit logMessage(tr("T%1 %2 is diving at the Destiny!").arg(indexOfId(e.id) + 1).arg(sc.name),
                        LogLevel::Critical);
    } else if (attack.kind == "launch") {
        const int payload = classIndexOf(attack.payload);
        const int count = std::min(attack.shots, kMaxContacts - int(aliveIds().size() + m_launches.size()));
        if (payload < 0 || count <= 0)
            return;
        // Spawned after the enemy loop of step(): m_enemies is being iterated
        for (int i = 0; i < count; ++i)
            m_launches.push_back({payload, e.pos});
        m_effects.push_back({e.pos, boltColor(sc.faction), 0.0f, 0.6f, 2.5f});
        emit sound(SoundCue::Launch, sc.faction, 0.0f);
        emit logMessage(tr("%1 launches %2 %3").arg(sc.name).arg(count).arg(attack.payload), LogLevel::Warning);
    }
}

void BattleView::ram(Enemy &e)
{
    const ShipClass &sc = classOf(e);
    float damage = 0;
    for (const ShipAttack &a : sc.attacks)
        if (a.kind == "kamikaze")
            damage = a.damage;

    Projectile impact;
    impact.fromDestiny = false;
    impact.pos = e.pos;
    impact.aimLocal = e.diveAim;
    impact.damage = damage;
    const bool shield = shieldProtecting() && insideShield(e.pos);
    if (shield)
        hitShield(impact);
    else
        hitHull(impact);
    m_effects.push_back({e.pos, kExplosion, 0.0f, 0.9f, 2.6f});
    emit sound(SoundCue::Explosion, {}, 0.0f);

    e.alive = false;
    if (m_lockedId == e.id)
        m_lockedId = -1;
    emit logMessage(shield ? tr("%1 crashed into the shield").arg(sc.name)
                           : tr("%1 rammed the hull (-%2)").arg(sc.name).arg(qRound(damage)),
                    LogLevel::Critical);
}

void BattleView::shoot(bool fromDestiny, const QVector3D &from, int targetId, const QVector3D &aimLocal,
                       float damage, float speed, const QVector3D &color, float length, float width,
                       float delay)
{
    Projectile p;
    p.id = m_nextShotId++;
    p.fromDestiny = fromDestiny;
    p.targetId = targetId;
    p.pos = from;
    p.aimLocal = aimLocal;
    p.damage = fromDestiny ? damage * m_damageBonus : damage;
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

BattleView::Projectile *BattleView::findShot(int id)
{
    for (Projectile &p : m_projectiles)
        if (p.id == id && !p.dead)
            return &p;
    return nullptr;
}

void BattleView::fireRailguns(float dt)
{
    m_railgunTimer -= dt;
    if (m_railgunTimer > 0.0f)
        return;
    const float efficiency = std::max(0.25f, subsystemHealth("weapons") / 100.0f);
    m_railgunTimer = 0.3f / efficiency;
    const float damage = m_railgun.damage / 10.0f * efficiency;
    const QVector3D &turret = m_turrets[QRandomGenerator::global()->bounded(int(m_turrets.size()))];
    const QVector3D from = destinyWorld(turret);

    // 1. Ships on a kamikaze run
    const Enemy *diver = nullptr;
    float closest = m_railgun.reach * 1.5f;
    for (const Enemy &e : m_enemies)
        if (e.alive && e.diving && e.pos.length() < closest) {
            closest = e.pos.length();
            diver = &e;
        }
    if (diver) {
        shoot(true, from, diver->id, {}, damage, kSlugSpeed, kSlug, 1.2f, 0.05f);
        m_projectiles.back().kind = Projectile::Kind::Slug;
        m_projectiles.back().life = 1.5f;
        emit sound(SoundCue::DestinyRailgun, {}, 0.0f);
        return;
    }

    // 2. Point defense: closest incoming missile not already engaged twice
    int missileId = -1;
    QVector3D missilePos;
    float best = m_railgun.reach;
    for (const Projectile &p : m_projectiles) {
        if (p.dead || p.delay > 0.0f || p.kind != Projectile::Kind::Missile || p.fromDestiny)
            continue;
        const float distance = p.pos.length();
        const auto engaged = std::count_if(m_projectiles.begin(), m_projectiles.end(),
                                           [&](const Projectile &s) { return !s.dead && s.targetShot == p.id; });
        if (distance < best && engaged < 2) {
            best = distance;
            missileId = p.id;
            missilePos = p.pos;
        }
    }
    if (missileId >= 0) {
        shoot(true, from, 0, {}, damage, kSlugSpeed, kSlug, 1.2f, 0.05f);
        Projectile &slug = m_projectiles.back();
        slug.kind = Projectile::Kind::Slug;
        slug.targetShot = missileId;
        slug.life = 1.5f;
        slug.vel = (missilePos - from).normalized() * kSlugSpeed;
        emit sound(SoundCue::DestinyRailgun, {}, 0.0f);
        return;
    }

    // 3. Otherwise the closest fighter in range
    const Enemy *target = nullptr;
    best = m_railgun.reach * 1.2f;
    for (const Enemy &e : m_enemies) {
        if (!e.alive || e.cls != EnemyClass::Fighter)
            continue;
        const float distance = e.pos.length();
        if (distance < best) {
            best = distance;
            target = &e;
        }
    }
    if (target) {
        shoot(true, from, target->id, {}, damage, kSlugSpeed, kSlug, 1.2f, 0.05f);
        m_projectiles.back().kind = Projectile::Kind::Slug;
        m_projectiles.back().life = 1.5f;
        emit sound(SoundCue::DestinyRailgun, {}, 0.0f);
    }
}

void BattleView::updateProjectiles(float dt)
{
    // Impacts spawn effects / logs but never new projectiles: indexes
    // stay valid; destroyed projectiles are flagged and removed after.
    for (size_t i = 0; i < m_projectiles.size(); ++i) {
        Projectile &p = m_projectiles[i];
        if (p.dead)
            continue;
        if (p.delay > 0.0f) {
            p.delay -= dt;
            continue;
        }

        p.life -= dt;
        if (p.life <= 0.0f) {
            p.dead = true;
            continue;
        }

        // Railgun slug chasing a missile
        if (p.targetShot >= 0) {
            const float speed = p.vel.length();
            if (Projectile *missile = findShot(p.targetShot)) {
                const QVector3D toMissile = missile->pos - p.pos;
                const float distance = toMissile.length();
                if (distance <= speed * dt + 0.6f) {
                    missile->dead = true;
                    p.dead = true;
                    ++m_interceptions;
                    m_effects.push_back({missile->pos, QVector3D(1.0f, 0.8f, 0.5f), 0.0f, 0.35f, 0.9f});
                    emit sound(SoundCue::HullHit, {}, 0.0f);
                    continue;
                }
                if (distance > 1e-3f)
                    p.vel = toMissile / distance * speed;
            }
            p.pos += p.vel * dt;
            continue;
        }

        bool remove = false;

        Enemy *enemy = nullptr;
        bool hasTarget = false;
        QVector3D target;
        float hitRadius = 0.7f;
        Enemy *ally = nullptr;
        if (p.targetAlly >= 0) {
            ally = findAlly(p.targetAlly);
            if (!ally || !ally->alive) {
                p.dead = true;              // its target is gone
                continue;
            }
            target = ally->pos;
            hitRadius = ally->radius * 0.8f;
            hasTarget = true;
        } else if (p.targetId < 0) {
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
            if (ally) {
                if (distance <= speed * dt + hitRadius) {
                    hitAlly(*ally, p);
                    remove = true;
                }
            } else if (p.targetId < 0 && shieldProtecting() && insideShield(p.pos)) {
                if (p.kind == Projectile::Kind::Beam) {
                    // Half of a beam goes through the shield
                    Projectile half = p;
                    half.damage *= 0.5f;
                    hitShield(half);
                    hitHull(half);
                } else {
                    hitShield(p);
                }
                remove = true;
            } else if (distance <= speed * dt + hitRadius) {
                if (p.targetId < 0)
                    hitHull(p);
                else
                    hitEnemy(*enemy, p);
                remove = true;
            }
        }

        if (remove)
            p.dead = true;
    }
    m_projectiles.erase(std::remove_if(m_projectiles.begin(), m_projectiles.end(),
                                       [](const Projectile &p) { return p.dead; }),
                        m_projectiles.end());
}

void BattleView::hitEnemy(Enemy &e, const Projectile &p)
{
    float damage = p.damage;
    if (p.kind == Projectile::Kind::Slug)
        damage *= e.cls == EnemyClass::Fighter ? kSlugVsFighter : kSlugVsMothership;
    e.hp -= damage;
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
    emit sound(big ? SoundCue::BigExplosion : SoundCue::Explosion, {}, 0.0f);
    if (m_lockedId == e.id)
        m_lockedId = -1;
    emit logMessage(tr("T%1 %2 destroyed").arg(index + 1).arg(className(e)), LogLevel::Success);
    emit enemyDestroyed(big);
}

void BattleView::hitShield(const Projectile &p)
{
    const float absorb = 1.0f / std::max(0.4f, subsystemHealth("shields") / 100.0f);
    m_shieldStrength = std::max(0.0f, m_shieldStrength - p.damage * absorb);
    m_shieldFlash = 1.0f;
    m_lastShieldHit = m_time;
    m_effects.push_back({p.pos, kShieldColor, 0.0f, 0.3f, 0.9f});
    emit sound(SoundCue::ShieldHit, {}, 0.0f);
    m_hits.push_back({m_destinyInverse.map(p.pos), {m_time, true}});

    if (m_shieldStrength <= 0.0f && !m_shieldCollapsed) {
        m_shieldCollapsed = true;
        emit logMessage(tr("Shields collapsed!"), LogLevel::Critical);
    }
}

void BattleView::hitHull(const Projectile &p)
{
    const float before = m_hull;
    m_hull = std::max(0.0f, m_hull - p.damage);
    const bool missile = p.kind == Projectile::Kind::Missile;
    m_effects.push_back({p.pos, kExplosion, 0.0f, missile ? 0.8f : 0.45f, missile ? 2.2f : 0.9f});
    emit sound(missile ? SoundCue::Explosion : SoundCue::HullHit, {}, 0.0f);
    if (missile && m_time - m_lastMissileLog > 4.0f) {
        m_lastMissileLog = m_time;
        emit logMessage(tr("Missile impact on the hull (-%1)").arg(qRound(p.damage)), LogLevel::Warning);
    }
    damageSubsystemNear(p.aimLocal, p.damage * 1.6f);
    m_hits.push_back({p.aimLocal, {m_time, false}});
    if (m_fires.size() < kMaxFires && randRange(0, 1) < 0.35f)
        m_fires.push_back(p.aimLocal);

    for (float threshold : {75.0f, 50.0f, 25.0f}) {
        if (before > threshold && m_hull <= threshold)
            emit logMessage(tr("Hull integrity %1%").arg(qRound(m_hull)),
                            threshold > 30.0f ? LogLevel::Warning : LogLevel::Critical);
    }
    if (m_hull <= 0.0f) {
        m_hull = 35.0f;
        emit hullBreached();
        emit logMessage(tr("Hull breach! Emergency repair teams deployed"), LogLevel::Critical);
    }
}

QList<HitMark> BattleView::recentHits() const
{
    QList<HitMark> hits;
    for (const auto &[local, info] : m_hits)
        hits.append({local, m_time - info.first, info.second});
    return hits;
}

QList<BattleView::RadarBlip> BattleView::radarContacts() const
{
    QList<RadarBlip> blips;
    const std::vector<int> ids = aliveIds();
    for (size_t i = 0; i < ids.size(); ++i) {
        const Enemy *e = findEnemy(ids[i]);
        blips.append({m_destinyInverse.map(e->pos), int(i), e->cls == EnemyClass::Mothership,
                      e->id == m_lockedId, 100.0f * e->hp / e->maxHp, false});
    }
    for (const Enemy &a : m_allies)
        if (a.alive)
            blips.append({m_destinyInverse.map(a.pos), -1, a.cls == EnemyClass::Mothership, false,
                          100.0f * a.hp / a.maxHp, true});
    return blips;
}

QList<BattleView::RadarShot> BattleView::radarShots() const
{
    QList<RadarShot> shots;
    for (const Projectile &p : m_projectiles)
        if (p.delay <= 0.0f)
            shots.append({m_destinyInverse.map(p.pos), p.fromDestiny, p.kind == Projectile::Kind::Missile});
    return shots;
}

WeaponReadiness BattleView::weaponReadiness() const
{
    WeaponReadiness w;
    w.mainCooldown = m_mainCooldown;
    w.mainMax = kMainCooldown;
    w.pulseCooldown = m_pulseCooldown;
    w.pulseMax = kPulseCooldown;
    w.burstCooldown = m_secondaryCooldown;
    w.burstMax = kSecondaryCooldown;
    w.efficiency = subsystemHealth("weapons");
    w.hostiles = int(aliveIds().size());
    w.kills = m_kills;
    w.railgunReach = m_railgun.reach;
    w.interceptions = m_interceptions;
    w.hangar = m_hangar;
    w.hangarMax = m_hangarCapacity;
    w.fightersOut = int(std::count_if(m_allies.begin(), m_allies.end(), [](const Enemy &a) { return a.group == -1 && a.alive; }));
    w.allies = allyCount();
    w.incomingMissiles = int(std::count_if(m_projectiles.begin(), m_projectiles.end(), [](const Projectile &p) {
        return !p.dead && !p.fromDestiny && p.kind == Projectile::Kind::Missile;
    }));
    if (const Enemy *e = findEnemy(m_lockedId); e && e->alive) {
        w.hasTarget = true;
        w.targetIndex = indexOfId(e->id);
        w.targetClass = className(*e);
        w.targetHealth = 100.0f * e->hp / e->maxHp;
    }
    return w;
}

float BattleView::secondsSinceHit(const QString &key) const
{
    for (const Subsystem &s : m_subsystems)
        if (s.key == key)
            return m_time - s.lastHit;
    return 1000.0f;
}

void BattleView::damageSubsystemNear(const QVector3D &local, float amount)
{
    Subsystem *closest = &m_subsystems.front();
    for (Subsystem &candidate : m_subsystems)
        if ((candidate.localPos - local).lengthSquared() < (closest->localPos - local).lengthSquared())
            closest = &candidate;
    Subsystem &s = *closest;
    s.lastHit = m_time;
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

QString BattleView::className(const Enemy &e) const
{
    return classOf(e).name;
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
    drawAllies();

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

        const QVector3D tint = colorOf(classOf(e).color);
        const QVector3D dark = tint * 0.5f;
        if (e.cls == EnemyClass::Mothership) {
            drawMesh(m_ring, m, dark, flash, 1.0f, ModeHull);
            QMatrix4x4 top = m;
            top.translate(0, 0.1f, 0);
            top.rotate(45, 0, 1, 0);
            drawMesh(m_pyramid, top, tint, flash, 1.0f, ModeHull);
            QMatrix4x4 bottom = m;
            bottom.translate(0, -0.1f, 0);
            bottom.scale(1.0f, -0.45f, 1.0f);
            bottom.rotate(45, 0, 1, 0);
            drawMesh(m_pyramid, bottom, dark, flash, 1.0f, ModeHull);
        } else {
            drawMesh(m_fighter, m, tint, flash + (e.diving ? 0.4f : 0.0f), 1.0f, ModeHull);
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
        if (p.kind == Projectile::Kind::Missile) {
            // Pale body + flickering exhaust behind it
            QMatrix4x4 body = core;
            body.scale(p.width, p.width, p.length);
            drawMesh(m_box, body, QVector3D(0.9f, 0.9f, 0.85f), 0.8f, 1.0f, ModeGlow);
            const float flicker = 0.4f + 0.12f * std::sin(m_time * 30.0f + p.id);
            drawGlow(p.pos - p.vel.normalized() * p.length * 0.7f, flicker, p.color, 0.9f);
            drawGlow(p.pos - p.vel.normalized() * p.length * 1.4f, flicker * 1.8f, p.color, 0.2f);
            continue;
        }
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

    // --- Friendly ships: blue brackets and health bars
    const QColor blue(90, 170, 255);
    for (const Enemy &a : m_allies) {
        if (!a.alive)
            continue;
        QPointF center, edge;
        if (!project(a.pos, &center) || !project(a.pos + m_camRight * a.radius, &edge))
            continue;
        const qreal r = std::clamp(QLineF(center, edge).length(), 7.0, 60.0);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(blue, 1.4));
        p.drawLine(QPointF(center.x() - r, center.y() - r), QPointF(center.x() - r + 5, center.y() - r));
        p.drawLine(QPointF(center.x() - r, center.y() - r), QPointF(center.x() - r, center.y() - r + 5));
        p.drawLine(QPointF(center.x() + r, center.y() + r), QPointF(center.x() + r - 5, center.y() + r));
        p.drawLine(QPointF(center.x() + r, center.y() + r), QPointF(center.x() + r, center.y() + r - 5));
        const qreal w = std::max(30.0, r * 1.8);
        const QRectF bar(center.x() - w / 2, center.y() - r - 10, w, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 90, 90, 200));
        p.drawRect(bar);
        p.setBrush(blue);
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * a.hp / a.maxHp, bar.height()));
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
    p.drawText(QRectF(0, 46, width() - 14, 18), Qt::AlignRight,     // below the MENU button
               ids.empty() ? tr("NO CONTACT") : tr("HOSTILES: %1").arg(ids.size()));
    // --- Quest objectives (top center)
    if (!m_objectiveLines.isEmpty()) {
        const qreal lineHeight = 16;
        const qreal boxWidth = std::min(460.0, width() - 600.0 > 300.0 ? 460.0 : width() * 0.5);
        const QRectF box((width() - boxWidth) / 2, 12, boxWidth, lineHeight * m_objectiveLines.size() + 10);
        p.setPen(QPen(QColor(240, 200, 60, 160), 1));
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, 4, 4);
        for (int i = 0; i < m_objectiveLines.size(); ++i) {
            const QString &text = m_objectiveLines.at(i);
            const bool done = text.startsWith("[x]");
            p.setPen(i == 0 ? QColor(240, 200, 60)
                     : text.startsWith('>') ? QColor(150, 210, 255)
                     : done ? QColor(120, 200, 120) : QColor(225, 225, 215));
            p.drawText(QRectF(box.left() + 10, box.top() + 5 + i * lineHeight, box.width() - 20, lineHeight),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(text, Qt::ElideRight, int(box.width() - 20)));
        }
    }

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
