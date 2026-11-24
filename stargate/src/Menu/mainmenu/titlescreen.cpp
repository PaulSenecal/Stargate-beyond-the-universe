#include "titlescreen.h"

#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "src/Menu/menustyle.h"
#include "src/Menu/setting/settingspanel.h"
#include "src/audio/musicplayer.h"
#include "src/audio/soundsystem.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"     // set by stargate.pro from git
#endif

TitleScreen::TitleScreen(SoundSystem *sound, QWidget *parent)
    : QWidget(parent)
    , m_background(":/images/background/destinyPlan.jpg")
    , m_sound(sound)
{
    setStyleSheet(MenuStyle::sheet());

    auto *title = new QLabel(tr("STARGATE"), this);
    title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter);
    auto *subtitle = new QLabel(tr("BEYOND THE UNIVERSE"), this);
    subtitle->setObjectName("subtitle");
    subtitle->setAlignment(Qt::AlignCenter);

    auto *panel = new QFrame(this);
    panel->setObjectName("panel");
    m_pages = new QStackedWidget(panel);

    auto *mainPage = new QWidget(m_pages);
    auto *play = new QPushButton(tr("Continue the game"), mainPage);
    auto *settings = new QPushButton(tr("Settings"), mainPage);
    auto *quit = new QPushButton(tr("Quit game"), mainPage);
    quit->setObjectName("danger");
    auto *mainLayout = new QVBoxLayout(mainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);
    for (QPushButton *b : {play, settings, quit})
        mainLayout->addWidget(b);
    m_firstButton = play;

    m_settings = new SettingsPanel(sound, m_pages);
    m_pages->addWidget(mainPage);
    m_pages->addWidget(m_settings);

    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(36, 28, 36, 28);
    panelLayout->addWidget(m_pages);

    auto *version = new QLabel(QStringLiteral(APP_VERSION), this);
    version->setObjectName("note");
    version->setAlignment(Qt::AlignRight);

    auto *layout = new QVBoxLayout(this);
    layout->addStretch(2);
    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addSpacing(28);
    layout->addWidget(panel, 0, Qt::AlignHCenter);
    layout->addStretch(3);
    layout->addWidget(version);

    connect(m_settings, &SettingsPanel::languageChanged, this, &TitleScreen::languageChanged);
    connect(play, &QPushButton::clicked, this, &TitleScreen::playRequested);
    connect(quit, &QPushButton::clicked, this, &TitleScreen::quitRequested);
    connect(settings, &QPushButton::clicked, this, [this] {
        m_settings->refresh();
        MenuStyle::showPage(m_pages, 1);
    });
    connect(m_settings, &SettingsPanel::backRequested, this, [this] {
        MenuStyle::showPage(m_pages, 0);
        m_firstButton->setFocus();
    });
}

void TitleScreen::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_sound && m_sound->music())
        m_sound->music()->setMood(MusicPlayer::Mood::Title);
    if (m_pages->currentIndex() != 0)
        return;
    MenuStyle::showPage(m_pages, 0);
    m_firstButton->setFocus();
}

void TitleScreen::showSettings()
{
    m_settings->refresh();
    MenuStyle::showPage(m_pages, 1);
}

// Background picture scaled to cover the screen, darkened for the text
void TitleScreen::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(6, 6, 8));
    if (!m_background.isNull()) {
        const QSize size = m_background.size().scaled(this->size(), Qt::KeepAspectRatioByExpanding);
        const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(target, m_background);
    }
    p.fillRect(rect(), QColor(0, 0, 0, 120));
}
