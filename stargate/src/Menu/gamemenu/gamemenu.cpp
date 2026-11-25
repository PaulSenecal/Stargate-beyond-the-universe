#include "gamemenu.h"

#include <QEvent>
#include <QJsonArray>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "commandconsole.h"
#include "hudimagepanel.h"
#include "routemapwidget.h"
#include "radarwidget.h"
#include "shiplibrarydialog.h"
#include "replaydialog.h"
#include "shipcrew.h"
#include "shipplandialog.h"
#include "shipstatuswidget.h"
#include "src/Menu/pause/pausemenu.h"
#include "src/conquest/conquestendscreen.h"
#include "src/network/coopsession.h"
#include "src/audio/musicplayer.h"
#include "src/audio/soundsystem.h"
#include "weaponstatuswidget.h"
#include "src/core/commandmanager.h"
#include "src/core/gameconfig.h"
#include "src/core/modloader.h"
#include "src/core/questsystem.h"
#include "src/db/databasemanager.h"
#include "src/db/repositories/questrepository.h"
#include "src/db/repositories/saverepository.h"
#include "src/db/repositories/shiplibraryrepository.h"
#include "src/db/repositories/worldrepository.h"
#include "src/render/planetlook.h"
#include "src/world/planetgenerator.h"
#include "src/db/dbtext.h"

namespace {
constexpr double kHoursPerTick = 0.25;     // 1 real second = 15 game minutes
constexpr int kAutosaveMs = 30000;
constexpr int kLowWater = 40;
constexpr int kLowFood = 25;

QString percent(float value)
{
    return QStringLiteral("%1%").arg(qRound(value * 100.0f));
}

} // namespace

namespace {

// The orders of the campaign, by category, for 'help'
struct HelpEntry { const char *syntax; const char *description; };
struct HelpCategory { const char *key; const char *title; QList<HelpEntry> entries; };

const QList<HelpCategory> &helpCatalog()
{
    static const QList<HelpCategory> catalog{
        {"combat", QT_TRANSLATE_NOOP("GameMenu", "Combat"), {
            {"attack <n>", QT_TRANSLATE_NOOP("GameMenu", "main weapon on target n (T1, T2... on screen)")},
            {"scan <n>", QT_TRANSLATE_NOOP("GameMenu", "lock target n")},
            {"target <n> <system>", QT_TRANSLATE_NOOP("GameMenu", "aim at weapons, shields, engines or hangar")},
            {"pulseweapon", QT_TRANSLATE_NOOP("GameMenu", "volley on every contact")},
            {"secondarytower", QT_TRANSLATE_NOOP("GameMenu", "burst of the towers on the target")},
            {"missiles [n]", QT_TRANSLATE_NOOP("GameMenu", "missile salvo (limited stock)")},
            {"drones [n]", QT_TRANSLATE_NOOP("GameMenu", "drone salvo through the shields (needs free power)")},
            {"shield on|off", QT_TRANSLATE_NOOP("GameMenu", "raise or lower the shields")},
            {"evade on|off", QT_TRANSLATE_NOOP("GameMenu", "evasive maneuvers: harder to hit")},
            {"auto <weapon|all> on|off", QT_TRANSLATE_NOOP("GameMenu", "automatic fire: main, pulse, missiles, drones, towers, railguns")},
            {"launch [n]", QT_TRANSLATE_NOOP("GameMenu", "fighters out of the hangar")},
            {"board <n>", QT_TRANSLATE_NOOP("GameMenu", "capture a crippled mothership")},
            {"replay", QT_TRANSLATE_NOOP("GameMenu", "watch the last battle again")}}},
        {"energy", QT_TRANSLATE_NOOP("GameMenu", "Energy"), {
            {"power", QT_TRANSLATE_NOOP("GameMenu", "report of the power use")},
            {"power shields|weapons|balanced", QT_TRANSLATE_NOOP("GameMenu", "where the spare power goes")},
            {"turrets on|off", QT_TRANSLATE_NOOP("GameMenu", "automatic towers (free their power)")},
            {"railguns on|off", QT_TRANSLATE_NOOP("GameMenu", "point defense against missiles")},
            {"star", QT_TRANSLATE_NOOP("GameMenu", "dive into a star to refill (shields up!)")}}},
        {"ship", QT_TRANSLATE_NOOP("GameMenu", "Ship"), {
            {"plan [room]", QT_TRANSLATE_NOOP("GameMenu", "plan of the compartments: fires, air, breaches")},
            {"repair <system|hull|room>", QT_TRANSLATE_NOOP("GameMenu", "a repair job for the crew")},
            {"jobs / cancel <n>", QT_TRANSLATE_NOOP("GameMenu", "repairs under way / stop one")},
            {"seal <room> / unseal <room>", QT_TRANSLATE_NOOP("GameMenu", "close or open its doors")},
            {"vent <room>", QT_TRANSLATE_NOOP("GameMenu", "open it to space: the fire dies")},
            {"unlock <room>", QT_TRANSLATE_NOOP("GameMenu", "reopen a closed section")},
            {"install <module> [room]", QT_TRANSLATE_NOOP("GameMenu", "build a module ('blueprints')")},
            {"defend <room>", QT_TRANSLATE_NOOP("GameMenu", "soldiers against boarders")},
            {"destinystatus", QT_TRANSLATE_NOOP("GameMenu", "hull, shields and systems")},
            {"weaponstatus", QT_TRANSLATE_NOOP("GameMenu", "weapons, target, ammunition")},
            {"showressource", QT_TRANSLATE_NOOP("GameMenu", "resources in the stores")}}},
        {"crew", QT_TRANSLATE_NOOP("GameMenu", "Crew"), {
            {"crew", QT_TRANSLATE_NOOP("GameMenu", "who does what, fatigue, injuries")},
            {"crew rest <name>", QT_TRANSLATE_NOOP("GameMenu", "send someone to rest")},
            {"crew rotate", QT_TRANSLATE_NOOP("GameMenu", "every tired crew member rests")}}},
        {"explore", QT_TRANSLATE_NOOP("GameMenu", "Exploration"), {
            {"scansector", QT_TRANSLATE_NOOP("GameMenu", "planets of the system and their addresses")},
            {"kino <address>", QT_TRANSLATE_NOOP("GameMenu", "scout a planet first")},
            {"stargate <address>", QT_TRANSLATE_NOOP("GameMenu", "send a team through the gate")},
            {"jumper <address>", QT_TRANSLATE_NOOP("GameMenu", "fly a team there (planets without gate)")},
            {"explore", QT_TRANSLATE_NOOP("GameMenu", "search the planet for resources")},
            {"stargate destiny", QT_TRANSLATE_NOOP("GameMenu", "bring the team back")},
            {"engine ftl / ftl abort", QT_TRANSLATE_NOOP("GameMenu", "jump to the next system / cancel")},
            {"salvage", QT_TRANSLATE_NOOP("GameMenu", "recover the wrecks after a battle")}}},
        {"journey", QT_TRANSLATE_NOOP("GameMenu", "Life aboard"), {
            {"choose <n>", QT_TRANSLATE_NOOP("GameMenu", "answer a decision")},
            {"stones", QT_TRANSLATE_NOOP("GameMenu", "communication stones (once a day)")},
            {"observe on|off", QT_TRANSLATE_NOOP("GameMenu", "observation deck (morale)")},
            {"quests / hint", QT_TRANSLATE_NOOP("GameMenu", "objectives / what to do next")},
            {"stats / achievements", QT_TRANSLATE_NOOP("GameMenu", "journey statistics / achievements")}}},
        {"game", QT_TRANSLATE_NOOP("GameMenu", "Game"), {
            {"save", QT_TRANSLATE_NOOP("GameMenu", "save now (also automatic)")},
            {"menu", QT_TRANSLATE_NOOP("GameMenu", "pause, settings, quit (Esc)")},
            {"library [ship]", QT_TRANSLATE_NOOP("GameMenu", "every ship, its attacks and chances")},
            {"coop host tactical|engineering", QT_TRANSLATE_NOOP("GameMenu", "a second player as co-pilot")},
            {"mods / seed", QT_TRANSLATE_NOOP("GameMenu", "loaded mods / universe seed")},
            {"sound / music on|off|<0-100>", QT_TRANSLATE_NOOP("GameMenu", "volume")},
            {"clear", QT_TRANSLATE_NOOP("GameMenu", "clear the console")}}},
    };
    return catalog;
}

} // namespace

