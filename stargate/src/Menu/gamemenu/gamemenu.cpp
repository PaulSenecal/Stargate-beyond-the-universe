#include "gamemenu.h"

#include <QHBoxLayout>
#include <QRandomGenerator>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

#include "commandconsole.h"
#include "hudimagepanel.h"
#include "routemapwidget.h"
#include "radarwidget.h"
#include "shipstatuswidget.h"
#include "weaponstatuswidget.h"
#include "src/core/commandmanager.h"
#include "src/core/questsystem.h"
#include "src/db/databasemanager.h"
#include "src/db/repositories/questrepository.h"
#include "src/db/repositories/saverepository.h"
#include "src/db/repositories/worldrepository.h"
#include "src/render/planetlook.h"
#include "src/world/planetgenerator.h"

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

GameMenu::GameMenu(QWidget *parent)
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
    connect(m_route, &DestinyRoute::notice, this, &GameMenu::onRouteNotice);
    connect(m_simTimer, &QTimer::timeout, this, &GameMenu::simulate);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] { saveGame(false); });

    if (m_commands->loadCommandsFromFile(":/commandList/commands.json")) {
        QStringList words;
        for (const QString &cmd : m_commands->getAvailableCommands())
            words << cmd.toLower();
        words << "help" << "clear" << "save" << "quests" << "hint";
        words.removeDuplicates();
        words.sort();
        m_console->setCompletions(words);
        m_console->setCompletionProvider([this](const QString &text) {
            QStringList addresses{"destiny"};
            for (const Planet &p : m_route->currentPlanets())
                addresses << p.gateAddress();
            QStringList lines = m_commands->completions(text, {{"stargate", addresses}});
            for (const QString &local : {QStringLiteral("help"), QStringLiteral("clear"), QStringLiteral("save"),
                                         QStringLiteral("quests"), QStringLiteral("hint")})
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
            picture.contacts.append({QPointF(b.local.x(), b.local.z()), b.index, b.mothership, b.locked, b.health});
        for (const BattleView::RadarShot &s : m_battle->radarShots())
            picture.bolts.append({QPointF(s.local.x(), s.local.z()), s.fromDestiny});
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
    m_console->appendLine(tr("Repairs use resources (showressource); <n> = target number shown as T1, T2…"));
}

void GameMenu::loadSave()
{
    if (!DatabaseManager::instance().isOpen() || !m_destiny.load(QStringLiteral("Commander"))) {
        m_console->appendLine(tr("No save database - progress will not be kept"), CommandConsole::Tone::Warning);
        return;
    }

    for (const QString &key : BattleView::systemKeys())
        m_battle->setSubsystemHealth(key, m_destiny.systemHealth(key));

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

void GameMenu::repair(const QString &system)
{
    const QString key = BattleView::systemKey(system);
    if (key.isEmpty()) {
        m_battle->repairSubsystem(system);      // prints the list of valid systems
        return;
    }
    if (m_battle->systemHealth(key) >= 99.5f) {
        m_console->appendLine(tr("%1 already at 100%").arg(key));
        return;
    }

    const QList<RepairCost> cost = m_destiny.repairCost(key);
    QString missing;
    if (!m_destiny.consume(cost, &missing)) {
        m_console->appendLine(tr("Cannot repair %1 - missing: %2").arg(key, missing),
                              CommandConsole::Tone::Warning);
        return;
    }
    if (m_destiny.isLoaded() && !cost.isEmpty()) {
        QStringList used;
        for (const RepairCost &c : cost)
            used << QStringLiteral("%1 %2").arg(c.quantity).arg(c.resourceName);
        m_console->appendLine(tr("Resources used: %1").arg(used.join(", ")));
    }
    if (m_battle->repairSubsystem(key))
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
                                  .arg(r.name.leftJustified(14, ' '))
                                  .arg(r.quantity, 4)
                                  .arg(r.rarity.toLower()));
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
    m_route->tick(kHoursPerTick);

    // Crew supplies
    if (m_destiny.isLoaded()) {
        const bool fed = m_destiny.consumeSupplies(m_route->state().crew, kHoursPerTick);
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

    const RouteStop &stop = m_route->currentStop();
    m_console->appendLine(tr("Game restored: day %1, G%2 %3 system %4, crew %5")
                              .arg(int(save->hours) / 24 + 1)
                              .arg(stop.galaxy, 2, 10, QChar('0'))
                              .arg(m_route->galaxyName(stop.galaxy))
                              .arg(stop.sequence, 4, 10, QChar('0'))
                              .arg(m_route->state().crew),
                          CommandConsole::Tone::Success);
}

void GameMenu::saveGame(bool announce)
{
    if (m_sessionId < 0) {
        if (announce)
            m_console->appendLine(tr("No save database - cannot save"), CommandConsole::Tone::Warning);
        return;
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

    const bool ok = SaveRepository::save(m_sessionId, save, m_route->galaxyName(m_route->currentStop().galaxy));
    if (announce)
        m_console->appendLine(ok ? tr("Game saved (day %1)").arg(int(state.hours) / 24 + 1) : tr("Save failed"),
                              ok ? CommandConsole::Tone::Success : CommandConsole::Tone::Critical);
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
    GameEvent event;
    QString reason;
    if (!m_route->jump(&event, &reason)) {
        m_console->appendLine(tr("FTL jump impossible: %1").arg(reason), CommandConsole::Tone::Warning);
        return;
    }

    // Enemies are left behind; new contacts may show up later
    m_battle->clearBattlefield(float(25 + QRandomGenerator::global()->bounded(50)));
    m_console->appendLine(tr("FTL jump - the Destiny enters hyperspace"), CommandConsole::Tone::Info);

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
