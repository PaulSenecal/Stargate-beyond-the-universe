#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "src/Menu/campaign/campaign.h"
#include <QMessageBox>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class GameMenu;
class QStackedWidget;
class SoundSystem;
class TitleScreen;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void menuBackground();

    void startGame();       // loads the saved game
    void showTitle();       // saves and leaves the game
    // Installs the new language and rebuilds the screens (the game is
    // saved and reloaded), back on the settings page
    void changeLanguage(const QString &code);
    void createTitle();

private slots:
    void on_pushButton_clicked();
    void on_campaignButton_clicked();

private:
    Ui::MainWindow *ui;
    QStackedWidget *m_screens = nullptr;
    SoundSystem *m_sound = nullptr;
    TitleScreen *m_title = nullptr;
    GameMenu *m_game = nullptr;

};
#endif // MAINWINDOW_H
