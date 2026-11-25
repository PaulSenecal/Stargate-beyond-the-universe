#ifndef GAMECONFIG_H
#define GAMECONFIG_H

#include <QJsonObject>
#include <QString>
#include <QStringList>

// ================================================================
//  GameConfig
//  Settings that change while playing: difficulty, game speed and
//  accessibility. They live in a JSON file next to the saves
//  (config.json in the AppData folder), read at start-up, so they can
//  be tuned without recompiling. The difficulty presets of the
//  settings panel write that file; "custom" keeps the values typed
//  in it by hand. Defaults come from Balance (src/core/balance.h).
//
//  Core only (no QtGui): also used by the conquest logic and the tests.
// ================================================================

class GameConfig
{
public:
    enum class Difficulty { Easy, Normal, Hard, Custom };

    struct Values {
        Difficulty difficulty = Difficulty::Normal;
        // Battles
        float shipHullScale = 1.0f;         // hull of the other ships (longer battles)
        float shipShieldScale = 1.0f;       // shields of the other ships
        float damageToPlayer = 1.0f;        // damage the player's ship takes
        float enemyFireRate = 1.0f;         // < 1: enemies shoot less often
        double autoBattleRate = 0.04;       // conquest battles far from the flagship
        // Accuracy: shots lose precision with the distance
        float closeRange = 18.0f;           // full accuracy under this distance…
        float longRange = 60.0f;            // …and the lowest one beyond this one
        float longRangeAccuracy = 0.55f;
        // Speed of the whole game (0.5 = half speed: more time to type)
        float gameSpeed = 1.0f;
        // Ironman: a single save, deleted when the ship is lost
        bool ironman = false;
        // Accessibility
        float textScale = 1.0f;             // console and panels
        bool colorBlind = false;            // blue / orange instead of green / red
        int seed = 0;                       // 0 = random universe at each new game
    };

    static GameConfig &instance();

    const Values &values() const { return m_values; }
    // Applies a preset (Easy / Normal / Hard) and saves the file
    void setDifficulty(Difficulty difficulty);
    void setGameSpeed(float speed);
    void setTextScale(float scale);
    void setColorBlind(bool on);
    void setIronman(bool on);
    void setSeed(int seed);

    // Reads the file (creates it with the defaults when missing)
    bool load();
    bool save() const;
    QString path() const;
    void setPath(const QString &path) { m_path = path; }   // tests

    static Values preset(Difficulty difficulty);
    // Battle values set by a mod ("config" of a mod file), on top of the preset
    void applyOverrides(const QJsonObject &json);
    static QString difficultyName(Difficulty difficulty);
    static Difficulty difficultyFromName(const QString &name);

    // Chance that a shot at `distance` hits (sensors / evasion apply on top)
    float accuracyAt(float distance) const;

private:
    GameConfig() = default;
    Values m_values;
    QString m_path;
    bool m_loaded = false;
};

// Short accessor for the values
inline const GameConfig::Values &gameConfig()
{
    return GameConfig::instance().values();
}

#endif // GAMECONFIG_H
