#ifndef GAMEMENU_H
#define GAMEMENU_H

#include <QJsonObject>
#include <QWidget>

#include "src/core/destinystate.h"
#include "src/render/battleview.h"
#include "src/world/destinyroute.h"

class CommandConsole;
class CommandManager;
class QuestSystem;
class RouteMapWidget;
class ShipStatusWidget;
class RadarWidget;
class WeaponStatusWidget;
class ShipLibraryDialog;
class SoundSystem;
class QTimer;
class QPushButton;
class PauseMenu;

// ================================================================
//  GameMenu
//  Main in-game screen, shown when the game starts:
//   - left:   tactical radar / live ship damage panel
//   - center: real-time 3D battle view (BattleView)
//   - right:  live route map / live weapon panel
//   - bottom: command console on the full width, resizable
//  Commands typed in the console are validated by CommandManager
//  and then dispatched to the battle view or the Destiny route
//  (FTL jumps, gate window, exploration, crew supplies). The Destiny state (system
//  health, resources) is loaded from and saved to the database.
// ================================================================

class GameMenu : public QWidget
{
    Q_OBJECT

public:
    // sound: owned by the main window (shared with the title screen)
    explicit GameMenu(SoundSystem *sound, QWidget *parent = nullptr);
    ~GameMenu() override;     // saves the game

    void openPauseMenu();
    void openSettings();          // pause menu on its settings page

signals:
    void quitToTitleRequested();
    void quitGameRequested();
    void languageChanged(const QString &code);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onCommandEntered(const QString &command);
    void onBattleLog(const QString &text, BattleView::LogLevel level);
    void onRouteNotice(const QString &text, DestinyRoute::Severity severity);
    void simulate();

private:
    void dispatch(const QString &name, const QStringList &args);
    void showHelp();
    void logReport(const QString &report);
    void loadSave();
    void repair(const QString &system);
    void showResources();
    void showLibrary(const QString &ship);
    void soundCommand(const QString &arg);
    void musicCommand(const QString &arg);
    void updateMusic(bool immediate);

    // Exploration (DestinyRoute)
    void jumpFtl();
    void performJump();             // after the FTL drive spooled
    void diveIntoStar();
    bool localCommand(const QString &first, const QStringList &words);
    void dialGate(const QString &address);
    void explore();
    void recordCurrentSystem();
    void restoreGame();
    bool saveGame(bool announce);
    void setPaused(bool paused);
    void updateNavigation();
    void updatePlanetView();
    void updateMap();
    void loadQuests();
    void updateObjectives();
    QString systemReport() const;
    QString routeReport() const;

    BattleView *m_battle = nullptr;
    CommandConsole *m_console = nullptr;
    RouteMapWidget *m_map = nullptr;
    ShipStatusWidget *m_shipStatus = nullptr;
    RadarWidget *m_radar = nullptr;
    WeaponStatusWidget *m_weapons = nullptr;
    QuestSystem *m_quests = nullptr;
    QList<ShipClass> m_library = ShipClass::fallbackRoster();
    ShipLibraryDialog *m_libraryDialog = nullptr;
    SoundSystem *m_sound = nullptr;
    PauseMenu *m_pauseMenu = nullptr;
    QPushButton *m_menuButton = nullptr;
    CommandManager *m_commands = nullptr;
    DestinyState m_destiny;
    DestinyRoute *m_route = nullptr;
    QTimer *m_simTimer = nullptr;
    QTimer *m_autosaveTimer = nullptr;
    int m_sessionId = -1;
    bool m_starving = false;
    double m_starvingHours = 0;
    bool m_lowWaterWarned = false;
    bool m_lowFoodWarned = false;
    int m_calmSeconds = 0;          // without enemy, before the calm music
    int m_starUsedAt = -1;
    QJsonObject m_extra;            // save data of the newer systems (see GameSave::extra)          // route stop where the Destiny last dived into a star
};

#endif // GAMEMENU_H
