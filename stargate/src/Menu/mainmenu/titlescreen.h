#ifndef TITLESCREEN_H
#define TITLESCREEN_H

#include <QPixmap>
#include <QWidget>

class QStackedWidget;
class SettingsPanel;
class SoundSystem;

// ================================================================
//  TitleScreen
//  Shown after "Quit to title screen": continue the saved game,
//  settings, quit. The background is the Destiny plan.
// ================================================================

class TitleScreen : public QWidget
{
    Q_OBJECT

public:
    explicit TitleScreen(SoundSystem *sound, QWidget *parent = nullptr);

    void showSettings();

signals:
    void playRequested();
    void quitRequested();
    void languageChanged(const QString &code);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QPixmap m_background;
    QStackedWidget *m_pages = nullptr;
    SettingsPanel *m_settings = nullptr;
    QWidget *m_firstButton = nullptr;
};

#endif // TITLESCREEN_H
