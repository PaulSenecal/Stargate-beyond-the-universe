#ifndef CONQUESTSCREEN_H
#define CONQUESTSCREEN_H

#include <QHash>
#include <QWidget>
#include <memory>

#include "src/core/interior.h"

#include "conquestgame.h"

class BattleView;
class CommandConsole;
class CommandPanel;
class ConquestEndScreen;
class GalaxyMapWidget;
class PauseMenu;
class QLabel;
class QPushButton;
class QTimer;
class RadarWidget;
class ShipStatusWidget;
class WeaponStatusWidget;
class SoundSystem;

// ================================================================
//  ConquestScreen
//  Conquest mode screen: galaxy map and planet panel (left), the 3D
//  battle view (center) where the flagship fights the planet fleets,
//  the tactical panels of the campaign - radar, ship damage, weapons -
//  (right, optional: HUD button or 'hud on|off') and the command
//  console (bottom). Escape / MENU opens the pause
//  menu. The game is saved in conquest.json (data folder) when
//  leaving, and can be continued from the title screen.
// ================================================================

class ShipCrew;

class ConquestScreen : public QWidget
{
    Q_OBJECT

public:
    // New game (setup) or the saved one (setup ignored if loaded)
    ConquestScreen(SoundSystem *sound, const ConquestGame::Setup &setup, bool continueSaved,
                   QWidget *parent = nullptr);
    ~ConquestScreen() override;     // saves the game

    static QString savePath();
    static bool hasSave();

    void openSettings();

signals:
    void quitToTitleRequested();
    void quitGameRequested();
    void languageChanged(const QString &code);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onCommand(const QString &command);
    void showHelpTopic(const QString &topic);
    QStringList completeLine(const QString &text) const;
    void onMessage(const QString &text, Conquest::Tone tone);
    void onBattleStarted(int planetId, const QString &faction, int motherships, int fighters);
    void tick();
    void refresh();
    void showHelp();
    void listPlanets();
    void listFleets();
    void listTechs();
    QString planetSummary(int planetId) const;
    bool save();
    void setPaused(bool paused);
    void openPauseMenu();
    void updatePlanetLook();
    void setHudVisible(bool visible);
    void setupHud();
    void joinFriendlyFleets();      // own and allied fleets at the flagship's planet: in orbit, in the battle
    void reportAllySurvivors();     // their losses go back to the fleets
    void showEnd(bool victory);

    SoundSystem *m_sound = nullptr;
    ConquestGame *m_game = nullptr;
    GalaxyMapWidget *m_map = nullptr;
    QLabel *m_info = nullptr;
    BattleView *m_battle = nullptr;
    CommandConsole *m_console = nullptr;
    PauseMenu *m_pauseMenu = nullptr;
    QPushButton *m_menuButton = nullptr;
    QPushButton *m_hudButton = nullptr;
    QPushButton *m_techButton = nullptr;
    CommandPanel *m_command = nullptr;
    QWidget *m_hud = nullptr;
    RadarWidget *m_radar = nullptr;
    ShipStatusWidget *m_shipStatus = nullptr;
    WeaponStatusWidget *m_weapons = nullptr;
    QTimer *m_timer = nullptr;
    int m_lookPlanet = -1;
    ConquestEndScreen *m_end = nullptr;
    struct Joined { int spawnedMotherships, spawnedFighters, motherships, fighters; };
    QHash<int, Joined> m_joined;        // fleet id -> ships sent into the 3D battle
    double m_playSeconds = 0;
    double m_supplyClock = 0;       // restocking of the flagship on its worlds
    std::unique_ptr<Interior> m_interior;   // compartments and crew of the flagship
    std::unique_ptr<ShipCrew> m_crew;
    double m_interiorClock = 0;
    int m_missileClock = 0;
    int m_droneClock = 0;
    int m_battlesWon = 0;
    int m_battlesLost = 0;
    bool m_wasTravelling = false;
    int m_calmSeconds = 0;
    double m_musicClock = 0;
};

#endif // CONQUESTSCREEN_H
