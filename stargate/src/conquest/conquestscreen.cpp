#include "conquestscreen.h"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "galaxymapwidget.h"
#include "src/Menu/gamemenu/commandconsole.h"
#include "src/Menu/pause/pausemenu.h"
#include "src/audio/musicplayer.h"
#include "src/audio/soundsystem.h"
#include "src/db/repositories/shiplibraryrepository.h"
#include "src/render/battleview.h"
#include "src/render/planetlook.h"
#include "src/world/planetgenerator.h"

using namespace Conquest;

namespace {

constexpr int kTickMs = 100;
constexpr int kLookGalaxy = 91;         // planet looks of the conquest galaxy

CommandConsole::Tone consoleTone(Tone tone)
{
    switch (tone) {
    case Tone::Success: return CommandConsole::Tone::Success;
    case Tone::Warning: return CommandConsole::Tone::Warning;
    case Tone::Critical: return CommandConsole::Tone::Critical;
    case Tone::Info: break;
    }
    return CommandConsole::Tone::Info;
}

} // namespace

QString ConquestScreen::savePath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("conquest.json");
}

bool ConquestScreen::hasSave()
{
    return QFile::exists(savePath());
}

ConquestScreen::ConquestScreen(SoundSystem *sound, const ConquestGame::Setup &setup, bool continueSaved,
                               QWidget *parent)
    : QWidget(parent)
    , m_sound(sound)
    , m_game(new ConquestGame(this))
    , m_timer(new QTimer(this))
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(6, 6, 8));
    setPalette(pal);

    // --- Left: galaxy map and planet panel; right: battle view
    auto *topArea = new QWidget(this);
    m_map = new GalaxyMapWidget(m_game, topArea);
    m_info = new QLabel(topArea);
    m_info->setTextFormat(Qt::RichText);
    m_info->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_info->setWordWrap(true);
    m_info->setMinimumHeight(150);
    m_info->setStyleSheet("QLabel { background: #14130e; color: #e8e2c8; border: 1px solid #5c5642;"
                          " border-radius: 6px; padding: 10px; font-size: 13px; }");
    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(m_map, 3);
    left->addWidget(m_info, 1);

    m_battle = new BattleView(topArea);
    auto *top = new QHBoxLayout(topArea);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(12);
    top->addLayout(left, 45);
    top->addWidget(m_battle, 55);

    m_console = new CommandConsole(this);
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->setStyleSheet("QSplitter::handle { background: #1c1a12; }"
                            "QSplitter::handle:hover { background: #e2bf3e; }");
    splitter->addWidget(topArea);
    splitter->addWidget(m_console);
    splitter->setStretchFactor(0, 1);
    splitter->setSizes({680, 260});
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->addWidget(splitter);

    // --- Battle view: no survival waves, fleets come from the planets
    m_battle->setAutoWaves(false);
    if (ShipLibraryRepository::migrate()) {
        const QList<ShipClass> library = ShipLibraryRepository::all();
        if (!library.isEmpty())
            m_battle->setShipClasses(library);
    }
    m_battle->clearBattlefield(0.0f);
    if (m_sound)
        connect(m_battle, &BattleView::sound, m_sound, &SoundSystem::play);
    connect(m_battle, &BattleView::logMessage, this, [this](const QString &text, BattleView::LogLevel level) {
        if (level == BattleView::LogLevel::Info)
            return;
        m_console->appendLine(text, level == BattleView::LogLevel::Critical  ? CommandConsole::Tone::Critical
                                    : level == BattleView::LogLevel::Warning ? CommandConsole::Tone::Warning
                                                                             : CommandConsole::Tone::Success);
    });
    connect(m_battle, &BattleView::sectorCleared, this, [this] {
        if (m_game->inBattle())
            m_game->battleWon();
    });
    connect(m_battle, &BattleView::hullBreached, this, [this] {
        if (!m_game->inBattle())
            return;
        m_battle->clearBattlefield(0.0f);
        m_game->battleLost();
        m_game->setFlagshipHull(m_battle->hull());
    });

    // --- Game
    connect(m_game, &ConquestGame::message, this, &ConquestScreen::onMessage);
    connect(m_game, &ConquestGame::battleStarted, this, &ConquestScreen::onBattleStarted);
    connect(m_game, &ConquestGame::planetChanged, this, &ConquestScreen::refresh);
    connect(m_game, &ConquestGame::gameOver, this, [this](bool victory) {
        m_console->appendLine(victory ? tr("VICTORY - the galaxy is yours. 'menu' to go back to the title screen.")
                                      : tr("DEFEAT - your faction has no world left."),
                              victory ? CommandConsole::Tone::Success : CommandConsole::Tone::Critical);
        QFile::remove(savePath());
    });
    connect(m_map, &GalaxyMapWidget::planetSelected, this, &ConquestScreen::refresh);
    connect(m_map, &GalaxyMapWidget::planetActivated, this, [this](int id) {
        QString error;
        if (!m_game->orderTravel(id, &error))
            m_console->appendLine(tr("Cannot go there: %1").arg(error), CommandConsole::Tone::Warning);
        refresh();
    });

    // --- Console
    m_console->setCompletions({"help", "goto", "planets", "status", "info", "fortify", "repair", "attack",
                               "maintower", "pulseweapon", "secondarytower", "shield", "scan", "menu", "clear"});
    connect(m_console, &CommandConsole::commandEntered, this, &ConquestScreen::onCommand);

    // --- Pause menu (Escape, MENU button, 'menu')
    m_pauseMenu = new PauseMenu(m_sound, this);
    connect(m_pauseMenu, &PauseMenu::resumeRequested, this, [this] { setPaused(false); });
    connect(m_pauseMenu, &PauseMenu::saveRequested, this, [this] {
        const bool ok = save();
        m_pauseMenu->showSaveResult(ok, ok ? tr("Conquest saved") : tr("Save failed"));
    });
    connect(m_pauseMenu, &PauseMenu::quitToTitleRequested, this, &ConquestScreen::quitToTitleRequested);
    connect(m_pauseMenu, &PauseMenu::quitGameRequested, this, &ConquestScreen::quitGameRequested);
    connect(m_pauseMenu, &PauseMenu::languageChanged, this, &ConquestScreen::languageChanged);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] {
        if (m_pauseMenu->isVisible())
            m_pauseMenu->escape();
        else
            openPauseMenu();
    });
    m_menuButton = new QPushButton(tr("MENU"), m_battle);
    m_menuButton->setFocusPolicy(Qt::NoFocus);
    m_menuButton->setCursor(Qt::PointingHandCursor);
    m_menuButton->setToolTip(tr("Pause, save, settings, quit (Esc)"));
    m_menuButton->setStyleSheet(
        "QPushButton { background: rgba(20, 19, 14, 200); color: #e8e2c8; border: 1px solid #8a8160;"
        "              border-radius: 4px; padding: 4px 14px; font-weight: bold; letter-spacing: 1px; }"
        "QPushButton:hover { color: #e2bf3e; border-color: #e2bf3e; }");
    m_menuButton->adjustSize();
    connect(m_menuButton, &QPushButton::clicked, this, &ConquestScreen::openPauseMenu);
    m_battle->installEventFilter(this);

    // --- Start or continue
    bool loaded = false;
    if (continueSaved) {
        QFile file(savePath());
        if (file.open(QIODevice::ReadOnly))
            loaded = m_game->fromJson(QJsonDocument::fromJson(file.readAll()).object());
        if (loaded)
            m_console->appendLine(tr("Conquest restored: %1 - %2 planet(s) of %3")
                                      .arg(m_game->player().id)
                                      .arg(m_game->planetCount(m_game->playerIndex()))
                                      .arg(m_game->planets().size()),
                                  CommandConsole::Tone::Success);
        else
            m_console->appendLine(tr("No valid conquest save - a new galaxy is generated"),
                                  CommandConsole::Tone::Warning);
    }
    if (!loaded)
        m_game->start(setup);
    m_battle->restoreDefenses(float(m_game->flagshipHull()), 100.0f, true);
    if (m_game->inBattle()) {
        const Conquest::Planet &p = m_game->planets()[m_game->battlePlanet()];
        onBattleStarted(p.id, m_game->factions()[p.owner].id, 2, 4);
    }
    m_console->appendLine(tr("Type 'help' for the conquest commands - double click a planet to travel there"));
    m_map->select(m_game->flagshipPlanet());
    updatePlanetLook();
    refresh();
    if (m_sound && m_sound->music())
        m_sound->music()->setMood(m_game->inBattle() ? MusicPlayer::Mood::Battle : MusicPlayer::Mood::Calm,
                                  m_game->inBattle() ? m_battle->waveFaction() : QString());

    connect(m_timer, &QTimer::timeout, this, &ConquestScreen::tick);
    m_timer->start(kTickMs);
    QTimer::singleShot(0, m_console, &CommandConsole::focusInput);
}