GameMenu::GameMenu(SoundSystem *sound, QWidget *parent)
    : QWidget(parent)
    , m_battle(new BattleView(this))
    , m_console(new CommandConsole(this))
    , m_commands(new CommandManager(this))
    , m_route(new DestinyRoute(this))
    , m_simTimer(new QTimer(this))
    , m_autosaveTimer(new QTimer(this))
{
    m_quests = new QuestSystem(this);
    m_crew = std::make_unique<ShipCrew>(&m_interior, m_battle, m_console, this);
    m_crew->pay = [this](const ShipCrew::Cost &cost) { return payFor(cost); };
    m_crew->systemCost = [this](const QString &key) {
        ShipCrew::Cost cost;
        for (const RepairCost &c : m_destiny.repairCost(key))
            cost.append({c.resourceName, c.quantity});
        return cost;
    };
    m_crew->onQuest = [this](const QString &type) {
        if (type.startsWith("REPAIR:")) {
            for (const QString &component : m_destiny.componentNames(type.mid(7)))
                m_quests->notify("REPAIR", component);
        } else {
            m_quests->notify(type);
        }
    };
    m_crew->onSystemChanged = [this](const QString &key, float health) { m_destiny.setSystemHealth(key, health); };
    m_crew->crewCount = [this] { return m_route->state().crew; };
    m_console->setTimeProvider([this] {
        const double hours = m_route->state().hours;
        const int h = int(hours);
        return QStringLiteral("D%1 %2:%3").arg(h / 24 + 1).arg(h % 24, 2, 10, QChar('0'))
            .arg(int((hours - h) * 60), 2, 10, QChar('0'));
    });
    m_console->setAlertProvider([this] {
        QList<CommandConsole::Alert> list = m_crew->alerts();
        if (const CampaignLife::Choice *c = m_life.pendingChoice())
            list.prepend({tr("DECISION - %1").arg(c->title), CommandConsole::Tone::Warning, QStringLiteral("choose ")});
        if (m_battle->jumpPending())
            list.prepend({tr("FTL %1 s").arg(qRound(m_battle->weaponReadiness().ftlCountdown)), CommandConsole::Tone::Info,
                          QStringLiteral("ftl abort")});
        else if (m_route->canJump() && m_battle->hostileCount() > 0)
            list.append({tr("FTL READY - jump away"), CommandConsole::Tone::Success, QStringLiteral("engine ftl")});
        if (m_wrecks > 0 && m_battle->hostileCount() == 0)
            list.append({tr("%1 WRECK(S)").arg(m_wrecks), CommandConsole::Tone::Success, QStringLiteral("salvage")});
        return list;
    });
    m_crew->onCapture = [this](const QString &faction) {
        m_life.addStat("captures");
        // Its stores and its technology
        const QString gained = m_destiny.addResources({{"naquadah", 12}, {"trinium", 4}, {"minerals", 8}});
        if (!gained.isEmpty())
            m_console->appendLine(tr("Salvage from the captured ship: %1").arg(gained), CommandConsole::Tone::Success);
        const QString blueprint = faction == "Goa'uld" || faction == "Lucian Alliance" ? QStringLiteral("goauld_cannon")
                                : faction == "Asgard"                                 ? QStringLiteral("asgard_shield")
                                : faction == "Ancients"                               ? QStringLiteral("sensor_suite")
                                                                                      : m_interior.randomUnknownBlueprint();
        m_interior.learnBlueprint(blueprint);
        announceAchievements();
    };
    for (const ModLoader::Event &e : ModLoader::instance().events())
        m_life.addCustomChoice(e.id, e.title, e.text, e.options, e.outcomes);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(6, 6, 8));
    setPalette(pal);

    // --- Top area: HUD screens on both sides of the 3D view
    auto *topArea = new QWidget(this);
    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    m_radar = new RadarWidget(topArea);
    left->addWidget(m_radar, 1);
    m_shipStatus = new ShipStatusWidget(topArea);
    left->addWidget(m_shipStatus, 1);

    auto *right = new QVBoxLayout;
    right->setSpacing(8);
    m_map = new RouteMapWidget(topArea);
    right->addWidget(m_map, 1);
    m_weapons = new WeaponStatusWidget(topArea);
    right->addWidget(m_weapons, 1);

    auto *top = new QHBoxLayout(topArea);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(12);
    top->addLayout(left, 20);
    top->addWidget(m_battle, 60);
    top->addLayout(right, 20);

    // --- Console on the full width; the handle between the 3D view and
    // the console can be dragged, its position is remembered
    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(8);
    splitter->setStyleSheet("QSplitter::handle { background: #1c1a12; }"
                            "QSplitter::handle:hover { background: #e2bf3e; }");
    splitter->addWidget(topArea);
    splitter->addWidget(m_console);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({650, 350});

    QSettings settings("Stargate", "stargate");
    const QByteArray savedLayout = settings.value("gameMenu/splitter").toByteArray();
    if (!savedLayout.isEmpty())
        splitter->restoreState(savedLayout);
    connect(splitter, &QSplitter::splitterMoved, this, [splitter] {
        QSettings("Stargate", "stargate").setValue("gameMenu/splitter", splitter->saveState());
    });

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->addWidget(splitter);

    // --- Wiring
    connect(m_console, &CommandConsole::commandEntered, this, &GameMenu::onCommandEntered);
    connect(m_battle, &BattleView::logMessage, this, &GameMenu::onBattleLog);
    m_sound = sound;
    if (m_sound)
        connect(m_battle, &BattleView::sound, m_sound, &SoundSystem::play);

    // --- Pause menu: Escape, the MENU button or the 'menu' command
    m_pauseMenu = new PauseMenu(m_sound, this);
    connect(m_pauseMenu, &PauseMenu::resumeRequested, this, [this] { setPaused(false); });
    connect(m_pauseMenu, &PauseMenu::saveRequested, this, [this] {
        const bool ok = saveGame(true);
        m_pauseMenu->showSaveResult(ok, ok ? tr("Game saved") : tr("Save failed (no save database)"));
    });
    connect(m_pauseMenu, &PauseMenu::quitToTitleRequested, this, &GameMenu::quitToTitleRequested);
    connect(m_pauseMenu, &PauseMenu::quitGameRequested, this, &GameMenu::quitGameRequested);
    connect(m_pauseMenu, &PauseMenu::languageChanged, this, &GameMenu::languageChanged);
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
    connect(m_menuButton, &QPushButton::clicked, this, &GameMenu::openPauseMenu);
    m_battle->installEventFilter(this);
    connect(m_route, &DestinyRoute::notice, this, &GameMenu::onRouteNotice);
    connect(m_battle, &BattleView::jumpReady, this, &GameMenu::performJump);
    connect(m_battle, &BattleView::battleEnded, this, &GameMenu::onBattleEnded);
    connect(m_battle, &BattleView::boardingParty, this, [this](int strength, const QString &, const QString &) {
        m_interior.board(strength);
        flushInteriorMessages();
    });
    connect(m_battle, &BattleView::hullBreached, this, [this] {
        if (!gameConfig().ironman)
            return;
        // Ironman: no second chance, the save is gone
        if (m_sessionId >= 0)
            SaveRepository::reset(m_sessionId);
        m_sessionId = -1;
        m_autosaveTimer->stop();
        endCampaign(false, tr("DESTINY LOST"), tr("Ironman: the hull gave way - this journey is over"));
    });
    connect(m_battle, &BattleView::hullHit, this, [this](const QVector3D &local, float damage) {
        m_interior.damageAt(QPointF(local.x(), local.z()), damage);
    });
    connect(m_simTimer, &QTimer::timeout, this, &GameMenu::simulate);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] { saveGame(false); });

    if (m_commands->loadCommandsFromFile(":/commandList/commands.json")) {
        QStringList words;
        for (const QString &cmd : m_commands->getAvailableCommands())
            words << cmd.toLower();
        words << "help" << "clear" << "save" << "quests" << "hint" << "library" << "sound" << "music" << "menu" << "launch" << "turrets" << "railguns" << "power"
              << "target" << "missiles" << "drones" << "evade" << "ftl" << "star"
              << "plan" << "crew" << "jobs" << "cancel" << "seal" << "unseal" << "vent" << "unlock" << "install"
              << "blueprints" << "defend" << "board" << "choose" << "stones" << "kino" << "jumper" << "salvage"
              << "observe" << "stats" << "achievements" << "seed" << "mods" << "replay" << "coop" << "auto";
        words.removeDuplicates();
        words.sort();
        m_console->setCompletions(words);
        m_console->setCompletionProvider([this](const QString &text) {
            QStringList addresses{"destiny"};
            for (const Planet &p : m_route->currentPlanets())
                addresses << p.gateAddress();
            QStringList lines = m_commands->completions(text, {{"stargate", addresses}});
            lines += argumentCompletions(text);
            for (const QString &local : {QStringLiteral("help"), QStringLiteral("clear"), QStringLiteral("save"),
                                         QStringLiteral("quests"), QStringLiteral("hint"), QStringLiteral("library"),
                                         QStringLiteral("sound"), QStringLiteral("music"),
                                         QStringLiteral("menu"), QStringLiteral("launch"), QStringLiteral("turrets"),
                                         QStringLiteral("railguns"), QStringLiteral("power"), QStringLiteral("target"),
                                         QStringLiteral("missiles"), QStringLiteral("drones"), QStringLiteral("evade"),
                                         QStringLiteral("ftl"), QStringLiteral("star"), QStringLiteral("plan"),
                                         QStringLiteral("crew"), QStringLiteral("jobs"), QStringLiteral("seal"),
                                         QStringLiteral("unseal"), QStringLiteral("vent"), QStringLiteral("unlock"),
                                         QStringLiteral("install"), QStringLiteral("blueprints"), QStringLiteral("defend"),
                                         QStringLiteral("board"), QStringLiteral("cancel"), QStringLiteral("choose"),
                                         QStringLiteral("stones"), QStringLiteral("kino"), QStringLiteral("jumper"),
                                         QStringLiteral("salvage"), QStringLiteral("observe"), QStringLiteral("stats"),
                                         QStringLiteral("achievements"), QStringLiteral("seed")})
                if (!text.trimmed().isEmpty() && local.startsWith(text.trimmed(), Qt::CaseInsensitive)
                    && !text.contains(' '))
                    lines << local;
            return lines;
        });
    } else {
        m_console->appendLine(tr("Command database unavailable"), CommandConsole::Tone::Critical);
    }

    m_console->appendLine(tr("Destiny tactical console online"), CommandConsole::Tone::Success);
    loadSave();
    restoreGame();
    recordCurrentSystem();
    loadQuests();
    m_console->appendLine(tr("Dropped out of FTL in %1 - %2 planet(s), gate window %3 h - type 'scansector'")
                              .arg(m_route->galaxyName(m_route->currentStop().galaxy))
                              .arg(m_route->currentStop().numVariants)
                              .arg(m_route->state().gateRemaining, 0, 'f', 1));
    m_console->appendLine(tr("Hostile ships detected - shields up"), CommandConsole::Tone::Warning);
    m_console->appendLine(tr("Type 'help' for the command list"));

    m_shipStatus->setProvider([this] {
        ShipStatusData data;
        data.hull = m_battle->hull();
        data.shield = m_battle->shieldStrength();
        data.shieldUp = m_battle->shieldActive();
        const BattleView::Schematic schematic = m_battle->schematic();
        data.silhouette = schematic.silhouette;
        data.silhouetteRect = schematic.rect;
        data.shieldCenter = schematic.shieldCenter;
        data.shieldRadii = schematic.shieldRadii;
        for (const ShipComponent &c : m_destiny.components()) {
            const QString key = m_destiny.systemKeyOf(c.name);
            data.components.append({c.name, c.currentHp, c.maxHp,
                                    !key.isEmpty() && m_battle->secondsSinceHit(key) < 1.5f});
        }
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

    m_weapons->setProvider([this] {
        WeaponStatusData data;
        data.readiness = m_battle->weaponReadiness();
        const bool hit = m_battle->secondsSinceHit("weapons") < 1.5f;
        for (const ShipComponent &c : m_destiny.components())
            if (c.category == "WEAPON")
                data.weapons.append({c.name, c.currentHp, c.maxHp, hit});
        return data;
    });

    updateNavigation();
    updatePlanetView();
    updateMusic(true);
    m_simTimer->start(1000);
    if (m_sessionId >= 0)
        m_autosaveTimer->start(kAutosaveMs);

    QTimer::singleShot(0, m_console, &CommandConsole::focusInput);
}

void GameMenu::onBattleLog(const QString &text, BattleView::LogLevel level)
{
    CommandConsole::Tone tone = CommandConsole::Tone::Info;
    switch (level) {
    case BattleView::LogLevel::Info:     tone = CommandConsole::Tone::Info; break;
    case BattleView::LogLevel::Success:  tone = CommandConsole::Tone::Success; break;
    case BattleView::LogLevel::Warning:  tone = CommandConsole::Tone::Warning; break;
    case BattleView::LogLevel::Critical: tone = CommandConsole::Tone::Critical; break;
    }
    m_console->appendLine(text, tone, CommandConsole::Channel::Combat);
}

void GameMenu::onCommandEntered(const QString &command)
{
    m_console->appendLine(command, CommandConsole::Tone::Echo);

    const QString first = command.section(' ', 0, 0, QString::SectionSkipEmpty).toLower();
    if (first == "help" || first == "?") {
        const QString topic = command.section(' ', 1, 1, QString::SectionSkipEmpty);
        if (topic.isEmpty())
            showHelp();
        else
            showHelpTopic(topic.toLower());
        return;
    }
    if (first == "clear") {
        m_console->clearLog();
        return;
    }
    if (first == "save") {
        saveGame(true);
        return;
    }
    if (first == "quests") {
        if (!m_quests->isLoaded())
            m_console->appendLine(tr("No quest available (no save database)"), CommandConsole::Tone::Warning);
        for (const QString &line : m_quests->journal())
            m_console->appendLine(line);
        return;
    }
    if (first == "library" || first == "ships") {
        showLibrary(command.section(' ', 1, -1, QString::SectionSkipEmpty));
        return;
    }
    if (first == "turrets" || first == "railguns" || first == "power") {
        const QString arg = command.section(' ', 1, 1, QString::SectionSkipEmpty).toLower();
        if (first == "turrets")
            m_battle->setTurretsActive(arg != "off");
        else if (first == "railguns")
            m_battle->setRailgunsActive(arg != "off");
        else if (arg.startsWith("shield"))
            m_battle->setPowerPriority(BattleView::PowerPriority::Shields);
        else if (arg.startsWith("weapon"))
            m_battle->setPowerPriority(BattleView::PowerPriority::Weapons);
        else if (arg.startsWith("bal"))
            m_battle->setPowerPriority(BattleView::PowerPriority::Balanced);
        else
            m_console->appendLine(m_battle->powerReport());
        return;
    }
    if (localCommand(first, command.split(' ', Qt::SkipEmptyParts)))
        return;
    if (first == "launch") {
        const QString count = command.section(' ', 1, 1, QString::SectionSkipEmpty);
        m_battle->launchFighters(count.isEmpty() ? -1 : std::max(1, count.toInt()));
        return;
    }
    if (first == "menu" || first == "pause") {
        openPauseMenu();
        return;
    }
    if (first == "music") {
        musicCommand(command.section(' ', 1, 1, QString::SectionSkipEmpty).toLower());
        return;
    }
    if (first == "sound" || first == "volume") {
        soundCommand(command.section(' ', 1, 1, QString::SectionSkipEmpty).toLower());
        return;
    }
    if (first == "hint") {
        const QString hint = m_quests->hint();
        m_console->appendLine(hint.isEmpty() ? tr("No hint right now") : tr("Hint: %1").arg(hint));
        return;
    }

    const ParsedCommand parsed = m_commands->parse(command);
    if (!parsed.valid) {
        m_console->appendLine(parsed.error, CommandConsole::Tone::Critical);
        const QString close = suggestLocal(first);
        bool known = false;
        for (const QString &name : m_commands->getAvailableCommands())
            known = known || name.compare(first, Qt::CaseInsensitive) == 0;
        if (!known && !close.isEmpty() && close != first)
            m_console->appendLine(tr("Did you mean '%1'? ('help %1' for details)").arg(close), CommandConsole::Tone::Info);
        return;
    }
    dispatch(parsed.name, parsed.args);
}

// Tactical orders that are not in commands.json
bool GameMenu::localCommand(const QString &first, const QStringList &words)
{
    const QString arg = words.value(1).toLower();
    if (first == "target" || first == "aim") {
        bool ok = false;
        const int target = words.value(1).toInt(&ok);
        if (!ok || target < 1)
            m_console->appendLine(tr("Usage: target <n> [weapons|shields|engines|hangar|hull]"), CommandConsole::Tone::Warning);
        else if (m_battle->aimAt(target - 1, words.value(2)) && !words.value(2).isEmpty()
                 && words.value(2).toLower() != "hull")
            m_quests->notify("TARGET");
        return true;
    }
    if (first == "missiles" || first == "missile") {
        m_battle->fireMissiles(arg.isEmpty() ? -1 : arg.toInt() - 1);
        return true;
    }
    if (first == "drones") {
        m_battle->fireDrones(arg.isEmpty() ? -1 : arg.toInt() - 1);
        return true;
    }
    if (first == "auto") {
        if (words.size() < 2)
            m_console->appendLine(m_battle->autoFireReport());
        else
            m_battle->setAutoFire(words.value(1), words.value(2).toLower() != "off");
        return true;
    }
    if (first == "evade" || first == "evasive") {
        m_battle->setEvasion(arg != "off");
        return true;
    }
    if (first == "ftl" || first == "jump") {
        if (arg == "abort" || arg == "cancel")
            m_battle->abortJumpSequence();
        else
            jumpFtl();
        return true;
    }
    if (first == "star" || first == "dive") {
        diveIntoStar();
        return true;
    }
    return m_crew->handle(first, words) || lifeCommand(first, words) || coopCommand(first, words);
}

bool GameMenu::payFor(const QList<std::pair<QString, int>> &cost)
{
    QList<RepairCost> list;
    for (const auto &[resource, quantity] : cost)
        list.append({0, resource, quantity});
    QString missing;
    if (!m_destiny.consume(list, &missing)) {
        m_console->appendLine(tr("Missing: %1").arg(missing), CommandConsole::Tone::Warning);
        return false;
    }
    return true;
}




void GameMenu::flushInteriorMessages()
{
    m_crew->flushMessages();
}

// The interior runs with the game clock: fires, air, jobs, crew
void GameMenu::updateInterior(float seconds)
{
    m_interior.setCrewCount(m_route->state().crew);
    const Interior::Effects e = m_crew->tick(seconds);
    if (e.crewLost > 0)
        m_route->changeCrew(-e.crewLost);
    if (e.firesOut > 0)
        m_life.addStat("fires", e.firesOut);
    if (e.food >= 1.0f || e.water >= 1.0f)
        m_destiny.addResources({{"food", int(e.food)}, {"water", int(e.water)}});
    applyBonuses();
    flushInteriorMessages();
}

// Key characters and modules change the ship
void GameMenu::applyBonuses()
{
    m_crew->applyBonuses();
}

// ================================================================
//  Life aboard: choices, stones, scouts, salvage, statistics
// ================================================================

bool GameMenu::lifeCommand(const QString &first, const QStringList &words)
{
    const QString arg = words.mid(1).join(' ');
    if (first == "choose" || first == "answer") {
        Outcome outcome;
        if (!m_life.pendingChoice())
            m_console->appendLine(tr("No decision is waiting"), CommandConsole::Tone::Warning);
        else if (!m_life.choose(arg.toInt() - 1, &outcome))
            m_console->appendLine(tr("Choose an option between 1 and %1").arg(m_life.pendingChoice()->options.size()),
                                  CommandConsole::Tone::Warning);
        else
            applyOutcome(outcome);
        return true;
    }
    if (first == "stones") {
        const double hours = m_route->state().hours;
        if (!m_life.stonesReady(hours)) {
            m_console->appendLine(tr("The communication stones need %1 h more to link with Earth")
                                      .arg(m_life.stonesCooldown(hours), 0, 'f', 1),
                                  CommandConsole::Tone::Warning);
            return true;
        }
        m_console->appendLine(tr("Communication stones: a crew member swaps body with someone on Earth..."));
        applyOutcome(m_life.useStones(hours));
        return true;
    }
    if (first == "kino") {
        const int variant = arg == "build" ? 0 : m_route->variantForAddress(arg);
        if (variant < 0) {
            m_console->appendLine(tr("Usage: kino <address> - a planet of this system ('scansector'), kino build"),
                                  CommandConsole::Tone::Warning);
            return true;
        }
        if (arg == "build") {
            if (m_kinos >= 6)
                m_console->appendLine(tr("Six Kinos already"), CommandConsole::Tone::Warning);
            else if (payFor({{QStringLiteral("minerals"), 4}}))
                m_console->appendLine(tr("Kino assembled (%1)").arg(++m_kinos), CommandConsole::Tone::Success);
            return true;
        }
        if (m_kinos <= 0) {
            m_console->appendLine(tr("No Kino left - they are rebuilt from minerals ('kino build', 4 minerals)"),
                                  CommandConsole::Tone::Warning);
            return true;
        }
        const Planet p = PlanetGenerator::generate(m_route->currentStop().galaxy, m_route->currentStop().sequence, variant);
        const PlanetConditions &c = p.conditions;
        m_console->appendLine(tr("Kino on %1 \"%2\": air %3, toxicity %4, radiation %5, %6")
                                  .arg(p.code, p.nickname, percent(c.oxygen), percent(c.toxicity), percent(c.radiation))
                                  .arg(c.hasLife ? tr("life signs") : tr("no life")),
                              CommandConsole::Tone::Info);
        QStringList finds;
        if (c.minerals >= 0.4f) finds << tr("minerals (railgun slugs)");
        if (c.energy >= 0.35f) finds << tr("naquadah (missiles)");
        if (c.water >= 0.5f) finds << tr("water");
        if (c.organics >= 0.5f) finds << tr("food");
        if (p.isAncientSite()) finds << tr("an Ancient structure (drones, blueprints?)");
        m_console->appendLine(finds.isEmpty() ? tr("Nothing worth the trip") : tr("Worth it: %1").arg(finds.join(", ")),
                              finds.isEmpty() ? CommandConsole::Tone::Warning : CommandConsole::Tone::Success);
        if (!p.hasGate)
            m_console->appendLine(tr("No Stargate there: the Puddle Jumper can fly ('jumper %1')").arg(p.gateAddress()));
        if (!m_scouted.contains(variant))
            m_scouted << variant;
        // The Kino may not survive a hostile surface
        if (c.toxicity + c.radiation > 0.8f && QRandomGenerator::global()->generateDouble() < 0.3) {
            --m_kinos;
            m_console->appendLine(tr("The Kino stopped transmitting (%1 left)").arg(m_kinos), CommandConsole::Tone::Warning);
        }
        return true;
    }
    if (first == "jumper") {
        if (arg == "build" || arg.isEmpty()) {
            m_console->appendLine(tr("Usage: jumper <address> - fly a team to a planet of the system, even without a gate "
                                     "(2 naquadah of fuel)"));
            return true;
        }
        const int variant = m_route->variantForAddress(arg);
        QString error;
        if (variant < 0) {
            m_console->appendLine(tr("No planet at %1 in this system").arg(arg.toUpper()), CommandConsole::Tone::Warning);
        } else if (m_battle->hostileCount() > 0) {
            m_console->appendLine(tr("Too dangerous with hostile ships around"), CommandConsole::Tone::Warning);
        } else if (m_battle->hangarFighters() <= 0) {
            m_console->appendLine(tr("No Puddle Jumper in the hangar"), CommandConsole::Tone::Warning);
        } else if (!payFor({{QStringLiteral("naquadah"), 2}})) {
        } else if (!m_route->flyTo(variant, &error)) {
            m_destiny.addResources({{"naquadah", 2}});
            m_console->appendLine(tr("Flight impossible: %1").arg(error), CommandConsole::Tone::Warning);
        } else {
            const Planet p = PlanetGenerator::generate(m_route->currentStop().galaxy, m_route->currentStop().sequence, variant);
            m_quests->notify("EXPLORE");
            m_console->appendLine(tr("The Puddle Jumper lands on %1 \"%2\" - 'explore', then 'stargate destiny' to fly back")
                                      .arg(p.code, p.nickname),
                                  CommandConsole::Tone::Success);
            updateNavigation();
            updatePlanetView();
        }
        return true;
    }
    if (first == "salvage") {
        if (m_wrecks <= 0) {
            m_console->appendLine(tr("No wreck to salvage - win a battle first"), CommandConsole::Tone::Warning);
        } else if (m_battle->hostileCount() > 0) {
            m_console->appendLine(tr("Not under fire"), CommandConsole::Tone::Warning);
        } else {
            const QString gained = m_destiny.addResources({{"minerals", 2 * m_wrecks},
                                                           {"naquadah", m_wrecks},
                                                           {"trinium", 2 * m_wreckMotherships}});
            m_console->appendLine(tr("Wrecks salvaged: %1").arg(gained), CommandConsole::Tone::Success);
            if (m_wreckMotherships > 0) {
                m_battle->setMissileBay(m_battle->missileCapacity(), m_battle->missiles() + m_wreckMotherships);
                m_battle->setRailgunAmmo(m_battle->railgunAmmo() + 60 * m_wreckMotherships);
                if (QRandomGenerator::global()->generateDouble() < 0.15 * m_wreckMotherships) {
                    m_interior.learnBlueprint(m_interior.randomUnknownBlueprint());
                    flushInteriorMessages();
                }
            }
            m_life.addStat("salvaged", m_wrecks);
            m_wrecks = m_wreckMotherships = 0;
            announceAchievements();
        }
        return true;
    }
    if (first == "observe") {
        if (words.value(1).toLower() == "off") {
            m_battle->setObservation(false);
        } else if (!m_interior.hasCapability("observation")) {
            m_console->appendLine(tr("The observation deck is not usable"), CommandConsole::Tone::Warning);
        } else {
            m_battle->setObservation(true);
            if (m_battle->observing())
                m_console->appendLine(tr("The crew gathers on the observation deck - morale rises while you watch"),
                                      CommandConsole::Tone::Success);
        }
        return true;
    }
    if (first == "stats") {
        const QMap<QString, double> &s = m_life.stats();
        m_console->appendTable({tr("Journey"), tr("Total")},
                               {{tr("FTL jumps"), QString::number(s.value("jumps"))},
                                {tr("Planets explored"), QString::number(s.value("explored"))},
                                {tr("Battles"), QString::number(s.value("battles"))},
                                {tr("Ships destroyed"), QString::number(s.value("kills"))},
                                {tr("Motherships destroyed"), QString::number(s.value("motherships"))},
                                {tr("Best accuracy"), QStringLiteral("%1%").arg(s.value("bestAccuracy"))},
                                {tr("Fires put out"), QString::number(s.value("fires"))},
                                {tr("Ships captured"), QString::number(s.value("captures"))}});
        m_console->appendLine(tr("Universe seed %1 - day %2").arg(PlanetGenerator::universeSeed())
                                  .arg(int(m_route->state().hours) / 24 + 1));
        return true;
    }
    if (first == "achievements") {
        int count = 0;
        for (const CampaignLife::Achievement &a : CampaignLife::achievements()) {
            const bool done = m_life.unlocked(a.id);
            count += done ? 1 : 0;
            m_console->appendLine(QStringLiteral("%1 %2 - %3")
                                      .arg(done ? QStringLiteral("[x]") : QStringLiteral("[ ]"),
                                           QCoreApplication::translate("CampaignLife", a.title.toUtf8().constData()),
                                           QCoreApplication::translate("CampaignLife", a.description.toUtf8().constData())),
                                  done ? CommandConsole::Tone::Success : CommandConsole::Tone::Info);
        }
        m_console->appendLine(tr("%1/%2 achievements").arg(count).arg(CampaignLife::achievements().size()));
        return true;
    }
    if (first == "replay") {
        if (m_battle->lastReplay().isEmpty()) {
            m_console->appendLine(tr("No battle to replay yet"), CommandConsole::Tone::Warning);
            return true;
        }
        auto *dialog = findChild<ReplayDialog *>();
        if (!dialog)
            dialog = new ReplayDialog(this);
        dialog->setReplay(m_battle->lastReplay(), m_battle->lastReplayFaction());
        dialog->show();
        dialog->raise();
        return true;
    }
    if (first == "mods") {
        const ModLoader &mods = ModLoader::instance();
        m_console->appendLine(tr("Mods folder: %1").arg(mods.folder()));
        if (mods.loaded().isEmpty())
            m_console->appendLine(tr("No mod loaded - put JSON files there (see rcs/mods/example_mod.json)"));
        for (const QString &name : mods.loaded())
            m_console->appendLine(name, CommandConsole::Tone::Success);
        for (const QString &error : mods.errors())
            m_console->appendLine(error, CommandConsole::Tone::Warning);
        return true;
    }
    if (first == "seed") {
        m_console->appendLine(tr("Universe seed: %1 (0 = classic). Set \"seed\" in config.json for the next new journey.")
                                  .arg(PlanetGenerator::universeSeed()));
        return true;
    }
    return false;
}

void GameMenu::showChoice()
{
    const CampaignLife::Choice *c = m_life.pendingChoice();
    if (!c)
        return;
    m_console->appendLine(tr("DECISION - %1: %2").arg(c->title, c->text), CommandConsole::Tone::Warning);
    for (int i = 0; i < c->options.size(); ++i)
        m_console->appendLine(QStringLiteral("   %1. %2").arg(i + 1).arg(c->options[i]), CommandConsole::Tone::Warning);
    m_console->appendLine(tr("Type 'choose <n>' (90 s, then the crew decides alone)"));
    if (m_sound)
        m_sound->play(SoundCue::Notice, {}, 0.0f);
    m_choiceTimer = 90.0;
}

// Consequences of an event on the ship and its crew
void GameMenu::applyOutcome(const Outcome &o)
{
    const CommandConsole::Tone tone = o.tone == 1 ? CommandConsole::Tone::Success
                                    : o.tone == 2 ? CommandConsole::Tone::Warning
                                    : o.tone == 3 ? CommandConsole::Tone::Critical
                                                  : CommandConsole::Tone::Info;
    m_console->appendLine(o.text, tone);
    QStringList effects;
    if (!o.resources.isEmpty()) {
        QList<std::pair<QString, int>> gains;
        QList<RepairCost> costs;
        for (const auto &[name, quantity] : o.resources)
            if (quantity > 0)
                gains.append({name, quantity});
            else
                costs.append({0, name, -quantity});
        const QString gained = m_destiny.addResources(gains);
        if (!gained.isEmpty())
            effects << gained;
        QString missing;
        if (!costs.isEmpty() && m_destiny.consume(costs, &missing))
            for (const RepairCost &cost : costs)
                effects << QStringLiteral("-%1 %2").arg(cost.quantity).arg(DbText::tr(cost.resourceName));
    }
    if (o.crew != 0) {
        m_route->changeCrew(o.crew);
        effects << tr("crew %1%2").arg(o.crew > 0 ? "+" : "").arg(o.crew);
    }
    if (o.morale != 0.0f) {
        m_interior.changeMorale(o.morale);
        effects << tr("morale %1%2").arg(o.morale > 0 ? "+" : "").arg(qRound(o.morale));
    }
    if (o.power != 0.0) {
        m_route->addPower(o.power);
        effects << tr("power %1%2%").arg(o.power > 0 ? "+" : "").arg(qRound(o.power * 100));
    }
    if (o.ftl != 0.0) {
        m_route->addFtlCharge(o.ftl);
        effects << tr("FTL charge %1%2%").arg(o.ftl > 0 ? "+" : "").arg(qRound(o.ftl * 100));
    }
    if (o.hullDamage > 0.0f) {
        m_battle->restoreDefenses(std::max(5.0f, m_battle->hull() - o.hullDamage), m_battle->shieldStrength(),
                                  m_battle->shieldActive());
        effects << tr("hull -%1%").arg(qRound(o.hullDamage));
    }
    if (o.injuries > 0)
        effects << tr("%1 crew member(s) out of duty").arg(m_interior.injureRandom(o.injuries, 150.0f));
    if (o.fire) {
        const QList<Interior::Compartment> &rooms = m_interior.compartments();
        m_interior.startFire(QRandomGenerator::global()->bounded(int(rooms.size())), 0.4f);
    }
    if (o.blueprint)
        m_interior.learnBlueprint(m_interior.randomUnknownBlueprint());
    if (o.boarders > 0)
        m_interior.board(o.boarders);
    if (o.missiles != 0)
        m_battle->setMissileBay(m_battle->missileCapacity(), m_battle->missiles() + o.missiles);
    if (o.drones != 0)
        m_battle->setDrones(m_battle->drones() + o.drones);
    if (o.kinos != 0)
        m_kinos = std::max(0, m_kinos + o.kinos);
    if (o.reveal == QLatin1String("heal")) {
        m_interior.heal(180.0f);                // a doctor from Earth helps TJ
    } else if (!o.reveal.isEmpty()) {
        m_console->appendLine(o.reveal, CommandConsole::Tone::Info);
    }
    if (!o.hostiles.isEmpty())
        m_battle->spawnWave(true, o.hostiles == "*" ? QString() : o.hostiles);
    if (!effects.isEmpty())
        m_console->appendLine(tr("Effects: %1").arg(effects.join(", ")), tone);
    flushInteriorMessages();
    announceAchievements();
    updateNavigation();
}

void GameMenu::announceAchievements()
{
    for (const QString &id : m_life.takeUnlocked())
        for (const CampaignLife::Achievement &a : CampaignLife::achievements())
            if (a.id == id) {
                if (m_sound)
                    m_sound->play(SoundCue::Notice, {}, 0.0f);
                m_console->appendLine(tr("ACHIEVEMENT UNLOCKED: %1 - %2")
                                          .arg(QCoreApplication::translate("CampaignLife", a.title.toUtf8().constData()),
                                               QCoreApplication::translate("CampaignLife", a.description.toUtf8().constData())),
                                      CommandConsole::Tone::Success);
            }
}

void GameMenu::onBattleEnded(const BattleStats &stats)
{
    m_life.addStat("battles");
    m_life.addStat("kills", stats.kills);
    m_life.addStat("motherships", stats.motherships);
    if (stats.shots >= 10)
        m_life.setStatMax("bestAccuracy", qRound(100.0 * stats.hits / stats.shots));
    if (stats.kills > 0 && stats.hullLost <= 0.0f)
        m_life.setStatMax("flawless", 1);
    m_interior.changeMorale(3.0f);
    m_quests->notify("BATTLE");
    // Wrecks drift for a while
    m_wrecks += stats.kills;
    m_wreckMotherships += stats.motherships;
    m_wreckTimer = 120.0;
    if (m_wrecks > 0)
        m_console->appendLine(tr("%1 wreck(s) drifting nearby - 'salvage' before they scatter").arg(m_wrecks));
    announceAchievements();
}

void GameMenu::endCampaign(bool victory, const QString &heading, const QString &subtitle)
{
    if (m_ended)
        return;
    m_ended = true;
    const QMap<QString, double> &s = m_life.stats();
    QStringList summary{
        tr("Days aboard: %1").arg(int(m_route->state().hours) / 24 + 1),
        tr("FTL jumps: %1 - planets explored: %2").arg(s.value("jumps")).arg(s.value("explored")),
        tr("Ships destroyed: %1 (%2 motherships)").arg(s.value("kills")).arg(s.value("motherships")),
        tr("Crew: %1 - morale %2%").arg(m_route->state().crew).arg(qRound(m_interior.morale())),
        tr("Universe seed: %1").arg(PlanetGenerator::universeSeed()),
    };
    if (!m_endScreen) {
        m_endScreen = new ConquestEndScreen(this);
        connect(m_endScreen, &ConquestEndScreen::titleRequested, this, &GameMenu::quitToTitleRequested);
        connect(m_endScreen, &ConquestEndScreen::quitRequested, this, &GameMenu::quitGameRequested);
    }
    setPaused(true);
    m_endScreen->showCustom(victory, heading, subtitle, summary);
}

// Detailed help: 'help <command>'
void GameMenu::showHelpTopic(const QString &topic)
{
    static const QHash<QString, QStringList> topics{
        {"repair", {QT_TRANSLATE_NOOP("GameMenuHelp", "repair <system|hull|room>: the crew starts a repair job (resources paid at once, work takes time)."),
                    QT_TRANSLATE_NOOP("GameMenuHelp", "Systems: engines, shields, weapons, lifesupport, sensors. Rooms: see 'plan' (fire, breach, leak first)."),
                    QT_TRANSLATE_NOOP("GameMenuHelp", "Jobs run in parallel only with free hands: 'jobs', 'cancel <n>', 'crew rotate'.")}},
        {"target", {QT_TRANSLATE_NOOP("GameMenuHelp", "target <n> <weapons|shields|engines|hangar|hull>: aim the main weapon, missiles and drones at a subsystem."),
                    QT_TRANSLATE_NOOP("GameMenuHelp", "Harder to hit (x0.8) but it knocks it out: no fire, no shield, slow ship, no launches.")}},
        {"seal", {QT_TRANSLATE_NOOP("GameMenuHelp", "seal <room> / unseal <room>: close the doors (fires and air do not pass)."),
                  QT_TRANSLATE_NOOP("GameMenuHelp", "vent <room>: open it to space - the fire dies, people and boarders inside suffer.")}},
        {"crew", {QT_TRANSLATE_NOOP("GameMenuHelp", "crew: list of the crew (bonus, fatigue, job). crew rest <name>, crew rotate (tired ones rest)."),
                  QT_TRANSLATE_NOOP("GameMenuHelp", "Injured people heal in the infirmary; key characters lose their bonus meanwhile.")}},
        {"ftl", {QT_TRANSLATE_NOOP("GameMenuHelp", "engine ftl: jump when the FTL drive is charged. Under fire it spools 10 s (7 s with Rush)."),
                 QT_TRANSLATE_NOOP("GameMenuHelp", "Fighters outside dock first; those too far are left behind. ftl abort cancels.")}},
        {"energy", {QT_TRANSLATE_NOOP("GameMenuHelp", "power: report. turrets on|off, railguns on|off, evade on|off, power shields|weapons|balanced."),
                    QT_TRANSLATE_NOOP("GameMenuHelp", "Drones need free power. Leaks lower the power; if it runs short the lowest priorities are shed.")}},
        {"explore", {QT_TRANSLATE_NOOP("GameMenuHelp", "scansector: planets of the system. kino <address>: scout first. stargate <address> or jumper <address>."),
                     QT_TRANSLATE_NOOP("GameMenuHelp", "explore: collect (minerals = slugs, naquadah = missiles, Ancient sites = drones/blueprints).")}},
    };
    // A category: every order of it
    for (const HelpCategory &category : helpCatalog())
        if (topic == category.key || tr(category.title).compare(topic, Qt::CaseInsensitive) == 0) {
            m_console->appendHeading(tr(category.title));
            for (const HelpEntry &e : category.entries)
                m_console->appendEntry(QString::fromLatin1(e.syntax), tr(e.description));
            return;
        }
    const QString key = topic.toLower() == "power" ? QStringLiteral("energy") : topic.toLower();
    const auto it = topics.constFind(key);
    // A command: its line(s) of the catalog, then the details if any
    bool found = false;
    for (const HelpCategory &category : helpCatalog())
        for (const HelpEntry &e : category.entries)
            if (QString::fromLatin1(e.syntax).section(' ', 0, 0) == key) {
                if (!found)
                    m_console->appendHeading(key);
                found = true;
                m_console->appendEntry(QString::fromLatin1(e.syntax), tr(e.description));
            }
    if (found && it != topics.constEnd())
        for (const QString &line : *it)
            m_console->appendLine(QCoreApplication::translate("GameMenuHelp", line.toUtf8().constData()));
    if (found)
        return;
    if (it == topics.constEnd()) {
        m_console->appendLine(tr("No detailed help for '%1' - topics: %2").arg(topic, QStringList(topics.keys()).join(", ")),
                              CommandConsole::Tone::Warning);
        return;
    }
    for (const QString &line : *it)
        m_console->appendLine(QCoreApplication::translate("GameMenuHelp", line.toUtf8().constData()));
}

// Closest console-only command to a mistyped word ("" if none is close)
QString GameMenu::suggestLocal(const QString &word) const
{
    static const QStringList names{"help", "clear", "save", "quests", "hint", "library", "sound", "music", "menu",
                                   "launch", "turrets", "railguns", "power", "target", "missiles", "drones", "evade",
                                   "ftl", "star", "plan", "crew", "jobs", "cancel", "seal", "unseal", "vent", "unlock",
                                   "install", "blueprints", "defend", "board", "choose", "stones", "kino", "jumper",
                                   "salvage", "observe", "stats", "achievements", "seed"};
    auto distance = [](const QString &a, const QString &b) {
        QList<int> previous(b.size() + 1), current(b.size() + 1);
        for (int j = 0; j <= b.size(); ++j)
            previous[j] = j;
        for (int i = 1; i <= a.size(); ++i) {
            current[0] = i;
            for (int j = 1; j <= b.size(); ++j)
                current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
                                       previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            std::swap(previous, current);
        }
        return previous[b.size()];
    };
    QString best;
    int bestDistance = 3;
    for (const QString &name : names) {
        const int d = distance(word.toLower(), name);
        if (d < bestDistance) {
            bestDistance = d;
            best = name;
        }
    }
    return best;
}

// ================================================================
//  Co-op: a co-pilot at the tactical or engineering station
// ================================================================

bool GameMenu::coopCommand(const QString &first, const QStringList &words)
{
    if (first != "coop")
        return false;
    const QString sub = words.value(1).toLower();
    if (sub == "stop" || sub == "off") {
        if (m_coop)
            m_coop->stop();
        m_console->appendLine(tr("Co-op closed"));
        return true;
    }
    if (sub == "host" || sub == "open") {
        CoopProtocol::Role role = CoopProtocol::Role::Engineering;
        if (!words.value(2).isEmpty() && !CoopProtocol::roleFromName(words.value(2), &role)) {
            m_console->appendLine(tr("Usage: coop host [tactical|engineering] [port]"), CommandConsole::Tone::Warning);
            return true;
        }
        const quint16 port = quint16(words.value(3).toUInt() > 0 ? words.value(3).toUInt() : CoopProtocol::kDefaultPort);
        if (!m_coop) {
            m_coop = new CoopHost(this);
            connect(m_coop, &CoopHost::guestJoined, this, [this](const QString &address) {
                m_console->appendLine(tr("Co-pilot aboard (%1) at the %2 station").arg(address)
                                          .arg(CoopProtocol::roleName(m_coop->guestRole())),
                                      CommandConsole::Tone::Success);
                m_coop->sendStatus(coopStatus());
            });
            connect(m_coop, &CoopHost::guestLeft, this, [this] {
                m_console->appendLine(tr("The co-pilot left the ship"), CommandConsole::Tone::Warning);
            });
            connect(m_coop, &CoopHost::guestCommand, this, [this](const QString &text) {
                m_console->appendLine(tr("[co-pilot] %1").arg(text), CommandConsole::Tone::Info);
                onCommandEntered(text);
            });
            connect(m_coop, &CoopHost::guestDenied, this, [this](const QString &text) {
                m_console->appendLine(tr("[co-pilot] '%1' refused: not their station").arg(text), CommandConsole::Tone::Warning);
            });
            // Everything written in the console goes to the co-pilot
            connect(m_console, &CommandConsole::lineAppended, this, [this](const QString &text, CommandConsole::Tone tone) {
                if (m_coop && m_coop->hasGuest())
                    m_coop->sendLog(text, int(tone));
            });
        }
        QString error;
        if (!m_coop->listen(port, role, &error)) {
            m_console->appendLine(tr("Co-op impossible: %1").arg(error), CommandConsole::Tone::Critical);
            return true;
        }
        m_console->appendLine(tr("Co-op open on port %1 - the co-pilot joins from the title screen with: %2")
                                  .arg(m_coop->port())
                                  .arg(CoopHost::localAddresses().join(", ")),
                              CommandConsole::Tone::Success);
        m_console->appendLine(tr("Co-pilot station: %1 (%2)").arg(CoopProtocol::roleName(role))
                                  .arg(CoopProtocol::commandsOf(role).join(", ")));
        return true;
    }
    m_console->appendLine(m_coop && m_coop->isListening()
                              ? tr("Co-op open on port %1 - %2").arg(m_coop->port())
                                    .arg(m_coop->hasGuest() ? tr("co-pilot aboard") : tr("waiting for a co-pilot"))
                              : tr("Co-op: coop host [tactical|engineering] [port], coop stop"));
    return true;
}

// Ship status sent to the co-pilot every second
QStringList GameMenu::coopStatus() const
{
    QStringList lines;
    lines << tr("HULL %1%   SHIELD %2%%3").arg(qRound(m_battle->hull())).arg(qRound(m_battle->shieldStrength()))
                 .arg(m_battle->shieldActive() ? QString() : tr(" (off)"));
    QStringList systems;
    for (const QString &key : BattleView::systemKeys())
        systems << QStringLiteral("%1 %2%").arg(key).arg(qRound(m_battle->systemHealth(key)));
    lines << systems.join("  ");
    lines << m_battle->powerReport();
    const WeaponReadiness w = m_battle->weaponReadiness();
    lines << tr("HOSTILES %1   KILLS %2   TARGET %3").arg(w.hostiles).arg(w.kills)
                 .arg(w.hasTarget ? QStringLiteral("T%1 %2 (%3%)").arg(w.targetIndex + 1).arg(w.targetClass)
                                        .arg(qRound(w.targetHealth))
                                  : tr("none"));
    lines << tr("MAIN %1   PULSE %2   RAILGUNS %3 rounds, heat %4%   MISSILES %5   DRONES %6")
                 .arg(w.mainCooldown > 0 ? QStringLiteral("%1 s").arg(w.mainCooldown, 0, 'f', 1) : tr("ready"))
                 .arg(w.pulseCooldown > 0 ? QStringLiteral("%1 s").arg(w.pulseCooldown, 0, 'f', 1) : tr("ready"))
                 .arg(w.railgunAmmo).arg(qRound(w.railgunHeat)).arg(w.missiles).arg(w.drones);
    int fires = 0, breaches = 0;
    for (const Interior::Compartment &c : m_interior.compartments()) {
        fires += c.fire > 0.0f ? 1 : 0;
        breaches += c.breach ? 1 : 0;
    }
    lines << tr("CREW %1   MORALE %2%   FREE HANDS %3   JOBS %4   FIRES %5   BREACHES %6   BOARDERS %7")
                 .arg(m_route->state().crew).arg(qRound(m_interior.morale())).arg(m_interior.freeWorkers())
                 .arg(m_interior.jobs().size()).arg(fires).arg(breaches).arg(m_interior.boarders());
    for (const Interior::Compartment &c : m_interior.compartments())
        if (c.fire > 0.0f || c.breach || c.leak || c.boarders > 0)
            lines << QStringLiteral("  %1 (%2):%3%4%5%6").arg(QCoreApplication::translate("Interior", c.name.toUtf8().constData()), c.id)
                         .arg(c.fire > 0.0f ? tr(" FIRE") : QString(), c.breach ? tr(" BREACH") : QString(),
                              c.leak ? tr(" LEAK") : QString(),
                              c.boarders > 0 ? tr(" %1 BOARDERS").arg(c.boarders) : QString());
    return lines;
}

// Values the next word can take, after the words already typed
QStringList GameMenu::argumentChoices(const QStringList &before) const
{
    const QString command = before.value(0).toLower();
    const int position = int(before.size());          // index of the word being typed
    if (ShipCrew::commands().contains(command))
        return m_crew->argumentChoices(before);
    QStringList rooms;
    for (const Interior::Compartment &c : m_interior.compartments())
        rooms << c.id;
    QStringList addresses;
    for (const Planet &p : m_route->currentPlanets())
        addresses << p.gateAddress();
    const QStringList systems{"engines", "shields", "weapons", "lifesupport", "sensors"};
    const QStringList onOff{"on", "off"};

    if (position == 1) {
        if (command == "repair")
            return systems + QStringList{"hull"} + rooms;
        if (command == "seal" || command == "unseal" || command == "vent" || command == "defend" || command == "plan")
            return rooms;
        if (command == "unlock") {
            QStringList closed;
            for (const Interior::Compartment &c : m_interior.compartments())
                if (c.locked)
                    closed << c.id;
            return closed;
        }
        if (command == "install")
            return m_interior.blueprints();
        if (command == "crew")
            return {"rest", "rotate"};
        if (command == "kino")
            return addresses + QStringList{"build"};
        if (command == "jumper")
            return addresses;
        if (command == "coop")
            return {"host", "stop"};
        if (command == "help")
            return {"combat", "energy", "ship", "crew", "explore", "journey", "game", "repair", "target", "seal", "ftl"};
        if (command == "power")
            return {"shields", "weapons", "balanced"};
        if (command == "turrets" || command == "railguns" || command == "evade" || command == "observe"
            || command == "sound" || command == "music")
            return onOff;
        if (command == "ftl")
            return {"abort"};
        if (command == "auto")
            return {"main", "pulse", "missiles", "drones", "towers", "railguns", "all"};
        if (command == "cancel") {
            QStringList ids;
            for (const Interior::Job &j : m_interior.jobs())
                ids << QString::number(j.id);
            return ids;
        }
        if (command == "target" || command == "aim" || command == "board" || command == "missiles" || command == "drones") {
            QStringList numbers;
            for (int i = 1; i <= m_battle->hostileCount(); ++i)
                numbers << QString::number(i);
            return numbers;
        }
        if (command == "choose" && m_life.pendingChoice()) {
            QStringList numbers;
            for (int i = 1; i <= m_life.pendingChoice()->options.size(); ++i)
                numbers << QString::number(i);
            return numbers;
        }
    }
    if (position == 2) {
        if (command == "target" || command == "aim")
            return BattleView::enemySystems() + QStringList{"hull"};
        if (command == "install")
            return rooms;
        if (command == "crew" && before.value(1).toLower() == "rest") {
            QStringList names;
            for (const Interior::Person &p : m_interior.people())
                if (!p.generic)
                    names << p.id;
            return names;
        }
        if (command == "coop" && before.value(1).toLower() == "host")
            return {"tactical", "engineering"};
        if (command == "auto")
            return {"on", "off"};
    }
    return {};
}

QStringList GameMenu::argumentCompletions(const QString &text) const
{
    if (!text.contains(' '))
        return {};
    // Words already complete, and the one being typed (may be empty)
    QStringList words = text.split(' ', Qt::SkipEmptyParts);
    const QString partial = text.endsWith(' ') ? QString() : words.takeLast();
    if (words.isEmpty())
        return {};
    const QString base = words.join(' ') + ' ';
    QStringList lines;
    for (const QString &choice : argumentChoices(words)) {
        bool match = choice.startsWith(partial, Qt::CaseInsensitive);
        // Rooms also answer to their name, translated or not ("infirm", "infirmerie")
        if (!match && !partial.isEmpty()) {
            const int room = m_interior.find(choice);
            if (room >= 0) {
                const QString name = m_interior.compartments()[room].name;
                const QString shown = QCoreApplication::translate("Interior", name.toUtf8().constData());
                match = QString(name).remove(' ').startsWith(partial, Qt::CaseInsensitive)
                     || QString(shown).remove(' ').startsWith(partial, Qt::CaseInsensitive);
            }
        }
        if (match)
            lines << base + choice;
    }
    return lines;
}

void GameMenu::dispatch(const QString &name, const QStringList &args)
{
    // Arguments were already validated by CommandManager (see commands.json):
    // target numbers are integers >= 1, allowed words are lower case.
    if (name == "attack") {
        m_battle->fireMainWeapon(args.value(0).toInt() - 1);
    } else if (name == "maintower") {
        m_battle->fireMainWeapon();
    } else if (name == "pulseweapon") {
        m_battle->firePulseVolley();
    } else if (name == "secondarytower") {
        m_battle->fireSecondaryBurst();
    } else if (name == "shield") {
        const QString state = args.value(0);
        m_battle->setShieldActive(state == "on" || state == "up" || state == "activate");
    } else if (name == "scan") {
        m_battle->lockTarget(args.value(0).toInt() - 1);
    } else if (name == "scansector") {
        logReport(systemReport());
        logReport(m_battle->sectorReport());
    } else if (name == "engine") {
        if (args.value(0) == "ftl")
            jumpFtl();
        else
            m_console->appendLine(tr("Sublight engines engaged"), CommandConsole::Tone::Info);
    } else if (name == "stargate") {
        dialGate(args.value(0));
    } else if (name == "explore") {
        explore();
    } else if (name == "destinystatus") {
        logReport(routeReport());
        logReport(m_battle->destinyReport());
        m_quests->notify("STATUS");
    } else if (name == "weaponstatus") {
        logReport(m_battle->weaponReport());
    } else if (name == "repair") {
        m_crew->handle(QStringLiteral("repair"), {QStringLiteral("repair"), args.value(0)});
    } else if (name == "showressource") {
        showResources();
    } else if (name == "closedoor") {
        m_console->appendLine(tr("Door %1 closed: success").arg(args.value(0).toUpper()),
                              CommandConsole::Tone::Success);
    } else {
        m_console->appendLine(tr("Order acknowledged: %1 (not simulated yet)").arg(name),
                              CommandConsole::Tone::Info);
    }
}


void GameMenu::showHelp()
{
    // Overview: the categories, each with a few of its orders
    m_console->appendHeading(tr("Help - type 'help <category>' or 'help <command>'"));
    for (const HelpCategory &category : helpCatalog()) {
        QStringList first;
        for (int i = 0; i < category.entries.size() && first.size() < 4; ++i) {
            const QString word = QString::fromLatin1(category.entries[i].syntax).section(' ', 0, 0);
            if (!first.contains(word))
                first << word;
        }
        m_console->appendEntry(QStringLiteral("help %1").arg(category.key),
                               tr("%1: %2...").arg(tr(category.title), first.join(", ")));
    }
    m_console->appendEntry(QStringLiteral("Tab"), tr("completes the command and its arguments (repair inf -> repair infirmary)"));
    m_console->appendEntry(QStringLiteral("Up / Down"), tr("previous commands"));
}

void GameMenu::openPauseMenu()
{
    if (m_pauseMenu->isVisible())
        return;
    setPaused(true);
    m_pauseMenu->open();
}

void GameMenu::openSettings()
{
    openPauseMenu();
    m_pauseMenu->openSettings();
}

void GameMenu::setPaused(bool paused)
{
    m_battle->setPaused(paused);
    if (paused) {
        m_simTimer->stop();
        m_autosaveTimer->stop();
        return;
    }
    m_pauseMenu->hide();
    m_simTimer->start(1000);
    if (m_sessionId >= 0)
        m_autosaveTimer->start(kAutosaveMs);
    m_console->focusInput();
}

// Keeps the MENU button in the top-right corner of the 3D view
bool GameMenu::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_battle && event->type() == QEvent::Resize)
        m_menuButton->move(m_battle->width() - m_menuButton->width() - 10, 10);
    return QWidget::eventFilter(watched, event);
}

