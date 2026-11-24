#ifndef PAUSEMENU_H
#define PAUSEMENU_H

#include <QWidget>

class QLabel;
class QStackedWidget;
class SettingsPanel;
class SoundSystem;

// ================================================================
//  PauseMenu
//  In-game menu (Escape key, MENU button or 'menu' command): darkens
//  the game and offers resume, save, settings, quit to the title
//  screen and quit the game. The game is paused while it is open.
// ================================================================

class PauseMenu : public QWidget
{
    Q_OBJECT

public:
    explicit PauseMenu(SoundSystem *sound, QWidget *parent = nullptr);

    void open();                            // covers the parent and takes the focus
    void showSaveResult(bool ok, const QString &text);
    void escape();                          // back from the settings, otherwise resume

signals:
    void resumeRequested();
    void saveRequested();
    void quitToTitleRequested();
    void quitGameRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QStackedWidget *m_pages = nullptr;
    SettingsPanel *m_settings = nullptr;
    QLabel *m_saveStatus = nullptr;
    QWidget *m_firstButton = nullptr;
};

#endif // PAUSEMENU_H
