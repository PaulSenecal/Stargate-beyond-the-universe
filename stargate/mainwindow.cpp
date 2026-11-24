#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "src/Menu/gamemenu/gamemenu.h"
#include "src/Menu/mainmenu/titlescreen.h"
#include "src/audio/soundsystem.h"
#include "src/conquest/conquestscreen.h"
#include "src/core/language.h"

#include <QStackedWidget>
#include <QTimer>

#ifndef APP_VERSION
#define APP_VERSION "dev"     // set by stargate.pro from git
#endif

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(tr("Stargate - Beyond the Universe") + QStringLiteral(" " APP_VERSION));

    // Screens: the title screen (first screen: continue, settings, quit)
    // and the game (3D battle view + HUD + command console). The former
    // title buttons stay in mainwindow.ui.
    m_sound = new SoundSystem(this);
    m_screens = new QStackedWidget(this);
    setCentralWidget(m_screens);
    menuBar()->hide();
    createTitle();
    m_screens->setCurrentWidget(m_title);
    //menuBackground();

}

MainWindow::~MainWindow()
{
    delete m_game;          // saves the game while the database is still open
    delete m_conquest;
    delete ui;
}

void MainWindow::createTitle()
{
    m_title = new TitleScreen(m_sound, m_screens);
    m_screens->insertWidget(0, m_title);
    connect(m_title, &TitleScreen::playRequested, this, &MainWindow::startGame);
    connect(m_title, &TitleScreen::quitRequested, this, &MainWindow::close);
    connect(m_title, &TitleScreen::languageChanged, this, &MainWindow::changeLanguage);
    connect(m_title, &TitleScreen::conquestRequested, this, &MainWindow::startConquest);
}

void MainWindow::startConquest(const ConquestGame::Setup &setup, bool continueSaved)
{
    delete m_conquest;      // a new conquest replaces the previous one
    m_conquest = new ConquestScreen(m_sound, setup, continueSaved, m_screens);
    m_screens->addWidget(m_conquest);
    connect(m_conquest, &ConquestScreen::quitToTitleRequested, this, &MainWindow::showTitle);
    connect(m_conquest, &ConquestScreen::quitGameRequested, this, &MainWindow::close);
    connect(m_conquest, &ConquestScreen::languageChanged, this, &MainWindow::changeLanguage);
    m_screens->setCurrentWidget(m_conquest);
}

void MainWindow::changeLanguage(const QString &code)
{
    Language::apply(code);
    // Later: the request comes from a widget that is about to be deleted
    QTimer::singleShot(0, this, [this] {
        const bool inGame = m_game && m_screens->currentWidget() == m_game;
        const bool inConquest = m_conquest && m_screens->currentWidget() == m_conquest;
        delete m_title;
        createTitle();
        if (inConquest) {
            delete m_conquest;      // saved, then reloaded in the new language
            m_conquest = nullptr;
            startConquest({}, true);
            m_conquest->openSettings();
        } else if (inGame) {
            delete m_game;          // saved, then reloaded in the new language
            m_game = nullptr;
            startGame();
            m_game->openSettings();
        } else {
            m_screens->setCurrentWidget(m_title);
            m_title->showSettings();
        }
    });
}

void MainWindow::startGame()
{
    if (!m_game) {
        m_game = new GameMenu(m_sound, m_screens);
        m_screens->addWidget(m_game);
        connect(m_game, &GameMenu::quitToTitleRequested, this, &MainWindow::showTitle);
        connect(m_game, &GameMenu::quitGameRequested, this, &MainWindow::close);
        connect(m_game, &GameMenu::languageChanged, this, &MainWindow::changeLanguage);
    }
    m_screens->setCurrentWidget(m_game);
}

void MainWindow::showTitle()
{
    m_screens->setCurrentWidget(m_title);
    if (m_game) {
        // Called from a button of the game screen: deleted (and saved) later
        m_screens->removeWidget(m_game);
        m_game->deleteLater();
        m_game = nullptr;
    }
    if (m_conquest) {
        m_screens->removeWidget(m_conquest);
        m_conquest->deleteLater();
        m_conquest = nullptr;
    }
}


void MainWindow::on_pushButton_clicked()
{
    QApplication::quit();
    // close();
}


void MainWindow::menuBackground()
{
    // Créer un QLabel pour l'image
    QLabel *backGroundLabel = new QLabel(this);
    QPixmap image(":/images/background/destinyPlan.jpg");

    // Configurer le QLabel pour qu'il s'adapte à la taille du widget
    backGroundLabel->setPixmap(image);
    backGroundLabel->setScaledContents(true); // Permet de redimensionner l'image au QLabel
    backGroundLabel->setAlignment(Qt::AlignCenter); // Alignement central

    // S'assurer que le QLabel prend toute la place
    backGroundLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Ajouter le QLabel au layout central
    backGroundLabel->setScaledContents(true);
    backGroundLabel->setGeometry(0,0,1920,1080);
}

void MainWindow::on_campaignButton_clicked()
{
    Campaign *window = new Campaign(this);
    window->setAttribute(Qt::WA_DeleteOnClose); // La mémoire sera libérée à la fermeture
    window->show();
}

