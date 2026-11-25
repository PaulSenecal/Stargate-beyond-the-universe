#include "gamemenu.h"

#include <QEvent>
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
#include "shipplandialog.h"
#include "shipstatuswidget.h"
#include "src/Menu/pause/pausemenu.h"
#include "src/audio/musicplayer.h"
#include "src/audio/soundsystem.h"
#include "weaponstatuswidget.h"
#include "src/core/commandmanager.h"
#include "src/core/gameconfig.h"
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
              << "blueprints" << "defend" << "board";
        words.removeDuplicates();
        words.sort();
        m_console->setCompletions(words);
        m_console->setCompletionProvider([this](const QString &text) {
            QStringList addresses{"destiny"};
            for (const Planet &p : m_route->currentPlanets())
                addresses << p.gateAddress();
            QStringList lines = m_commands->completions(text, {{"stargate", addresses}});
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
                                         QStringLiteral("board"), QStringLiteral("cancel")})
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
    m_console->appendLine(text, tone);
}

void GameMenu::onCommandEntered(const QString &command)
{
    m_console->appendLine(command, CommandConsole::Tone::Echo);

    const QString first = command.section(' ', 0, 0, QString::SectionSkipEmpty).toLower();
    if (first == "help" || first == "?") {
        showHelp();
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
        else
            m_battle->aimAt(target - 1, words.value(2));
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
    return interiorCommand(first, words);
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

// Orders for the inside of the ship and its crew
bool GameMenu::interiorCommand(const QString &first, const QStringList &words)
{
    const QString arg = words.mid(1).join(' ');
    QString error;
    auto failed = [this, &error] {
        m_console->appendLine(error, CommandConsole::Tone::Warning);
    };
    if (first == "plan" || first == "ship") {
        showPlan();
        if (!arg.isEmpty()) {
            const int room = m_interior.find(arg);
            for (const QString &line : ShipPlanDialog::describe(m_interior, room))
                m_console->appendLine(line);
        }
        return true;
    }
    if (first == "crew") {
        const QString sub = words.value(1).toLower();
        if (sub == "rest") {
            if (m_interior.rest(words.mid(2).join(' '), &error))
                m_console->appendLine(tr("%1 goes to rest").arg(words.mid(2).join(' ')), CommandConsole::Tone::Info);
            else
                failed();
        } else if (sub == "rotate") {
            m_console->appendLine(tr("Rotation: %1 tired crew member(s) sent to rest").arg(m_interior.rotate()),
                                  CommandConsole::Tone::Info);
        } else {
            showCrew();
        }
        return true;
    }
    if (first == "jobs") {
        if (m_interior.jobs().isEmpty())
            m_console->appendLine(tr("No repair job under way"));
        for (const Interior::Job &j : m_interior.jobs()) {
            QStringList names;
            for (int w : j.workers)
                names << m_interior.people()[w].name;
            m_console->appendLine(tr("#%1 %2 %3 - %4% - %5")
                                      .arg(j.id)
                                      .arg(j.kind, j.target)
                                      .arg(qRound(100.0f * (1.0f - j.work / std::max(0.01f, j.total))))
                                      .arg(names.isEmpty() ? tr("waiting for free hands") : names.join(", ")));
        }
        return true;
    }
    if (first == "cancel") {
        if (!m_interior.cancelJob(arg.toInt()))
            m_console->appendLine(tr("No job #%1 ('jobs' for the list)").arg(arg), CommandConsole::Tone::Warning);
        return true;
    }
    if (first == "seal" || first == "unseal") {
        if (!m_interior.setSealed(arg, first == "seal", &error))
            failed();
        flushInteriorMessages();
        return true;
    }
    if (first == "vent") {
        if (!m_interior.vent(arg, &error))
            failed();
        flushInteriorMessages();
        return true;
    }
    if (first == "unlock") {
        const int room = m_interior.find(arg);
        if (room < 0 || !m_interior.compartments()[room].locked) {
            m_interior.orderUnlock(arg, &error);
            failed();
            return true;
        }
        if (!payFor(m_interior.compartments()[room].unlockCost))
            return true;
        if (m_interior.orderUnlock(arg, &error) < 0)
            failed();
        else
            m_console->appendLine(tr("Teams sent to open %1").arg(arg), CommandConsole::Tone::Info);
        return true;
    }
    if (first == "install") {
        const InteriorDef::Module *module = InteriorDef::module(words.value(1));
        const QString room = words.mid(2).join(' ');
        // Check everything before paying
        if (!module || !m_interior.blueprints().contains(module->id)) {
            m_interior.orderInstall(words.value(1), room, &error);
            failed();
            return true;
        }
        Interior probe = m_interior;
        if (probe.orderInstall(module->id, room, &error) < 0) {
            failed();
            return true;
        }
        if (payFor(module->cost) && m_interior.orderInstall(module->id, room, &error) >= 0)
            m_console->appendLine(tr("Installation of the %1 started")
                                      .arg(QCoreApplication::translate("Interior", module->name.toUtf8().constData())),
                                  CommandConsole::Tone::Info);
        return true;
    }
    if (first == "blueprints") {
        if (m_interior.blueprints().isEmpty())
            m_console->appendLine(tr("No blueprint yet - explore Ancient sites, open the archives, capture ships"));
        for (const QString &id : m_interior.blueprints())
            if (const InteriorDef::Module *m = InteriorDef::module(id)) {
                QStringList cost;
                for (const auto &[resource, quantity] : m->cost)
                    cost << QStringLiteral("%1 %2").arg(quantity).arg(DbText::tr(resource));
                m_console->appendLine(QStringLiteral("%1 (%2): %3 - %4")
                                          .arg(QCoreApplication::translate("Interior", m->name.toUtf8().constData()), m->id,
                                               QCoreApplication::translate("Interior", m->effect.toUtf8().constData()),
                                               cost.join(", ")));
            }
        return true;
    }
    if (first == "defend") {
        if (!m_interior.defend(arg, &error))
            failed();
        flushInteriorMessages();
        return true;
    }
    if (first == "board") {
        bool ok = false;
        const int target = words.value(1).toInt(&ok);
        if (!ok || target < 1) {
            m_console->appendLine(tr("Usage: board <n> - a crippled mothership (shield down, hull under 35%)"),
                                  CommandConsole::Tone::Warning);
            return true;
        }
        if (!m_battle->canBoard(target - 1, &error)) {
            m_console->appendLine(tr("Cannot board: %1").arg(error), CommandConsole::Tone::Warning);
            return true;
        }
        m_console->appendLine(tr("Boarding team away (success chance %1%)").arg(qRound(m_interior.boardingChance() * 100)),
                              CommandConsole::Tone::Info);
        if (m_interior.boardEnemy()) {
            const QString faction = m_battle->captureEnemy(target - 1);
            // Its stores and its technology
            const QString gained = m_destiny.addResources({{"naquadah", 12}, {"trinium", 4}, {"minerals", 8}});
            if (!gained.isEmpty())
                m_console->appendLine(tr("Salvage from the captured ship: %1").arg(gained), CommandConsole::Tone::Success);
            const QString blueprint = faction == "Goa'uld" || faction == "Lucian Alliance" ? QStringLiteral("goauld_cannon")
                                    : faction == "Asgard"                                 ? QStringLiteral("asgard_shield")
                                    : faction == "Ancients"                               ? QStringLiteral("sensor_suite")
                                                                                          : m_interior.randomUnknownBlueprint();
            m_interior.learnBlueprint(blueprint);
        } else {
            m_console->appendLine(tr("The boarding failed - the team falls back with wounded"), CommandConsole::Tone::Critical);
        }
        flushInteriorMessages();
        return true;
    }
    return false;
}

void GameMenu::showCrew()
{
    m_console->appendLine(tr("Crew %1 - morale %2% - free workers %3").arg(m_route->state().crew)
                              .arg(qRound(m_interior.morale())).arg(m_interior.freeWorkers()));
    for (const Interior::Person &p : m_interior.people()) {
        QString state = p.injury > 0.0f ? tr("INJURED (%1 s)").arg(qRound(p.injury))
                      : p.resting        ? tr("resting")
                      : p.job >= 0       ? tr("repair job #%1").arg(p.job)
                                         : tr("available");
        const int room = m_interior.find(p.location);
        const QString where = room >= 0
            ? QCoreApplication::translate("Interior", m_interior.compartments()[room].name.toUtf8().constData())
            : p.location;
        m_console->appendLine(tr("%1%2 - fatigue %3% - %4 - %5")
                                  .arg(p.name)
                                  .arg(p.bonus.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(p.bonus))
                                  .arg(qRound(p.fatigue))
                                  .arg(state, where),
                              p.injury > 0.0f ? CommandConsole::Tone::Warning : CommandConsole::Tone::Info);
    }
    m_console->appendLine(tr("Orders: crew rest <name>, crew rotate (tired ones rest), jobs, cancel <job>"));
}

void GameMenu::showPlan()
{
    if (!m_planDialog)
        m_planDialog = new ShipPlanDialog(&m_interior, [this] {
            QHash<QString, float> health;
            for (const QString &key : BattleView::systemKeys())
                health.insert(key, m_battle->systemHealth(key));
            return health;
        }, this);
    m_planDialog->show();
    m_planDialog->raise();
}

void GameMenu::flushInteriorMessages()
{
    for (const Interior::Message &m : m_interior.takeMessages()) {
        const CommandConsole::Tone tone = m.tone == 1 ? CommandConsole::Tone::Success
                                        : m.tone == 2 ? CommandConsole::Tone::Warning
                                        : m.tone == 3 ? CommandConsole::Tone::Critical
                                                      : CommandConsole::Tone::Info;
        m_console->appendLine(m.text, tone);
    }
}

// The interior runs with the game clock: fires, air, jobs, crew
void GameMenu::updateInterior(float seconds)
{
    m_interior.setCrewCount(m_route->state().crew);
    const bool battle = m_battle->hostileCount() > 0;
    const Interior::Effects e = m_interior.tick(seconds, m_battle->systemHealth("lifesupport"), battle);
    for (const auto &[key, amount] : e.systemDamage) {
        const float health = std::max(0.0f, m_battle->systemHealth(key) - amount);
        m_battle->setSubsystemHealth(key, health);
        m_destiny.setSystemHealth(key, health);
    }
    for (const auto &[key, amount] : e.systemRepair)
        m_battle->repairBy(key, amount);
    if (e.hullRepair > 0.0f)
        m_battle->repairBy(QStringLiteral("hull"), e.hullRepair);
    if (e.crewLost > 0)
        m_route->changeCrew(-e.crewLost);
    if (e.food >= 1.0f || e.water >= 1.0f)
        m_destiny.addResources({{"food", int(e.food)}, {"water", int(e.water)}});
    // Boarding parties: Jaffa, Lucian Alliance and Wraith come aboard
    const QString faction = m_battle->waveFaction();
    if (battle && (faction == "Goa'uld" || faction == "Lucian Alliance" || faction == "Wraith")
        && QRandomGenerator::global()->generateDouble() < 0.004 * seconds)
        m_interior.board(2 + QRandomGenerator::global()->bounded(4));
    // Burning compartments on the 3D hull
    QList<QVector3D> fires;
    for (const Interior::Compartment &c : m_interior.compartments())
        if (c.fire > 0.0f)
            fires << QVector3D(float(c.rect.center().x()), 0.3f, float(c.rect.center().y()));
    m_battle->setFireSpots(fires);
    applyBonuses();
    flushInteriorMessages();
}

// Key characters and modules change the ship
void GameMenu::applyBonuses()
{
    m_battle->setPowerLoss(m_interior.powerLoss());
    m_battle->setPowerBonus(m_interior.hasModule("naquadah_generator") ? 15 : 0);
    m_battle->setDamageBonus((m_interior.bonusActive("combat") ? 1.1f : 1.0f)
                             * (m_interior.hasModule("goauld_cannon") ? 1.15f : 1.0f));
    m_battle->setShieldRegenBonus(m_interior.hasModule("asgard_shield") ? 1.3f : 1.0f);
    m_battle->setAccuracyBonus((m_interior.bonusActive("ancient") ? 1.05f : 1.0f)
                               * (m_interior.hasModule("sensor_suite") ? 1.1f : 1.0f)
                               * (m_interior.hasCapability("command") ? 1.0f : 0.9f));
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
        repair(args.value(0));
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
    for (const QString &category : m_commands->categories())
        m_console->appendLine(QStringLiteral("%1: %2").arg(category.toUpper(),
                                                          m_commands->usages(category).join(", ")));
    m_console->appendLine(tr("Combat: attack 1, scan 2, shield off, pulseweapon - Repair: repair engines"));
    m_console->appendLine(tr("Exploration: scansector, stargate <address>, explore, stargate destiny, engine ftl"));
    m_console->appendLine(tr("Game: quests, hint, save (also automatic every 30 s, after each jump and on exit), clear"));
    m_console->appendLine(tr("Library: library (all ships), library <ship> (e.g. library dart) - attacks and chances"));
    m_console->appendLine(tr("Sound: sound (status), sound on, sound off, sound <0-100> (volume)"));
    m_console->appendLine(tr("Music: music (status), music on, music off, music <0-100> (volume)"));
    m_console->appendLine(tr("Menu: menu or Esc - pause, save, settings, quit"));
    m_console->appendLine(tr("Hangar: launch [n] - Puddle Jumpers fight at your side, back after the battle"));
    m_console->appendLine(tr("Energy: turrets on|off, railguns on|off, power shields|weapons|balanced (power: report)"));
    m_console->appendLine(tr("Tactics: target <n> weapons|shields|engines|hangar (aim at a subsystem), missiles [n], "
                             "drones [n] (Ancient caches only, need %1 free power), evade on|off").arg(Balance::kDronePower));
    m_console->appendLine(tr("FTL: engine ftl (under fire the drive spools %1 s; fighters dock, those too far are left behind), "
                             "ftl abort - star: dive into a star to recharge (shields up!)").arg(qRound(Balance::kFtlSpoolUnderFire)));
    m_console->appendLine(tr("Supplies: railgun slugs (mineral planets), missiles (naquadah planets), drones (Ancient sites)"));
    m_console->appendLine(tr("Ship: plan (zoomable plan of the compartments), repair <system|hull|room> (a crew job), "
                             "jobs, cancel <job>, seal|unseal|vent <room>, unlock <room>, install <module> [room], blueprints"));
    m_console->appendLine(tr("Crew: crew (list), crew rest <name>, crew rotate - defend <room> (boarders), "
                             "board <n> (crippled mothership)"));
    m_console->appendLine(tr("Repairs use resources (showressource); <n> = target number shown as T1, T2…"));
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
        music->setMood(MusicPlayer::Mood::Battle, m_battle->waveFaction());
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
        const QList<ShipClass> library = ShipLibraryRepository::all();
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

// Repairs are jobs for the crew: resources are paid now, the work takes time
void GameMenu::repair(const QString &system)
{
    const QString what = system.trimmed().toLower();
    const QString key = BattleView::systemKey(what);
    if (!key.isEmpty() && m_battle->systemHealth(key) >= 99.5f) {
        m_console->appendLine(tr("%1 already at 100%").arg(key));
        return;
    }
    QList<RepairCost> cost;
    if (!key.isEmpty()) {
        cost = m_destiny.repairCost(key);
    } else if (const int room = m_interior.find(what); room >= 0) {
        const Interior::Compartment &c = m_interior.compartments()[room];
        if (c.fire <= 0.0f && c.breach)
            cost.append({0, QStringLiteral("minerals"), 3});           // hull plates
        else if (c.fire <= 0.0f && !c.leak && c.integrity < 99.0f)
            cost.append({0, QStringLiteral("minerals"), 2});
    }
    QString error;
    const int job = m_interior.orderRepair(key.isEmpty() ? what : key, &error);
    if (job < 0) {
        m_console->appendLine(tr("Cannot repair: %1").arg(error), CommandConsole::Tone::Warning);
        return;
    }
    QString missing;
    if (!m_destiny.consume(cost, &missing)) {
        m_interior.cancelJob(job);
        m_console->appendLine(tr("Cannot repair %1 - missing: %2").arg(what, missing), CommandConsole::Tone::Warning);
        return;
    }
    if (m_destiny.isLoaded() && !cost.isEmpty()) {
        QStringList used;
        for (const RepairCost &c : cost)
            used << QStringLiteral("%1 %2").arg(c.quantity).arg(DbText::tr(c.resourceName));
        m_console->appendLine(tr("Resources used: %1").arg(used.join(", ")));
    }
    const Interior::Job *created = nullptr;
    for (const Interior::Job &j : m_interior.jobs())
        if (j.id == job)
            created = &j;
    m_console->appendLine(created && created->workers.isEmpty()
                              ? tr("Repair job #%1 queued - no free hands yet ('jobs', 'crew rotate')").arg(job)
                              : tr("Repair job #%1 started (%2 worker(s))").arg(job).arg(created ? created->workers.size() : 0),
                          CommandConsole::Tone::Info);
    if (!key.isEmpty())
        for (const QString &component : m_destiny.componentNames(key))
            m_quests->notify("REPAIR", component);
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
    m_console->appendLine(text, tone);
}

void GameMenu::simulate()
{
    const double kHoursPerTick = ::kHoursPerTick * gameConfig().gameSpeed;
    m_route->tick(kHoursPerTick);
    updateMusic(false);
    updateInterior(gameConfig().gameSpeed);

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

    QString gate = s.gateOpen ? tr("GATE OPEN %1 h").arg(s.gateRemaining, 0, 'f', 1) : tr("GATE CLOSED");
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
        m_console->appendLine(tr("New journey - progress is saved automatically ('save' to force it)"));
        return;
    }

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
    if (danger > 0.8f && QRandomGenerator::global()->generateDouble() < 0.35) {
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