// Battle music as soon as enemies are there, calm music after 10 s of peace
void GameMenu::updateMusic(bool immediate)
{
    MusicPlayer *music = m_sound ? m_sound->music() : nullptr;
    if (!music)
        return;
    if (m_battle->hostileCount() > 0) {
        m_calmSeconds = 0;
        // The battle turns bad: darker music
        music->setMood(m_battle->hull() < 35.0f ? MusicPlayer::Mood::Danger : MusicPlayer::Mood::Battle,
                       m_battle->waveFaction());
    } else if (immediate || ++m_calmSeconds >= 10 || music->mood() == MusicPlayer::Mood::Title) {
        music->setMood(MusicPlayer::Mood::Calm);
    }
}

void GameMenu::musicCommand(const QString &arg)
{
    MusicPlayer *music = m_sound ? m_sound->music() : nullptr;
    if (!music) {
        m_console->appendLine(tr("Music: no audio output available"), CommandConsole::Tone::Warning);
        return;
    }
    static int lastVolume = 40;
    if (arg == "off" || arg == "mute") {
        if (music->volume() > 0)
            lastVolume = music->volume();
        music->setVolume(0);
    } else if (arg == "on") {
        music->setVolume(music->volume() > 0 ? music->volume() : lastVolume);
    } else if (!arg.isEmpty()) {
        bool ok = false;
        const int volume = arg.toInt(&ok);
        if (!ok || volume < 0 || volume > 100) {
            m_console->appendLine(tr("Usage: music on | off | <0-100>"), CommandConsole::Tone::Warning);
            return;
        }
        music->setVolume(volume);
    }
    m_console->appendLine(music->status(),
                          music->hasMusic() ? CommandConsole::Tone::Info : CommandConsole::Tone::Warning);
}

