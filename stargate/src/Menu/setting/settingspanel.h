#ifndef SETTINGSPANEL_H
#define SETTINGSPANEL_H

#include <QWidget>

class QCheckBox;
class QComboBox;
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
    // The new language is saved; the screens must be rebuilt to show it
    void languageChanged(const QString &code);

private:
    void applyVolume();

    SoundSystem *m_sound = nullptr;
    QSlider *m_volume = nullptr;
    QLabel *m_volumeValue = nullptr;
    QSlider *m_music = nullptr;
    QLabel *m_musicValue = nullptr;
    QCheckBox *m_mute = nullptr;
    QCheckBox *m_fullScreen = nullptr;
    QComboBox *m_language = nullptr;
    QComboBox *m_textures = nullptr;
    QCheckBox *m_assist = nullptr;
    QComboBox *m_difficulty = nullptr;
    QSlider *m_speed = nullptr;
    QLabel *m_speedValue = nullptr;
    QComboBox *m_textSize = nullptr;
    QCheckBox *m_colorBlind = nullptr;
    QLabel *m_soundStatus = nullptr;
    int m_lastVolume = 70;
    int m_lastMusic = 40;
};

#endif // SETTINGSPANEL_H
