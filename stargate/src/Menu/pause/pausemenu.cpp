#include "pausemenu.h"

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPainter>
#include <QPushButton>

#include "src/Menu/gamemenu/shiplibrarydialog.h"
#include <QStackedWidget>
#include <QVBoxLayout>

#include "src/Menu/menustyle.h"
#include "src/Menu/setting/settingspanel.h"

PauseMenu::PauseMenu(SoundSystem *sound, QWidget *parent)
    : QWidget(parent)
{
    setStyleSheet(MenuStyle::sheet());
    setFocusPolicy(Qt::StrongFocus);

    auto *panel = new QFrame(this);
    panel->setObjectName("panel");
    m_pages = new QStackedWidget(panel);

    // --- Main page
    auto *mainPage = new QWidget(m_pages);
    auto *heading = new QLabel(tr("GAME PAUSED"), mainPage);
    heading->setObjectName("heading");
    heading->setAlignment(Qt::AlignCenter);
    auto *resume = new QPushButton(tr("Resume"), mainPage);
    auto *save = new QPushButton(tr("Save game"), mainPage);
    auto *library = new QPushButton(tr("Ship library"), mainPage);
    auto *settings = new QPushButton(tr("Settings"), mainPage);
    auto *quitToTitle = new QPushButton(tr("Quit to title screen"), mainPage);
    auto *quitGame = new QPushButton(tr("Quit game"), mainPage);
    quitGame->setObjectName("danger");
    m_saveStatus = new QLabel(mainPage);
    m_saveStatus->setObjectName("note");
    m_saveStatus->setAlignment(Qt::AlignCenter);
    auto *hint = new QLabel(tr("The game is saved when you quit"), mainPage);
    hint->setObjectName("note");
    hint->setAlignment(Qt::AlignCenter);

    auto *mainLayout = new QVBoxLayout(mainPage);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(10);
    mainLayout->addWidget(heading);
    mainLayout->addSpacing(8);
    for (QPushButton *b : {resume, save, library, settings, quitToTitle, quitGame})
        mainLayout->addWidget(b);
    mainLayout->addWidget(m_saveStatus);
    mainLayout->addWidget(hint);
    m_firstButton = resume;

    // --- Settings page
    m_settings = new SettingsPanel(sound, m_pages);

    m_pages->addWidget(mainPage);
    m_pages->addWidget(m_settings);

    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(36, 28, 36, 28);
    panelLayout->addWidget(m_pages);

    auto *layout = new QVBoxLayout(this);
    layout->addStretch(1);
    layout->addWidget(panel, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    connect(resume, &QPushButton::clicked, this, &PauseMenu::resumeRequested);
    connect(save, &QPushButton::clicked, this, &PauseMenu::saveRequested);
    // The game stays paused while the library is open
    connect(library, &QPushButton::clicked, this, [this] { ShipLibraryDialog::open(window()); });
    connect(settings, &QPushButton::clicked, this, [this] {
        m_settings->refresh();
        MenuStyle::showPage(m_pages, 1);
    });
    connect(m_settings, &SettingsPanel::backRequested, this, [this] {
        MenuStyle::showPage(m_pages, 0);
        m_firstButton->setFocus();
    });
    connect(m_settings, &SettingsPanel::languageChanged, this, &PauseMenu::languageChanged);
    connect(quitToTitle, &QPushButton::clicked, this, &PauseMenu::quitToTitleRequested);
    connect(quitGame, &QPushButton::clicked, this, &PauseMenu::quitGameRequested);

    if (parent)
        parent->installEventFilter(this);
    hide();
}

void PauseMenu::open()
{
    if (parentWidget())
        setGeometry(parentWidget()->rect());
    MenuStyle::showPage(m_pages, 0);
    m_saveStatus->clear();
    show();
    raise();
    m_firstButton->setFocus();
}

void PauseMenu::showSaveResult(bool ok, const QString &text)
{
    m_saveStatus->setStyleSheet(ok ? "color: #8be07a;" : "color: #ff5a48;");
    m_saveStatus->setText(text);
}

void PauseMenu::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 170));
}

void PauseMenu::openSettings()
{
    m_settings->refresh();
    MenuStyle::showPage(m_pages, 1);
}

void PauseMenu::escape()
{
    if (m_pages->currentIndex() != 0) {
        MenuStyle::showPage(m_pages, 0);
        m_firstButton->setFocus();
    } else {
        emit resumeRequested();
    }
}

// Follows the size of the game screen
bool PauseMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(watched, event);
}
