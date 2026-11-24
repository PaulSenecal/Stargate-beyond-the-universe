#ifndef SETTINGSPANEL_H
#define SETTINGSPANEL_H

#include <QWidget>

class QCheckBox;
class QLabel;
class QSlider;
class SoundSystem;

// ================================================================
//  SettingsPanel
//  Game settings, shared by the title screen and the pause menu:
//  sound volume / mute and full screen. Every change is applied and
//  saved immediately (QSettings), there is no "apply" button.
// ================================================================

class SettingsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPanel(SoundSystem *sound, QWidget *parent = nullptr);

    // Reads the current values again (volume changed by the console…)
    void refresh();

    // Full screen saved in the settings, applied by MainWindow at start-up
    static bool fullScreenSetting();

signals:
    void backRequested();

private:
    void applyVolume();

    SoundSystem *m_sound = nullptr;
    QSlider *m_volume = nullptr;
    QLabel *m_volumeValue = nullptr;
    QCheckBox *m_mute = nullptr;
    QCheckBox *m_fullScreen = nullptr;
    QLabel *m_soundStatus = nullptr;
    int m_lastVolume = 70;
};

#endif // SETTINGSPANEL_H