ConquestScreen::~ConquestScreen()
{
    if (!m_game->isOver())
        save();
}

bool ConquestScreen::save()
{
    if (m_game->isOver())
        return false;
    m_game->setFlagshipHull(m_battle->hull());
    QDir().mkpath(QFileInfo(savePath()).absolutePath());
    QSaveFile file(savePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(m_game->toJson()).toJson(QJsonDocument::Compact));
    return file.commit();
}

// ================================================================
//  Simulation
// ================================================================

void ConquestScreen::tick()
{
    const double dt = kTickMs / 1000.0;
    // The hull is simulated by the battle view during a battle, by the game otherwise
    if (m_game->inBattle()) {
        m_game->setFlagshipHull(m_battle->hull());
    } else {
        m_battle->restoreDefenses(float(m_game->flagshipHull()), m_battle->shieldStrength(), m_battle->shieldActive());
        const Conquest::Planet &here = m_game->planets()[m_game->flagshipPlanet()];
        if (here.owner == m_game->playerIndex() && !m_game->travelling())
            for (const QString &key : BattleView::systemKeys())
                m_battle->setSubsystemHealth(key, 100.0f);         // repaired at home
    }
    m_game->tick(dt);

    if (m_game->flagshipPlanet() != m_lookPlanet && !m_game->travelling())
        updatePlanetLook();

    // Music: battle as long as the fight lasts, calm 6 s after
    if (m_sound && m_sound->music()) {
        m_musicClock = m_game->inBattle() ? 0 : m_musicClock + dt;
        if (m_musicClock > 6)
            m_sound->music()->setMood(MusicPlayer::Mood::Calm);
    }
    refresh();
}

