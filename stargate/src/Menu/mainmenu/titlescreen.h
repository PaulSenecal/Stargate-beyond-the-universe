#ifndef TITLESCREEN_H
#define TITLESCREEN_H

#include <QPixmap>
#include <QWidget>

#include "src/conquest/conquestgame.h"

class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class SettingsPanel;
class SoundSystem;

// ================================================================
//  TitleScreen
//  First screen: the campaign (survival aboard the Destiny), the
//  conquest mode (faction and galaxy size), settings, quit. The
//  background is the Destiny plan.
// ================================================================

class TitleScreen : public QWidget
{
    Q_OBJECT

public:
    explicit TitleScreen(SoundSystem *sound, QWidget *parent = nullptr);

    void showSettings();

signals:
    void playRequested();
    void conquestRequested(const ConquestGame::Setup &setup, bool continueSaved);
    void coopJoinRequested(const QString &host, quint16 port);
    void quitRequested();
    void languageChanged(const QString &code);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    QPixmap m_background;
    SoundSystem *m_sound = nullptr;
    QStackedWidget *m_pages = nullptr;
    SettingsPanel *m_settings = nullptr;
    QWidget *m_firstButton = nullptr;
    QComboBox *m_faction = nullptr;
    QComboBox *m_size = nullptr;
    QLabel *m_factionText = nullptr;
    QPushButton *m_continueConquest = nullptr;
};

#endif // TITLESCREEN_H