void GameMenu::soundCommand(const QString &arg)
{
    if (!m_sound) {
        m_console->appendLine(tr("Sound: no audio output available"), CommandConsole::Tone::Warning);
        return;
    }
    static int lastVolume = 70;
    if (arg == "off" || arg == "mute") {
        if (m_sound->volume() > 0)
            lastVolume = m_sound->volume();
        m_sound->setVolume(0);
    } else if (arg == "on") {
        m_sound->setVolume(m_sound->volume() > 0 ? m_sound->volume() : lastVolume);
    } else if (!arg.isEmpty()) {
        bool ok = false;
        const int volume = arg.toInt(&ok);
        if (!ok || volume < 0 || volume > 100) {
            m_console->appendLine(tr("Usage: sound on | off | <0-100>"), CommandConsole::Tone::Warning);
            return;
        }
        m_sound->setVolume(volume);
    }
    m_console->appendLine(m_sound->volume() == 0 ? tr("Sound: off") : m_sound->status(),
                          m_sound->isActive() ? CommandConsole::Tone::Info : CommandConsole::Tone::Warning);
}

void GameMenu::showLibrary(const QString &ship)
{
    if (!m_libraryDialog) {
        m_libraryDialog = new ShipLibraryDialog(this);
        m_libraryDialog->setShips(m_library);
    }

    if (ship.isEmpty()) {
        QStringList names;
        for (const ShipClass &c : std::as_const(m_library))
            names << QStringLiteral("%1 (%2)").arg(c.name, c.faction);
        m_console->appendLine(tr("Ship library: %1").arg(names.join(", ")));
    } else {
        const int index = ShipLibraryDialog::find(m_library, ship);
        if (index < 0) {
            m_console->appendLine(tr("Unknown ship '%1' - type 'library' for the list").arg(ship),
                                  CommandConsole::Tone::Warning);
            return;
        }
        for (const QString &line : ShipLibraryDialog::describe(m_library[index]))
            m_console->appendLine(line);
        m_libraryDialog->showShip(ship);
    }
    m_libraryDialog->show();
    m_libraryDialog->raise();
}

