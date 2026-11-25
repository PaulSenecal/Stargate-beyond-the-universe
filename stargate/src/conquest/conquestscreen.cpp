#include "conquestscreen.h"
#include "src/core/balance.h"
#include "src/core/gameconfig.h"
#include "src/core/modloader.h"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include "commandpanel.h"
#include "conquestendscreen.h"
#include "galaxymapwidget.h"
#include "src/Menu/gamemenu/commandconsole.h"
#include "src/Menu/gamemenu/radarwidget.h"
#include "src/Menu/gamemenu/replaydialog.h"
#include "src/Menu/gamemenu/shipstatuswidget.h"
#include "src/Menu/gamemenu/weaponstatuswidget.h"
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

    // Tactical panels of the campaign (optional)
    m_hud = new QWidget(topArea);
    auto *hudLayout = new QVBoxLayout(m_hud);
    hudLayout->setContentsMargins(0, 0, 0, 0);
    hudLayout->setSpacing(8);
    m_radar = new RadarWidget(m_hud);
    m_shipStatus = new ShipStatusWidget(m_hud);
    m_weapons = new WeaponStatusWidget(m_hud);
    hudLayout->addWidget(m_radar, 1);
    hudLayout->addWidget(m_shipStatus, 1);
    hudLayout->addWidget(m_weapons, 1);

    auto *top = new QHBoxLayout(topArea);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(12);
    top->addLayout(left, 34);
    top->addWidget(m_battle, 46);
    top->addWidget(m_hud, 20);

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
        const QList<ShipClass> library = ModLoader::instance().merged(ShipLibraryRepository::all());
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
        if (!m_game->inBattle())
            return;
        reportAllySurvivors();                      // the fleets stay in orbit, hangar fighters land
        ++m_battlesWon;
        m_game->battleWon();
    });
    connect(m_battle, &BattleView::hullBreached, this, [this] {
        if (!m_game->inBattle())
            return;
        reportAllySurvivors();
        ++m_battlesLost;
        m_battle->clearBattlefield(0.0f);
        m_joined.clear();
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
        QTimer::singleShot(1500, this, [this, victory] { showEnd(victory); });
    });
    connect(m_map, &GalaxyMapWidget::planetSelected, this, &ConquestScreen::refresh);
    connect(m_map, &GalaxyMapWidget::planetActivated, this, [this](int id) {
        QString error;
        if (!m_game->orderTravel(id, &error))
            m_console->appendLine(tr("Cannot go there: %1").arg(error), CommandConsole::Tone::Warning);
        refresh();
    });

    // --- Console
    m_console->setCompletions({"help", "goto", "planets", "status", "info", "fortify", "repair", "hud", "fleets",
                               "send", "build", "fighter", "mothership", "engage", "research", "ally", "break",
                               "diplomacy", "launch", "drones", "turrets", "railguns", "power", "attack", "target",
                               "missiles", "evade", "spy", "sabotage", "replay",
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
        if (m_command->isVisible())
            m_command->hide();
        else if (m_pauseMenu->isVisible())
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
    m_hudButton = new QPushButton(tr("HUD"), m_battle);
    m_hudButton->setFocusPolicy(Qt::NoFocus);
    m_hudButton->setCursor(Qt::PointingHandCursor);
    m_hudButton->setToolTip(tr("Show or hide the radar, ship and weapon panels ('hud on|off')"));
    m_hudButton->setStyleSheet(m_menuButton->styleSheet());
    m_hudButton->adjustSize();
    connect(m_hudButton, &QPushButton::clicked, this, [this] { setHudVisible(!m_hud->isVisible()); });
    m_techButton = new QPushButton(tr("TECH"), m_battle);
    m_techButton->setFocusPolicy(Qt::NoFocus);
    m_techButton->setCursor(Qt::PointingHandCursor);
    m_techButton->setToolTip(tr("Research tree and diplomacy ('research', 'diplomacy')"));
    m_techButton->setStyleSheet(m_menuButton->styleSheet());
    m_techButton->adjustSize();
    m_command = new CommandPanel(m_game, this);
    connect(m_techButton, &QPushButton::clicked, this, [this] { m_command->open(CommandPanel::Page::Research); });
    m_battle->installEventFilter(this);
    setupHud();
    setHudVisible(QSettings("Stargate", "stargate").value("conquest/hud", true).toBool());

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
    // Weapons, hangar and drones of the flagship of the faction (balance.h)
    const Balance::FlagshipLoadout loadout = Balance::flagshipLoadout(m_game->player().id);
    m_battle->setLoadout(loadout);
    m_battle->setHangar(ConquestGame::hangarFighter(m_game->player().id), loadout.hangar);
    m_battle->setFlagshipStyle(m_game->player().id);
    m_battle->setDroneBay(loadout.drones);
    m_battle->setMissileBay(Balance::flagshipMissiles(m_game->player().id), Balance::flagshipMissiles(m_game->player().id));
    if (loaded) {
        // Supplies of the flagship (saved next to the game)
        QFile file(savePath());
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject supplies = QJsonDocument::fromJson(file.readAll()).object().value("flagshipSupplies").toObject();
            if (!supplies.isEmpty()) {
                m_battle->setRailgunAmmo(supplies.value("railgunAmmo").toInt(BattleView::kRailgunAmmoMax()));
                m_battle->setMissileBay(m_battle->missileCapacity(), supplies.value("missiles").toInt(m_battle->missileCapacity()));
                m_battle->setDrones(supplies.value("drones").toInt(m_battle->droneCapacity()));
            }
        }
    }
    m_wasTravelling = m_game->travelling();
    m_end = new ConquestEndScreen(this);
    connect(m_end, &ConquestEndScreen::titleRequested, this, &ConquestScreen::quitToTitleRequested);
    connect(m_end, &ConquestEndScreen::quitRequested, this, &ConquestScreen::quitGameRequested);
    if (m_game->inBattle())
        onBattleStarted(m_game->battlePlanet(), m_game->battleFaction(), 2, 4);
    m_console->appendLine(tr("Faction trait: %1").arg(ConquestGame::traitSummary(m_game->player().id)),
                          CommandConsole::Tone::Info);
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
    QJsonObject json = m_game->toJson();
    QJsonObject supplies;
    supplies.insert("railgunAmmo", m_battle->railgunAmmo());
    supplies.insert("missiles", m_battle->missiles());
    supplies.insert("drones", m_battle->drones());
    json.insert("flagshipSupplies", supplies);
    file.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
    return file.commit();
}

