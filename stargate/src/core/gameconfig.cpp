#include "gameconfig.h"
#include "balance.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

GameConfig &GameConfig::instance()
{
    static GameConfig config;
    if (!config.m_loaded) {
        config.m_loaded = true;
        config.m_values = preset(Difficulty::Normal);
    }
    return config;
}

QString GameConfig::path() const
{
    if (!m_path.isEmpty())
        return m_path;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("config.json");
}

GameConfig::Values GameConfig::preset(Difficulty difficulty)
{
    Values v;
    v.shipHullScale = Balance::kShipHullScale;
    v.shipShieldScale = Balance::kShipShieldScale;
    v.damageToPlayer = Balance::kDamageToPlayerScale;
    v.autoBattleRate = Balance::kAutoBattleRate;
    v.difficulty = difficulty;
    switch (difficulty) {
    case Difficulty::Easy:
        // Longer battles that hurt less: time to type the orders
        v.shipHullScale *= 1.3f;
        v.damageToPlayer *= 0.55f;
        v.enemyFireRate = 0.75f;
        v.autoBattleRate *= 0.6;
        v.gameSpeed = 0.8f;
        break;
    case Difficulty::Hard:
        v.shipHullScale *= 1.2f;
        v.shipShieldScale *= 1.3f;
        v.damageToPlayer *= 1.35f;
        v.enemyFireRate = 1.2f;
        v.longRangeAccuracy = 0.45f;
        break;
    case Difficulty::Normal:
    case Difficulty::Custom:
        break;
    }
    return v;
}

QString GameConfig::difficultyName(Difficulty difficulty)
{
    switch (difficulty) {
    case Difficulty::Easy: return QStringLiteral("easy");
    case Difficulty::Hard: return QStringLiteral("hard");
    case Difficulty::Custom: return QStringLiteral("custom");
    case Difficulty::Normal: break;
    }
    return QStringLiteral("normal");
}

GameConfig::Difficulty GameConfig::difficultyFromName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == "easy") return Difficulty::Easy;
    if (n == "hard") return Difficulty::Hard;
    if (n == "custom") return Difficulty::Custom;
    return Difficulty::Normal;
}

bool GameConfig::load()
{
    m_loaded = true;
    QFile file(path());
    if (!file.open(QIODevice::ReadOnly)) {
        m_values = preset(Difficulty::Normal);
        return save();                      // writes the file with the defaults
    }
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    const Difficulty difficulty = difficultyFromName(o.value("difficulty").toString("normal"));
    // A preset gives every value; "custom" starts from Normal and reads them all
    Values v = preset(difficulty);
    auto number = [&o](const char *key, double fallback, double low, double high) {
        return std::clamp(o.value(key).toDouble(fallback), low, high);
    };
    if (difficulty == Difficulty::Custom) {
        v.shipHullScale = float(number("shipHullScale", v.shipHullScale, 0.1, 10.0));
        v.shipShieldScale = float(number("shipShieldScale", v.shipShieldScale, 0.0, 10.0));
        v.damageToPlayer = float(number("damageToPlayer", v.damageToPlayer, 0.0, 10.0));
        v.enemyFireRate = float(number("enemyFireRate", v.enemyFireRate, 0.1, 5.0));
        v.autoBattleRate = number("autoBattleRate", v.autoBattleRate, 0.001, 1.0);
        v.closeRange = float(number("closeRange", v.closeRange, 1.0, 200.0));
        v.longRange = float(number("longRange", v.longRange, v.closeRange + 1.0, 400.0));
        v.longRangeAccuracy = float(number("longRangeAccuracy", v.longRangeAccuracy, 0.05, 1.0));
    }
    // Not part of the difficulty: always read
    v.gameSpeed = float(number("gameSpeed", v.gameSpeed, 0.25, 2.0));
    v.textScale = float(number("textScale", 1.0, 0.7, 1.8));
    v.colorBlind = o.value("colorBlind").toBool(false);
    v.ironman = o.value("ironman").toBool(false);
    v.seed = o.value("seed").toInt(0);
    m_values = v;
    return true;
}