void GameMenu::loadSave()
{
    if (!DatabaseManager::instance().isOpen() || !m_destiny.load(QStringLiteral("Commander"))) {
        m_console->appendLine(tr("No save database - progress will not be kept"), CommandConsole::Tone::Warning);
        return;
    }

    for (const QString &key : BattleView::systemKeys())
        m_battle->setSubsystemHealth(key, m_destiny.systemHealth(key));

    QList<WeaponProfile> weapons;
    for (const ComponentInfo &c : ComponentRepository::all())
        if (c.category == "WEAPON")
            weapons.append({c.name, c.munitionType, float(c.damage), float(c.penetration), float(c.reach)});
    m_battle->setWeaponProfiles(weapons);

    if (ShipLibraryRepository::migrate()) {
        const QList<ShipClass> library = ModLoader::instance().merged(ShipLibraryRepository::all());
        if (!library.isEmpty()) {
            m_library = library;
            m_battle->setShipClasses(m_library);
            m_battle->setHangar(QStringLiteral("Puddle Jumper"), 4);   // the Destiny's shuttle bay
            m_battle->setDroneBay(4);           // empty: drones come from Ancient caches
            m_battle->setDrones(0);
            m_battle->setMissileBay(Balance::flagshipMissiles(QString()), Balance::flagshipMissiles(QString()));
        }
    } else {
        m_console->appendLine(tr("Ship library unavailable - built-in enemies only"), CommandConsole::Tone::Warning);
    }

    connect(m_battle, &BattleView::subsystemChanged, this, [this](const QString &key, float health) {
        m_destiny.setSystemHealth(key, health);
    });
    connect(m_battle, &BattleView::enemyDestroyed, this, [this](bool mothership) {
        const QString salvage = m_destiny.collectSalvage(mothership);
        if (!salvage.isEmpty())
            m_console->appendLine(tr("Salvage recovered: %1").arg(salvage), CommandConsole::Tone::Success);
    });

    m_console->appendLine(tr("Ship data loaded: %1 components aboard").arg(m_destiny.components().size()),
                          CommandConsole::Tone::Success);
}