// ================================================================
//  Simulation
// ================================================================

void ConquestScreen::tick()
{
    const double dt = kTickMs / 1000.0 * gameConfig().gameSpeed;
    // The hull is simulated by the battle view during a battle, by the game otherwise
    if (m_game->inBattle()) {
        m_game->setFlagshipHull(m_battle->hull());
    } else {
        m_battle->restoreDefenses(float(m_game->flagshipHull()), m_battle->shieldStrength(), m_battle->shieldActive());
        const Conquest::Planet &here = m_game->planets()[m_game->flagshipPlanet()];
        if (here.owner == m_game->playerIndex() && !m_game->travelling()) {
            for (const QString &key : BattleView::systemKeys())
                m_battle->setSubsystemHealth(key, 100.0f);         // repaired at home
            // Restocked at home: slugs everywhere, missiles at shipyards,
            // drones on Ancient sites (one salvo every 30 s)
            m_supplyClock += dt;
            if (m_supplyClock >= 1.0) {
                m_supplyClock -= 1.0;
                m_battle->setRailgunAmmo(m_battle->railgunAmmo() + 40);
                if (here.hasShipyard() && ++m_missileClock >= 5) {
                    m_missileClock = 0;
                    m_battle->setMissileBay(m_battle->missileCapacity(), m_battle->missiles() + 1);
                }
                if (here.bonus == Conquest::Bonus::AncientSite && ++m_droneClock >= 30) {
                    m_droneClock = 0;
                    m_battle->setDrones(m_battle->drones() + 1);
                }
            }
        }
    }
    m_game->tick(dt);
    m_playSeconds += dt;
    m_command->refresh();
    joinFriendlyFleets();               // fleets in orbit, reinforcements during a battle
    if (m_game->travelling() != m_wasTravelling) {
        m_wasTravelling = m_game->travelling();
        m_battle->hyperspaceFlash();     // jump into / out of hyperspace
    }
    m_battle->setDamageBonus(float(m_game->flagshipDamageBonus()));
    m_battle->setShieldRegenBonus(float(m_game->flagshipShieldBonus()));

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
    m_battle->clearBattlefield(0.0f, true);         // the fleets in orbit fight along
    m_battle->restoreDefenses(float(m_game->flagshipHull()), m_battle->shieldStrength(), true);
    if (!m_battle->spawnFleet(faction, motherships, fighters)) {
        // No ship of that faction in the library: the planet surrenders
        m_console->appendLine(tr("No %1 fleet available - the planet surrenders").arg(faction),
                              CommandConsole::Tone::Warning);
        m_game->battleWon();
        return;
    }
    joinFriendlyFleets();
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
                        : m_game->isAllied(p.owner) ? tr("%1 (ally)").arg(m_game->factions()[p.owner].id)
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
                .arg(m_game->naquadahRate(p) * 60, 0, 'f', 0)
                .arg(m_game->triniumRate(p) * 60, 0, 'f', 0);
    if (p.hasShipyard() && p.owner >= 0)
        html += "<br>" + tr("Shipyard: %1 mothership(s), %2 fighter(s) ready").arg(p.dockMotherships).arg(p.dockFighters);
    QStringList here;
    for (const Conquest::Fleet &f : m_game->fleets()) {
        if (f.at != p.id || f.moving())
            continue;
        const QString who = f.owner == m_game->playerIndex() ? tr("your fleet %1").arg(f.id)
                                                             : m_game->factions()[f.owner].id;
        here << QStringLiteral("<span style='color:%1'>%2 (%3+%4)</span>")
                    .arg(m_game->factions()[f.owner].color, who.toHtmlEscaped())
                    .arg(f.motherships)
                    .arg(f.fighters);
    }
    if (!here.isEmpty())
        html += "<br>" + tr("In orbit: %1").arg(here.join(QStringLiteral(", ")));
    if (const Conquest::Skirmish *battle = m_game->skirmishAt(p.id)) {
        const QString defender = battle->defender < 0 ? tr("militia") : m_game->factions()[battle->defender].id;
        html += "<br><span style='color:#ff9a5a'><b>"
                + tr("BATTLE: %1 (%2) attack %3 (%4) - %5 s")
                      .arg(m_game->factions()[battle->attacker].id.toHtmlEscaped())
                      .arg(m_game->sidePower(p.id, battle->attacker))
                      .arg(defender.toHtmlEscaped())
                      .arg(m_game->sidePower(p.id, battle->defender))
                      .arg(qRound(battle->elapsed))
                + "</b></span>";
    }

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
    QString research;
    if (!me.research.isEmpty())
        research = "<br>" + tr("Research: %1 %2%").arg(ConquestGame::techName(me.research).toHtmlEscaped())
                                                    .arg(qRound(me.researchProgress * 100));
    m_info->setText(planetSummary(selected) + stock + research);
    m_map->update();

    QStringList objective;
    if (m_game->inBattle()) {
        objective << tr("BATTLE FOR %1").arg(m_game->planets()[m_game->battlePlanet()].name.toUpper());
    } else if (m_game->observing()) {
        const Conquest::Skirmish *battle = m_game->skirmishAt(m_game->flagshipPlanet());
        objective << tr("BATTLE IN PROGRESS: %1 %2 vs %3 %4")
                         .arg(m_game->factions()[battle->attacker].id.toUpper())
                         .arg(m_game->sidePower(battle->planet, battle->attacker))
                         .arg(battle->defender < 0 ? tr("MILITIA") : m_game->factions()[battle->defender].id.toUpper())
                         .arg(m_game->sidePower(battle->planet, battle->defender))
                  << tr("'engage <faction>' to join in - or wait for the winner");
    }
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
        m_console->appendLine(tr("Faction trait: %1").arg(ConquestGame::traitSummary(me.id)));
        if (m_game->powerMultiplier(m_game->playerIndex()) > 1.0)
            m_console->appendLine(tr("Ancient technology: fleet power +%1%")
                                      .arg(qRound((m_game->powerMultiplier(m_game->playerIndex()) - 1.0) * 100)));
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
    } else if (name == "research" || name == "tech") {
        if (arg.isEmpty()) {
            listTechs();
            m_command->open(CommandPanel::Page::Research);
        } else {
            const int index = m_game->findTech(arg);
            QString error;
            if (index < 0)
                warn(tr("Unknown technology '%1' - type 'research' for the list").arg(arg));
            else if (!m_game->startResearch(Conquest::techInfos()[index].id, &error))
                warn(tr("Cannot research: %1").arg(error));
        }
    } else if (name == "ally") {
        QString answer;
        if (!m_game->proposeAlliance(arg, &answer))
            warn(tr("No alliance: %1").arg(answer));
    } else if (name == "break") {
        QString error;
        if (!m_game->breakAlliance(arg, &error))
            warn(tr("Cannot break: %1").arg(error));
    } else if (name == "diplomacy") {
        m_command->open(CommandPanel::Page::Diplomacy);
        for (int f = 0; f < m_game->factions().size(); ++f) {
            if (f == m_game->playerIndex())
                continue;
            const Conquest::Faction &other = m_game->factions()[f];
            m_console->appendLine(tr("%1: %2 - %3 world(s), %4 fleet(s)")
                                      .arg(other.id)
                                      .arg(m_game->isAllied(f) ? tr("ally") : tr("at war"))
                                      .arg(m_game->planetCount(f))
                                      .arg(m_game->fleetCount(f)),
                                  m_game->isAllied(f) ? CommandConsole::Tone::Success : CommandConsole::Tone::Info);
        }
    } else if (name == "turrets") {
        m_battle->setTurretsActive(arg.toLower() != "off");
    } else if (name == "railguns") {
        m_battle->setRailgunsActive(arg.toLower() != "off");
    } else if (name == "power") {
        const QString a = arg.toLower();
        if (a.startsWith("shield"))
            m_battle->setPowerPriority(BattleView::PowerPriority::Shields);
        else if (a.startsWith("weapon"))
            m_battle->setPowerPriority(BattleView::PowerPriority::Weapons);
        else if (a.startsWith("bal"))
            m_battle->setPowerPriority(BattleView::PowerPriority::Balanced);
        else
            m_console->appendLine(m_battle->powerReport());
    } else if (name == "drones") {
        m_battle->fireDrones(arg.isEmpty() ? -1 : arg.toInt() - 1);
    } else if (name == "launch") {
        m_battle->launchFighters(arg.isEmpty() ? -1 : std::max(1, arg.toInt()));
    } else if (name == "replay") {
        if (m_battle->lastReplay().isEmpty()) {
            warn(tr("No battle to replay yet"));
        } else {
            auto *dialog = findChild<ReplayDialog *>();
            if (!dialog)
                dialog = new ReplayDialog(this);
            dialog->setReplay(m_battle->lastReplay(), m_battle->lastReplayFaction());
            dialog->show();
            dialog->raise();
        }
    } else if (name == "spy") {
        const int id = planetArg();
        QStringList report;
        QString error;
        if (id >= 0 && !m_game->spy(id, &report, &error))
            warn(tr("No spy mission: %1").arg(error));
        for (const QString &line : report)
            m_console->appendLine(line, CommandConsole::Tone::Info);
    } else if (name == "sabotage") {
        // sabotage <yard|defense|mine> <planet>
        const QString what = words.value(1).toLower();
        const int id = words.size() > 2 ? m_game->findPlanet(words.mid(2).join(' ')) : m_map->selected();
        bool success = false;
        QString error;
        if (id < 0)
            warn(tr("Usage: sabotage yard|defense|mine <planet> (%1 naquadah)").arg(Balance::kSabotageCost));
        else if (!m_game->sabotage(id, what, &success, &error))
            warn(tr("No sabotage: %1").arg(error));
    } else if (name == "target" || name == "aim") {
        bool ok = false;
        const int target = words.value(1).toInt(&ok);
        if (!ok || target < 1)
            warn(tr("Usage: target <n> [weapons|shields|engines|hangar|hull]"));
        else
            m_battle->aimAt(target - 1, words.value(2));
    } else if (name == "missiles" || name == "missile") {
        m_battle->fireMissiles(arg.isEmpty() ? -1 : arg.toInt() - 1);
    } else if (name == "evade" || name == "evasive") {
        m_battle->setEvasion(arg.toLower() != "off");
    } else if (name == "engage") {
        QString error;
        if (!m_game->engage(arg, &error))
            warn(tr("Cannot engage: %1").arg(error));
    } else if (name == "fleets") {
        listFleets();
    } else if (name == "send") {
        // send <fleet number> <planet>
        bool ok = false;
        const int fleet = words.value(1).toInt(&ok);
        const int id = m_game->findPlanet(words.mid(2).join(' '));
        QString error;
        if (!ok || id < 0)
            warn(tr("Usage: send <fleet number> <planet> - see 'fleets'"));
        else if (!m_game->orderFleet(fleet, id, &error))
            warn(tr("Cannot send the fleet: %1").arg(error));
    } else if (name == "build") {
        // build fighter|mothership [planet]
        const QString kind = words.value(1).toLower();
        const bool mothership = kind.startsWith("mother") || kind == "m";
        const bool fighter = kind.startsWith("fight") || kind == "f";
        int id = words.size() > 2 ? m_game->findPlanet(words.mid(2).join(' ')) : m_map->selected();
        if (id < 0 || m_game->planets()[id].owner != m_game->playerIndex())
            id = m_game->flagshipPlanet();
        QString error;
        if (!mothership && !fighter)
            warn(tr("Usage: build fighter|mothership [planet] (%1 / %2 naquadah)")
                     .arg(ConquestGame::kFighterCost)
                     .arg(ConquestGame::kMothershipCost));
        else if (!m_game->build(id, mothership, &error))
            warn(tr("Cannot build: %1").arg(error));
    } else if (name == "hud") {
        const QString state = arg.toLower();
        setHudVisible(state.isEmpty() ? !m_hud->isVisible() : state != "off" && state != "hide");
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
        tr("Fleets: shipyards build ships on their own, fleets attack and defend by themselves"),
        tr("   fleets (list), send <fleet> <planet>, build fighter|mothership [planet] (%1 / %2 naquadah)")
            .arg(ConquestGame::kFighterCost)
            .arg(ConquestGame::kMothershipCost),
        tr("Battle: attack <n>, maintower, pulseweapon, secondarytower, shield on|off, scan <n>"),
        tr("Battles between other factions last a while: watch them, or 'engage <faction>' to fight one side"),
        tr("Hangar: launch [n] - your fighters fight at your side; your fleets and allies in orbit join the battle"),
        tr("Drones (Ancients): drones <n> - a salvo through the shields; needs %1 free power, restocked on your "
           "worlds with an Ancient site").arg(Balance::kDronePower),
        tr("Tactics: target <n> weapons|shields|engines|hangar, missiles [n] (BC-304, Lucian), evade on|off"),
        tr("Supplies: railgun slugs and missiles are restocked on your worlds (missiles at shipyards)"),
        tr("Energy: turrets on|off, railguns on|off, power shields|weapons|balanced (power: report)"),
        tr("Research: research (tree), research <n> - weapons, defense and industry, 3 tiers each"),
        tr("Diplomacy: diplomacy (relations), ally <faction> (against a common enemy), break <faction>"),
        tr("Gate teams (%1 lanes from your worlds): spy <planet> (%2 naquadah), sabotage yard|defense|mine <planet> (%3)")
            .arg(Balance::kGateRange).arg(Balance::kSpyCost).arg(Balance::kSabotageCost),
        tr("Galactic events may strike anyone: starving Wraith, plague, Ascended gifts, hyperspace storms"),
        tr("replay: watch the last 3D battle again from above"),
        tr("Display: hud on|off (radar, ship and weapon panels), or the HUD button"),
        tr("Neutral worlds join you after a stay in orbit; enemy worlds are taken by destroying their fleet."),
        tr("Win by taking every planet of the enemy factions. Menu: Esc or 'menu'."),
    };
    for (const QString &line : lines)
        m_console->appendLine(line);
}

