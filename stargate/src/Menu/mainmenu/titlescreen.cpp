#include "titlescreen.h"
#include "src/network/coopprotocol.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QIntValidator>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "src/Menu/menustyle.h"
#include "src/Menu/setting/settingspanel.h"
#include "src/audio/musicplayer.h"
#include "src/audio/soundsystem.h"
#include "src/conquest/conquestscreen.h"

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
    auto *play = new QPushButton(tr("Campaign"), mainPage);
    play->setToolTip(tr("Survival aboard the Destiny - continues your journey"));
    auto *conquest = new QPushButton(tr("Conquest"), mainPage);
    conquest->setToolTip(tr("Pick a faction and conquer a galaxy"));
    auto *coop = new QPushButton(tr("Co-op: join as co-pilot"), mainPage);
    auto *settings = new QPushButton(tr("Settings"), mainPage);
    auto *quit = new QPushButton(tr("Quit game"), mainPage);
    quit->setObjectName("danger");
    auto *mainLayout = new QVBoxLayout(mainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);
    for (QPushButton *b : {play, conquest, coop, settings, quit})
        mainLayout->addWidget(b);
    m_firstButton = play;

    m_settings = new SettingsPanel(sound, m_pages);
    // --- Conquest setup: faction and galaxy size
    auto *conquestPage = new QWidget(m_pages);
    auto *heading = new QLabel(tr("CONQUEST"), conquestPage);
    heading->setObjectName("heading");
    heading->setAlignment(Qt::AlignCenter);
    auto *factionTitle = new QLabel(tr("Your faction"), conquestPage);
    factionTitle->setObjectName("field");
    m_faction = new QComboBox(conquestPage);
    for (const Conquest::FactionInfo &f : Conquest::factionInfos())
        m_faction->addItem(f.id, f.id);
    m_factionText = new QLabel(conquestPage);
    m_factionText->setObjectName("note");
    m_factionText->setWordWrap(true);
    m_factionText->setMinimumHeight(80);
    auto *sizeTitle = new QLabel(tr("Galaxy size"), conquestPage);
    sizeTitle->setObjectName("field");
    m_size = new QComboBox(conquestPage);
    m_size->addItem(tr("Small - 12 planets, 1 enemy faction (about 20 min)"), int(Conquest::Size::Small));
    m_size->addItem(tr("Medium - 24 planets, 2 enemy factions (about 45 min)"), int(Conquest::Size::Medium));
    m_size->addItem(tr("Large - 40 planets, 3 enemy factions (1 h 30 and more)"), int(Conquest::Size::Large));
    m_size->addItem(tr("Huge - 56 planets, every other faction (6 enemies)"), int(Conquest::Size::Huge));
    m_size->setCurrentIndex(1);
    auto *startConquest = new QPushButton(tr("Start a new conquest"), conquestPage);
    m_continueConquest = new QPushButton(tr("Continue the conquest"), conquestPage);
    auto *backFromConquest = new QPushButton(tr("Back"), conquestPage);
    auto *conquestLayout = new QVBoxLayout(conquestPage);
    conquestLayout->setContentsMargins(0, 0, 0, 0);
    conquestLayout->setSpacing(10);
    conquestLayout->addWidget(heading);
    conquestLayout->addWidget(factionTitle);
    conquestLayout->addWidget(m_faction);
    conquestLayout->addWidget(m_factionText);
    conquestLayout->addWidget(sizeTitle);
    conquestLayout->addWidget(m_size);
    conquestLayout->addSpacing(8);
    conquestLayout->addWidget(startConquest);
    conquestLayout->addWidget(m_continueConquest);
    conquestLayout->addWidget(backFromConquest);

    auto describeFaction = [this] {
        const QString id = m_faction->currentData().toString();
        const QString text = id == "Tau'ri"            ? tr("Earth's forces: Daedalus battlecruisers and F-302 fighters.")
                           : id == "Goa'uld"           ? tr("The System Lords: Ha'tak motherships, Al'kesh and Death Gliders.")
                           : id == "Wraith"            ? tr("Hive ships, cruisers and swarms of Darts.")
                           : id == "Asgard"            ? tr("The oldest allies of Earth: few ships, but far ahead in technology.")
                           : id == "Ancients"          ? tr("The builders of the Stargates: Lantean warships, drones and Puddle Jumpers.")
                           : id == "Ori"               ? tr("The crusade of Origin: Ori warships, fighters and Priors.")
                                                       : tr("Smugglers and mercenaries: stolen Ha'taks and Gliders.");
        m_factionText->setText(text + QStringLiteral("\n") + ConquestGame::traitSummary(id));
    };
    describeFaction();
    connect(m_faction, &QComboBox::currentIndexChanged, this, describeFaction);
    connect(startConquest, &QPushButton::clicked, this, [this] {
        ConquestGame::Setup setup;
        setup.playerFaction = m_faction->currentData().toString();
        setup.size = Conquest::Size(m_size->currentData().toInt());
        emit conquestRequested(setup, false);
    });
    connect(m_continueConquest, &QPushButton::clicked, this, [this] { emit conquestRequested({}, true); });
    connect(backFromConquest, &QPushButton::clicked, this, [this] {
        MenuStyle::showPage(m_pages, 0);
        m_firstButton->setFocus();
    });
    connect(conquest, &QPushButton::clicked, this, [this] {
        m_continueConquest->setVisible(ConquestScreen::hasSave());
        MenuStyle::showPage(m_pages, 2);
        m_faction->setFocus();
    });

    m_pages->addWidget(mainPage);
    m_pages->addWidget(m_settings);
    m_pages->addWidget(conquestPage);

    // --- Co-op page: address of the captain's game
    auto *coopPage = new QWidget(m_pages);
    auto *coopHeading = new QLabel(tr("CO-OP"), coopPage);
    coopHeading->setObjectName("heading");
    coopHeading->setAlignment(Qt::AlignCenter);
    auto *coopText = new QLabel(tr("The captain opens the co-op in the campaign with 'coop host tactical' or "
                                   "'coop host engineering', and gives you the address shown in the console."),
                                coopPage);
    coopText->setObjectName("note");
    coopText->setWordWrap(true);
    auto *hostField = new QLineEdit(QSettings("Stargate", "stargate").value("coop/host", "127.0.0.1").toString(), coopPage);
    auto *portField = new QLineEdit(QString::number(CoopProtocol::kDefaultPort), coopPage);
    portField->setValidator(new QIntValidator(1, 65535, portField));
    auto *join = new QPushButton(tr("Join the ship"), coopPage);
    auto *backFromCoop = new QPushButton(tr("Back"), coopPage);
    auto *coopLayout = new QVBoxLayout(coopPage);
    coopLayout->setContentsMargins(0, 0, 0, 0);
    coopLayout->setSpacing(10);
    coopLayout->addWidget(coopHeading);
    coopLayout->addWidget(coopText);
    coopLayout->addWidget(new QLabel(tr("Address of the captain"), coopPage));
    coopLayout->addWidget(hostField);
    coopLayout->addWidget(new QLabel(tr("Port"), coopPage));
    coopLayout->addWidget(portField);
    coopLayout->addWidget(join);
    coopLayout->addWidget(backFromCoop);
    m_pages->addWidget(coopPage);
    connect(coop, &QPushButton::clicked, this, [this, coopPage] { m_pages->setCurrentWidget(coopPage); });
    connect(backFromCoop, &QPushButton::clicked, this, [this] { m_pages->setCurrentIndex(0); });
    connect(join, &QPushButton::clicked, this, [this, hostField, portField] {
        QSettings("Stargate", "stargate").setValue("coop/host", hostField->text().trimmed());
        emit coopJoinRequested(hostField->text().trimmed(), quint16(portField->text().toUInt()));
    });

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