void ConquestScreen::onBattleStarted(int planetId, const QString &faction, int motherships, int fighters)
{
    m_map->select(planetId);
    updatePlanetLook();
    m_battle->clearBattlefield(0.0f);
    m_battle->restoreDefenses(float(m_game->flagshipHull()), m_battle->shieldStrength(), true);
    if (!m_battle->spawnFleet(faction, motherships, fighters)) {
        // No ship of that faction in the library: the planet surrenders
        m_console->appendLine(tr("No %1 fleet available - the planet surrenders").arg(faction),
                              CommandConsole::Tone::Warning);
        m_game->battleWon();
        return;
    }
    if (m_sound && m_sound->music())
        m_sound->music()->setMood(MusicPlayer::Mood::Battle, faction);
    m_console->appendLine(tr("%1 mothership(s) and %2 fighter(s) - shields up, fire at will ('attack <n>', "
                             "'pulseweapon')")
                              .arg(motherships)
                              .arg(fighters),
                          CommandConsole::Tone::Warning);
}

void ConquestScreen::onMessage(const QString &text, Tone tone)
{
    m_console->appendLine(text, consoleTone(tone));
}

void ConquestScreen::updatePlanetLook()
{
    m_lookPlanet = m_game->flagshipPlanet();
    m_battle->setPlanetLook(lookOf(PlanetGenerator::generate(kLookGalaxy, m_lookPlanet, 0)));
}

// ================================================================
//  Panels
// ================================================================