bool GameConfig::save() const
{
    const QString file = path();
    QDir().mkpath(QFileInfo(file).absolutePath());
    QJsonObject o;
    o.insert("_comment", QStringLiteral("Stargate settings. difficulty: easy | normal | hard | custom. "
                                        "Battle values are only read with \"custom\" (otherwise the preset "
                                        "gives them). gameSpeed 0.25-2, textScale 0.7-1.8, seed 0 = random."));
    o.insert("difficulty", difficultyName(m_values.difficulty));
    o.insert("shipHullScale", double(m_values.shipHullScale));
    o.insert("shipShieldScale", double(m_values.shipShieldScale));
    o.insert("damageToPlayer", double(m_values.damageToPlayer));
    o.insert("enemyFireRate", double(m_values.enemyFireRate));
    o.insert("autoBattleRate", m_values.autoBattleRate);
    o.insert("closeRange", double(m_values.closeRange));
    o.insert("longRange", double(m_values.longRange));
    o.insert("longRangeAccuracy", double(m_values.longRangeAccuracy));
    o.insert("gameSpeed", double(m_values.gameSpeed));
    o.insert("textScale", double(m_values.textScale));
    o.insert("colorBlind", m_values.colorBlind);
    o.insert("ironman", m_values.ironman);
    o.insert("seed", m_values.seed);
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return out.commit();
}

void GameConfig::setDifficulty(Difficulty difficulty)
{
    if (difficulty != Difficulty::Custom) {
        Values v = preset(difficulty);
        v.gameSpeed = m_values.gameSpeed;
        v.textScale = m_values.textScale;
        v.colorBlind = m_values.colorBlind;
        v.ironman = m_values.ironman;
        v.seed = m_values.seed;
        m_values = v;
    }
    m_values.difficulty = difficulty;
    save();
}

void GameConfig::setGameSpeed(float speed)
{
    m_values.gameSpeed = std::clamp(speed, 0.25f, 2.0f);
    save();
}

void GameConfig::setTextScale(float scale)
{
    m_values.textScale = std::clamp(scale, 0.7f, 1.8f);
    save();
}

void GameConfig::setColorBlind(bool on)
{
    m_values.colorBlind = on;
    save();
}

void GameConfig::setIronman(bool on)
{
    m_values.ironman = on;
    save();
}

void GameConfig::setSeed(int seed)
{
    m_values.seed = seed;
    save();
}

void GameConfig::applyOverrides(const QJsonObject &o)
{
    auto number = [&o](const char *key, double current, double low, double high) {
        return std::clamp(o.value(key).toDouble(current), low, high);
    };
    Values &v = m_values;
    v.shipHullScale = float(number("shipHullScale", v.shipHullScale, 0.1, 10.0));
    v.shipShieldScale = float(number("shipShieldScale", v.shipShieldScale, 0.0, 10.0));
    v.damageToPlayer = float(number("damageToPlayer", v.damageToPlayer, 0.0, 10.0));
    v.enemyFireRate = float(number("enemyFireRate", v.enemyFireRate, 0.1, 5.0));
    v.autoBattleRate = number("autoBattleRate", v.autoBattleRate, 0.001, 1.0);
    v.closeRange = float(number("closeRange", v.closeRange, 1.0, 200.0));
    v.longRange = float(number("longRange", v.longRange, v.closeRange + 1.0, 400.0));
    v.longRangeAccuracy = float(number("longRangeAccuracy", v.longRangeAccuracy, 0.05, 1.0));
}

float GameConfig::accuracyAt(float distance) const
{
    const Values &v = m_values;
    if (distance <= v.closeRange)
        return 1.0f;
    if (distance >= v.longRange)
        return v.longRangeAccuracy;
    const float t = (distance - v.closeRange) / (v.longRange - v.closeRange);
    return 1.0f - t * (1.0f - v.longRangeAccuracy);
}
