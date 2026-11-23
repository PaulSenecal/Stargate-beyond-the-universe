#ifndef GAMEMENU_H
#define GAMEMENU_H

#include <QWidget>

#include "src/render/battleview.h"

class CommandConsole;
class CommandManager;

// ================================================================
//  GameMenu
//  Main in-game screen, shown when the game starts:
//   - left:   map (with legend) / navigation / ship status screens
//   - center: real-time 3D battle view (BattleView)
//   - bottom: command console + weapon status screen
//  Commands typed in the console are validated by CommandManager
//  and then dispatched to the battle view.
// ================================================================

class GameMenu : public QWidget
{
    Q_OBJECT

public:
    explicit GameMenu(QWidget *parent = nullptr);

private slots:
    void onCommandEntered(const QString &command);
    void onBattleLog(const QString &text, BattleView::LogLevel level);

private:
    void dispatch(const QString &name, const QStringList &args);
    void showHelp();
    void logReport(const QString &report);

    BattleView *m_battle = nullptr;
    CommandConsole *m_console = nullptr;
    CommandManager *m_commands = nullptr;
};

#endif // GAMEMENU_H