QString ConquestScreen::planetSummary(int planetId) const
{
    const Conquest::Planet &p = m_game->planets()[planetId];
    const QString owner = p.owner < 0 ? tr("Neutral")
                        : p.owner == m_game->playerIndex() ? tr("You (%1)").arg(m_game->player().id)
                                                           : m_game->factions()[p.owner].id;
    const QString color = p.owner < 0 ? QStringLiteral("#9a9a94") : m_game->factions()[p.owner].color;
    QString pips;
    for (int i = 0; i < 5; ++i)
        pips += i < p.defense ? QStringLiteral("■") : QStringLiteral("□");

    QString html = QStringLiteral("<div style='font-size:15px'><b>%1 · %2</b>%3</div>")
                       .arg(p.id + 1)
                       .arg(p.name.toHtmlEscaped(), p.capital ? tr(" — capital") : QString());
    html += QStringLiteral("<span style='color:%1'>%2</span><br>").arg(color, owner.toHtmlEscaped());
    html += tr("Population %1 M / %2 M").arg(p.population, 0, 'f', 1).arg(p.maxPopulation, 0, 'f', 0) + "<br>";
    html += tr("Defense %1").arg(pips) + "&nbsp;&nbsp;" + tr("Bonus: %1").arg(ConquestGame::bonusName(p.bonus))
            + "<br>";
    html += tr("Produces %1 naquadah and %2 trinium per minute")
                .arg(ConquestGame::naquadahRate(p) * 60, 0, 'f', 0)
                .arg(ConquestGame::triniumRate(p) * 60, 0, 'f', 0);

    if (planetId == m_game->flagshipPlanet() && !m_game->travelling())
        html += "<br><span style='color:#e2bf3e'>" + tr("Your flagship is here") + "</span>";
    else if (!m_game->inBattle()) {
        const int jumps = int(m_game->path(m_game->flagshipPlanet(), planetId).size());
        if (jumps > 0)
            html += "<br><span style='color:#a9a28a'>"
                    + tr("%1 jump(s) - double click or 'goto %2' to travel").arg(jumps).arg(p.id + 1) + "</span>";
    }
    return html;
}

void ConquestScreen::refresh()
{
    const int selected = m_map->selected() >= 0 ? m_map->selected() : m_game->flagshipPlanet();
    const Faction &me = m_game->player();
    const QString stock = QStringLiteral("<hr>")
                          + tr("<b>%1</b> - naquadah %2 - trinium %3 - planets %4/%5 - hull %6%")
                                .arg(me.id.toHtmlEscaped())
                                .arg(int(me.naquadah))
                                .arg(int(me.trinium))
                                .arg(m_game->planetCount(m_game->playerIndex()))
                                .arg(m_game->planets().size())
                                .arg(qRound(m_game->flagshipHull()));
    m_info->setText(planetSummary(selected) + stock);
    m_map->update();

    QStringList objective;
    if (m_game->inBattle())
        objective << tr("BATTLE FOR %1").arg(m_game->planets()[m_game->battlePlanet()].name.toUpper());
    else if (m_game->travelling())
        objective << tr("IN HYPERSPACE - %1 jump(s) to go").arg(m_game->route().size());
    else if (m_game->captureProgress() > 0)
        objective << tr("TAKING CONTROL OF %1: %2%")
                         .arg(m_game->planets()[m_game->flagshipPlanet()].name.toUpper())
                         .arg(qRound(m_game->captureProgress() * 100));
    m_battle->setObjectiveLines(objective);

    QStringList enemies;
    for (int f = 0; f < m_game->factions().size(); ++f)
        if (f != m_game->playerIndex())
            enemies << QStringLiteral("%1 %2").arg(m_game->factions()[f].id.toUpper()).arg(m_game->planetCount(f));
    m_battle->setNavigationLines({tr("NAQUADAH %1   TRINIUM %2").arg(int(me.naquadah)).arg(int(me.trinium)),
                                  enemies.join(QStringLiteral("   "))});
}

// ================================================================
//  Commands
// ================================================================

