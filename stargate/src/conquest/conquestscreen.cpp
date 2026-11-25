#include "conquestscreen.h"
#include "src/core/balance.h"
#include "src/core/gameconfig.h"
#include "src/core/modloader.h"

#include <QDir>
#include <QRegularExpression>
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
#include "src/Menu/gamemenu/shiplibrarydialog.h"
#include "src/Menu/gamemenu/shipcrew.h"
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
        const double enemyDamage = m_battle->enemyDamage();     // they keep it
        m_battle->clearBattlefield(0.0f);
        m_joined.clear();
        m_game->battleLost(enemyDamage);
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
    m_battle->setShipEquipment(Balance::flagshipShield(m_game->player().id), Balance::flagshipArmor(m_game->player().id));
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
    // The inside of the flagship: plan, crew, repairs (as in the campaign)
    m_interior = std::make_unique<Interior>(QStringLiteral("flagship"), m_game->player().id);
    if (m_game->player().id != QLatin1String("Asgard"))
        m_interior->setCrewCount(60);
    if (loaded) {
        QFile file(savePath());
        if (file.open(QIODevice::ReadOnly))
            m_interior->fromJson(QJsonDocument::fromJson(file.readAll()).object().value("flagshipInterior").toObject());
        m_interior->takeMessages();
    }
    m_crew = std::make_unique<ShipCrew>(m_interior.get(), m_battle, m_console, this);
    // Costs in conquest: naquadah stays naquadah, every material is trinium
    m_crew->pay = [this](const ShipCrew::Cost &cost) {
        double naquadah = 0, trinium = 0;
        for (const auto &[resource, quantity] : cost) {
            if (resource == QLatin1String("naquadah"))
                naquadah += quantity;
            else if (resource != QLatin1String("water") && resource != QLatin1String("food"))
                trinium += quantity;
        }
        QString missing;
        if (!m_game->spend(naquadah, trinium, &missing)) {
            m_console->appendLine(tr("Missing: %1").arg(missing), CommandConsole::Tone::Warning);
            return false;
        }
        return true;
    };
    m_crew->systemCost = [](const QString &) { return ShipCrew::Cost{{QStringLiteral("trinium"), 3}}; };
    m_crew->onCapture = [this](const QString &) {
        m_game->gain(40, 10);
        m_console->appendLine(tr("Salvage from the captured ship: +40 naquadah, +10 trinium"), CommandConsole::Tone::Success);
        m_game->addCapturedShip();
    };
    connect(m_battle, &BattleView::boardingParty, this, [this](int strength, const QString &, const QString &) {
        m_interior->board(strength);
        m_crew->flushMessages();
    });
    m_console->setCompletionProvider([this](const QString &text) { return completeLine(text); });

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
    if (m_interior)
        json.insert("flagshipInterior", m_interior->toJson());
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
    // The inside of the flagship, once per second; dock crews on our worlds
    m_interiorClock += dt;
    if (m_interiorClock >= 1.0) {
        const float seconds = float(m_interiorClock);
        m_interiorClock = 0;
        const Conquest::Planet &here = m_game->planets()[m_game->flagshipPlanet()];
        if (here.owner == m_game->playerIndex() && !m_game->travelling() && !m_game->inBattle())
            m_interior->dockRepair(seconds * (here.hasShipyard() ? 2.0f : 1.0f));
        m_crew->tick(seconds);
    }
    m_crew->applyBonuses(float(m_game->flagshipDamageBonus()), float(m_game->flagshipShieldBonus()));

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
    // Fleets that fled an earlier battle come back hurt
    m_battle->setIncomingDamage(float(m_game->battleDamage()));
    const bool spawned = m_battle->spawnFleet(faction, motherships, fighters);
    m_battle->setIncomingDamage(0.0f);
    if (m_game->battleDamage() > 0.05)
        m_console->appendLine(tr("Their ships still bear the scars of an earlier battle (%1% damage)")
                                  .arg(qRound(m_game->battleDamage() * 100)),
                              CommandConsole::Tone::Info);
    if (!spawned) {
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

    if (ShipCrew::commands().contains(name) && !(name == "repair" && arg.isEmpty())
        && m_crew->handle(name, words)) {
        refresh();
        return;
    }
    if ((name == "help" || name == "?") && !arg.isEmpty()) {
        showHelpTopic(arg.toLower());
    } else if (name == "help" || name == "?") {
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
    } else if (name == "library" || name == "ships") {
        ShipLibraryDialog::open(this, arg);
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

namespace {

struct HelpEntry { const char *syntax; const char *description; };
struct HelpCategory { const char *key; const char *title; QList<HelpEntry> entries; };

const QList<HelpCategory> &conquestHelp()
{
    static const QList<HelpCategory> catalog{
        {"travel", QT_TRANSLATE_NOOP("ConquestScreen", "Travel"), {
            {"goto <planet>", QT_TRANSLATE_NOOP("ConquestScreen", "fly there (or double click it on the map)")},
            {"planets / info <planet>", QT_TRANSLATE_NOOP("ConquestScreen", "list of the worlds / one world")},
            {"status", QT_TRANSLATE_NOOP("ConquestScreen", "your faction, resources, flagship")}}},
        {"economy", QT_TRANSLATE_NOOP("ConquestScreen", "Economy"), {
            {"fortify <planet>", QT_TRANSLATE_NOOP("ConquestScreen", "+1 defense (naquadah)")},
            {"build fighter|mothership [planet]", QT_TRANSLATE_NOOP("ConquestScreen", "order ships at a shipyard")},
            {"research [n]", QT_TRANSLATE_NOOP("ConquestScreen", "technology tree (also the TECH button)")}}},
        {"fleets", QT_TRANSLATE_NOOP("ConquestScreen", "Fleets"), {
            {"fleets", QT_TRANSLATE_NOOP("ConquestScreen", "your fleets (they also act on their own)")},
            {"send <fleet> <planet>", QT_TRANSLATE_NOOP("ConquestScreen", "order a fleet somewhere")},
            {"engage <faction>", QT_TRANSLATE_NOOP("ConquestScreen", "join a battle between two other factions")}}},
        {"battle", QT_TRANSLATE_NOOP("ConquestScreen", "Battle"), {
            {"attack <n> / scan <n>", QT_TRANSLATE_NOOP("ConquestScreen", "main weapon / lock a target")},
            {"target <n> <system>", QT_TRANSLATE_NOOP("ConquestScreen", "aim at weapons, shields, engines, hangar")},
            {"pulseweapon / secondarytower", QT_TRANSLATE_NOOP("ConquestScreen", "volleys of the towers")},
            {"missiles [n] / drones [n]", QT_TRANSLATE_NOOP("ConquestScreen", "limited salvos")},
            {"shield on|off / evade on|off", QT_TRANSLATE_NOOP("ConquestScreen", "shields / evasive maneuvers")},
            {"launch [n]", QT_TRANSLATE_NOOP("ConquestScreen", "fighters of your hangar")},
            {"turrets / railguns / power", QT_TRANSLATE_NOOP("ConquestScreen", "energy of the flagship")},
            {"replay", QT_TRANSLATE_NOOP("ConquestScreen", "the last battle again")},
            {"library [ship]", QT_TRANSLATE_NOOP("ConquestScreen", "every ship, its attacks and chances")}}},
        {"ship", QT_TRANSLATE_NOOP("ConquestScreen", "Flagship and crew"), {
            {"plan [room] / plan T<n>", QT_TRANSLATE_NOOP("ConquestScreen", "your plan / an enemy mothership's plan")},
            {"repair <system|hull|room>", QT_TRANSLATE_NOOP("ConquestScreen", "a crew job (trinium); 'repair' alone: the hull")},
            {"jobs / crew / crew rotate", QT_TRANSLATE_NOOP("ConquestScreen", "repairs / your crew / rest the tired ones")},
            {"seal / unseal / vent <room>", QT_TRANSLATE_NOOP("ConquestScreen", "doors and fires")},
            {"defend <room>", QT_TRANSLATE_NOOP("ConquestScreen", "soldiers against boarders")}}},
        {"boarding", QT_TRANSLATE_NOOP("ConquestScreen", "Boarding"), {
            {"dock <n>", QT_TRANSLATE_NOOP("ConquestScreen", "a hangar ship latches onto mothership n")},
            {"board <n> [gate]", QT_TRANSLATE_NOOP("ConquestScreen", "take a crippled mothership (it joins you)")},
            {"sabotage <n> <room> [gate]", QT_TRANSLATE_NOOP("ConquestScreen", "set a room of mothership n on fire")},
            {"(the way in)", QT_TRANSLATE_NOOP("ConquestScreen", "a docked ship, a transport beam (shield down) or the gate (both ships)")}}},
        {"intrigue", QT_TRANSLATE_NOOP("ConquestScreen", "Diplomacy and intrigue"), {
            {"diplomacy / ally / break", QT_TRANSLATE_NOOP("ConquestScreen", "relations, alliances")},
            {"spy <planet>", QT_TRANSLATE_NOOP("ConquestScreen", "intelligence by a gate team")},
            {"sabotage yard|defense|mine <planet>", QT_TRANSLATE_NOOP("ConquestScreen", "strike a world of an enemy")}}},
        {"game", QT_TRANSLATE_NOOP("ConquestScreen", "Game"), {
            {"hud on|off", QT_TRANSLATE_NOOP("ConquestScreen", "radar, ship and weapon panels")},
            {"menu", QT_TRANSLATE_NOOP("ConquestScreen", "pause, save, settings (Esc)")},
            {"clear", QT_TRANSLATE_NOOP("ConquestScreen", "clear the console")}}},
    };
    return catalog;
}

} // namespace

void ConquestScreen::showHelp()
{
    m_console->appendHeading(tr("Help - type 'help <category>'"));
    for (const HelpCategory &category : conquestHelp()) {
        QStringList first;
        for (int i = 0; i < category.entries.size() && first.size() < 4; ++i) {
            const QString word = QString::fromLatin1(category.entries[i].syntax).section(' ', 0, 0);
            if (!first.contains(word) && !word.startsWith('('))
                first << word;
        }
        m_console->appendEntry(QStringLiteral("help %1").arg(category.key),
                               tr("%1: %2...").arg(tr(category.title), first.join(", ")));
    }
    m_console->appendEntry(QStringLiteral("Tab"), tr("completes commands and arguments (plan T2, repair eng...)"));
    m_console->appendLine(tr("Win by taking every planet of the enemy factions."));
}

void ConquestScreen::showHelpTopic(const QString &topic)
{
    for (const HelpCategory &category : conquestHelp())
        if (topic == category.key || tr(category.title).compare(topic, Qt::CaseInsensitive) == 0) {
            m_console->appendHeading(tr(category.title));
            for (const HelpEntry &e : category.entries)
                m_console->appendEntry(QString::fromLatin1(e.syntax), tr(e.description));
            return;
        }
    // A command: its lines
    bool found = false;
    for (const HelpCategory &category : conquestHelp())
        for (const HelpEntry &e : category.entries)
            if (QString::fromLatin1(e.syntax).split(QRegularExpression("[ /]+")).contains(topic)) {
                if (!found)
                    m_console->appendHeading(topic);
                found = true;
                m_console->appendEntry(QString::fromLatin1(e.syntax), tr(e.description));
            }
    if (!found)
        m_console->appendLine(tr("No help for '%1' - 'help' lists the categories").arg(topic), CommandConsole::Tone::Warning);
}

// Tab completion: command names, then their arguments
QStringList ConquestScreen::completeLine(const QString &text) const
{
    static const QStringList names{"help", "goto", "planets", "status", "info", "fortify", "repair", "hud", "fleets",
                                   "send", "build", "engage", "research", "ally", "break", "diplomacy", "launch",
                                   "drones", "turrets", "railguns", "power", "attack", "maintower", "pulseweapon",
                                   "secondarytower", "shield", "scan", "menu", "clear", "target", "missiles", "evade",
                                   "spy", "sabotage", "replay", "plan", "crew", "jobs", "cancel", "seal", "unseal",
                                   "vent", "defend", "dock", "board", "library"};
    QStringList lines;
    if (!text.contains(' ')) {
        for (const QString &n : names)
            if (!text.isEmpty() && n.startsWith(text.trimmed(), Qt::CaseInsensitive))
                lines << n;
        return lines;
    }
    QStringList words = text.split(' ', Qt::SkipEmptyParts);
    const QString partial = text.endsWith(' ') ? QString() : words.takeLast();
    if (words.isEmpty())
        return lines;
    const QString command = words.first().toLower();
    QStringList choices;
    if (ShipCrew::commands().contains(command))
        choices = m_crew->argumentChoices(words);
    if (words.size() == 1 && (command == "goto" || command == "info" || command == "fortify" || command == "spy"))
        for (const Conquest::Planet &p : m_game->planets())
            choices << p.name;
    if (words.size() == 1 && command == "help")
        for (const HelpCategory &category : conquestHelp())
            choices << QString::fromLatin1(category.key);
    if (words.size() == 1 && command == "sabotage")
        choices << QStringList{"yard", "defense", "mine"};
    if (words.size() == 1 && (command == "turrets" || command == "railguns" || command == "evade" || command == "hud"
                              || command == "shield"))
        choices << QStringList{"on", "off"};
    const QString base = words.join(' ') + ' ';
    for (const QString &c : choices)
        if (c.startsWith(partial, Qt::CaseInsensitive))
            lines << base + c;
    lines.removeDuplicates();
    return lines;
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
    // The flagship has no component table: its parts follow the battle
    // systems, and only those it carries (no shield on a hive, no railguns
    // on a Ha'tak...)
    struct Part
    {
        QString name;
        const char *system;
        bool weapon;
    };
    auto parts = [this] {
        const WeaponReadiness w = m_battle->weaponReadiness();
        QList<Part> list{{QStringLiteral("Life Support"), "lifesupport", false}};
        if (m_battle->hasShield())
            list.append({QStringLiteral("Shield Generator"), "shields", false});
        list.append({QStringLiteral("Power Core"), "", false});
        list.append({QStringLiteral("FTL Drive"), "engines", false});
        list.append({QStringLiteral("Sublight Engine"), "engines", false});
        list.append({QCoreApplication::translate("Balance", w.mainName.toLatin1().constData()), "weapons", true});
        if (w.hasTowers)
            list.append({QStringLiteral("Energy Cannon"), "weapons", true});
        if (w.hasRailguns)
            list.append({QStringLiteral("Railgun"), "weapons", true});
        if (w.missilesMax > 0)
            list.append({QStringLiteral("Missile Bay"), "weapons", true});
        if (w.dronesMax > 0)
            list.append({QStringLiteral("Drone Bay"), "weapons", true});
        return list;
    };
    auto health = [this](const char *system) {
        return *system ? qRound(m_battle->subsystemHealth(QString::fromLatin1(system))) : 100;
    };
    auto hit = [this](const char *system) {
        return *system && m_battle->secondsSinceHit(QString::fromLatin1(system)) < 1.5f;
    };

    m_shipStatus->setProvider([this, parts, health, hit] {
        ShipStatusData data;
        data.hull = m_battle->hull();
        data.shield = m_battle->shieldStrength();
        data.shieldUp = m_battle->shieldActive();
        data.hasShield = m_battle->hasShield();
        const BattleView::Schematic schematic = m_battle->schematic();
        data.silhouette = schematic.silhouette;
        data.silhouetteRect = schematic.rect;
        data.shieldCenter = schematic.shieldCenter;
        data.shieldRadii = schematic.shieldRadii;
        for (const Part &part : parts())
            data.components.append({part.name, health(part.system), 100, hit(part.system)});
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
    m_weapons->setProvider([this, parts, health, hit] {
        WeaponStatusData data;
        data.readiness = m_battle->weaponReadiness();
        for (const Part &part : parts())
            if (part.weapon)
                data.weapons.append({part.name, health(part.system), 100, hit(part.system)});
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