void ConquestScreen::listTechs()
{
    const QStringList branches{tr("Weapons"), tr("Defense"), tr("Industry")};
    const Conquest::Faction &me = m_game->player();
    for (int i = 0; i < Conquest::techInfos().size(); ++i) {
        const Conquest::TechInfo &t = Conquest::techInfos()[i];
        QString state;
        CommandConsole::Tone tone = CommandConsole::Tone::Info;
        if (me.techs.contains(t.id)) {
            state = tr("done");
            tone = CommandConsole::Tone::Success;
        } else if (me.research == t.id) {
            state = tr("in progress %1%").arg(qRound(me.researchProgress * 100));
            tone = CommandConsole::Tone::Warning;
        } else {
            state = tr("%1 naquadah, %2 s")
                        .arg(m_game->researchCost(m_game->playerIndex(), t))
                        .arg(m_game->researchTime(m_game->playerIndex(), t));
        }
        m_console->appendLine(QStringLiteral("%1  %2 %3  %4 - %5 [%6]")
                                  .arg(i + 1)
                                  .arg(branches.value(t.branch))
                                  .arg(t.tier)
                                  .arg(ConquestGame::techName(t.id), ConquestGame::techEffect(t.id), state),
                              tone);
    }
}

void ConquestScreen::listFleets()
{
    if (m_game->fleets().isEmpty()) {
        m_console->appendLine(tr("No fleet in the galaxy"));
        return;
    }
    for (const Conquest::Fleet &f : m_game->fleets()) {
        const bool mine = f.owner == m_game->playerIndex();
        const QString where = m_game->planets()[f.at].name;
        const QString going = f.target >= 0 ? tr(" -> %1").arg(m_game->planets()[f.target].name) : QString();
        m_console->appendLine(tr("%1 %2: %3 mothership(s), %4 fighter(s) at %5%6")
                                  .arg(mine ? tr("Fleet %1").arg(f.id) : m_game->factions()[f.owner].id)
                                  .arg(mine ? tr("(yours)") : QString())
                                  .arg(f.motherships)
                                  .arg(f.fighters)
                                  .arg(where, going),
                              mine ? CommandConsole::Tone::Success : CommandConsole::Tone::Info);
    }
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

// ================================================================
//  Tactical panels (same widgets as the campaign)
// ================================================================

void ConquestScreen::setHudVisible(bool visible)
{
    m_hud->setVisible(visible);
    QSettings("Stargate", "stargate").setValue("conquest/hud", visible);
}

void ConquestScreen::setupHud()
{
    // The flagship has no component table: its parts follow the battle systems
    struct Part
    {
        const char *name;
        const char *system;
        bool weapon;
    };
    static const Part kParts[] = {
        {"Life Support", "lifesupport", false}, {"Shield Generator", "shields", false},
        {"Power Core", "", false},              {"FTL Drive", "engines", false},
        {"Sublight Engine", "engines", false},  {"Energy Cannon", "weapons", true},
        {"Railgun", "weapons", true},           {"Missile Bay", "weapons", true},
    };
    auto health = [this](const char *system) {
        return *system ? qRound(m_battle->subsystemHealth(QString::fromLatin1(system))) : 100;
    };
    auto hit = [this](const char *system) {
        return *system && m_battle->secondsSinceHit(QString::fromLatin1(system)) < 1.5f;
    };

    m_shipStatus->setProvider([this, health, hit] {
        ShipStatusData data;
        data.hull = m_battle->hull();
        data.shield = m_battle->shieldStrength();
        data.shieldUp = m_battle->shieldActive();
        const BattleView::Schematic schematic = m_battle->schematic();
        data.silhouette = schematic.silhouette;
        data.silhouetteRect = schematic.rect;
        data.shieldCenter = schematic.shieldCenter;
        data.shieldRadii = schematic.shieldRadii;
        for (const Part &part : kParts)
            data.components.append({QString::fromLatin1(part.name), health(part.system), 100, hit(part.system)});
        for (const QVector3D &f : m_battle->fireSpots())
            data.fires << QPointF(f.x(), f.z());
        for (const HitMark &h : m_battle->recentHits())
            data.hits.append({QPointF(h.local.x(), h.local.z()), h.age, h.shield});
        return data;
    });
    m_radar->setProvider([this] {
        RadarPicture picture;
        picture.shieldUp = m_battle->shieldActive() && m_battle->shieldStrength() > 0.0f;
        for (const BattleView::RadarBlip &b : m_battle->radarContacts())
            picture.contacts.append({QPointF(b.local.x(), b.local.z()), b.index, b.mothership, b.locked, b.health, b.ally});
        for (const BattleView::RadarShot &s : m_battle->radarShots())
            picture.bolts.append({QPointF(s.local.x(), s.local.z()), s.fromDestiny, s.missile});
        return picture;
    });
    m_weapons->setProvider([this, health, hit] {
        WeaponStatusData data;
        data.readiness = m_battle->weaponReadiness();
        for (const Part &part : kParts)
            if (part.weapon)
                data.weapons.append({QString::fromLatin1(part.name), health(part.system), 100, hit(part.system)});
        return data;
    });
}

// ================================================================
//  Friendly fleets in the 3D battle, end of the game
// ================================================================

void ConquestScreen::joinFriendlyFleets()
{
    // Planet shown in 3D: the battle, or the orbit of the flagship (none in hyperspace)
    const bool battle = m_game->inBattle();
    const int planet = battle ? m_game->battlePlanet() : m_game->travelling() ? -1 : m_game->flagshipPlanet();
    auto friendly = [this](const Conquest::Fleet &f) {
        return f.owner == m_game->playerIndex() || m_game->isAllied(f.owner);
    };

    // Fleets that left the planet (or the flagship left): out of the view
    for (auto it = m_joined.begin(); it != m_joined.end();) {
        const Conquest::Fleet *fleet = nullptr;
        for (const Conquest::Fleet &f : m_game->fleets())
            if (f.id == it.key())
                fleet = &f;
        const bool here = fleet && fleet->at == planet && !fleet->moving() && friendly(*fleet);
        // Out of battle, a fleet whose strength changed (new ships, losses) is shown again
        const bool changed = here && !battle
                          && (fleet->motherships != it->motherships || fleet->fighters != it->fighters);
        if (here && !changed) {
            ++it;
            continue;
        }
        m_battle->removeAllyGroup(it.key(), planet >= 0 && !changed);   // it jumps away
        it = m_joined.erase(it);
    }

    if (planet < 0)
        return;
    for (const Conquest::Fleet &f : m_game->fleets()) {
        if (f.at != planet || f.moving() || m_joined.contains(f.id) || !friendly(f))
            continue;
        if (f.motherships + f.fighters <= 0)
            continue;
        // A big fleet sends part of its ships (the 3D view is limited);
        // the survivors' share is applied to the whole fleet afterwards
        const int big = std::min(f.motherships, 3);
        const int small = std::min(f.fighters, 6);
        if (!m_battle->spawnAllies(m_game->factions()[f.owner].id, big, small, f.id, false))
            continue;
        m_joined.insert(f.id, {big, small, f.motherships, f.fighters});
        const bool own = f.owner == m_game->playerIndex();
        const QString faction = m_game->factions()[f.owner].id;
        if (battle)
            m_console->appendLine(own ? tr("Your fleet %1 joins the battle").arg(f.id)
                                      : tr("Allied %1 ships join the battle").arg(faction),
                                  CommandConsole::Tone::Success);
        else
            m_console->appendLine(own ? tr("Your fleet %1 is in orbit").arg(f.id)
                                      : tr("Allied %1 ships in orbit").arg(faction),
                                  CommandConsole::Tone::Info);
    }
}

// End of a battle: the losses go back to the fleets; the survivors stay
// in orbit (what they show now becomes their reference strength)
void ConquestScreen::reportAllySurvivors()
{
    const QList<BattleView::AllySurvivors> survivors = m_battle->allySurvivors();
    for (auto it = m_joined.begin(); it != m_joined.end(); ++it) {
        int big = 0, small = 0;
        for (const BattleView::AllySurvivors &s : survivors)
            if (s.group == it.key()) {
                big = s.motherships;
                small = s.fighters;
            }
        Joined &j = it.value();
        const int motherships = j.spawnedMotherships > 0 ? qRound(double(j.motherships) * big / j.spawnedMotherships)
                                                         : j.motherships;
        const int fighters = j.spawnedFighters > 0 ? qRound(double(j.fighters) * small / j.spawnedFighters) : j.fighters;
        m_game->setFleetStrength(it.key(), motherships, fighters);
        j = {big, small, motherships, fighters};
    }
}

void ConquestScreen::showEnd(bool victory)
{
    setPaused(true);
    m_pauseMenu->hide();
    const int techs = int(m_game->player().techs.size());
    const int minutes = int(m_playSeconds) / 60;
    const QStringList summary{
        tr("Worlds held: %1 of %2").arg(m_game->planetCount(m_game->playerIndex())).arg(m_game->planets().size()),
        tr("Fleets: %1").arg(m_game->fleetCount(m_game->playerIndex())),
        tr("Flagship battles: %1 won, %2 lost").arg(m_battlesWon).arg(m_battlesLost),
        tr("Technologies: %1 of %2").arg(techs).arg(Conquest::techInfos().size()),
        tr("Time of this session: %1 min %2 s").arg(minutes).arg(int(m_playSeconds) % 60, 2, 10, QChar('0')),
    };
    m_end->showResult(victory, m_game->player().id, summary);
    if (m_sound && m_sound->music())
        m_sound->music()->setMood(victory ? MusicPlayer::Mood::Title : MusicPlayer::Mood::Calm);
}

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
    if (watched == m_battle && event->type() == QEvent::Resize) {
        m_menuButton->move(m_battle->width() - m_menuButton->width() - 10, 10);
        m_hudButton->move(m_menuButton->x() - m_hudButton->width() - 8, 10);
        m_techButton->move(m_hudButton->x() - m_techButton->width() - 8, 10);
    }
    return QWidget::eventFilter(watched, event);
}
