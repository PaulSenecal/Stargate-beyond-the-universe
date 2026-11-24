#ifndef GAMEMENU_H
#define GAMEMENU_H

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
class QTimer;

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
    explicit GameMenu(QWidget *parent = nullptr);
    ~GameMenu() override;     // saves the game

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

    // Exploration (DestinyRoute)
    void jumpFtl();
    void dialGate(const QString &address);
    void explore();
    void recordCurrentSystem();
    void restoreGame();
    void saveGame(bool announce);
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
};

#endif // GAMEMENU_H