void ConquestScreen::onCommand(const QString &command)
{
    m_console->appendLine(command, CommandConsole::Tone::Echo);
    const QStringList words = command.simplified().split(' ', Qt::SkipEmptyParts);
    if (words.isEmpty())
        return;
    const QString name = words.first().toLower();
    const QString arg = words.mid(1).join(' ');
    auto warn = [this](const QString &text) { m_console->appendLine(text, CommandConsole::Tone::Warning); };
    auto planetArg = [&]() {
        const int id = arg.isEmpty() ? m_map->selected() : m_game->findPlanet(arg);
        if (id < 0)
            warn(tr("Unknown planet '%1' - type 'planets' for the list").arg(arg));
        return id;
    };

    if (name == "help" || name == "?") {
        showHelp();
    } else if (name == "clear") {
        m_console->clearLog();
    } else if (name == "menu" || name == "pause") {
        openPauseMenu();
    } else if (name == "goto" || name == "go" || name == "travel") {
        const int id = planetArg();
        QString error;
        if (id >= 0 && !m_game->orderTravel(id, &error))
            warn(tr("Cannot go there: %1").arg(error));
        if (id >= 0)
            m_map->select(id);
    } else if (name == "planets") {
        listPlanets();
    } else if (name == "info") {
        const int id = planetArg();
        if (id >= 0) {
            m_map->select(id);
            const Conquest::Planet &p = m_game->planets()[id];
            m_console->appendLine(tr("%1 %2 - population %3 M, defense %4/5, %5")
                                      .arg(p.id + 1)
                                      .arg(p.name)
                                      .arg(p.population, 0, 'f', 1)
                                      .arg(p.defense)
                                      .arg(ConquestGame::bonusName(p.bonus)));
        }
    } else if (name == "status") {
        const Faction &me = m_game->player();
        m_console->appendLine(tr("%1: %2 planet(s) of %3, naquadah %4, trinium %5, flagship hull %6% at %7")
                                  .arg(me.id)
                                  .arg(m_game->planetCount(m_game->playerIndex()))
                                  .arg(m_game->planets().size())
                                  .arg(int(me.naquadah))
                                  .arg(int(me.trinium))
                                  .arg(qRound(m_battle->hull()))
                                  .arg(m_game->planets()[m_game->flagshipPlanet()].name));
    } else if (name == "fortify") {
        const int id = planetArg();
        QString error;
        if (id >= 0 && !m_game->fortify(id, &error))
            warn(tr("Cannot fortify: %1").arg(error));
    } else if (name == "repair") {
        QString error;
        if (m_game->inBattle())
            warn(tr("No repairs during a battle"));
        else if (!m_game->repairFlagship(&error))
            warn(tr("Cannot repair: %1").arg(error));
    } else if (name == "attack" || name == "maintower") {
        const int target = name == "attack" ? arg.toInt() - 1 : -1;
        m_battle->fireMainWeapon(target);
    } else if (name == "pulseweapon") {
        m_battle->firePulseVolley();
    } else if (name == "secondarytower") {
        m_battle->fireSecondaryBurst();
    } else if (name == "shield") {
        m_battle->setShieldActive(arg.toLower() != "off" && arg.toLower() != "down");
    } else if (name == "scan") {
        m_battle->lockTarget(arg.toInt() - 1);
    } else {
        warn(tr("Unknown command '%1' - type 'help'").arg(name));
    }
    refresh();
}

void ConquestScreen::showHelp()
{
    const QStringList lines{
        tr("Travel: goto <planet> (number or name), or double click it on the map"),
        tr("Planets: planets (list), info <planet>, status"),
        tr("Economy: fortify <planet> (+1 defense, %1 naquadah), repair (flagship hull, trinium)")
            .arg(ConquestGame::kFortifyCost),
        tr("Battle: attack <n>, maintower, pulseweapon, secondarytower, shield on|off, scan <n>"),
        tr("Neutral worlds join you after a stay in orbit; enemy worlds are taken by destroying their fleet."),
        tr("Win by taking every planet of the enemy factions. Menu: Esc or 'menu'."),
    };
    for (const QString &line : lines)
        m_console->appendLine(line);
}

void ConquestScreen::listPlanets()
{
    for (const Conquest::Planet &p : m_game->planets()) {
        const QString owner = p.owner < 0 ? tr("neutral")
                            : p.owner == m_game->playerIndex() ? tr("yours")
                                                               : m_game->factions()[p.owner].id;
        m_console->appendLine(QStringLiteral("%1  %2  %3  def %4  pop %5")
                                  .arg(p.id + 1, 2)
                                  .arg(p.name.leftJustified(16, ' '))
                                  .arg(owner.leftJustified(16, ' '))
                                  .arg(p.defense)
                                  .arg(p.population, 0, 'f', 1));
    }
}

// ================================================================
//  Pause
// ================================================================

void ConquestScreen::openPauseMenu()
{
    if (m_pauseMenu->isVisible())
        return;
    setPaused(true);
    m_pauseMenu->open();
}

void ConquestScreen::openSettings()
{
    openPauseMenu();
    m_pauseMenu->openSettings();
}

void ConquestScreen::setPaused(bool paused)
{
    m_battle->setPaused(paused);
    if (paused) {
        m_timer->stop();
        return;
    }
    m_pauseMenu->hide();
    m_timer->start(kTickMs);
    m_console->focusInput();
}

bool ConquestScreen::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_battle && event->type() == QEvent::Resize)
        m_menuButton->move(m_battle->width() - m_menuButton->width() - 10, 10);
    return QWidget::eventFilter(watched, event);
}
