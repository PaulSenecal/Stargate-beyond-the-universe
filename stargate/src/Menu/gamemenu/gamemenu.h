#ifndef GAMEMENU_H
#define GAMEMENU_H

#include <QJsonObject>
#include <QWidget>

#include "src/core/destinystate.h"
#include "src/core/campaignlife.h"
#include "src/core/interior.h"
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
class ShipPlanDialog;
class ConquestEndScreen;
class CoopHost;

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
    bool interiorCommand(const QString &first, const QStringList &words);
    void updateInterior(float seconds);
    void applyBonuses();
    void showCrew();
    void showPlan();
    void flushInteriorMessages();
    bool payFor(const QList<std::pair<QString, int>> &cost);
    bool lifeCommand(const QString &first, const QStringList &words);
    void applyOutcome(const Outcome &outcome);
    void announceAchievements();
    void showChoice();
    void showHelpTopic(const QString &topic);
    QString suggestLocal(const QString &word) const;
    void endCampaign(bool victory, const QString &heading, const QString &subtitle);
    void onBattleEnded(const BattleStats &stats);
    bool coopCommand(const QString &first, const QStringList &words);
    QStringList coopStatus() const;
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
    QJsonObject m_extra;            // save data of the newer systems (see GameSave::extra)
    Interior m_interior;            // compartments and crew of the Destiny
    ShipPlanDialog *m_planDialog = nullptr;
    CampaignLife m_life;            // choice events, communication stones, statistics
    int m_kinos = 4;                // remote scouts
    QList<int> m_scouted;           // planets of this system already seen by a Kino
    int m_wrecks = 0, m_wreckMotherships = 0;   // wrecks to salvage after a battle
    double m_wreckTimer = 0;
    double m_choiceTimer = 0;       // seconds left to answer a choice event
    double m_wearClock = 0;
    bool m_ended = false;
    int m_lastFires = 0, m_lastBoarders = 0;    // alarms when they rise
    CoopHost *m_coop = nullptr;     // co-pilot over the network
    ConquestEndScreen *m_endScreen = nullptr;          // route stop where the Destiny last dived into a star
};

#endif // GAMEMENU_H