void GameMenu::showResources()
{
    if (!m_destiny.isLoaded()) {
        m_console->appendLine(tr("No save database"), CommandConsole::Tone::Warning);
        return;
    }
    for (const ResourceStock &r : m_destiny.stock())
        m_console->appendLine(QStringLiteral("%1 %2 (%3)")
                                  .arg(DbText::tr(r.name).leftJustified(14, ' '))
                                  .arg(r.quantity, 4)
                                  .arg(DbText::tr(r.rarity.toLower())));
}

// ================================================================
//  Exploration
// ================================================================

void GameMenu::onRouteNotice(const QString &text, DestinyRoute::Severity severity)
{
    CommandConsole::Tone tone = CommandConsole::Tone::Info;
    switch (severity) {
    case DestinyRoute::Severity::Good:     tone = CommandConsole::Tone::Success; break;
    case DestinyRoute::Severity::Neutral:  tone = CommandConsole::Tone::Info; break;
    case DestinyRoute::Severity::Warning:  tone = CommandConsole::Tone::Warning; break;
    case DestinyRoute::Severity::Critical: tone = CommandConsole::Tone::Critical; break;
    }
    m_console->appendLine(text, tone, CommandConsole::Channel::Journey);
}

void GameMenu::simulate()
{
    const double kHoursPerTick = ::kHoursPerTick * gameConfig().gameSpeed;
    m_route->tick(kHoursPerTick);
    updateMusic(false);
    updateInterior(gameConfig().gameSpeed);
    const double seconds = gameConfig().gameSpeed;
    if (m_coop && m_coop->hasGuest())
        m_coop->sendStatus(coopStatus());

    // A decision without answer: the crew decides alone
    if (m_life.pendingChoice()) {
        m_choiceTimer -= seconds;
        if (m_choiceTimer <= 0.0)
            applyOutcome(m_life.expire());
    }
    // Wrecks scatter after a while
    if (m_wrecks > 0 && (m_wreckTimer -= seconds) <= 0.0) {
        m_wrecks = m_wreckMotherships = 0;
        m_console->appendLine(tr("The wrecks drifted out of reach"));
    }
    // Systems wear out without combat: regular maintenance ('repair <system>')
    m_wearClock += seconds;
    if (m_wearClock >= 20.0) {
        m_wearClock = 0;
        const QStringList keys = BattleView::systemKeys();
        const QString key = keys[QRandomGenerator::global()->bounded(int(keys.size()))];
        const float before = m_battle->systemHealth(key);
        const float after = std::max(0.0f, before - 1.5f);
        m_battle->setSubsystemHealth(key, after);
        m_destiny.setSystemHealth(key, after);
        if (before >= 60.0f && after < 60.0f)
            m_console->appendLine(tr("%1 wearing out (%2%) - schedule maintenance: 'repair %1'").arg(key).arg(qRound(after)),
                                  CommandConsole::Tone::Warning);
    }
    // Watching the stars from the observation deck
    if (m_battle->observing())
        m_interior.changeMorale(0.08f * float(seconds));
    // Red alert lights: fires or boarders aboard
    bool alert = m_interior.boarders() > 0;
    for (const Interior::Compartment &c : m_interior.compartments())
        alert = alert || c.fire > 0.3f;
    m_battle->setRedAlert(alert);
    // Alarms when a new fire breaks out or boarders come aboard
    int fires = 0;
    for (const Interior::Compartment &c : m_interior.compartments())
        fires += c.fire > 0.0f ? 1 : 0;
    if (m_sound && fires > m_lastFires)
        m_sound->play(SoundCue::AlertFire, {}, 0.0f);
    if (m_sound && m_interior.boarders() > m_lastBoarders)
        m_sound->play(SoundCue::AlertBoarders, {}, 0.0f);
    m_lastFires = fires;
    m_lastBoarders = m_interior.boarders();
    // Statistics of the interior
    int opened = 0;
    for (const Interior::Compartment &c : m_interior.compartments())
        opened += !c.locked && c.unlockWork > 0.0f ? 1 : 0;
    m_life.setStatMax("unlocked", opened);
    announceAchievements();

    // Crew supplies
    if (m_destiny.isLoaded()) {
        // Camile Wray rations the stores
        const int eaters = m_interior.bonusActive("supplies") ? qRound(m_route->state().crew * 0.9) : m_route->state().crew;
        const bool fed = m_destiny.consumeSupplies(eaters, kHoursPerTick);
        if (!fed) {
            if (!m_starving) {
                m_starving = true;
                m_console->appendLine(tr("Supplies exhausted: water or food missing - crew in danger"),
                                      CommandConsole::Tone::Critical);
            }
            m_starvingHours += kHoursPerTick;
            if (m_starvingHours >= 12.0) {
                m_starvingHours = 0;
                m_route->changeCrew(-1);
                m_console->appendLine(tr("A crew member died of deprivation (%1 left)").arg(m_route->state().crew),
                                      CommandConsole::Tone::Critical);
            }
        } else if (m_starving) {
            m_starving = false;
            m_starvingHours = 0;
            m_console->appendLine(tr("Supplies restored"), CommandConsole::Tone::Success);
        }

        const int water = m_destiny.stockOf("water");
        const int food = m_destiny.stockOf("food");
        if (water < kLowWater && !m_lowWaterWarned)
            m_console->appendLine(tr("Water reserves low (%1) - find a planet with water").arg(water),
                                  CommandConsole::Tone::Warning);
        if (food < kLowFood && !m_lowFoodWarned)
            m_console->appendLine(tr("Food reserves low (%1) - find a planet with organics").arg(food),
                                  CommandConsole::Tone::Warning);
        m_lowWaterWarned = water < kLowWater;
        m_lowFoodWarned = food < kLowFood;
    }

    updateNavigation();
}

