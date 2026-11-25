#include "battleview.h"
#include "ddstexture.h"
#include "modmesh.h"
#include "src/core/assets.h"
#include "src/core/balance.h"
#include "src/core/gameconfig.h"
#include "src/Menu/uipalette.h"
#include "src/db/dbtext.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFontDatabase>
#include <QHash>
#include <QLineF>
#include <QMap>
#include <QMutex>
#include <QRegularExpression>
#include <QSettings>
#include <QThreadPool>
#include <QMouseEvent>
#include <QPainter>
#include <QQuaternion>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QTimer>
#include <QVector4D>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

// ================================================================
//  Shaders
// ================================================================

namespace {

using namespace Balance;               // all the tunable values: src/core/balance.h

const char *kLitVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aTangent;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vObjPos;
out vec2 vUv;
out vec3 vTangent;
out float vSign;
void main() {
    vUv = aUv;
    vTangent = uNormalMat * aTangent.xyz;
    vSign = aTangent.w < 0.0 ? -1.0 : 1.0;
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
in vec2 vUv;
in vec3 vTangent;
in float vSign;
uniform sampler2D uTexture;
uniform sampler2D uMask;
uniform sampler2D uOrm;
uniform sampler2D uNormalMap;
uniform int uTextured;      // hull with the textures of a mod model
uniform int uHasMask;
uniform int uHasOrm;
uniform int uHasNormal;
uniform vec3 uLightColor;   // color of the ship lights (faction)
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

    // Textured hull (mod model): color, relief, metal and lights
    if (uTextured == 1) {
        vec3 base = texture(uTexture, vUv).rgb;
        vec3 n = N;
        if (uHasNormal == 1) {
            vec2 relief = texture(uNormalMap, vUv).rg * 2.0 - 1.0;
            relief.y = -relief.y;                   // DirectX normal maps
            float z = sqrt(max(0.0, 1.0 - dot(relief, relief)));
            vec3 T = normalize(vTangent - N * dot(N, vTangent));
            vec3 B = cross(N, T) * vSign;
            n = normalize(T * relief.x + B * relief.y + N * z);
        }
        float ao = 1.0, rough = 0.7, metal = 0.2;
        if (uHasOrm == 1) {
            vec3 orm = texture(uOrm, vUv).rgb;
            ao = orm.r;
            rough = orm.g;
            metal = orm.b;
        }
        float diff = max(dot(n, L), 0.0);
        vec3 H = normalize(L + V);
        vec3 specColor = mix(vec3(0.06), base, metal);
        vec3 spec = specColor * pow(max(dot(n, H), 0.0), mix(96.0, 6.0, rough)) * (1.0 - 0.7 * rough) * 2.5;
        vec3 back = vec3(0.12, 0.18, 0.3) * max(dot(n, -L), 0.0) * 0.5;
        // No environment map: metal gets a soft fake reflection of the sky
        float fresnel = pow(1.0 - max(dot(n, V), 0.0), 3.0);
        vec3 env = specColor * (0.35 + 0.65 * fresnel) * (1.0 - 0.6 * rough) * 0.8 * ao;
        vec3 col = base * (1.0 - 0.2 * metal) * (0.45 * ao + diff * 1.35) + spec + env + back * ao;
        if (uHasMask == 1)
            col += texture(uMask, vUv).b * uLightColor * 2.2;   // lights, even on the dark side
        fragColor = vec4(col + uColor * uEmissive, uAlpha);
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
const QVector3D kDroneColor(1.0f, 0.82f, 0.35f);       // Ancient drones: golden
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
    if (faction == "Nakai") return {0.55f, 1.0f, 0.35f};
    if (faction == "Ursini") return {0.85f, 0.6f, 1.0f};
    return kEnemyBolt;
}

QColor healthColor(float pct)
{
    return UiPalette::health(pct);
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
        {"sensors",     tr("Sensors"),      { 3.4f, 0.9f, -0.6f}, { 150, -150}},
    };

    for (float a : {150.0f, 165.0f, 195.0f, 210.0f}) {
        const float r = qDegreesToRadians(a);
        m_turrets.emplace_back(kFanCenter.x() + 4.0f * std::cos(r), 0.4f, 4.0f * std::sin(r));
    }
    m_turrets.emplace_back(3.0f, 0.55f, 0.35f);
    m_turrets.emplace_back(3.0f, 0.55f, -0.35f);

    step(0.0f);
    spawnWave(false);
    // The log of a battle goes into its replay
    connect(this, &BattleView::logMessage, this, [this](const QString &text, LogLevel) {
        if (m_inBattle && m_replayEvents.size() < 6)
            m_replayEvents << text;
    });

    connect(m_timer, &QTimer::timeout, this, [this] {
        const float dt = std::min(0.05f, float(m_clock.restart()) / 1000.0f) * gameConfig().gameSpeed;
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
    for (auto &[key, model] : m_modModels)
        if (model) {
            model->mesh.vao.destroy();
            model->mesh.vbo.destroy();
            for (GLuint texture : {model->textures.color, model->textures.mask, model->textures.orm,
                                   model->textures.normal})
                if (texture)
                    glDeleteTextures(1, &texture);
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
    if (m_lockedId != ids[enemyIndex])
        m_aimSystem = -1;
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

    if (QString::fromLatin1(m_loadout.mainName).contains(QLatin1String("beam"), Qt::CaseInsensitive)) {
        // Asgard / Ori beam: a continuous ray, not a bolt
        Ray ray;
        ray.fromDestiny = true;
        ray.sourceLocal = kMainTowerLocal;
        ray.targetEnemy = target->id;
        ray.color = QString::fromLatin1(m_loadout.mainName).startsWith(QLatin1String("Ori"))
                        ? QVector3D(1.0f, 0.85f, 0.55f) : kBeamColor;
        ray.width = 0.35f;
        ray.weapon = Weapon::Main;
        fireRay(ray, m_loadout.mainDamage * m_damageBonus);
    } else {
        shoot(true, destinyWorld(kMainTowerLocal), target->id, {}, m_loadout.mainDamage, 48.0f,
              QVector3D(0.7f, 0.92f, 1.0f), 3.2f, 0.32f);
        m_projectiles.back().weapon = Weapon::Main;
        m_projectiles.back().aimSystem = m_aimSystem;
    }
    m_mainCooldown = kMainCooldown;
    emit sound(SoundCue::DestinyMainGun, {}, 0.0f);
    emit logMessage(tr("Main tower fired at T%1").arg(indexOfId(target->id) + 1), LogLevel::Success);
    return true;
}

void BattleView::setLoadout(const Balance::FlagshipLoadout &loadout)
{
    m_loadout = loadout;
    m_turretsOn = loadout.towers;
    m_railgunsOn = loadout.railguns;
}

void BattleView::setDroneBay(int salvos)
{
    m_droneBay = std::max(0, salvos);
    m_drones = m_droneBay;
}

bool BattleView::fireDrones(int enemyIndex)
{
    if (m_droneBay == 0) {
        emit logMessage(tr("No drone bay aboard"), LogLevel::Warning);
        return false;
    }
    if (enemyIndex >= 0 && !lockTarget(enemyIndex))
        return false;
    if (m_drones == 0) {
        emit logMessage(tr("Drone bay empty - drones are only found in Ancient caches"), LogLevel::Warning);
        return false;
    }
    if (m_dronesInFlight == 0 && freePower() < kDronePower) {
        emit logMessage(tr("Not enough power to guide the drones (%1 free, %2 needed) - turn off the turrets or the railguns")
                            .arg(freePower()).arg(kDronePower),
                        LogLevel::Warning);
        return false;
    }
    const Enemy *target = findEnemy(m_lockedId);
    if (!target || !target->alive) {
        target = pickTarget();
        if (!target) {
            emit logMessage(tr("Drones: no target"), LogLevel::Warning);
            return false;
        }
        m_lockedId = target->id;
    }
    for (int i = 0; i < kPlayerDronesPerSalvo; ++i) {
        const QVector3D from = destinyWorld({randRange(-2.0f, 3.0f), randRange(0.2f, 0.8f), randRange(-1.5f, 1.5f)});
        shoot(true, from, target->id, {}, kPlayerDroneDamage, kMissileSpeed * 2.2f, kDroneColor, 0.5f, 0.14f,
              i * 0.12f);
        m_projectiles.back().kind = Projectile::Kind::Missile;
        m_projectiles.back().drone = true;
        m_projectiles.back().life = 10.0f;
        m_projectiles.back().weapon = Weapon::Drones;
        m_projectiles.back().aimSystem = m_aimSystem;
    }
    --m_drones;
    emit sound(SoundCue::EnemyMissile, QStringLiteral("Ancients"), 0.0f);
    emit logMessage(tr("Drone salvo launched at T%1 (%2 left)").arg(indexOfId(target->id) + 1).arg(m_drones),
                    LogLevel::Success);
    return true;
}

bool BattleView::firePulseVolley()
{
    if (!m_loadout.pulse) {
        emit logMessage(tr("No pulse weapons aboard this ship"), LogLevel::Warning);
        return false;
    }
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
            m_projectiles.back().weapon = Weapon::Pulse;
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
    if (!m_loadout.towers) {
        emit logMessage(tr("No secondary towers aboard this ship"), LogLevel::Warning);
        return false;
    }
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
        m_projectiles.back().weapon = Weapon::Towers;
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
    if (key == "sensor" || key == "sensors") return QStringLiteral("sensors");
    if (key == "hull") return QStringLiteral("hull");
    return QString();
}

QStringList BattleView::systemKeys()
{
    return {"engines", "shields", "weapons", "lifesupport", "sensors"};
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

void BattleView::clearBattlefield(float calmSeconds, bool keepAllies)
{
    for (Enemy &e : m_enemies)
        e.alive = false;
    m_enemies.clear();
    if (!keepAllies)
        clearAllies();
    m_projectiles.clear();
    m_rays.clear();
    m_effects.clear();
    m_lockedId = -1;
    m_aimSystem = -1;
    m_waveTimer = calmSeconds;
    m_jumpCountdown = -1.0f;
    m_inBattle = false;                 // left behind: no summary
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
    emit logMessage(tr("Unknown system '%1' (engines, shields, weapons, lifesupport, sensors, hull)").arg(name),
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

    QString report = tr("Main tower: %1\nPulse weapons: %2\nSecondary towers: %3 (auto-fire, %4% efficiency)\n"
                        "Current target: %5 | Kills: %6")
                         .arg(state(m_mainCooldown), state(m_pulseCooldown), state(m_secondaryCooldown))
                         .arg(qRound(subsystemHealth("weapons")))
                         .arg(target)
                         .arg(m_kills);
    if (const Enemy *e = findEnemy(m_lockedId); e && e->alive) {
        report += tr("\nHit chance on the target: %1% (distance %2 km, sensors %3%)")
                      .arg(qRound(100.0f * hitChance(destinyWorld(kMainTowerLocal), e->pos, true, e)
                                  * (m_aimSystem >= 0 ? kSubsystemAim : 1.0f)))
                      .arg(qRound(e->pos.length() * 10.0f))
                      .arg(qRound(subsystemHealth("sensors")));
        report += tr("\nTarget systems: weapons %1%, shields %2%, engines %3%, hangar %4%")
                      .arg(qRound(e->systems[0])).arg(qRound(e->systems[1])).arg(qRound(e->systems[2])).arg(qRound(e->systems[3]));
    }
    if (m_loadout.railguns)
        report += tr("\nRailguns: %1 rounds, heat %2%%3").arg(m_railgunAmmo).arg(qRound(m_railgunHeat))
                      .arg(m_railgunOverheated ? tr(" (OVERHEATED)") : QString());
    if (m_missileBay > 0)
        report += tr("\nMissiles: %1/%2").arg(m_missiles).arg(m_missileBay);
    if (m_droneBay > 0)
        report += tr("\nDrone salvos: %1/%2").arg(m_drones).arg(m_droneBay);
    return report;
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

    // Evasive maneuvers: the ship weaves sideways and rolls
    m_evadeBlend = std::clamp(m_evadeBlend + (m_evading ? dt : -dt) * 0.5f, 0.0f, 1.0f);
    m_destinyModel.setToIdentity();
    m_destinyModel.translate(0.0f, 0.25f * std::sin(m_time * 0.5f) + 0.8f * m_evadeBlend * std::sin(m_time * 1.3f),
                             2.6f * m_evadeBlend * std::sin(m_time * 0.7f));
    m_destinyModel.rotate(30.0f, 0, 1, 0);
    m_destinyModel.rotate(3.0f * std::sin(m_time * 0.3f) + 14.0f * m_evadeBlend * std::cos(m_time * 0.7f), 1, 0, 0);
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
    // Battle statistics, summary when the last enemy falls
    if (!aliveIds().empty()) {
        if (!m_inBattle) {
            m_inBattle = true;
            m_stats = BattleStats();
            m_stats.faction = m_waveFaction;
            m_stats.interceptions = m_interceptions;    // baseline
            m_replay.clear();
            m_replayClock = 0;
            m_weaponHits.clear();
            m_adaptedWeapon = -1;
        }
        m_stats.seconds += dt;
        m_replayClock += dt;
        if (m_replayClock >= 0.1f) {
            m_replayClock = 0;
            recordFrame();
        }
    } else if (m_inBattle) {
        finishBattle();
    }
    m_dronesInFlight = int(std::count_if(m_projectiles.begin(), m_projectiles.end(), [](const Projectile &p) {
        return !p.dead && p.drone && p.fromDestiny && !p.fromAlly;
    }));

    // FTL drive spooling under fire
    if (m_jumpCountdown >= 0.0f) {
        const float before = m_jumpCountdown;
        m_jumpCountdown -= dt;
        if (std::ceil(before) != std::ceil(m_jumpCountdown) && m_jumpCountdown > 0.0f && m_jumpCountdown <= 3.0f)
            emit logMessage(tr("FTL jump in %1...").arg(int(std::ceil(m_jumpCountdown))), LogLevel::Info);
        if (m_jumpCountdown <= 0.0f) {
            m_jumpCountdown = -1.0f;
            emit jumpReady();
        }
    }

    // Railguns cool down when they do not fire
    m_railgunHeat = std::max(0.0f, m_railgunHeat - kRailgunCooling * dt);
    if (m_railgunOverheated && m_railgunHeat <= kRailgunRestart) {
        m_railgunOverheated = false;
        emit logMessage(tr("Railguns cooled down - point defense back"), LogLevel::Success);
    }
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
    if (m_autoFireTimer <= 0.0f && m_turretsOn) {
        const float efficiency = std::max(0.25f, subsystemHealth("weapons") / 100.0f);
        m_autoFireTimer = kAutoFireDelay / efficiency;
        if (const Enemy *t = pickTarget()) {
            const QVector3D &turret = m_turrets[QRandomGenerator::global()->bounded(int(m_turrets.size()))];
            shoot(true, destinyWorld(turret), t->id, {}, m_cannon.damage / 17.0f * efficiency, 38.0f,
                  kDestinyBolt, 1.0f, 0.09f);
            m_projectiles.back().weapon = Weapon::Towers;
            emit sound(SoundCue::DestinyTurret, {}, 0.0f);
        }
    }

    // Not enough power (leaks, drones, evasion): the lowest priorities are shed
    {
        int need = kMainTowerPower + kEnginePower + (m_turretsOn && m_loadout.towers ? kTurretPower : 0)
                 + (m_railgunsOn && m_loadout.railguns ? kRailgunPower : 0) + (m_shieldOrdered ? kShieldPower : 0)
                 + (m_dronesInFlight > 0 ? kDronePower : 0) + (m_evading ? kEvasionPower : 0);
        const int available = kCorePower + m_powerBonus - m_powerLoss;
        QStringList shed;
        if (need > available && m_turretsOn && m_loadout.towers) {
            m_turretsOn = false;
            need -= kTurretPower;
            shed << tr("auto-turrets");
        }
        if (need > available && m_evading) {
            m_evading = false;
            need -= kEvasionPower;
            shed << tr("evasive maneuvers");
        }
        if (need > available && m_railgunsOn && m_loadout.railguns) {
            m_railgunsOn = false;
            need -= kRailgunPower;
            shed << tr("railguns");
        }
        if (!shed.isEmpty())
            emit logMessage(tr("Power shortage (%1 available) - shed: %2").arg(available).arg(shed.join(", ")),
                            LogLevel::Critical);
    }

    if (m_railgunsOn)
        fireRailguns(dt);

    const float recharge = dt * weaponPowerBoost();         // spare power speeds up the recharge
    m_mainCooldown = std::max(0.0f, m_mainCooldown - recharge);
    m_pulseCooldown = std::max(0.0f, m_pulseCooldown - recharge);
    m_secondaryCooldown = std::max(0.0f, m_secondaryCooldown - recharge);
    m_shieldFlash = std::max(0.0f, m_shieldFlash - dt * 2.0f);
    m_shake = std::max(0.0f, m_shake - dt * 2.5f);
    if (m_observing && !aliveIds().empty())
        setObservation(false);              // contacts: back to the tactical view

    // Organic hull (Wraith): it heals itself, even in battle
    if (m_loadout.hullRegen > 0.0f && m_hull > 0.0f)
        m_hull = std::min(100.0f, m_hull + m_loadout.hullRegen * dt);

    // Shield regeneration (slower while under fire)
    if (m_shieldOrdered) {
        const float base = m_time - m_lastShieldHit > 1.5f ? kPlayerShieldRegen : kPlayerShieldRegenUnderFire;
        const float rate = base * m_shieldRegenBonus * shieldPowerBoost()
                         * std::max(0.2f, subsystemHealth("shields") / 100.0f);
        m_shieldStrength = std::min(100.0f, m_shieldStrength + rate * dt);
        if (m_shieldCollapsed && m_shieldStrength >= 35.0f) {
            m_shieldCollapsed = false;
            emit logMessage(tr("Shields restored (%1%)").arg(qRound(m_shieldStrength)), LogLevel::Success);
        }
    }

    updateProjectiles(dt);
    updateRays(dt);

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

// Line ships first: the heavy flagships of a faction (O'Neill, Aurora…)
// only come when it has nothing lighter
void BattleView::preferLineShips(QList<int> &motherships) const
{
    QList<int> line;
    for (int i : motherships)
        if (m_classes[i].hull <= 650)
            line << i;
    if (!line.isEmpty())
        motherships = line;
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
    preferLineShips(big);
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
    setupShip(a);
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

void BattleView::removeAllyGroup(int group, bool jumpOut)
{
    for (Enemy &a : m_allies)
        if (a.alive && a.group == group) {
            if (jumpOut)
                hyperspaceWindow(a.pos + a.facing * a.radius * 2.0f, a.cls == EnemyClass::Mothership ? 1.6f : 0.7f);
            a.alive = false;
        }
    m_allies.erase(std::remove_if(m_allies.begin(), m_allies.end(), [](const Enemy &a) { return !a.alive; }),
                   m_allies.end());
}

bool BattleView::spawnAllies(const QString &faction, int motherships, int fighters, int group, bool announce)
{
    constexpr int kBossHull = 900;
    QList<int> big, small;
    for (int i = 0; i < m_classes.size(); ++i) {
        const ShipClass &c = m_classes[i];
        if (c.faction == faction && c.hull < kBossHull)
            (c.isMothership() ? big : small) << i;
    }
    preferLineShips(big);
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
    if (announce)
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
    m_launchedByOrder += launched;
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
    regenShield(a, dt);
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
    if (a.group == -1 && m_jumpCountdown >= 0.0f)
        target = nullptr;                   // recalled: they fly back to dock
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
        // Hangar fighter: stays outside, patrolling close to the ship,
        // until it docks for an FTL jump
        a.idle += dt;
        const float angle = m_time * 0.5f + a.phase;
        goal = destinyWorld({2.0f + 7.0f * std::cos(angle), 1.5f + std::sin(angle * 1.3f), 7.0f * std::sin(angle)});
        if (m_jumpCountdown >= 0.0f)
            goal = destinyWorld({0.0f, -0.6f, 0.0f});           // back to the hangar bay
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
        if (randRange(0.0f, 1.0f) <= attacks[i].chance && useAmmo(a, i))
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
        p.weapon = Weapon::Ally;
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
            shoot(true, a.pos, target.id, {}, attack.damage, kMissileSpeed * 1.3f,
                  isDrone(attack) ? kDroneColor : kMissileExhaust, 0.6f, 0.16f, i * 0.4f);
            fired(Projectile::Kind::Missile);
            m_projectiles.back().drone = isDrone(attack);
            m_projectiles.back().life = 12.0f;
        }
        emit sound(SoundCue::EnemyMissile, sc.faction, 0.0f);
    } else if (attack.kind == "beam") {
        Ray ray;
        ray.fromDestiny = true;
        ray.sourceAlly = a.id;
        ray.targetEnemy = target.id;
        ray.color = sc.faction == "Ori" ? QVector3D(1.0f, 0.85f, 0.55f) : kBeamColor;
        ray.width = big ? 0.32f : 0.18f;
        ray.weapon = Weapon::Ally;
        fireRay(ray, attack.damage);
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
    const float damage = absorbByShield(a, p, p.damage);
    if (damage <= 0.0f)
        return;
    a.hp -= damage;
    a.hitFlash = 1.0f;
    m_effects.push_back({p.pos, kExplosion, 0.0f, 0.3f, 0.5f + p.width * 2.0f});
    if (a.hp > 0.0f)
        return;
    a.alive = false;
    const bool big = a.cls == EnemyClass::Mothership;
    m_effects.push_back({a.pos, kExplosion, 0.0f, big ? 1.6f : 0.9f, big ? 6.0f : 2.2f});
    emit sound(big ? SoundCue::BigExplosion : SoundCue::Explosion, {}, 0.0f);
    ++m_stats.alliesLost;
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
        // Friendly ships light up in blue, whatever their faction
        if (drawModShip(meshKeyOf(classOf(a)), m, a.cls == EnemyClass::Mothership ? 9.0f : 2.6f, tint, flash,
                        QVector3D(0.35f, 0.7f, 1.0f))) {
            drawEngineSpots(meshKeyOf(classOf(a)), m, a.cls == EnemyClass::Mothership ? 9.0f : 2.6f,
                            QVector3D(0.35f, 0.7f, 1.0f), 1.0f);
            continue;
        }
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

// ================================================================
//  Tactics: subsystem aiming, missiles, evasion, FTL under fire
// ================================================================

QStringList BattleView::enemySystems()
{
    return {"weapons", "shields", "engines", "hangar"};
}

bool BattleView::aimAt(int enemyIndex, const QString &system)
{
    const QString key = system.trimmed().toLower();
    int index = -1;
    if (!key.isEmpty() && key != "hull") {
        index = int(enemySystems().indexOf(key.endsWith('s') || key == "hangar" ? key : key + 's'));
        if (index < 0) {
            emit logMessage(tr("Unknown subsystem '%1' (weapons, shields, engines, hangar, hull)").arg(system),
                            LogLevel::Warning);
            return false;
        }
    }
    if (!lockTarget(enemyIndex))
        return false;
    m_aimSystem = index;
    const Enemy *e = findEnemy(m_lockedId);
    if (index < 0)
        emit logMessage(tr("Aiming at the hull of T%1").arg(enemyIndex + 1), LogLevel::Info);
    else
        emit logMessage(tr("Aiming at the %1 of T%2 (%3%) - harder to hit")
                            .arg(tr(qPrintable(enemySystems().at(index))))
                            .arg(enemyIndex + 1)
                            .arg(qRound(e->systems[index])),
                        LogLevel::Info);
    return true;
}

void BattleView::setMissileBay(int capacity, int stock)
{
    m_missileBay = std::max(0, capacity);
    m_missiles = std::clamp(stock, 0, m_missileBay);
}

bool BattleView::fireMissiles(int enemyIndex)
{
    if (m_missileBay == 0) {
        emit logMessage(tr("No missile bay aboard this ship"), LogLevel::Warning);
        return false;
    }
    if (enemyIndex >= 0 && !lockTarget(enemyIndex))
        return false;
    if (m_missiles == 0) {
        emit logMessage(tr("Missile bay empty - resupply on a naquadah-rich planet"), LogLevel::Warning);
        return false;
    }
    const Enemy *target = findEnemy(m_lockedId);
    if (!target || !target->alive) {
        target = pickTarget();
        if (!target) {
            emit logMessage(tr("Missiles: no target"), LogLevel::Warning);
            return false;
        }
        m_lockedId = target->id;
    }
    const int salvo = std::min(2, m_missiles);
    for (int i = 0; i < salvo; ++i) {
        shoot(true, destinyWorld({randRange(-2.0f, 1.0f), -0.2f, randRange(-1.0f, 1.0f)}), target->id, {},
              kPlayerMissileDamage, kMissileSpeed * 1.6f, kMissileExhaust, 0.7f, 0.18f, i * 0.35f);
        Projectile &p = m_projectiles.back();
        p.kind = Projectile::Kind::Missile;
        p.life = 12.0f;
        p.weapon = Weapon::Missiles;
        p.aimSystem = m_aimSystem;
    }
    m_missiles -= salvo;
    emit sound(SoundCue::EnemyMissile, {}, 0.0f);
    emit logMessage(tr("%1 missile(s) launched at T%2 (%3 left)").arg(salvo).arg(indexOfId(target->id) + 1).arg(m_missiles),
                    LogLevel::Success);
    return true;
}

void BattleView::setEvasion(bool on)
{
    if (on && subsystemHealth("engines") <= 20.0f) {
        emit logMessage(tr("Engines too damaged for evasive maneuvers"), LogLevel::Warning);
        return;
    }
    m_evading = on;
    emit logMessage(on ? tr("Evasive maneuvers: enemy fire less accurate, yours too (-%1 power)").arg(kEvasionPower)
                       : tr("Evasive maneuvers stopped"),
                    LogLevel::Info);
}

void BattleView::repairBy(const QString &key, float amount)
{
    if (key == "hull") {
        m_hull = std::min(100.0f, m_hull + amount);
        if (!m_fires.empty() && amount >= 5.0f)
            m_fires.erase(m_fires.begin() + QRandomGenerator::global()->bounded(int(m_fires.size())));
        return;
    }
    if (Subsystem *s = subsystem(key)) {
        s->health = std::min(100.0f, s->health + amount);
        emit subsystemChanged(s->key, s->health);
    }
}

void BattleView::startJumpSequence(float seconds)
{
    m_jumpCountdown = std::max(0.0f, seconds);
    const int out = int(std::count_if(m_allies.begin(), m_allies.end(),
                                      [](const Enemy &a) { return a.alive && a.group == -1; }));
    emit logMessage(out > 0 ? tr("FTL drive spooling: %1 s - %2 fighter(s) recalled to the hangar").arg(qRound(seconds)).arg(out)
                            : tr("FTL drive spooling: %1 s - hold on").arg(qRound(seconds)),
                    LogLevel::Warning);
}

void BattleView::abortJumpSequence()
{
    if (m_jumpCountdown < 0.0f)
        return;
    m_jumpCountdown = -1.0f;
    emit logMessage(tr("FTL jump aborted"), LogLevel::Info);
}

int BattleView::dockForJump()
{
    int lost = 0, docked = 0;
    const QVector3D bay = destinyWorld({0.0f, -0.6f, 0.0f});
    for (Enemy &a : m_allies) {
        if (!a.alive || a.group != -1)
            continue;
        a.alive = false;
        if ((a.pos - bay).length() > kDockRange)
            ++lost;                         // too far: left behind
        else
            ++docked;
    }
    m_hangar = std::min(m_hangarCapacity, m_hangar + docked);
    m_relaunch = std::min(m_launchedByOrder, docked);
    m_launchedByOrder = 0;
    m_allies.erase(std::remove_if(m_allies.begin(), m_allies.end(), [](const Enemy &a) { return !a.alive; }),
                   m_allies.end());
    if (lost > 0)
        emit logMessage(tr("%1 fighter(s) too far from the ship were left behind").arg(lost), LogLevel::Critical);
    return lost;
}

void BattleView::relaunchAfterJump()
{
    const int count = m_relaunch;
    m_relaunch = 0;
    if (count > 0)
        launchFighters(count);
}

void BattleView::setObservation(bool on)
{
    if (on && !aliveIds().empty()) {
        emit logMessage(tr("Not with hostile contacts around"), LogLevel::Warning);
        return;
    }
    m_observing = on;
    update();
}

void BattleView::setFireSpots(const QList<QVector3D> &spots)
{
    m_externalFires = true;
    m_fires.assign(spots.begin(), spots.end());
}

bool BattleView::canBoard(int enemyIndex, QString *why) const
{
    const std::vector<int> ids = aliveIds();
    if (enemyIndex < 0 || enemyIndex >= int(ids.size())) {
        if (why) *why = tr("no contact T%1").arg(enemyIndex + 1);
        return false;
    }
    const Enemy *e = findEnemy(ids[enemyIndex]);
    if (e->cls != EnemyClass::Mothership) {
        if (why) *why = tr("only motherships can be boarded");
        return false;
    }
    if (e->shield > 1.0f) {
        if (why) *why = tr("its shield is still up (%1%)").arg(qRound(100.0f * e->shield / std::max(1.0f, e->maxShield)));
        return false;
    }
    if (e->hp > 0.35f * e->maxHp) {
        if (why) *why = tr("not crippled enough (hull %1%, needs 35%)").arg(qRound(100.0f * e->hp / e->maxHp));
        return false;
    }
    if (e->pos.length() > 45.0f) {
        if (why) *why = tr("too far for the shuttles");
        return false;
    }
    return true;
}

QString BattleView::captureEnemy(int enemyIndex)
{
    const std::vector<int> ids = aliveIds();
    if (enemyIndex < 0 || enemyIndex >= int(ids.size()))
        return QString();
    Enemy *e = findEnemy(ids[enemyIndex]);
    e->alive = false;
    ++m_kills;
    ++m_stats.kills;
    ++m_stats.motherships;
    if (m_lockedId == e->id)
        m_lockedId = -1;
    hyperspaceWindow(e->pos, 1.2f);
    emit logMessage(tr("T%1 %2 captured - its crew surrendered").arg(enemyIndex + 1).arg(className(*e)), LogLevel::Success);
    emit enemyDestroyed(true);
    return classOf(*e).faction;
}

void BattleView::recordFrame()
{
    constexpr int kMaxFrames = 3000;        // 5 minutes
    if (m_replay.size() >= kMaxFrames)
        return;
    ReplayFrame f;
    f.time = m_stats.seconds;
    f.hull = m_hull;
    f.shield = shieldProtecting() ? m_shieldStrength : 0.0f;
    const QVector3D center = destinyWorld({});
    const QVector3D bow = destinyWorld({1, 0, 0}) - center;
    f.ships.append({QPointF(center.x(), center.z()), QPointF(bow.x(), bow.z()), true, 0, m_hull});
    for (const Enemy &e : m_enemies)
        if (e.alive)
            f.ships.append({QPointF(e.pos.x(), e.pos.z()), QPointF(e.facing.x(), e.facing.z()),
                            e.cls == EnemyClass::Mothership, 1, 100.0f * e.hp / e.maxHp});
    for (const Enemy &a : m_allies)
        if (a.alive)
            f.ships.append({QPointF(a.pos.x(), a.pos.z()), QPointF(a.facing.x(), a.facing.z()),
                            a.cls == EnemyClass::Mothership, 2, 100.0f * a.hp / a.maxHp});
    for (const Projectile &p : m_projectiles)
        if (!p.dead && p.delay <= 0.0f)
            f.shots.append({QPointF(p.pos.x(), p.pos.z()), p.fromDestiny, p.kind == Projectile::Kind::Missile, false});
    for (const Ray &r : m_rays) {
        const QVector3D a = rayStart(r), b = rayEnd(r);
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            const QVector3D p = a + (b - a) * t;
            f.shots.append({QPointF(p.x(), p.z()), r.fromDestiny, false, true});
        }
    }
    f.events = m_replayEvents;
    m_replayEvents.clear();
    m_replay << f;
}

void BattleView::finishBattle()
{
    m_inBattle = false;
    recordFrame();
    m_lastReplay = m_replay;
    m_lastReplayFaction = m_stats.faction;
    m_replay.clear();
    m_stats.interceptions = m_interceptions - m_stats.interceptions;
    const int accuracy = m_stats.shots > 0 ? qRound(100.0f * m_stats.hits / m_stats.shots) : 0;
    emit logMessage(tr("Battle report (%1): %2 s, %3 kill(s), accuracy %4% (%5/%6), damage dealt %7, "
                       "hull -%8%, shields -%9%")
                        .arg(m_stats.faction)
                        .arg(qRound(m_stats.seconds))
                        .arg(m_stats.kills)
                        .arg(accuracy)
                        .arg(m_stats.hits)
                        .arg(m_stats.shots)
                        .arg(qRound(m_stats.damageDealt))
                        .arg(qRound(m_stats.hullLost))
                        .arg(qRound(m_stats.shieldLost)),
                    LogLevel::Info);
    emit battleEnded(m_stats);
}

// ================================================================
//  Continuous beams
// ================================================================

void BattleView::fireRay(Ray ray, float damage)
{
    ray.damageLeft = damage;
    const QVector3D from = rayStart(ray);
    const QVector3D to = rayEnd(ray);
    const bool fromPlayer = ray.fromDestiny && ray.sourceAlly < 0;
    const Enemy *target = ray.targetEnemy >= 0 ? findEnemy(ray.targetEnemy) : nullptr;
    float chance = hitChance(from, to, fromPlayer, target);
    if (fromPlayer && m_aimSystem >= 0)
        chance *= kSubsystemAim;
    if (fromPlayer)
        ++m_stats.shots;
    if (randRange(0.0f, 1.0f) > chance) {
        // Misses: the ray sweeps past its target
        const QVector3D dir = (to - from).normalized();
        QVector3D side = QVector3D::crossProduct(dir, {0, 1, 0});
        if (side.lengthSquared() < 1e-4f)
            side = {1, 0, 0};
        ray.missOffset = side.normalized() * randRange(3.0f, 5.0f) * (randRange(0, 1) < 0.5f ? -1.0f : 1.0f);
    }
    m_rays.push_back(ray);
}

QVector3D BattleView::rayStart(const Ray &r) const
{
    if (r.sourceEnemy >= 0)
        if (const Enemy *e = findEnemy(r.sourceEnemy))
            return e->pos;
    if (r.sourceAlly >= 0)
        for (const Enemy &a : m_allies)
            if (a.id == r.sourceAlly)
                return a.pos;
    return destinyWorld(r.sourceLocal);
}

QVector3D BattleView::rayEnd(const Ray &r) const
{
    QVector3D end;
    if (r.targetEnemy >= 0) {
        const Enemy *e = findEnemy(r.targetEnemy);
        end = e ? e->pos : rayStart(r);
    } else if (r.targetAlly >= 0) {
        end = rayStart(r);
        for (const Enemy &a : m_allies)
            if (a.id == r.targetAlly)
                end = a.pos;
    } else {
        end = destinyWorld(r.aimLocal);
        // Stops on the player's shield while it holds
        if (shieldProtecting() && r.missOffset.isNull()) {
            const QVector3D from = rayStart(r);
            for (float t = 0.0f; t <= 1.0f; t += 0.02f) {
                const QVector3D p = from + (end - from) * t;
                if (insideShield(p))
                    return p;
            }
        }
    }
    if (!r.missOffset.isNull()) {
        const QVector3D from = rayStart(r);
        end = from + ((end + r.missOffset) - from) * 1.6f;     // goes on into space
    }
    return end;
}

void BattleView::updateRays(float dt)
{
    constexpr float kTick = 0.2f;
    for (Ray &r : m_rays) {
        r.age += dt;
        r.tick += dt;
        // Source or target gone: the beam stops
        const bool sourceGone = (r.sourceEnemy >= 0 && (!findEnemy(r.sourceEnemy) || !findEnemy(r.sourceEnemy)->alive))
                             || (r.sourceAlly >= 0 && !findAlly(r.sourceAlly));
        const Enemy *enemy = r.targetEnemy >= 0 ? findEnemy(r.targetEnemy) : nullptr;
        Enemy *ally = r.targetAlly >= 0 ? findAlly(r.targetAlly) : nullptr;
        const bool targetGone = (r.targetEnemy >= 0 && (!enemy || !enemy->alive)) || (r.targetAlly >= 0 && (!ally || !ally->alive));
        if (sourceGone || targetGone) {
            r.age = kBeamDuration;
            continue;
        }
        if (r.tick < kTick || !r.missOffset.isNull() || r.damageLeft <= 0.0f)
            continue;
        r.tick -= kTick;
        Projectile hit;
        hit.kind = Projectile::Kind::Beam;
        hit.fromDestiny = r.fromDestiny;
        hit.fromAlly = r.sourceAlly >= 0;
        hit.weapon = r.weapon;
        hit.aimSystem = hit.fromDestiny && !hit.fromAlly ? m_aimSystem : -1;
        const int remaining = int((kBeamDuration - r.age) / kTick + 1e-4f) + 1;
        hit.damage = r.damageLeft / float(std::max(1, remaining));
        r.damageLeft -= hit.damage;
        hit.width = r.width;
        hit.pos = rayEnd(r);
        hit.aimLocal = r.aimLocal;
        if (r.targetEnemy >= 0) {
            if (Enemy *e = findEnemy(r.targetEnemy)) {
                const bool counted = hit.fromDestiny && !hit.fromAlly;
                if (counted && r.age > kTick * 1.5f)
                    --m_stats.hits;             // one hit per beam, not per tick
                hitEnemy(*e, hit);
            }
        } else if (ally) {
            hitAlly(*ally, hit);
        } else if (shieldProtecting()) {
            Projectile half = hit, through = hit;
            half.damage *= 1.0f - kBeamShieldPierce;
            through.damage *= kBeamShieldPierce;
            hitShield(half);
            hitHull(through);
        } else {
            hitHull(hit);
        }
    }
    m_rays.erase(std::remove_if(m_rays.begin(), m_rays.end(), [](const Ray &r) { return r.age >= kBeamDuration; }),
                 m_rays.end());
}

void BattleView::drawRays()
{
    for (const Ray &r : m_rays) {
        const QVector3D from = rayStart(r);
        const QVector3D to = rayEnd(r);
        const QVector3D span = to - from;
        const float length = span.length();
        if (length < 0.1f)
            continue;
        // Bright at the start, fading out at the end
        const float t = r.age / kBeamDuration;
        const float strength = t < 0.1f ? t / 0.1f : 1.0f - std::max(0.0f, t - 0.7f) / 0.3f;
        QMatrix4x4 m;
        m.translate(from + span * 0.5f);
        m.rotate(QQuaternion::rotationTo({0, 0, 1}, span / length));
        QMatrix4x4 core = m, halo = m;
        const float flicker = 1.0f + 0.15f * std::sin(m_time * 40.0f + r.age * 13.0f);
        core.scale(r.width * 0.5f * flicker, r.width * 0.5f * flicker, length);
        halo.scale(r.width * 2.2f, r.width * 2.2f, length);
        drawMesh(m_box, core, QVector3D(1, 1, 1) * 0.7f + r.color * 0.5f, 1.5f, strength, ModeGlow);
        drawMesh(m_box, halo, r.color, 1.2f, 0.3f * strength, ModeGlow);
        drawGlow(from, r.width * 2.5f, r.color, 0.6f * strength);
        if (r.missOffset.isNull())
            drawGlow(to, r.width * 3.5f * flicker, r.color, 0.7f * strength);
    }
}

// ================================================================
//  Energy
// ================================================================

void BattleView::setTurretsActive(bool on)
{
    if (!m_loadout.towers) {
        emit logMessage(tr("No secondary towers aboard this ship"), LogLevel::Warning);
        return;
    }
    m_turretsOn = on;
    emit logMessage(on ? tr("Secondary towers: auto-fire on") : tr("Secondary towers: auto-fire off (+%1 power)").arg(kTurretPower),
                    LogLevel::Info);
}

void BattleView::setRailgunsActive(bool on)
{
    if (!m_loadout.railguns) {
        emit logMessage(tr("No railguns aboard this ship"), LogLevel::Warning);
        return;
    }
    m_railgunsOn = on;
    emit logMessage(on ? tr("Railguns: point defense on") : tr("Railguns: point defense off (+%1 power)").arg(kRailgunPower),
                    on ? LogLevel::Info : LogLevel::Warning);
}

void BattleView::setPowerPriority(PowerPriority priority)
{
    m_priority = priority;
    emit logMessage(powerReport(), LogLevel::Info);
}

// Power left once every active system took its share
int BattleView::freePower() const
{
    int used = kMainTowerPower + kEnginePower;
    if (m_turretsOn && m_loadout.towers)
        used += kTurretPower;
    if (m_railgunsOn && m_loadout.railguns)
        used += kRailgunPower;
    if (m_shieldOrdered)
        used += kShieldPower;
    if (m_dronesInFlight > 0)
        used += kDronePower;                // guidance of the drones in flight
    if (m_evading)
        used += kEvasionPower;
    return std::max(0, kCorePower + m_powerBonus - m_powerLoss - used);
}

float BattleView::shieldPowerBoost() const
{
    const float share = m_priority == PowerPriority::Shields ? 1.0f : m_priority == PowerPriority::Balanced ? 0.5f : 0.0f;
    return 1.0f + share * freePower() / 40.0f;
}

float BattleView::weaponPowerBoost() const
{
    const float share = m_priority == PowerPriority::Weapons ? 1.0f : m_priority == PowerPriority::Balanced ? 0.5f : 0.0f;
    return 1.0f + share * freePower() / 40.0f;
}

QString BattleView::powerReport() const
{
    const QString priority = m_priority == PowerPriority::Shields   ? tr("shields")
                           : m_priority == PowerPriority::Weapons ? tr("weapons")
                                                                  : tr("balanced");
    return tr("Power %1: free %2 - priority %3 (shield recharge x%4, weapon recharge x%5) - towers %6, railguns %7")
               .arg(kCorePower)
               .arg(freePower())
               .arg(priority)
               .arg(shieldPowerBoost(), 0, 'f', 2)
               .arg(weaponPowerBoost(), 0, 'f', 2)
               .arg(m_turretsOn ? tr("on") : tr("off"), m_railgunsOn ? tr("on") : tr("off"))
           + tr(" - evasion %1, drones need %2 free").arg(m_evading ? tr("on") : tr("off")).arg(kDronePower)
           + (m_powerLoss > 0 ? tr(" - LEAKS -%1").arg(m_powerLoss) : QString())
           + (m_powerBonus > 0 ? tr(" - generator +%1").arg(m_powerBonus) : QString());
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
    setupShip(e);
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
    regenShield(e, dt);

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
    // Damaged engines: the ship maneuvers slowly
    const float engines = 0.25f + 0.75f * e.systems[SysEngines] / 100.0f;
    // Motherships close in and fall back; a damaged one whose shield is
    // down retreats for a while to recharge it
    float radius = e.orbitRadius;
    if (e.cls == EnemyClass::Mothership) {
        radius += 7.0f * std::sin(m_time * 0.045f + e.phase);
        if (dt > 0.0f && e.retreat <= 0.0f && e.hp < 0.4f * e.maxHp && e.maxShield > 0.0f
            && e.shield < 0.3f * e.maxShield && e.systems[SysEngines] > 10.0f) {
            e.retreat = 14.0f;
            emit logMessage(tr("T%1 %2 falls back to recharge its shields").arg(indexOfId(e.id) + 1).arg(className(e)),
                            LogLevel::Info);
        }
        if (e.retreat > 0.0f) {
            e.retreat -= dt;
            radius += 16.0f;
        }
    }
    const float a = e.baseAngle + e.angleAmp * std::sin(m_time * e.angleSpeed * engines + e.phase);
    QVector3D orbit(radius * std::cos(a), e.height + 0.8f * std::sin(m_time * 0.7f + e.phase),
                    radius * std::sin(a));
    // Freshly launched ships fly from their hangar to their orbit; the
    // others move at their own speed between their positions
    const float gap = (orbit - previous).length();
    const float maxSpeed = (e.cls == EnemyClass::Mothership ? 4.0f : 12.0f) * engines;
    if (dt > 0.0f && gap > maxSpeed * dt)
        orbit = previous + (orbit - previous) / gap * (maxSpeed * dt);
    e.pos = orbit;

    const QVector3D motion = e.pos - previous;
    if (e.cls == EnemyClass::Fighter && dt > 0.0f && motion.lengthSquared() > 1e-6f)
        e.facing = motion.normalized();
    else
        e.facing = QVector3D(-e.pos.x(), 0.0f, -e.pos.z()).normalized();

    if (dt <= 0.0f)
        return;

    // Attacks from the ship library: each one has its own cooldown and a
    // chance to be used when ready. Damaged weapons fire less, a ship
    // falling back does not fire its main batteries.
    if (e.systems[SysWeapons] <= 0.0f)
        return;
    const QList<ShipAttack> &attacks = classOf(e).attacks;
    for (int i = 0; i < attacks.size() && i < int(e.attackTimers.size()); ++i) {
        const ShipAttack &attack = attacks[i];
        float &timer = e.attackTimers[size_t(i)];
        timer -= dt;
        if (timer > 0.0f)
            continue;
        timer = attack.cooldown * randRange(0.85f, 1.15f) / gameConfig().enemyFireRate;
        if (attack.trigger == "damaged" && e.hp > 0.5f * e.maxHp)
            continue;
        if (randRange(0.0f, 1.0f) > attack.chance * (0.4f + 0.6f * e.systems[SysWeapons] / 100.0f))
            continue;
        if (attack.kind == "launch" && e.systems[SysHangar] <= 0.0f)
            continue;                       // hangar destroyed: nothing takes off
        if (attack.kind == "kamikaze" && e.systems[SysEngines] < 30.0f)
            continue;
        if (e.retreat > 0.0f && attack.kind != "launch")
            continue;
        // Ancient drones draw on the shield of their ship
        if (attack.kind == "missile" && isDrone(attack)) {
            const float cost = e.maxShield * kEnemyDroneShieldCost;
            if (e.maxShield <= 0.0f || e.shield < cost)
                continue;
            if (!useAmmo(e, i))
                continue;
            e.shield -= cost;
        } else if (!useAmmo(e, i)) {
            continue;
        }
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
            shoot(false, e.pos, -1, randomHullPoint(), attack.damage, kMissileSpeed,
                  isDrone(attack) ? kDroneColor : kMissileExhaust, 0.6f, 0.16f, i * 0.4f);
            m_projectiles.back().kind = Projectile::Kind::Missile;
            m_projectiles.back().drone = isDrone(attack);
            m_projectiles.back().life = 14.0f;
            aimAtVictim();
            emit sound(SoundCue::EnemyMissile, sc.faction, i * 0.4f);
        }
        if (attack.shots > 1)
            emit logMessage(tr("%1 fires a salvo of %2 missiles").arg(sc.name).arg(attack.shots), LogLevel::Warning);
    } else if (attack.kind == "beam") {
        Ray ray;
        ray.sourceEnemy = e.id;
        ray.aimLocal = randomHullPoint();
        ray.targetAlly = victim ? victim->id : -1;
        ray.color = sc.faction == "Ori" ? QVector3D(1.0f, 0.85f, 0.55f) : kBeamColor;
        ray.width = big ? 0.32f : 0.18f;
        fireRay(ray, attack.damage);
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

float BattleView::hitChance(const QVector3D &from, const QVector3D &to, bool fromPlayer, const Enemy *target) const
{
    float chance = GameConfig::instance().accuracyAt((to - from).length());
    if (fromPlayer) {
        chance *= (0.4f + 0.6f * subsystemHealth("sensors") / 100.0f) * m_accuracyBonus;  // damaged sensors: poor aim
        if (m_evading)
            chance *= kEvasionOwnAim;
    }
    if (target) {
        if (target->cls == EnemyClass::Fighter)
            chance *= 0.9f;
        // A ship with damaged engines maneuvers badly: easier to hit
        chance *= 1.0f + 0.3f * (1.0f - target->systems[SysEngines] / 100.0f);
    } else if (!fromPlayer && m_evading && subsystemHealth("engines") > 20.0f) {
        chance *= kEvasionDodge;
    }
    return std::clamp(chance, 0.05f, 1.0f);
}

void BattleView::rollAccuracy(Projectile &p, const QVector3D &target, float floor)
{
    const bool fromPlayer = p.fromDestiny && !p.fromAlly;
    const Enemy *enemy = p.fromDestiny && p.targetId >= 0 ? findEnemy(p.targetId) : nullptr;
    float chance = hitChance(p.pos, target, fromPlayer, enemy);
    if (fromPlayer && p.aimSystem >= 0)
        chance *= kSubsystemAim;
    chance = std::max(chance, floor);
    if (fromPlayer)
        ++m_stats.shots;
    if (randRange(0.0f, 1.0f) <= chance)
        return;
    // Miss: it flies a few meters beside the target and goes on
    p.miss = true;
    const QVector3D dir = (target - p.pos).normalized();
    QVector3D side = QVector3D::crossProduct(dir, {0, 1, 0});
    if (side.lengthSquared() < 1e-4f)
        side = {1, 0, 0};
    side.normalize();
    const QVector3D up = QVector3D::crossProduct(side, dir).normalized();
    const float a = randRange(0.0f, 2.0f * float(M_PI));
    const QVector3D offset = (side * std::cos(a) + up * std::sin(a)) * randRange(2.5f, 5.0f);
    const float speed = std::max(1.0f, p.vel.length());
    p.vel = ((target + offset) - p.pos).normalized() * speed;
    p.life = std::min(p.life, (target - p.pos).length() / speed + 1.2f);
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
    if (m_railgunTimer > 0.0f || m_railgunOverheated || m_railgunAmmo <= 0)
        return;
    const float efficiency = std::max(0.25f, subsystemHealth("weapons") / 100.0f);
    m_railgunTimer = kRailgunDelay / efficiency;
    const float damage = m_railgun.damage / 10.0f * efficiency;
    const QVector3D &turret = m_turrets[QRandomGenerator::global()->bounded(int(m_turrets.size()))];
    const QVector3D from = destinyWorld(turret);
    // Every slug uses ammunition and heats the guns
    auto slugFired = [this] {
        m_projectiles.back().weapon = Weapon::Railgun;
        emit sound(SoundCue::DestinyRailgun, {}, 0.0f);
        --m_railgunAmmo;
        m_railgunHeat += kRailgunHeatPerShot;
        if (m_railgunHeat >= 100.0f) {
            m_railgunHeat = 100.0f;
            m_railgunOverheated = true;
            emit logMessage(tr("Railguns overheated - point defense offline while they cool down"), LogLevel::Critical);
        }
        if (m_railgunAmmo == 0)
            emit logMessage(tr("Railguns out of ammunition - resupply on a mineral-rich planet"), LogLevel::Critical);
        else if (m_railgunAmmo == kRailgunAmmo / 5)
            emit logMessage(tr("Railgun ammunition low (%1 rounds)").arg(m_railgunAmmo), LogLevel::Warning);
    };

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
        slugFired();
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
        slugFired();
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
        slugFired();
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

        // Hit chance, rolled once when the shot leaves its gun
        if (!p.rolled) {
            p.rolled = true;
            QVector3D aim;
            bool known = true;
            if (p.targetShot >= 0)
                known = false;                  // railgun slug on a missile: own rules
            else if (p.targetAlly >= 0) {
                const Enemy *ally = findAlly(p.targetAlly);
                known = ally != nullptr;
                if (ally)
                    aim = ally->pos;
            } else if (p.targetId < 0)
                aim = destinyWorld(p.aimLocal);
            else if (const Enemy *e = findEnemy(p.targetId))
                aim = e->pos;
            else
                known = false;
            if (known)
                rollAccuracy(p, aim, p.drone ? 0.92f : p.kind == Projectile::Kind::Missile ? 0.8f : 0.0f);
        }
        if (p.miss) {
            p.pos += p.vel * dt;                // flies past, no impact
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
            } else if (p.targetId < 0 && !p.drone && shieldProtecting() && insideShield(p.pos)) {
                if (p.kind == Projectile::Kind::Beam) {
                    // Half of a beam goes through the shield
                    Projectile half = p;
                    Projectile through = p;
                    half.damage *= 1.0f - kBeamShieldPierce;
                    through.damage *= kBeamShieldPierce;
                    hitShield(half);
                    hitHull(through);
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
    const bool fromPlayer = p.fromDestiny && !p.fromAlly;
    if (fromPlayer)
        ++m_stats.hits;
    damage = adaptedDamage(e, p.weapon, damage);
    damage = absorbByShield(e, p, damage);
    if (damage <= 0.0f)
        return;
    // Aimed at a subsystem: it takes the blow, the hull less of it.
    // Any other hit may still damage one at random.
    if (p.aimSystem >= 0) {
        damageEnemySystem(e, p.aimSystem, damage);
        damage *= kSubsystemHullShare;
    } else if (randRange(0.0f, 1.0f) < 0.25f) {
        damageEnemySystem(e, QRandomGenerator::global()->bounded(4), damage * 0.4f);
    }
    if (fromPlayer)
        m_stats.damageDealt += std::min(damage, std::max(0.0f, e.hp));
    e.hp -= damage;
    e.hitFlash = 1.0f;
    m_effects.push_back({p.pos, kExplosion, 0.0f, 0.35f, 0.5f + p.width * 2.0f});

    if (e.hp > 0.0f)
        return;

    const int index = indexOfId(e.id);
    e.alive = false;
    ++m_kills;
    ++m_stats.kills;
    if (e.cls == EnemyClass::Mothership)
        ++m_stats.motherships;
    const bool big = e.cls == EnemyClass::Mothership;
    m_effects.push_back({e.pos, kExplosion, 0.0f, big ? 1.6f : 0.9f, big ? 6.0f : 2.2f});
    m_effects.push_back({e.pos, QVector3D(1.0f, 0.9f, 0.7f), 0.0f, big ? 0.6f : 0.35f, big ? 3.0f : 1.2f});
    emit sound(big ? SoundCue::BigExplosion : SoundCue::Explosion, {}, 0.0f);
    if (m_lockedId == e.id)
        m_lockedId = -1;
    emit logMessage(tr("T%1 %2 destroyed").arg(index + 1).arg(className(e)), LogLevel::Success);
    emit enemyDestroyed(big);
}

// Damage to a subsystem of an enemy ship (percent of its hull max)
void BattleView::damageEnemySystem(Enemy &e, int system, float hullDamage)
{
    if (system < 0 || system > 3)
        return;
    float &health = e.systems[system];
    const float before = health;
    health = std::max(0.0f, health - hullDamage / std::max(1.0f, e.maxHp) * kSubsystemDamage);
    if (before > 0.0f && health <= 0.0f) {
        static const char *names[4] = {QT_TR_NOOP("weapons"), QT_TR_NOOP("shields"), QT_TR_NOOP("engines"),
                                       QT_TR_NOOP("hangar")};
        emit logMessage(tr("T%1 %2: %3 knocked out").arg(indexOfId(e.id) + 1).arg(className(e)).arg(tr(names[system])),
                        LogLevel::Success);
        if (system == SysShields) {
            e.shield = 0.0f;
            e.maxShield = 0.0f;         // no more shield for this battle
        }
    }
}

// Command drones learn: after many hits of the same weapon, their
// shields resist it
float BattleView::adaptedDamage(const Enemy &e, Weapon weapon, float damage)
{
    if (classOf(e).faction != QLatin1String("Drones") || weapon == Weapon::None || weapon == Weapon::Ally)
        return damage;
    const int key = int(weapon);
    const int hits = ++m_weaponHits[key];
    if (m_adaptedWeapon != key && hits >= kAdaptAfterHits) {
        int best = key;
        for (const auto &[w, count] : m_weaponHits)
            if (count > m_weaponHits[best])
                best = w;
        if (best == key) {
            m_adaptedWeapon = key;
            static const char *names[] = {"", QT_TR_NOOP("main weapon"), QT_TR_NOOP("pulse weapons"),
                                          QT_TR_NOOP("towers"), QT_TR_NOOP("railguns"), QT_TR_NOOP("drones"),
                                          QT_TR_NOOP("missiles"), ""};
            emit logMessage(tr("The drones adapted their shields to your %1 - switch weapons").arg(tr(names[key])),
                            LogLevel::Critical);
        }
    }
    return m_adaptedWeapon == key ? damage * kAdaptedDamage : damage;
}

// Hull and shield of a ship that enters the battle (Balance: scales
// and shield per faction)
void BattleView::setupShip(Enemy &ship)
{
    const ShipClass &sc = m_classes[ship.classIndex];
    ship.hp = ship.maxHp = std::max(1.0f, float(sc.hull) * gameConfig().shipHullScale);
    ship.shield = ship.maxShield
        = float(sc.hull) * shipShieldShare(sc.faction, ship.cls == EnemyClass::Mothership) * gameConfig().shipShieldScale;
    ship.ammo.clear();
    for (const ShipAttack &attack : sc.attacks)
        ship.ammo.push_back(attack.kind != "missile" ? -1 : isDrone(attack) ? kDroneSalvos : kMissileSalvos);
}

BattleView::Schematic BattleView::schematic() const
{
    const auto it = m_modModels.find(flagshipMeshKey());
    if (it == m_modModels.end() || !it->second || m_shieldRadii.isNull())
        return {};
    const ModModel &ship = *it->second;
    const QRectF &r = ship.silhouetteRect;
    return {ship.silhouette, QRectF(r.x() * 13.5, r.y() * 13.5, r.width() * 13.5, r.height() * 13.5),
            QPointF(m_shieldCenter.x(), m_shieldCenter.z()), QSizeF(m_shieldRadii.x(), m_shieldRadii.z())};
}

bool BattleView::isDrone(const ShipAttack &attack)
{
    return attack.name.contains(QLatin1String("drone"), Qt::CaseInsensitive);
}

// Missiles and drones are limited: false when the magazine is empty
bool BattleView::useAmmo(Enemy &ship, int attack)
{
    if (attack < 0 || attack >= int(ship.ammo.size()) || ship.ammo[size_t(attack)] < 0)
        return true;
    if (ship.ammo[size_t(attack)] == 0)
        return false;
    --ship.ammo[size_t(attack)];
    return true;
}

// Engine glows on the exhausts of a mod model (or the spots at its rear)
void BattleView::drawEngineSpots(const QString &key, const QMatrix4x4 &model, float length, const QVector3D &color,
                                 float strength)
{
    const ModModel *ship = modModel(key);
    if (!ship)
        return;
    QMatrix4x4 m = model;
    m.scale(length);
    const float size = length * (ship->engines.size() > 6 ? 0.010f : 0.014f);
    for (size_t i = 0; i < ship->engines.size(); ++i) {
        const QVector3D pos = m.map(ship->engines[i]);
        const float pulse = size * (1.0f + 0.12f * std::sin(m_time * 9.0f + float(i)));
        drawGlow(pos, pulse, color, 0.9f * strength);
        drawGlow(pos, pulse * 2.2f, color, 0.1f * strength);
    }
}

namespace {
std::atomic<int> g_assist{-1};
} // namespace

bool BattleView::assistSetting()
{
    if (g_assist < 0)
        g_assist = QSettings("Stargate", "stargate").value("gameplay/combatAssist", true).toBool() ? 1 : 0;
    return g_assist == 1;
}

void BattleView::setAssistSetting(bool on)
{
    g_assist = on ? 1 : 0;
    QSettings("Stargate", "stargate").setValue("gameplay/combatAssist", on);
}

// The shield takes the hit first; returns what is left for the hull.
// Half of a beam goes through (kBeamShieldPierce), like on the Destiny.
float BattleView::absorbByShield(Enemy &ship, const Projectile &p, float damage)
{
    ship.lastHit = m_time;
    if (ship.shield <= 0.0f || p.drone)
        return damage;                          // drones go through the shields
    float through = 0.0f;
    if (p.kind == Projectile::Kind::Beam) {
        through = damage * kBeamShieldPierce;
        damage -= through;
    }
    const float absorbed = std::min(ship.shield, damage);
    ship.shield -= absorbed;
    ship.shieldFlash = 1.0f;
    m_effects.push_back({p.pos, kShieldColor, 0.0f, 0.25f, 0.4f + p.width * 2.0f});
    return damage - absorbed + through;
}

void BattleView::regenShield(Enemy &ship, float dt)
{
    ship.shieldFlash = std::max(0.0f, ship.shieldFlash - dt * 2.0f);
    if (ship.maxShield > 0.0f && m_time - ship.lastHit > kShipShieldDelay)
        ship.shield = std::min(ship.maxShield, ship.shield + ship.maxShield * kShipShieldRegen * dt
                                                                 * ship.systems[SysShields] / 100.0f);
}

// Bubble of a ship shield, visible while it takes hits: the shield model
// of the mod when there is one, an ellipsoid otherwise
void BattleView::drawShipShield(const Enemy &ship, const QMatrix4x4 &model)
{
    if (ship.shieldFlash <= 0.01f || ship.shield <= 0.0f)
        return;
    const float alpha = 0.22f * ship.shieldFlash * (0.4f + 0.6f * ship.shield / ship.maxShield);
    const bool big = ship.cls == EnemyClass::Mothership;
    const float length = big ? 9.0f : 2.6f;
    QMatrix4x4 m = model;
    const QString key = meshKeyOf(classOf(ship));
    if (ModModel *bubble = modModel(key + QStringLiteral("_shield"), key)) {
        m.scale(length * 1.03f);                // same frame as the hull: it wraps it
        drawMesh(bubble->mesh, m, kShieldColor, 0.0f, alpha, ModeShield);
    } else {
        m.scale(length * 0.62f, length * 0.32f, length * 0.42f);
        drawMesh(m_sphere, m, kShieldColor, 0.0f, alpha, ModeShield);
    }
}

void BattleView::hitShield(const Projectile &p)
{
    const float absorb = 1.0f / std::max(0.4f, subsystemHealth("shields") / 100.0f);
    const float shieldBefore = m_shieldStrength;
    m_shieldStrength = std::max(0.0f, m_shieldStrength - p.damage * absorb * gameConfig().damageToPlayer);
    m_stats.shieldLost += shieldBefore - m_shieldStrength;
    m_shieldFlash = 1.0f;
    m_lastShieldHit = m_time;
    m_effects.push_back({p.pos, kShieldColor, 0.0f, 0.3f, 0.9f});
    emit sound(SoundCue::ShieldHit, {}, 0.0f);
    m_hits.push_back({m_destinyInverse.map(p.pos), {m_time, true}});

    if (m_shieldStrength <= 0.0f && !m_shieldCollapsed) {
        m_shieldCollapsed = true;
        emit sound(SoundCue::AlertShields, {}, 0.0f);
        emit logMessage(tr("Shields collapsed!"), LogLevel::Critical);
    }
}

void BattleView::hitHull(const Projectile &p)
{
    const float before = m_hull;
    m_hull = std::max(0.0f, m_hull - p.damage * gameConfig().damageToPlayer);
    m_stats.hullLost += before - m_hull;
    m_shake = std::min(1.0f, m_shake + p.damage / 40.0f);
    const bool missile = p.kind == Projectile::Kind::Missile;
    m_effects.push_back({p.pos, kExplosion, 0.0f, missile ? 0.8f : 0.45f, missile ? 2.2f : 0.9f});
    emit sound(missile ? SoundCue::Explosion : SoundCue::HullHit, {}, 0.0f);
    if (missile && m_time - m_lastMissileLog > 4.0f) {
        m_lastMissileLog = m_time;
        emit logMessage(tr("Missile impact on the hull (-%1)").arg(qRound(p.damage)), LogLevel::Warning);
    }
    damageSubsystemNear(p.aimLocal, p.damage * 1.6f);
    m_hits.push_back({p.aimLocal, {m_time, false}});
    if (!m_externalFires && m_fires.size() < kMaxFires && randRange(0, 1) < 0.35f)
        m_fires.push_back(p.aimLocal);
    emit hullHit(p.aimLocal, p.damage * gameConfig().damageToPlayer);

    if (before > 25.0f && m_hull <= 25.0f)
        emit sound(SoundCue::AlertHull, {}, 0.0f);
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
    w.drones = m_drones;
    w.mainName = QString::fromLatin1(m_loadout.mainName);
    w.hasPulse = m_loadout.pulse;
    w.hasTowers = m_loadout.towers;
    w.hasRailguns = m_loadout.railguns;
    w.dronesMax = m_droneBay;
    w.fightersOut = int(std::count_if(m_allies.begin(), m_allies.end(), [](const Enemy &a) { return a.group == -1 && a.alive; }));
    w.allies = allyCount();
    w.turretsOn = m_turretsOn;
    w.railgunsOn = m_railgunsOn;
    w.freePower = freePower();
    w.priority = m_priority == PowerPriority::Shields   ? QStringLiteral("shields")
               : m_priority == PowerPriority::Weapons ? QStringLiteral("weapons")
                                                      : QStringLiteral("balanced");
    w.incomingMissiles = int(std::count_if(m_projectiles.begin(), m_projectiles.end(), [](const Projectile &p) {
        return !p.dead && !p.fromDestiny && p.kind == Projectile::Kind::Missile;
    }));
    w.railgunHeat = m_railgunHeat;
    w.railgunOverheated = m_railgunOverheated;
    w.railgunAmmo = m_railgunAmmo;
    w.railgunAmmoMax = kRailgunAmmo;
    w.missiles = m_missiles;
    w.missilesMax = m_missileBay;
    w.evading = m_evading;
    w.ftlCountdown = m_jumpCountdown;
    if (m_aimSystem >= 0)
        w.aimSystem = enemySystems().at(m_aimSystem);
    if (const Enemy *e = findEnemy(m_lockedId); e && e->alive) {
        w.accuracy = 100.0f * hitChance(destinyWorld(kMainTowerLocal), e->pos, true, e)
                   * (m_aimSystem >= 0 ? kSubsystemAim : 1.0f);
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
    const QVector3D center = m_shieldRadii.isNull() ? kShieldCenter : m_shieldCenter;
    const QVector3D radii = m_shieldRadii.isNull() ? kShieldRadii : m_shieldRadii;
    const QVector3D l = (m_destinyInverse.map(world) - center) / radii;
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
    // Observation deck: a slow orbit far around the ship
    const float yaw = m_observing ? qDegreesToRadians(m_time * 4.0f)
                                  : qDegreesToRadians(m_yawOffset + 28.0f * std::sin(m_time * 0.07f));
    const float pitch = qDegreesToRadians(m_observing ? 8.0f + 6.0f * std::sin(m_time * 0.05f) : m_pitch);
    QVector3D target(0.8f, 0.5f, 0.0f);
    // Heavy hits shake the camera
    if (m_shake > 0.0f)
        target += QVector3D(std::sin(m_time * 53.0f), std::sin(m_time * 47.0f + 1.0f), std::sin(m_time * 59.0f + 2.0f))
                * 0.35f * m_shake;
    const float distance = m_observing ? 34.0f : m_distance;

    m_eye = target + distance * QVector3D(std::cos(pitch) * std::sin(yaw),
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

    // Shields of the other ships, then the Destiny's (alpha blended)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const std::vector<Enemy> *ships : {&m_enemies, &m_allies})
        for (const Enemy &ship : *ships) {
            if (!ship.alive)
                continue;
            QMatrix4x4 m;
            m.translate(ship.pos);
            m.rotate(QQuaternion::rotationTo({1, 0, 0}, ship.facing));
            drawShipShield(ship, m);
        }
    const float shieldAlpha = shieldProtecting()
        ? (0.05f + 0.45f * m_shieldFlash) * (0.4f + 0.6f * m_shieldStrength / 100.0f)
        : 0.0f;
    if (shieldAlpha > 0.003f) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        QMatrix4x4 shield = m_destinyModel;
        shield.translate(m_shieldRadii.isNull() ? kShieldCenter : m_shieldCenter);
        shield.scale(m_shieldRadii.isNull() ? kShieldRadii : m_shieldRadii);
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
    drawMesh(mesh, model, color, emissive, alpha, mode, nullptr);
}

void BattleView::drawMesh(Mesh &mesh, const QMatrix4x4 &model, const QVector3D &color, float emissive, float alpha,
                          ShaderMode mode, const ShipTextures *textures)
{
    const bool textured = textures && textures->color != 0;
    m_lit.setUniformValue("uTextured", textured ? 1 : 0);
    if (textured) {
        const GLuint units[4] = {textures->color, textures->mask, textures->orm, textures->normal};
        const char *samplers[4] = {"uTexture", "uMask", "uOrm", "uNormalMap"};
        for (int i = 0; i < 4; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, units[i]);
            m_lit.setUniformValue(samplers[i], i);
        }
        glActiveTexture(GL_TEXTURE0);
        m_lit.setUniformValue("uHasMask", textures->mask != 0 ? 1 : 0);
        m_lit.setUniformValue("uHasOrm", textures->orm != 0 ? 1 : 0);
        m_lit.setUniformValue("uHasNormal", textures->normal != 0 ? 1 : 0);
        m_lit.setUniformValue("uLightColor", m_shipLights);
    }
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

    // --- Model of the mod when installed (same size as the Destiny)
    const QVector3D modelColor = m_flagshipStyle.isEmpty()                                   ? QVector3D(0.6f, 0.6f, 0.64f)
                               : m_flagshipStyle == "Wraith"                                ? QVector3D(0.3f, 0.34f, 0.28f)
                               : m_flagshipStyle == "Asgard" || m_flagshipStyle == "Ancients" ? QVector3D(0.62f, 0.66f, 0.7f)
                               : m_flagshipStyle == "Lucian Alliance"                       ? QVector3D(0.62f, 0.36f, 0.26f)
                                                                                            : QVector3D(0.78f, 0.64f, 0.32f);
    if (drawModShip(flagshipMeshKey(), m_destinyModel, 13.5f, modelColor * 1.3f, 0.0f,
                    factionLights(m_flagshipStyle.isEmpty() ? QStringLiteral("Ancients") : m_flagshipStyle))) {
        // The shield bubble fits the hull of the model
        const ModModel *ship = modModel(flagshipMeshKey());
        m_shieldCenter = QVector3D();
        m_shieldRadii = QVector3D(std::max(ship->halfExtents.x() * 13.5f * 1.2f, 3.0f),
                                  std::max(ship->halfExtents.y() * 13.5f * 1.6f, 2.0f),
                                  std::max(ship->halfExtents.z() * 13.5f * 1.3f, 2.5f));
        return;
    }
    m_shieldRadii = QVector3D();

    // --- Conquest flagships of the other factions
    if (m_flagshipStyle == "Tau'ri") {
        // BC-304: long hull, two side hangar pods, bridge, engine blocks
        const QVector3D hull(0.52f, 0.55f, 0.6f), dark(0.3f, 0.32f, 0.36f);
        part(m_box, {0.0f, 0.0f, 0.0f}, {11.5f, 0.8f, 1.5f}, 0, hull);
        part(m_box, {-0.8f, -0.05f, 1.75f}, {7.5f, 0.7f, 1.0f}, 0, dark);
        part(m_box, {-0.8f, -0.05f, -1.75f}, {7.5f, 0.7f, 1.0f}, 0, dark);
        part(m_box, {2.4f, 0.75f, 0.0f}, {1.6f, 0.7f, 0.8f}, 0, hull);
        part(m_box, {5.9f, 0.0f, 0.0f}, {1.0f, 0.55f, 1.0f}, 45, dark);
        for (float z : {-1.75f, 0.0f, 1.75f})
            part(m_sphere, {-5.9f, 0.0f, z}, {0.35f, 0.35f, 0.35f}, 0, QVector3D(0.4f, 0.6f, 1.0f));
        return;
    }
    if (m_flagshipStyle == "Goa'uld" || m_flagshipStyle == "Lucian Alliance") {
        // Ha'tak: golden pyramid on its ring (rust colors for a stolen one)
        const bool lucian = m_flagshipStyle == "Lucian Alliance";
        const QVector3D gold = lucian ? QVector3D(0.62f, 0.36f, 0.26f) : QVector3D(0.78f, 0.64f, 0.32f);
        const QVector3D dark = gold * 0.5f;
        QMatrix4x4 m = m_destinyModel;
        m.scale(1.9f);
        drawMesh(m_ring, m, dark, 0.0f, 1.0f, ModeHull);
        QMatrix4x4 top = m;
        top.translate(0, 0.1f, 0);
        top.rotate(45, 0, 1, 0);
        drawMesh(m_pyramid, top, gold, 0.0f, 1.0f, ModeHull);
        QMatrix4x4 bottom = m;
        bottom.translate(0, -0.1f, 0);
        bottom.scale(1.0f, -0.45f, 1.0f);
        bottom.rotate(45, 0, 1, 0);
        drawMesh(m_pyramid, bottom, dark, 0.0f, 1.0f, ModeHull);
        return;
    }
    if (m_flagshipStyle == "Asgard") {
        // O'Neill class: long pale hull with a split bow and side wings
        const QVector3D hull(0.72f, 0.76f, 0.8f), dark(0.45f, 0.48f, 0.52f), glow(0.7f, 0.85f, 1.0f);
        part(m_sphere, {0.0f, 0.0f, 0.0f}, {6.2f, 0.7f, 1.6f}, 0, hull);
        part(m_box, {4.6f, 0.0f, 0.75f}, {3.2f, 0.4f, 0.45f}, 0, dark);
        part(m_box, {4.6f, 0.0f, -0.75f}, {3.2f, 0.4f, 0.45f}, 0, dark);
        part(m_box, {-1.0f, 0.0f, 2.2f}, {4.0f, 0.18f, 2.2f}, 0, hull);
        part(m_box, {-1.0f, 0.0f, -2.2f}, {4.0f, 0.18f, 2.2f}, 0, hull);
        part(m_sphere, {-5.9f, 0.0f, 0.0f}, {0.45f, 0.45f, 0.9f}, 0, glow);
        return;
    }
    if (m_flagshipStyle == "Ancients") {
        // Aurora class: very long thin hull, central spine, engine pods
        const QVector3D hull(0.56f, 0.62f, 0.66f), dark(0.34f, 0.38f, 0.42f), glow(0.5f, 0.9f, 1.0f);
        part(m_box, {0.0f, 0.0f, 0.0f}, {12.5f, 0.5f, 1.1f}, 0, hull);
        part(m_box, {0.5f, 0.45f, 0.0f}, {8.0f, 0.4f, 0.5f}, 0, dark);
        part(m_box, {-3.0f, -0.1f, 1.1f}, {5.0f, 0.4f, 0.6f}, 0, dark);
        part(m_box, {-3.0f, -0.1f, -1.1f}, {5.0f, 0.4f, 0.6f}, 0, dark);
        part(m_box, {6.6f, 0.0f, 0.0f}, {1.0f, 0.4f, 0.9f}, 45, hull);
        for (float z : {-1.1f, 1.1f})
            part(m_sphere, {-5.7f, -0.1f, z}, {0.35f, 0.3f, 0.35f}, 0, glow);
        return;
    }
    if (m_flagshipStyle == "Ori") {
        // Ori warship: wide disc with a raised crown and a burning heart
        const QVector3D hull(0.6f, 0.55f, 0.48f), dark(0.36f, 0.32f, 0.28f), fire(1.0f, 0.62f, 0.25f);
        part(m_sphere, {0.0f, 0.0f, 0.0f}, {5.8f, 0.55f, 5.0f}, 0, hull);
        part(m_sphere, {0.0f, 0.45f, 0.0f}, {3.2f, 0.6f, 2.8f}, 0, dark);
        for (int i = 0; i < 6; ++i)
            part(m_box, {0.0f, 0.7f, 0.0f}, {6.6f, 0.15f, 0.25f}, 30.0f * i, dark);
        part(m_sphere, {0.0f, 0.9f, 0.0f}, {0.9f, 0.5f, 0.9f}, 0, fire);
        return;
    }
    if (m_flagshipStyle == "Wraith") {
        // Hive ship: long organic hull, bristling with spikes
        const QVector3D skin(0.26f, 0.3f, 0.24f), dark(0.14f, 0.16f, 0.14f), glow(0.45f, 0.85f, 0.5f);
        part(m_sphere, {0.0f, 0.0f, 0.0f}, {6.4f, 1.1f, 2.4f}, 0, skin);
        part(m_sphere, {-1.0f, 0.5f, 0.0f}, {4.2f, 0.8f, 1.4f}, 0, dark);
        part(m_sphere, {5.6f, 0.0f, 0.0f}, {1.4f, 0.7f, 1.0f}, 0, skin);
        for (int i = 0; i < 9; ++i) {
            const float x = -5.0f + i * 1.15f;
            const float side = (i % 2 ? 1.0f : -1.0f) * (1.2f + 0.4f * (i % 3));
            part(m_box, {x, 0.9f, side * 0.5f}, {0.18f, 1.3f, 0.18f}, 20.0f * i, dark);
        }
        part(m_sphere, {-6.1f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.9f}, 0, glow);
        return;
    }

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

// ================================================================
//  Ship models of the mod
// ================================================================

struct BattleView::TextureJob
{
    QMutex mutex;
    QImage images[4];           // color, mask, orm, normal
    bool done = false;
};

BattleView::ModModel *BattleView::modModel(const QString &key, const QString &frameKey)
{
    if (key.isEmpty())
        return nullptr;
    auto it = m_modModels.find(key);
    if (it == m_modModels.end()) {
        ModMesh::Model frame;
        const ModModel *parent = frameKey.isEmpty() ? nullptr : modModel(frameKey);
        if (parent) {
            frame.center = parent->center;
            frame.length = parent->length;
        }
        const ModMesh::Model data = ModMesh::load(Assets::modFile(QStringLiteral("meshes/%1.mesh").arg(key)),
                                                  parent ? &frame : nullptr);
        std::unique_ptr<ModModel> model;
        if (!data.vertices.empty()) {
            model = std::make_unique<ModModel>();
            model->engines = data.engines;
            model->points = data.points;
            model->halfExtents = data.halfExtents;
            model->center = data.center;
            model->length = data.length;
            model->silhouette = data.silhouette;
            model->silhouetteRect = data.silhouetteRect;
            model->texturePaths[0] = data.texturePath;
            model->texturePaths[1] = data.maskPath;
            model->texturePaths[2] = data.ormPath;
            model->texturePaths[3] = data.normalPath;
            // position, normal, uv, tangent + sign
            const int stride = ModMesh::kFloatsPerVertex * sizeof(float);
            Mesh &mesh = model->mesh;
            mesh.vao.create();
            mesh.vao.bind();
            mesh.vbo.create();
            mesh.vbo.bind();
            mesh.vbo.allocate(data.vertices.data(), int(data.vertices.size() * sizeof(float)));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(3 * sizeof(float)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(6 * sizeof(float)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(8 * sizeof(float)));
            mesh.vao.release();
            mesh.vbo.release();
            mesh.vertexCount = int(data.vertices.size()) / ModMesh::kFloatsPerVertex;
        }
        it = m_modModels.emplace(key, std::move(model)).first;
    }
    if (it->second)
        updateTexture(*it->second);
    return it->second.get();
}

// The color texture is decoded in the background at the chosen quality,
// then sent to the GPU here (the GL context is current while painting)
void BattleView::updateTexture(ModModel &model)
{
    if (model.texturePaths[0].isEmpty())
        return;
    const int quality = DdsTexture::qualitySetting();
    if (model.job) {
        QImage images[4];
        {
            QMutexLocker lock(&model.job->mutex);
            if (!model.job->done)
                return;
            for (int i = 0; i < 4; ++i)
                images[i] = std::move(model.job->images[i]);
        }
        model.job.reset();
        if (images[0].isNull()) {
            model.texturePaths[0].clear();      // unreadable: keep the plain color
            return;
        }
        GLuint *targets[4] = {&model.textures.color, &model.textures.mask, &model.textures.orm,
                              &model.textures.normal};
        for (int i = 0; i < 4; ++i) {
            GLuint &texture = *targets[i];
            if (images[i].isNull()) {
                if (texture)
                    glDeleteTextures(1, &texture);
                texture = 0;
                continue;
            }
            if (texture == 0)
                glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, images[i].width(), images[i].height(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, images[i].constBits());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }
    if (model.quality == quality)
        return;
    model.quality = quality;
    auto job = std::make_shared<TextureJob>();
    model.job = job;
    const QStringList paths{model.texturePaths[0], model.texturePaths[1], model.texturePaths[2],
                            model.texturePaths[3]};
    const int size = DdsTexture::maxSizeFor(quality);
    QThreadPool::globalInstance()->start([job, paths, size] {
        QImage images[4];
        for (int i = 0; i < 4; ++i)
            images[i] = DdsTexture::load(paths[i], size);
        QMutexLocker lock(&job->mutex);
        for (int i = 0; i < 4; ++i)
            job->images[i] = std::move(images[i]);
        job->done = true;
    });
}

// Color of the ship lights (mask of the mod textures)
QVector3D BattleView::factionLights(const QString &faction)
{
    if (faction == "Goa'uld")
        return {1.0f, 0.72f, 0.3f};
    if (faction == "Wraith")
        return {0.4f, 1.0f, 0.5f};
    if (faction == "Lucian Alliance")
        return {1.0f, 0.42f, 0.3f};
    if (faction == "Ori")
        return {1.0f, 0.6f, 0.2f};
    if (faction == "Drones")
        return {1.0f, 0.3f, 0.25f};
    if (faction == "Tau'ri")
        return {0.45f, 0.65f, 1.0f};
    return {0.55f, 0.9f, 1.0f};                 // Ancients, Asgard: cool blue
}

QString BattleView::meshKeyOf(const ShipClass &shipClass)
{
    if (shipClass.picture == "ancients_destiny")
        return QStringLiteral("ancientsdestiny");
    if (!shipClass.picture.isEmpty())
        return shipClass.picture;
    // Ships of the library without a picture: closest model of the mod
    static const QHash<QString, QString> kByName{
        {"Asgard Cruiser", "asgard_cruiser_heavy"},
        {"Asgard Scout", "asgard_corvette_scout"},
        {"Lantean Warship", "ancients_battlecruiser"},
    };
    return kByName.value(shipClass.name);
}

QString BattleView::flagshipMeshKey() const
{
    if (m_flagshipStyle.isEmpty())
        return QStringLiteral("ancientsdestiny");
    if (m_flagshipStyle == "Goa'uld" || m_flagshipStyle == "Lucian Alliance")
        return QStringLiteral("goauld_cruiser_hatak");
    if (m_flagshipStyle == "Wraith")
        return QStringLiteral("wraith_titan_superhive");
    if (m_flagshipStyle == "Asgard")
        return QStringLiteral("asgard_capital_oneill");
    if (m_flagshipStyle == "Ancients")
        return QStringLiteral("ancients_aurora");
    return QString();                   // Tau'ri, Ori: no model in the mod
}

bool BattleView::drawModShip(const QString &key, const QMatrix4x4 &model, float length, const QVector3D &color,
                             float flash, const QVector3D &lights)
{
    m_shipLights = lights;
    ModModel *ship = modModel(key);
    if (!ship)
        return false;
    QMatrix4x4 m = model;
    m.scale(length);
    drawMesh(ship->mesh, m, color, flash, 1.0f, ModeHull, &ship->textures);
    return true;
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
        if (drawModShip(meshKeyOf(classOf(e)), m, e.cls == EnemyClass::Mothership ? 9.0f : 2.6f, tint,
                        flash + (e.diving ? 0.4f : 0.0f), factionLights(classOf(e).faction)))
            continue;
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
    // Engines of the flagship: on the mod model when there is one
    const float engines = subsystemHealth("engines") / 100.0f;
    const bool modFlagship = modModel(flagshipMeshKey()) != nullptr;
    if (modFlagship)
        drawEngineSpots(flagshipMeshKey(), m_destinyModel, 13.5f, kEngineGlow * (0.3f + 0.7f * engines),
                        0.4f + 0.6f * engines);
    const bool hatak = modFlagship || m_flagshipStyle == "Goa'uld" || m_flagshipStyle == "Lucian Alliance" || m_flagshipStyle == "Ori";
    for (float angle = 160.0f; angle <= 200.0f && !hatak; angle += 10.0f) {
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
        const QString key = meshKeyOf(classOf(e));
        if (modModel(key)) {
            QMatrix4x4 m;
            m.translate(e.pos);
            m.rotate(QQuaternion::rotationTo({1, 0, 0}, e.facing));
            drawEngineSpots(key, m, e.cls == EnemyClass::Mothership ? 9.0f : 2.6f, factionLights(classOf(e).faction), 1.0f);
            continue;
        }
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

    drawRays();

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

    // Red alert: pulsing red light at the edges (low hull, fires, boarders)
    if (m_hull < 30.0f || m_redAlert) {
        const float pulse = 0.5f + 0.5f * std::sin(m_time * 4.0f);
        QRadialGradient glow(rect().center(), std::max(width(), height()) * 0.75);
        glow.setColorAt(0.55, QColor(255, 0, 0, 0));
        glow.setColorAt(1.0, QColor(255, 20, 10, int(40 + 70 * pulse)));
        p.fillRect(rect(), glow);
    }
    if (m_observing) {
        p.setPen(QColor(200, 220, 255, 200));
        QFont big = font;
        big.setPixelSize(16);
        big.setLetterSpacing(QFont::AbsoluteSpacing, 4);
        p.setFont(big);
        p.drawText(QRectF(0, height() - 60, width(), 24), Qt::AlignCenter, tr("OBSERVATION DECK"));
        p.setFont(font);
        p.drawText(QRectF(0, height() - 36, width(), 20), Qt::AlignCenter, tr("the crew rests its eyes - 'observe off' to go back"));
        return;
    }

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
        if (!assistSetting())
            continue;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 90, 90, 200));
        p.drawRect(bar);
        p.setBrush(QColor(60, 230, 90));
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * e->hp / e->maxHp, bar.height()));
        if (e->maxShield > 0.0f) {
            const QRectF shieldBar = bar.translated(0, -7);
            p.setBrush(QColor(90, 90, 90, 200));
            p.drawRect(shieldBar);
            p.setBrush(QColor(110, 200, 255));
            p.drawRect(QRectF(shieldBar.left(), shieldBar.top(), shieldBar.width() * e->shield / e->maxShield,
                              shieldBar.height()));
        }

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
        if (!assistSetting())
            continue;
        const qreal w = std::max(30.0, r * 1.8);
        const QRectF bar(center.x() - w / 2, center.y() - r - 10, w, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(90, 90, 90, 200));
        p.drawRect(bar);
        p.setBrush(blue);
        p.drawRect(QRectF(bar.left(), bar.top(), bar.width() * a.hp / a.maxHp, bar.height()));
        if (a.maxShield > 0.0f) {
            const QRectF shieldBar = bar.translated(0, -6);
            p.setBrush(QColor(90, 90, 90, 200));
            p.drawRect(shieldBar);
            p.setBrush(QColor(200, 240, 255));
            p.drawRect(QRectF(shieldBar.left(), shieldBar.top(), shieldBar.width() * a.shield / a.maxShield,
                              shieldBar.height()));
        }
    }

    // --- Weapons of the flagship, on their hardpoints (mod model)
    if (const ModModel *ship = modModel(flagshipMeshKey())) {
        QMap<QString, std::pair<QVector3D, int>> groups;
        for (const ModMesh::AttachPoint &point : ship->points) {
            QString kind;
            if (point.name.startsWith(QLatin1String("weapon.")))
                kind = point.name.mid(7).section(QRegularExpression("[-_.0-9]"), 0, 0);
            else if (point.name.startsWith(QLatin1String("child.")) && point.name.contains(QLatin1String("turret")))
                kind = QStringLiteral("turret");
            if (kind.isEmpty())
                continue;
            auto &group = groups[kind];
            group.first += point.pos;
            ++group.second;
        }
        QMatrix4x4 m = m_destinyModel;
        m.scale(13.5f);
        const QFont before = p.font();
        QFont small = before;
        small.setPixelSize(std::max(8, qRound(before.pixelSize() * 0.85)));
        p.setFont(small);
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            QPointF a;
            if (!project(m.map(it->first / float(it->second)), &a))
                continue;
            const QString &kind = it.key();
            const QString label = kind == "main" ? QCoreApplication::translate("Balance", m_loadout.mainName)
                                : kind == "pulse"               ? tr("Pulse")
                                : kind == "turret"              ? tr("Towers")
                                : kind == "beam"                ? tr("Beam")
                                : kind.contains("drone")        ? tr("Drones")
                                : kind.contains("missile")      ? tr("Missiles")
                                : kind.contains("railgun")      ? tr("Railguns")
                                                                : kind.left(1).toUpper() + kind.mid(1);
            const QColor cyan(120, 220, 255, 220);
            p.setPen(QPen(cyan, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(a.x() - 3, a.y() - 3, 6, 6));
            p.drawLine(a, a + QPointF(14, -14));
            p.drawText(a + QPointF(16, -16), label);
        }
        p.setFont(before);
    }

    // --- Destiny subsystem callouts (combat assist only: otherwise the HUD panels)
    for (const Subsystem &s : m_subsystems) {
        if (!assistSetting())
            break;
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
