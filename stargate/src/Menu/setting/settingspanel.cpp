#include "settingspanel.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include "src/audio/soundsystem.h"

SettingsPanel::SettingsPanel(SoundSystem *sound, QWidget *parent)
    : QWidget(parent)
    , m_sound(sound)
{
    auto *heading = new QLabel(tr("SETTINGS"), this);
    heading->setObjectName("heading");
    heading->setAlignment(Qt::AlignCenter);

    // --- Sound
    auto *soundTitle = new QLabel(tr("Sound effects volume"), this);
    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 100);
    m_volume->setPageStep(10);
    m_volume->setMinimumWidth(260);
    m_volumeValue = new QLabel(this);
    m_volumeValue->setMinimumWidth(44);
    m_mute = new QCheckBox(tr("Mute"), this);
    m_soundStatus = new QLabel(this);
    m_soundStatus->setObjectName("note");
    m_soundStatus->setWordWrap(true);

    // --- Display
    m_fullScreen = new QCheckBox(tr("Full screen"), this);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->addWidget(soundTitle, 0, 0, 1, 2);
    grid->addWidget(m_volume, 1, 0);
    grid->addWidget(m_volumeValue, 1, 1);
    grid->addWidget(m_mute, 2, 0, 1, 2);
    grid->addWidget(m_soundStatus, 3, 0, 1, 2);
    grid->setRowMinimumHeight(4, 12);
    grid->addWidget(m_fullScreen, 5, 0, 1, 2);

    auto *back = new QPushButton(tr("Back"), this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);
    layout->addWidget(heading);
    layout->addLayout(grid);
    layout->addWidget(back, 0, Qt::AlignHCenter);

    connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
        if (value > 0)
            m_lastVolume = value;
        applyVolume();
    });
    connect(m_mute, &QCheckBox::toggled, this, [this](bool muted) {
        if (muted && m_volume->value() > 0)
            m_lastVolume = m_volume->value();
        applyVolume();
    });
    connect(m_fullScreen, &QCheckBox::toggled, this, [this](bool on) {
        QSettings("Stargate", "stargate").setValue("display/fullScreen", on);
        if (QWidget *w = window())
            on ? w->showFullScreen() : w->showMaximized();
    });
    connect(back, &QPushButton::clicked, this, &SettingsPanel::backRequested);

    refresh();
}

bool SettingsPanel::fullScreenSetting()
{
    return QSettings("Stargate", "stargate").value("display/fullScreen", false).toBool();
}

void SettingsPanel::refresh()
{
    const int volume = m_sound ? m_sound->volume() : 0;
    const QSignalBlocker blockVolume(m_volume), blockMute(m_mute), blockScreen(m_fullScreen);
    if (volume > 0)
        m_lastVolume = volume;
    m_volume->setValue(volume > 0 ? volume : m_lastVolume);
    m_mute->setChecked(volume == 0);
    m_fullScreen->setChecked(window() && window()->isFullScreen());
    applyVolume();
}

void SettingsPanel::applyVolume()
{
    const bool muted = m_mute->isChecked() || m_volume->value() == 0;
    const int volume = muted ? 0 : m_volume->value();
    m_volume->setEnabled(!m_mute->isChecked());
    m_volumeValue->setText(muted ? tr("off") : QStringLiteral("%1%").arg(volume));
    if (!m_sound) {
        m_soundStatus->setText(tr("No audio output"));
        return;
    }
    if (m_sound->volume() != volume)
        m_sound->setVolume(volume);
    m_soundStatus->setText(m_sound->status());
}
