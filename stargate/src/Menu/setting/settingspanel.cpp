#include "settingspanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QUrl>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include "src/audio/musicplayer.h"
#include "src/audio/soundsystem.h"
#include "src/core/gameconfig.h"
#include "src/core/language.h"
#include "src/render/battleview.h"
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
    m_assist = new QCheckBox(tr("Combat assist (health bars of the ships, damage callouts on the hull)"), this);
    m_assist->setToolTip(tr("Off: watch the ship status panels of the HUD to know your damage"));
    grid->addWidget(m_assist, 10, 0, 1, 2);
    connect(m_assist, &QCheckBox::toggled, this, [](bool on) { BattleView::setAssistSetting(on); });

    // --- Gameplay: difficulty and speed (saved in config.json, see GameConfig)
    auto *difficultyTitle = new QLabel(tr("Difficulty"), this);
    difficultyTitle->setObjectName("field");
    m_difficulty = new QComboBox(this);
    m_difficulty->addItem(tr("Easy - long battles, less damage"), int(GameConfig::Difficulty::Easy));
    m_difficulty->addItem(tr("Normal"), int(GameConfig::Difficulty::Normal));
    m_difficulty->addItem(tr("Hard"), int(GameConfig::Difficulty::Hard));
    m_difficulty->addItem(tr("Custom (config file)"), int(GameConfig::Difficulty::Custom));
    auto *openConfig = new QPushButton(tr("Config file"), this);
    openConfig->setToolTip(tr("Opens config.json: with the Custom difficulty, every battle value can be tuned there"));
    auto *difficultyRow = new QHBoxLayout;
    difficultyRow->addWidget(difficultyTitle);
    difficultyRow->addWidget(m_difficulty);
    difficultyRow->addWidget(openConfig);
    difficultyRow->addStretch(1);
    grid->addLayout(difficultyRow, 11, 0, 1, 2);
    auto *speedTitle = new QLabel(tr("Game speed (lower = more time to type orders)"), this);
    speedTitle->setObjectName("field");
    m_speed = new QSlider(Qt::Horizontal, this);
    m_speed->setRange(25, 200);
    m_speed->setPageStep(25);
    m_speedValue = new QLabel(this);
    grid->addWidget(speedTitle, 12, 0, 1, 2);
    grid->addWidget(m_speed, 13, 0);
    grid->addWidget(m_speedValue, 13, 1);

    // --- Accessibility
    auto *textTitle = new QLabel(tr("Console text size"), this);
    textTitle->setObjectName("field");
    m_textSize = new QComboBox(this);
    m_textSize->addItem(tr("Small"), 0.85);
    m_textSize->addItem(tr("Normal"), 1.0);
    m_textSize->addItem(tr("Large"), 1.25);
    m_textSize->addItem(tr("Very large"), 1.5);
    m_textSize->setToolTip(tr("Applied to the console when a game starts"));
    auto *textRow = new QHBoxLayout;
    textRow->addWidget(textTitle);
    textRow->addWidget(m_textSize);
    textRow->addStretch(1);
    grid->addLayout(textRow, 14, 0, 1, 2);
    m_colorBlind = new QCheckBox(tr("Colour-blind palette (blue / orange instead of green / red)"), this);
    grid->addWidget(m_colorBlind, 15, 0, 1, 2);

    connect(m_difficulty, &QComboBox::currentIndexChanged, this, [this] {
        GameConfig::instance().setDifficulty(GameConfig::Difficulty(m_difficulty->currentData().toInt()));
        refresh();
    });
    connect(openConfig, &QPushButton::clicked, this, [] {
        GameConfig::instance().save();
        QDesktopServices::openUrl(QUrl::fromLocalFile(GameConfig::instance().path()));
    });
    connect(m_speed, &QSlider::valueChanged, this, [this](int value) {
        GameConfig::instance().setGameSpeed(float(value) / 100.0f);
        m_speedValue->setText(QStringLiteral("%1%").arg(value));
    });
    connect(m_textSize, &QComboBox::currentIndexChanged, this, [this] {
        GameConfig::instance().setTextScale(float(m_textSize->currentData().toDouble()));
    });
    connect(m_colorBlind, &QCheckBox::toggled, this, [](bool on) { GameConfig::instance().setColorBlind(on); });

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
        blockLanguage(m_language), blockMusic(m_music), blockTextures(m_textures), blockAssist(m_assist),
        blockDifficulty(m_difficulty), blockSpeed(m_speed), blockText(m_textSize), blockColor(m_colorBlind);
    const GameConfig::Values &config = GameConfig::instance().values();
    m_difficulty->setCurrentIndex(std::max(0, m_difficulty->findData(int(config.difficulty))));
    m_speed->setValue(qRound(config.gameSpeed * 100.0f));
    m_speedValue->setText(QStringLiteral("%1%").arg(qRound(config.gameSpeed * 100.0f)));
    int textIndex = 1;
    for (int i = 0; i < m_textSize->count(); ++i)
        if (std::abs(m_textSize->itemData(i).toDouble() - config.textScale) < 0.05)
            textIndex = i;
    m_textSize->setCurrentIndex(textIndex);
    m_colorBlind->setChecked(config.colorBlind);
    m_assist->setChecked(BattleView::assistSetting());
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
