#ifndef CONQUESTSCREEN_H
#define CONQUESTSCREEN_H

#include <QWidget>

#include "conquestgame.h"

class BattleView;
class CommandConsole;
class GalaxyMapWidget;
class PauseMenu;
class QLabel;
class QPushButton;
class QTimer;
class SoundSystem;

// ================================================================
//  ConquestScreen
//  Conquest mode screen: galaxy map and planet panel (left), the 3D
//  battle view (right) where the flagship fights the planet fleets,
//  and the command console (bottom). Escape / MENU opens the pause
//  menu. The game is saved in conquest.json (data folder) when
//  leaving, and can be continued from the title screen.
// ================================================================

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
    void onMessage(const QString &text, Conquest::Tone tone);
    void onBattleStarted(int planetId, const QString &faction, int motherships, int fighters);
    void tick();
    void refresh();
    void showHelp();
    void listPlanets();
    QString planetSummary(int planetId) const;
    bool save();
    void setPaused(bool paused);
    void openPauseMenu();
    void updatePlanetLook();

    SoundSystem *m_sound = nullptr;
    ConquestGame *m_game = nullptr;
    GalaxyMapWidget *m_map = nullptr;
    QLabel *m_info = nullptr;
    BattleView *m_battle = nullptr;
    CommandConsole *m_console = nullptr;
    PauseMenu *m_pauseMenu = nullptr;
    QPushButton *m_menuButton = nullptr;
    QTimer *m_timer = nullptr;
    int m_lookPlanet = -1;
    int m_calmSeconds = 0;
    double m_musicClock = 0;
};

#endif // CONQUESTSCREEN_H
