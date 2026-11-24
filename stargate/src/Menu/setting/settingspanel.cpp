#include "settingspanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include "src/audio/musicplayer.h"
#include "src/audio/soundsystem.h"
#include "src/core/language.h"
#include "src/render/ddstexture.h"

SettingsPanel::SettingsPanel(SoundSystem *sound, QWidget *parent)
    : QWidget(parent)
    , m_sound(sound)
{
    auto *heading = new QLabel(tr("SETTINGS"), this);
    heading->setObjectName("heading");
    heading->setAlignment(Qt::AlignCenter);

    // --- Sound
    auto *soundTitle = new QLabel(tr("Sound effects volume"), this);
    soundTitle->setObjectName("field");
    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 100);
    m_volume->setPageStep(10);
    m_volume->setMinimumWidth(260);
    m_volumeValue = new QLabel(this);
    m_volumeValue->setMinimumWidth(44);
    auto *musicTitle = new QLabel(tr("Music volume"), this);
    musicTitle->setObjectName("field");
    m_music = new QSlider(Qt::Horizontal, this);
    m_music->setRange(0, 100);
    m_music->setPageStep(10);
    m_musicValue = new QLabel(this);
    m_musicValue->setMinimumWidth(44);
    m_mute = new QCheckBox(tr("Mute"), this);
    m_soundStatus = new QLabel(this);
    m_soundStatus->setObjectName("note");
    m_soundStatus->setWordWrap(true);

    // --- Display
    m_fullScreen = new QCheckBox(tr("Full screen"), this);
    auto *languageTitle = new QLabel(tr("Language"), this);
    languageTitle->setObjectName("field");
    m_language = new QComboBox(this);
    for (const auto &language : Language::available())
        m_language->addItem(language.second, language.first);
    auto *texturesTitle = new QLabel(tr("Ship textures"), this);
    texturesTitle->setObjectName("field");
    m_textures = new QComboBox(this);
    m_textures->addItem(tr("Normal (1024)"), 0);
    m_textures->addItem(tr("High (2048)"), 1);
    m_textures->addItem(tr("Ultra (4096)"), 2);
    m_textures->setToolTip(tr("Textures of the mod ship models (if the mod folder is installed)"));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->addWidget(soundTitle, 0, 0, 1, 2);
    grid->addWidget(m_volume, 1, 0);
    grid->addWidget(m_volumeValue, 1, 1);
    grid->addWidget(musicTitle, 2, 0, 1, 2);
    grid->addWidget(m_music, 3, 0);
    grid->addWidget(m_musicValue, 3, 1);
    grid->addWidget(m_mute, 4, 0, 1, 2);
    grid->addWidget(m_soundStatus, 5, 0, 1, 2);
    grid->setRowMinimumHeight(6, 12);
    grid->addWidget(m_fullScreen, 7, 0, 1, 2);
    auto *languageRow = new QHBoxLayout;
    languageRow->addWidget(languageTitle);
    languageRow->addWidget(m_language);
    languageRow->addStretch(1);
    grid->addLayout(languageRow, 8, 0, 1, 2);
    auto *texturesRow = new QHBoxLayout;
    texturesRow->addWidget(texturesTitle);
    texturesRow->addWidget(m_textures);
    texturesRow->addStretch(1);
    grid->addLayout(texturesRow, 9, 0, 1, 2);

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
    connect(m_music, &QSlider::valueChanged, this, [this](int value) {
        if (value > 0)
            m_lastMusic = value;
        applyVolume();
    });
    connect(m_mute, &QCheckBox::toggled, this, [this](bool muted) {
        if (muted && m_volume->value() > 0)
            m_lastVolume = m_volume->value();
        if (muted && m_music->value() > 0)
            m_lastMusic = m_music->value();
        applyVolume();
    });
    connect(m_fullScreen, &QCheckBox::toggled, this, [this](bool on) {
        QSettings("Stargate", "stargate").setValue("display/fullScreen", on);
        if (QWidget *w = window())
            on ? w->showFullScreen() : w->showMaximized();
    });
    connect(m_language, &QComboBox::currentIndexChanged, this, [this] {
        const QString code = m_language->currentData().toString();
        if (code == Language::current())
            return;
        Language::save(code);
        emit languageChanged(code);
    });
    connect(m_textures, &QComboBox::currentIndexChanged, this, [this] {
        DdsTexture::setQualitySetting(m_textures->currentData().toInt());
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
    MusicPlayer *music = m_sound ? m_sound->music() : nullptr;
    const int musicVolume = music ? music->volume() : 0;
    const QSignalBlocker blockVolume(m_volume), blockMute(m_mute), blockScreen(m_fullScreen),
        blockLanguage(m_language), blockMusic(m_music), blockTextures(m_textures);
    m_textures->setCurrentIndex(std::max(0, m_textures->findData(DdsTexture::qualitySetting())));
    if (musicVolume > 0)
        m_lastMusic = musicVolume;
    m_music->setValue(volume == 0 && musicVolume == 0 ? m_lastMusic : musicVolume);
    m_language->setCurrentIndex(std::max(0, m_language->findData(Language::current())));
    if (volume > 0)
        m_lastVolume = volume;
    m_volume->setValue(volume > 0 ? volume : m_lastVolume);
    m_mute->setChecked(volume == 0 && musicVolume == 0);
    m_fullScreen->setChecked(window() && window()->isFullScreen());
    applyVolume();
}

void SettingsPanel::applyVolume()
{
    const bool muteAll = m_mute->isChecked();
    const int volume = muteAll ? 0 : m_volume->value();
    const int musicVolume = muteAll ? 0 : m_music->value();
    m_volume->setEnabled(!muteAll);
    m_music->setEnabled(!muteAll);
    m_volumeValue->setText(volume == 0 ? tr("off") : QStringLiteral("%1%").arg(volume));
    m_musicValue->setText(musicVolume == 0 ? tr("off") : QStringLiteral("%1%").arg(musicVolume));
    if (!m_sound) {
        m_soundStatus->setText(tr("No audio output"));
        return;
    }
    if (m_sound->volume() != volume)
        m_sound->setVolume(volume);
    MusicPlayer *music = m_sound->music();
    if (music && music->volume() != musicVolume)
        music->setVolume(musicVolume);
    m_soundStatus->setText(m_sound->status() + (music ? QStringLiteral("\n") + music->status() : QString()));
}