void GameMenu::updateNavigation()
{
    const RouteState &s = m_route->state();
    const RouteStop &stop = m_route->currentStop();

    QStringList lines;
    lines << tr("G%1 %2 - SYSTEM %3")
                 .arg(stop.galaxy, 2, 10, QChar('0'))
                 .arg(m_route->galaxyName(stop.galaxy).toUpper())
                 .arg(stop.sequence, 4, 10, QChar('0'));
    lines << tr("FTL %1   POWER %2").arg(percent(float(s.ftlCharge)), percent(float(s.power)));
    {
        const Planet shown = PlanetGenerator::generate(stop.galaxy, stop.sequence, std::max(0, s.teamPlanet));
        lines << tr("VIEW %1 \"%2\" %3").arg(shown.code, shown.nickname.toUpper(), shown.classificationName().toUpper());
    }

    // Real seconds left: 1 s = 15 game minutes at normal speed
    const int realSeconds = qRound(s.gateRemaining / ::kHoursPerTick / std::max(0.25f, gameConfig().gameSpeed));
    QString gate = s.gateOpen ? tr("GATE OPEN %1 h (%2:%3)").arg(s.gateRemaining, 0, 'f', 1)
                                    .arg(realSeconds / 60).arg(realSeconds % 60, 2, 10, QChar('0'))
                              : tr("GATE CLOSED");
    if (s.teamPlanet >= 0)
        gate += tr("  TEAM ON %1").arg(QChar('A' + s.teamPlanet));
    lines << gate;

    QString crew = tr("CREW %1").arg(s.crew);
    if (m_destiny.isLoaded())
        crew += tr("   WATER %1   FOOD %2").arg(m_destiny.stockOf("water")).arg(m_destiny.stockOf("food"));
    lines << crew;

    const int hours = int(s.hours);
    lines << tr("DAY %1  %2:%3").arg(hours / 24 + 1)
                 .arg(hours % 24, 2, 10, QChar('0'))
                 .arg(int((s.hours - hours) * 60), 2, 10, QChar('0'));

    if (gameConfig().ironman)
        lines << tr("IRONMAN");
    if (m_life.pendingChoice())
        lines << tr("DECISION WAITING - 'choose <n>'");
    m_battle->setNavigationLines(lines);
    updateMap();
}

void GameMenu::updateMap()
{
    const RouteState &s = m_route->state();
    const RouteStop &stop = m_route->currentStop();

    RouteMapData data;
    data.galaxyCode = QStringLiteral("G%1").arg(stop.galaxy, 2, 10, QChar('0'));
    data.galaxyName = m_route->galaxyName(stop.galaxy);
    data.stops = m_route->stopsInGalaxy(stop.galaxy);
    data.currentSequence = stop.sequence;
    data.gateOpen = s.gateOpen;
    data.gateRemaining = s.gateRemaining;
    data.hostiles = m_battle->hostileCount();

    for (const Planet &p : m_route->currentPlanets()) {
        RouteMapData::PlanetMark mark;
        mark.letter = QString(QChar('A' + p.variant));
        mark.address = p.gateAddress();
        mark.nickname = p.nickname;
        mark.classification = p.classificationName();
        mark.explored = m_route->hasCollected(p.variant);
        mark.team = s.teamPlanet == p.variant;
        // Planet colour: its ground, blended toward blue with its water
        const PlanetLook look = lookOf(p);
        const float w = std::clamp(p.conditions.water, 0.0f, 1.0f) * 0.6f;
        const QVector3D c = look.groundAlt * 1.6f * (1.0f - w) + QVector3D(0.25f, 0.45f, 0.85f) * w;
        mark.tint = QColor::fromRgbF(std::min(c.x(), 1.0f), std::min(c.y(), 1.0f), std::min(c.z(), 1.0f));
        data.planets << mark;
    }
    m_map->setData(data);
}

GameMenu::~GameMenu()
{
    saveGame(false);
}

void GameMenu::restoreGame()
{
    if (!m_destiny.isLoaded() || !SaveRepository::ensureSchema())
        return;
    m_sessionId = SaveRepository::autosaveSession(m_destiny.shipId());
    if (m_sessionId < 0) {
        m_console->appendLine(tr("Progress cannot be saved (no session)"), CommandConsole::Tone::Warning);
        return;
    }

    const std::optional<GameSave> save = SaveRepository::load(m_sessionId);
    if (!save) {
        // New journey: the universe of the config file (0 = the classic one)
        if (gameConfig().seed != 0) {
            m_route->setUniverseSeed(quint32(gameConfig().seed));
            m_route->restore(0, m_route->state(), {});
        }
        m_console->appendLine(tr("New journey - progress is saved automatically ('save' to force it)"));
        if (gameConfig().ironman)
            m_console->appendLine(tr("IRONMAN: a single save, lost with the ship"), CommandConsole::Tone::Critical);
        return;
    }

    // The universe of this journey, before the route is restored
    const quint32 seed = quint32(save->extra.value("seed").toInteger(0));
    if (seed != PlanetGenerator::universeSeed())
        m_route->setUniverseSeed(seed);
    RouteState state;
    state.ftlCharge = save->ftlCharge;
    state.power = save->power;
    state.crew = save->crew;
    state.hours = save->hours;
    state.gateOpen = save->gateOpen;
    state.gateRemaining = save->gateRemaining;
    state.gateTotal = save->gateTotal;
    state.teamPlanet = save->teamPlanet;
    m_route->restore(save->routeIndex, state, save->collected);
    m_battle->restoreDefenses(save->hull, save->shield, save->shieldOn);
    m_extra = save->extra;
    m_battle->setRailgunAmmo(m_extra.value("railgunAmmo").toInt(BattleView::kRailgunAmmoMax()));
    m_battle->setMissileBay(m_battle->missileCapacity(), m_extra.value("missiles").toInt(m_battle->missileCapacity()));
    m_battle->setDrones(m_extra.value("drones").toInt(0));
    m_starUsedAt = m_extra.value("starUsedAt").toInt(-1);
    m_interior.fromJson(m_extra.value("interior").toObject());
    m_life.fromJson(m_extra.value("life").toObject());
    m_kinos = m_extra.value("kinos").toInt(4);
    m_route->setTeamByShuttle(m_extra.value("teamByShuttle").toBool());
    for (const QJsonValue &v : m_extra.value("scouted").toArray())
        m_scouted << v.toInt();
    if (m_life.pendingChoice())
        QTimer::singleShot(0, this, &GameMenu::showChoice);
    m_interior.setCrewCount(save->crew);
    m_interior.takeMessages();

    const RouteStop &stop = m_route->currentStop();
    m_console->appendLine(tr("Game restored: day %1, G%2 %3 system %4, crew %5")
                              .arg(int(save->hours) / 24 + 1)
                              .arg(stop.galaxy, 2, 10, QChar('0'))
                              .arg(m_route->galaxyName(stop.galaxy))
                              .arg(stop.sequence, 4, 10, QChar('0'))
                              .arg(m_route->state().crew),
                          CommandConsole::Tone::Success);
}

bool GameMenu::saveGame(bool announce)
{
    if (m_sessionId < 0) {
        if (announce)
            m_console->appendLine(tr("No save database - cannot save"), CommandConsole::Tone::Warning);
        return false;
    }

    const RouteState &state = m_route->state();
    GameSave save;
    save.routeIndex = m_route->currentIndex();
    save.hours = state.hours;
    save.crew = state.crew;
    save.power = state.power;
    save.ftlCharge = state.ftlCharge;
    save.gateOpen = state.gateOpen;
    save.gateRemaining = state.gateRemaining;
    save.gateTotal = state.gateTotal;
    save.teamPlanet = state.teamPlanet;
    save.collected = m_route->collectedPlanets();
    save.hull = m_battle->hull();
    save.shield = m_battle->shieldStrength();
    save.shieldOn = m_battle->shieldActive();
    save.extra = m_extra;
    save.extra.insert("railgunAmmo", m_battle->railgunAmmo());
    save.extra.insert("missiles", m_battle->missiles());
    save.extra.insert("drones", m_battle->drones());
    save.extra.insert("starUsedAt", m_starUsedAt);
    save.extra.insert("interior", m_interior.toJson());
    save.extra.insert("life", m_life.toJson());
    save.extra.insert("kinos", m_kinos);
    save.extra.insert("seed", qint64(PlanetGenerator::universeSeed()));
    save.extra.insert("teamByShuttle", m_route->teamByShuttle());
    QJsonArray scouted;
    for (int v : m_scouted)
        scouted.append(v);
    save.extra.insert("scouted", scouted);

    const bool ok = SaveRepository::save(m_sessionId, save, m_route->galaxyName(m_route->currentStop().galaxy));
    if (announce)
        m_console->appendLine(ok ? tr("Game saved (day %1)").arg(int(state.hours) / 24 + 1) : tr("Save failed"),
                              ok ? CommandConsole::Tone::Success : CommandConsole::Tone::Critical);
    return ok;
}

void GameMenu::updatePlanetView()
{
    // The planet where the team is, otherwise the first of the system
    const RouteStop &stop = m_route->currentStop();
    const int variant = std::max(0, m_route->state().teamPlanet);
    m_battle->setPlanetLook(lookOf(PlanetGenerator::generate(stop.galaxy, stop.sequence, variant)));
}

void GameMenu::loadQuests()
{
    if (!m_destiny.isLoaded() || !QuestRepository::migrate())
        return;
    const int playerId = QuestRepository::playerIdOfShip(m_destiny.shipId());
    if (playerId < 0)
        return;

    m_quests->setClock([this] { return m_route->state().hours; });
    m_destiny.setGainListener([this](const QString &resource, int amount) {
        m_quests->notify("COLLECT", resource, amount);
    });

    connect(m_quests, &QuestSystem::questStarted, this,
            [this](const QString &title, const QString &description, bool firstQuest) {
        if (firstQuest) {
            // Tutorial: the emergency drop damaged the systems to repair
            for (const QString &key : {QStringLiteral("lifesupport"), QStringLiteral("engines")}) {
                if (m_battle->systemHealth(key) > 50.0f) {
                    m_battle->setSubsystemHealth(key, 35.0f);
                    m_destiny.setSystemHealth(key, 35.0f);
                }
            }
            m_console->appendLine(tr("Emergency drop: life support and sublight engines damaged"),
                                  CommandConsole::Tone::Critical);
        }
        m_console->appendLine(tr("New quest: %1 - %2").arg(title, description), CommandConsole::Tone::Warning);
        updateObjectives();
    });
    connect(m_quests, &QuestSystem::objectiveCompleted, this, [this](const QString &description) {
        m_console->appendLine(tr("Objective complete: %1").arg(description), CommandConsole::Tone::Success);
        updateObjectives();
    });
    connect(m_quests, &QuestSystem::questCompleted, this,
            [this](const QString &title, const QList<std::pair<QString, int>> &rewards) {
        m_console->appendLine(tr("Quest complete: %1").arg(title), CommandConsole::Tone::Success);
        const QString gained = m_destiny.addResources(rewards);
        if (!gained.isEmpty())
            m_console->appendLine(tr("Reward: %1").arg(gained), CommandConsole::Tone::Success);
        updateObjectives();
    });
    connect(m_quests, &QuestSystem::campaignCompleted, this, [this] {
        m_console->appendLine(tr("Tutorial campaign complete - the Destiny is yours to command"),
                              CommandConsole::Tone::Success);
        updateObjectives();
    });

    m_quests->load(playerId);
    updateObjectives();
    const QString hint = m_quests->hint();
    if (!hint.isEmpty() && m_quests->activeQuest())
        m_console->appendLine(tr("Hint: %1  ('quests' for the journal)").arg(hint));
}

void GameMenu::updateObjectives()
{
    m_battle->setObjectiveLines(m_quests->objectiveLines());
}

void GameMenu::recordCurrentSystem()
{
    if (m_destiny.isLoaded())
        WorldRepository::recordSystem(m_route->galaxyName(m_route->currentStop().galaxy),
                                      m_route->currentPlanets());
}

void GameMenu::jumpFtl()
{
    if (m_battle->jumpPending()) {
        m_console->appendLine(tr("The FTL drive is already spooling ('ftl abort' to cancel)"), CommandConsole::Tone::Warning);
        return;
    }
    QString reason;
    if (!m_route->canJump(&reason)) {
        m_console->appendLine(tr("FTL jump impossible: %1").arg(reason), CommandConsole::Tone::Warning);
        return;
    }
    // Under fire the drive needs time to spool; fighters outside must dock
    if (m_battle->hostileCount() > 0)
        m_battle->startJumpSequence(Balance::kFtlSpoolUnderFire * (m_interior.bonusActive("energy") ? 0.7f : 1.0f));
    else if (m_battle->weaponReadiness().fightersOut > 0)
        m_battle->startJumpSequence(Balance::kFtlSpoolDocking);
    else
        performJump();
}

void GameMenu::performJump()
{
    GameEvent event;
    QString reason;
    // The gate window may have changed during the countdown
    if (!m_route->canJump(&reason)) {
        m_console->appendLine(tr("FTL jump impossible: %1").arg(reason), CommandConsole::Tone::Warning);
        return;
    }
    saveGame(false);                    // autosave before every jump
    m_battle->dockForJump();
    if (!m_route->jump(&event, &reason)) {
        m_console->appendLine(tr("FTL jump impossible: %1").arg(reason), CommandConsole::Tone::Warning);
        return;
    }

    // Enemies are left behind; new contacts may show up later
    m_battle->clearBattlefield(float(25 + QRandomGenerator::global()->bounded(50)));
    m_console->appendLine(tr("FTL jump - the Destiny enters hyperspace"), CommandConsole::Tone::Info);
    m_battle->hyperspaceFlash();

    CommandConsole::Tone tone = CommandConsole::Tone::Info;
    switch (event.severity) {
    case GameEvent::Severity::Good:     tone = CommandConsole::Tone::Success; break;
    case GameEvent::Severity::Neutral:  tone = CommandConsole::Tone::Info; break;
    case GameEvent::Severity::Warning:  tone = CommandConsole::Tone::Warning; break;
    case GameEvent::Severity::Critical: tone = CommandConsole::Tone::Critical; break;
    }
    m_console->appendLine(tr("Transit: %1 - %2").arg(event.title, event.description), tone);

    QStringList effects;
    if (event.powerDelta != 0.0)
        effects << tr("power %1%2%").arg(event.powerDelta > 0 ? "+" : "").arg(qRound(event.powerDelta * 100));
    if (event.ftlDelta != 0.0)
        effects << tr("FTL charge %1%2%").arg(event.ftlDelta > 0 ? "+" : "").arg(qRound(event.ftlDelta * 100));
    if (event.crewDelta != 0)
        effects << tr("crew %1%2").arg(event.crewDelta > 0 ? "+" : "").arg(event.crewDelta);
    const QString gained = m_destiny.addResources({{"water", event.water}, {"food", event.food},
                                                   {"naquadah", event.naquadah}, {"minerals", event.minerals}});
    if (!gained.isEmpty())
        effects << gained;
    if (!effects.isEmpty())
        m_console->appendLine(tr("Effects: %1").arg(effects.join(", ")), tone);

    if (event.hostiles)
        m_battle->spawnWave(true);
    m_battle->relaunchAfterJump();      // fighters sent out by order take off again
    m_scouted.clear();
    m_wrecks = m_wreckMotherships = 0;
    m_life.addStat("jumps");
    if (gameConfig().ironman)
        m_life.addStat("ironmanJumps");
    announceAchievements();

    recordCurrentSystem();
    saveGame(false);
    updatePlanetView();
    const RouteStop &stop = m_route->currentStop();
    m_console->appendLine(tr("Dropped out of FTL: G%1 %2, system %3 - %4 planet(s), gate window %5 h")
                              .arg(stop.galaxy, 2, 10, QChar('0'))
                              .arg(m_route->galaxyName(stop.galaxy))
                              .arg(stop.sequence, 4, 10, QChar('0'))
                              .arg(stop.numVariants)
                              .arg(m_route->state().gateRemaining, 0, 'f', 1),
                          CommandConsole::Tone::Success);
    updateNavigation();
    // The end of the route: the Destiny reaches what it was sent for
    if (m_route->currentIndex() >= m_route->totalStops() - 1) {
        m_life.addStat("ending");
        announceAchievements();
        endCampaign(true, tr("DESTINY'S PURPOSE"),
                    tr("The Destiny reached the source of the signal at the edge of the universe"));
        return;
    }
    if (m_life.rollChoice())
        showChoice();
}

// The Destiny skims a star to refill its energy: the shields take the heat
void GameMenu::diveIntoStar()
{
    if (!m_route->hasUsableStar()) {
        m_console->appendLine(tr("The star of this system is not suitable - jump on and try further"),
                              CommandConsole::Tone::Warning);
        return;
    }
    if (m_starUsedAt == m_route->currentIndex()) {
        m_console->appendLine(tr("Already recharged in this star"), CommandConsole::Tone::Warning);
        return;
    }
    if (m_battle->hostileCount() > 0) {
        m_console->appendLine(tr("Not under fire - clear the sector first"), CommandConsole::Tone::Warning);
        return;
    }
    m_starUsedAt = m_route->currentIndex();
    m_life.addStat("stars");
    announceAchievements();
    const RouteState &s = m_route->state();
    const double gained = 1.0 - s.power;
    m_route->addPower(gained);
    m_route->addFtlCharge(0.5);
    // Shields absorb the heat; without them the hull burns
    const float shield = m_battle->shieldActive() ? m_battle->shieldStrength() : 0.0f;
    const float heat = 70.0f;
    const float hullDamage = std::max(0.0f, heat - shield) * 0.5f;
    m_battle->restoreDefenses(std::max(5.0f, m_battle->hull() - hullDamage), std::max(0.0f, shield - heat),
                              m_battle->shieldActive());
    m_console->appendLine(tr("Dive into the star: power %1%, FTL charge +50%").arg(qRound(m_route->state().power * 100)),
                          CommandConsole::Tone::Success);
    if (hullDamage > 0.0f)
        m_console->appendLine(tr("The shields could not hold the heat: hull -%1%").arg(qRound(hullDamage)),
                              CommandConsole::Tone::Critical);
    updateNavigation();
}

void GameMenu::dialGate(const QString &address)
{
    QString error;
    if (address.compare("destiny", Qt::CaseInsensitive) == 0) {
        if (m_route->recallTeam(&error)) {
            m_console->appendLine(tr("Gate dialed home - the team is back aboard the Destiny"),
                                  CommandConsole::Tone::Success);
            m_quests->notify("SURVIVE");
        } else
            m_console->appendLine(tr("Recall impossible: %1").arg(error), CommandConsole::Tone::Warning);
        updateNavigation();
        updatePlanetView();
        return;
    }

    const int variant = m_route->variantForAddress(address);
    if (variant < 0) {
        m_console->appendLine(tr("No gate answers at %1 - addresses of this system: 'scansector'")
                                  .arg(address.toUpper()),
                              CommandConsole::Tone::Warning);
        return;
    }
    if (!m_route->dial(variant, &error)) {
        m_console->appendLine(tr("Dialing failed: %1").arg(error), CommandConsole::Tone::Warning);
        return;
    }

    m_quests->notify("EXPLORE");
    const Planet p = PlanetGenerator::generate(m_route->currentStop().galaxy, m_route->currentStop().sequence, variant);
    m_console->appendLine(tr("Chevron 7 locked - team deployed on %1 \"%2\" (%3)")
                              .arg(p.code, p.nickname, p.classificationName()),
                          CommandConsole::Tone::Success);
    if (p.conditions.toxicity > 0.5f || p.conditions.radiation > 0.4f)
        m_console->appendLine(tr("Hazardous environment: toxicity %1, radiation %2")
                                  .arg(percent(p.conditions.toxicity), percent(p.conditions.radiation)),
                              CommandConsole::Tone::Warning);
    m_console->appendLine(tr("Type 'explore' to search for resources, 'stargate destiny' to come back"));
    updateNavigation();
    updatePlanetView();
}

void GameMenu::explore()
{
    const int variant = m_route->state().teamPlanet;
    if (variant < 0) {
        m_console->appendLine(tr("No team deployed - dial a planet first ('stargate <address>')"),
                              CommandConsole::Tone::Warning);
        return;
    }
    const Planet p = PlanetGenerator::generate(m_route->currentStop().galaxy, m_route->currentStop().sequence, variant);
    if (m_route->hasCollected(variant)) {
        m_console->appendLine(tr("%1 already explored - nothing more to find").arg(p.code),
                              CommandConsole::Tone::Warning);
        return;
    }

    m_route->markCollected(variant);
    m_life.addStat("explored");
    const QString result = m_destiny.isLoaded() ? m_destiny.collectFromPlanet(p) : tr("no save database");
    m_console->appendLine(tr("Exploration of %1: %2").arg(p.code, result), CommandConsole::Tone::Success);
    if (p.isAncientSite())
        m_console->appendLine(tr("Ancient site found: \"%1\"").arg(p.ancientName), CommandConsole::Tone::Success);

    // Supplies: slugs from minerals, missiles from naquadah, drones from Ancient caches
    QStringList supplies;
    if (p.conditions.minerals >= 0.4f && m_battle->railgunAmmo() < BattleView::kRailgunAmmoMax()) {
        const int before = m_battle->railgunAmmo();
        m_battle->setRailgunAmmo(before + qRound(500 * p.conditions.minerals));
        supplies << tr("%1 railgun slugs").arg(m_battle->railgunAmmo() - before);
    }
    if (p.conditions.energy >= 0.35f && m_battle->missiles() < m_battle->missileCapacity()) {
        const int before = m_battle->missiles();
        m_battle->setMissileBay(m_battle->missileCapacity(), before + 3);
        supplies << tr("%1 missile(s)").arg(m_battle->missiles() - before);
    }
    if (p.isAncientSite() && QRandomGenerator::global()->generateDouble() < 0.4) {
        const QString blueprint = m_interior.randomUnknownBlueprint();
        if (!blueprint.isEmpty())
            m_interior.learnBlueprint(blueprint);
        flushInteriorMessages();
    }
    if (p.isAncientSite() && m_battle->drones() < m_battle->droneCapacity()) {
        const int before = m_battle->drones();
        m_battle->setDrones(before + 1 + QRandomGenerator::global()->bounded(2));
        supplies << tr("%1 drone salvo(s) from an Ancient cache").arg(m_battle->drones() - before);
    }
    if (!supplies.isEmpty())
        m_console->appendLine(tr("Supplies loaded: %1").arg(supplies.join(", ")), CommandConsole::Tone::Success);

    // Hostile surfaces can cost a team member
    const float danger = p.conditions.toxicity + p.conditions.radiation;
    // A Kino scouted the way: half the risk
    const double risk = m_scouted.contains(variant) ? 0.17 : 0.35;
    if (danger > 0.8f && QRandomGenerator::global()->generateDouble() < risk) {
        m_route->changeCrew(-1);
        m_console->appendLine(tr("A team member was lost on the hostile surface (crew %1)").arg(m_route->state().crew),
                              CommandConsole::Tone::Critical);
    }
    updateNavigation();
}

QString GameMenu::systemReport() const
{
    const RouteState &s = m_route->state();
    const RouteStop &stop = m_route->currentStop();
    QString report = tr("System G%1-%2 (%3) - gate window %4")
                         .arg(stop.galaxy, 2, 10, QChar('0'))
                         .arg(stop.sequence, 4, 10, QChar('0'))
                         .arg(m_route->galaxyName(stop.galaxy))
                         .arg(s.gateOpen ? tr("open %1 h").arg(s.gateRemaining, 0, 'f', 1) : tr("closed"));

    for (const Planet &p : m_route->currentPlanets()) {
        QString line = tr("\n%1 \"%2\" %3 - address %4").arg(p.code, p.nickname, p.classificationName(), p.gateAddress());
        if (s.teamPlanet == p.variant)
            line += tr(" [TEAM]");
        if (m_route->hasCollected(p.variant))
            line += tr(" [explored]");
        const PlanetConditions &c = p.conditions;
        line += tr("\n   water %1  O2 %2  minerals %3  energy %4  organics %5  | toxicity %6  radiation %7%8")
                    .arg(percent(c.water), percent(c.oxygen), percent(c.minerals), percent(c.energy),
                         percent(c.organics), percent(c.toxicity), percent(c.radiation))
                    .arg(c.hasLife ? tr("  | life signs") : QString());
        report += line;
    }
    return report;
}

QString GameMenu::routeReport() const
{
    const RouteState &s = m_route->state();
    return tr("Route stop %1/%2 | Crew %3 | Power %4 | FTL %5 | Planets discovered: %6")
        .arg(m_route->currentIndex() + 1)
        .arg(m_route->totalStops())
        .arg(s.crew)
        .arg(percent(float(s.power)))
        .arg(percent(float(s.ftlCharge)))
        .arg(m_destiny.isLoaded() ? WorldRepository::discoveredPlanetCount() : 0);
}

void GameMenu::logReport(const QString &report)
{
    for (const QString &line : report.split('\n'))
        m_console->appendLine(line);
}
