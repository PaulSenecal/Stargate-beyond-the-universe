// ================================================================
//  Game logic tests (Qt Test)
//  - CommandParserTest: command parsing, validation, Tab completion
//  - WorldTest:         planet generation, Destiny route, gate window
//  - DatabaseTest:      ship creation, resources, repairs, supplies,
//                       world records, save/restore, old-save migration
//  The database tests run on a temporary copy of the template
//  database, never on the player's save.
// ================================================================

#include <QJsonArray>
#include <array>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "src/conquest/conquestgame.h"
#include "src/core/commandmanager.h"
#include "src/core/destinystate.h"
#include "src/core/questsystem.h"
#include "src/db/databasemanager.h"
#include "src/db/repositories/questrepository.h"
#include "src/db/repositories/saverepository.h"
#include "src/db/repositories/shiplibraryrepository.h"
#include "src/db/repositories/worldrepository.h"
#include "src/world/destinyroute.h"
#include "src/world/planetgenerator.h"

// ================================================================
//  Command parser
// ================================================================

class CommandParserTest : public QObject
{
    Q_OBJECT

private:
    CommandManager m_commands;

private slots:
    void initTestCase()
    {
        QVERIFY2(m_commands.loadCommandsFromFile(COMMANDS_JSON), COMMANDS_JSON);
    }

    void parse_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QStringList>("args");

        QTest::newRow("simple") << "attack 2" << true << "attack" << QStringList{"2"};
        QTest::newRow("case") << "ATTACK 2" << true << "attack" << QStringList{"2"};
        QTest::newRow("spaces") << "  repair    shields  " << true << "repair" << QStringList{"shields"};
        QTest::newRow("multi-word") << "close door a20" << true << "closedoor" << QStringList{"a20"};
        QTest::newRow("multi-word case") << "Close Door B5" << true << "closedoor" << QStringList{"B5"};
        QTest::newRow("compact name") << "closedoor a20" << true << "closedoor" << QStringList{"a20"};
        QTest::newRow("value lower-cased") << "engine FTL" << true << "engine" << QStringList{"ftl"};
        QTest::newRow("optional absent") << "pulseweapon" << true << "pulseweapon" << QStringList{};
        QTest::newRow("optional present") << "pulseweapon all" << true << "pulseweapon" << QStringList{"all"};
        QTest::newRow("duplicate 0 param") << "activate" << true << "activate" << QStringList{};
        QTest::newRow("duplicate 1 param") << "activate railgun" << true << "activate" << QStringList{"railgun"};
        QTest::newRow("numbers") << "moveto 1.5 -3" << true << "moveto" << QStringList{"1.5", "-3"};
        QTest::newRow("gate address") << "stargate G000003A" << true << "stargate" << QStringList{"G000003A"};

        QTest::newRow("missing param") << "attack" << false << "" << QStringList{};
        QTest::newRow("not a number") << "attack abc" << false << "" << QStringList{};
        QTest::newRow("below min") << "attack 0" << false << "" << QStringList{};
        QTest::newRow("bad value") << "shield maybe" << false << "" << QStringList{};
        QTest::newRow("bad pattern") << "close door 20" << false << "" << QStringList{};
        QTest::newRow("too many") << "maintower now" << false << "" << QStringList{};
        QTest::newRow("unknown") << "xyz" << false << "" << QStringList{};
    }

    void parse()
    {
        QFETCH(QString, input);
        QFETCH(bool, valid);
        QFETCH(QString, name);
        QFETCH(QStringList, args);

        const ParsedCommand cmd = m_commands.parse(input);
        QCOMPARE(cmd.valid, valid);
        if (valid) {
            QCOMPARE(cmd.name, name);
            QCOMPARE(cmd.args, args);
        } else {
            QVERIFY(!cmd.error.isEmpty());
        }
    }

    void errorMessages()
    {
        QVERIFY(m_commands.parse("atack 1").error.contains("did you mean 'attack'"));
        QVERIFY(m_commands.parse("close").error.contains("did you mean 'close door'"));
        QVERIFY(m_commands.parse("attack").error.contains("usage: attack <target>"));
        QVERIFY(m_commands.parse("attack abc").error.contains("must be a whole number"));
    }

    void completions_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QStringList>("expected");

        QTest::newRow("name prefix") << "sca" << QStringList{"scan", "scansector"};
        QTest::newRow("multi-word") << "close d" << QStringList{"close door"};
        QTest::newRow("value") << "engine f" << QStringList{"engine ftl"};
        QTest::newRow("values") << "shield o" << QStringList{"shield off", "shield on"};
        QTest::newRow("keeps casing") << "Shield OF" << QStringList{"Shield off"};
        QTest::newRow("dynamic") << "stargate d" << QStringList{"stargate destiny"};
        QTest::newRow("dynamic all") << "stargate " << QStringList{"stargate destiny", "stargate G000003A"};
        QTest::newRow("free text") << "closedoor " << QStringList{};
        QTest::newRow("complete name") << "scansector" << QStringList{};
    }

    void completions()
    {
        QFETCH(QString, input);
        QFETCH(QStringList, expected);
        const QHash<QString, QStringList> dynamic{{"stargate", {"destiny", "G000003A"}}};
        QCOMPARE(m_commands.completions(input, dynamic), expected);
    }
};

// ================================================================
//  World: planets and route
// ================================================================

class WorldTest : public QObject
{
    Q_OBJECT

private slots:
    void planetCodeAndAddress()
    {
        const Planet p = PlanetGenerator::generate(4, 17, 1);
        QCOMPARE(p.code, QStringLiteral("G04-0017-B"));
        QCOMPARE(p.gateAddress(), QStringLiteral("G040017B"));
    }

    void planetsAreDeterministic()
    {
        const Planet a = PlanetGenerator::generate(7, 42, 2);
        const Planet b = PlanetGenerator::generate(7, 42, 2);
        QCOMPARE(a.nickname, b.nickname);
        QCOMPARE(a.conditions.water, b.conditions.water);
        QCOMPARE(a.conditions.toxicity, b.conditions.toxicity);
        QCOMPARE(int(a.classification), int(b.classification));
    }

    void ancientSitesAreRare()
    {
        int ancient = 0;
        for (int s = 0; s < 2000; ++s)
            ancient += PlanetGenerator::generate(0, s, 0).isAncientSite();
        QVERIFY2(ancient > 50 && ancient < 160, qPrintable(QString::number(ancient)));
    }

    void conditionsInRange()
    {
        for (int s = 0; s < 200; ++s) {
            const PlanetConditions c = PlanetGenerator::generate(3, s, s % 3).conditions;
            for (float v : {c.water, c.oxygen, c.minerals, c.energy, c.organics, c.toxicity, c.radiation})
                QVERIFY(v >= 0.0f && v <= 1.0f);
        }
    }

    void gateDialAndRecall()
    {
        DestinyRoute route;
        QString error;
        const Planet first = route.currentPlanets().first();
        QCOMPARE(route.variantForAddress(first.gateAddress().toLower()), 0);
        QCOMPARE(route.variantForAddress("NOPE1234"), -1);
        QVERIFY(route.dial(0, &error));
        QVERIFY(!route.dial(0, &error));                 // already there
        GameEvent event;
        QVERIFY(!route.jump(&event, &error));            // team still down
        QVERIFY(error.contains("stargate destiny"));
        QVERIFY(route.recallTeam(&error));
        QVERIFY(!route.recallTeam(&error));
    }

    void strandedTeamCostsCrew()
    {
        DestinyRoute route;
        QString error;
        QVERIFY(route.dial(0, &error));
        const int crew = route.state().crew;
        for (int i = 0; i < 200 && route.state().gateOpen; ++i)
            route.tick(0.25);
        QVERIFY(!route.state().gateOpen);
        QCOMPARE(route.state().crew, crew - 3);
        QCOMPARE(route.state().teamPlanet, -1);
    }

    void jumpAndRecharge()
    {
        DestinyRoute route;
        GameEvent event;
        QString reason;
        QVERIFY(route.canJump());
        QVERIFY(route.jump(&event, &reason));
        QVERIFY(!event.title.isEmpty());
        QCOMPARE(route.currentIndex(), 1);
        QVERIFY(route.state().gateOpen);
        QVERIFY(!route.canJump(&reason));
        for (int i = 0; i < 2000 && !route.canJump(); ++i)
            route.tick(0.25);
        QVERIFY(route.canJump());
    }

    void restoreClampsValues()
    {
        DestinyRoute route;
        RouteState state;
        state.crew = -5;
        state.power = 3.0;
        state.teamPlanet = 9;
        route.restore(999999, state, {0});
        QCOMPARE(route.currentIndex(), route.totalStops() - 1);
        QCOMPARE(route.state().crew, 1);
        QCOMPARE(route.state().power, 1.0);
        QCOMPARE(route.state().teamPlanet, -1);
        QVERIFY(route.hasCollected(0));
    }
};

// ================================================================
//  Database (temporary copy of the template)
// ================================================================

class DatabaseTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    DestinyState m_destiny;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const QString path = m_dir.filePath("test.db");
        QVERIFY(QFile::copy(TEMPLATE_DB, path));
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
        QVERIFY(DatabaseManager::instance().init(path));
        // Simulate a save created before "food" existed
        QVERIFY(DatabaseManager::instance().exec("DELETE FROM Resource WHERE name = 'food'"));
    }

    void createsTheDestiny()
    {
        QVERIFY(m_destiny.load("Tester"));
        QCOMPARE(m_destiny.components().size(), 12);
        QCOMPARE(m_destiny.stockOf("water"), 250);
        QCOMPARE(m_destiny.stockOf("food"), 150);          // migration re-created the resource
        QCOMPARE(m_destiny.stockOf("naquadah"), 20);

        DestinyState again;
        QVERIFY(again.load("Tester"));
        QCOMPARE(again.shipId(), m_destiny.shipId());       // same ship, no second delivery
        QCOMPARE(again.stockOf("food"), 150);
    }

    void damageIsStored()
    {
        m_destiny.setSystemHealth("engines", 40.0f);
        DestinyState reloaded;
        QVERIFY(reloaded.load("Tester"));
        QCOMPARE(qRound(reloaded.systemHealth("engines")), 40);
        m_destiny.setSystemHealth("engines", 100.0f);
    }

    void repairCostsResources()
    {
        const QList<RepairCost> cost = m_destiny.repairCost("engines");   // Sublight Engine
        QCOMPARE(cost.size(), 2);
        const int naquadah = m_destiny.stockOf("naquadah");
        QString missing;
        QVERIFY(m_destiny.consume(cost, &missing));
        QCOMPARE(m_destiny.stockOf("naquadah"), naquadah - 4);

        QList<RepairCost> tooMuch{{0, "crystal", 999}};
        QVERIFY(!m_destiny.consume(tooMuch, &missing));
        QVERIFY(missing.contains("crystal"));
    }

    void crewConsumesSupplies()
    {
        const int water = m_destiny.stockOf("water");
        const int food = m_destiny.stockOf("food");
        for (int i = 0; i < 40; ++i)
            QVERIFY(m_destiny.consumeSupplies(80, 0.25));   // 10 hours
        QCOMPARE(water - m_destiny.stockOf("water"), 16);   // 0.02 / person / hour
        QCOMPARE(food - m_destiny.stockOf("food"), 12);     // 0.015 / person / hour
    }

    void planetCollection()
    {
        Planet rich = PlanetGenerator::generate(0, 0, 0);
        rich.conditions = PlanetConditions{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, true};
        const int water = m_destiny.stockOf("water");
        const QString loot = m_destiny.collectFromPlanet(rich);
        QVERIFY(loot.contains("+60 water"));
        QCOMPARE(m_destiny.stockOf("water"), water + 60);
    }

    void worldRecordsAreIdempotent()
    {
        const int before = WorldRepository::discoveredPlanetCount();
        const QList<Planet> planets{PlanetGenerator::generate(1, 2, 0), PlanetGenerator::generate(1, 2, 1)};
        QVERIFY(WorldRepository::recordSystem("Othala", planets));
        QVERIFY(WorldRepository::recordSystem("Othala", planets));
        QCOMPARE(WorldRepository::discoveredPlanetCount(), before + 2);
    }

    void saveRoundTrip()
    {
        QVERIFY(SaveRepository::ensureSchema());
        const int session = SaveRepository::autosaveSession(m_destiny.shipId());
        QVERIFY(session > 0);
        QCOMPARE(SaveRepository::autosaveSession(m_destiny.shipId()), session);
        QVERIFY(!SaveRepository::load(session).has_value());

        GameSave empty;      // empty explored list must not be stored as NULL
        QVERIFY(SaveRepository::save(session, empty, "Haeble"));
        QVERIFY(SaveRepository::load(session)->collected.isEmpty());

        GameSave s;
        s.routeIndex = 7;
        s.hours = 51.25;
        s.crew = 71;
        s.power = 0.42;
        s.ftlCharge = 0.3;
        s.gateOpen = true;
        s.gateRemaining = 3.5;
        s.gateTotal = 9.0;
        s.teamPlanet = 1;
        s.collected = {0, 2};
        s.hull = 55.5f;
        s.shield = 0.0f;
        s.shieldOn = false;
        QVERIFY(SaveRepository::save(session, s, "Othala"));

        const std::optional<GameSave> l = SaveRepository::load(session);
        QVERIFY(l.has_value());
        QCOMPARE(l->routeIndex, 7);
        QCOMPARE(l->hours, 51.25);
        QCOMPARE(l->crew, 71);
        QCOMPARE(l->power, 0.42);
        QCOMPARE(l->teamPlanet, 1);
        QCOMPARE(l->collected, (QList<int>{0, 2}));
        QCOMPARE(l->hull, 55.5f);
        QCOMPARE(l->shieldOn, false);

        QVERIFY(SaveRepository::reset(session));
        QVERIFY(!SaveRepository::load(session).has_value());
    }

    void tutorialCampaign()
    {
        QVERIFY(QuestRepository::migrate());
        const int player = QuestRepository::playerIdOfShip(m_destiny.shipId());
        QVERIFY(player > 0);

        QuestSystem quests;
        QStringList started, objectives, completed;
        bool firstFlag = false;
        bool campaignDone = false;
        QList<std::pair<QString, int>> lastRewards;
        connect(&quests, &QuestSystem::questStarted, this, [&](const QString &t, const QString &, bool first) {
            started << t;
            firstFlag = firstFlag || first;
        });
        connect(&quests, &QuestSystem::objectiveCompleted, this, [&](const QString &d) { objectives << d; });
        connect(&quests, &QuestSystem::questCompleted, this,
                [&](const QString &t, const QList<std::pair<QString, int>> &r) { completed << t; lastRewards = r; });
        connect(&quests, &QuestSystem::campaignCompleted, this, [&] { campaignDone = true; });

        QVERIFY(quests.load(player));
        QCOMPARE(started, QStringList{"First Contact"});
        QVERIFY(firstFlag);
        QCOMPARE(quests.hint(), QStringLiteral("repair lifesupport"));

        // Quest 1: two repairs + status, in any order; wrong targets ignored
        quests.notify("REPAIR", "Shield Generator");
        QVERIFY(objectives.isEmpty());
        quests.notify("REPAIR", "Life Support");
        quests.notify("STATUS");
        QCOMPARE(objectives.size(), 2);
        QCOMPARE(quests.hint(), QStringLiteral("repair engines"));
        quests.notify("REPAIR", "Sublight Engine");
        QCOMPARE(completed, QStringList{"First Contact"});
        QCOMPARE(quests.activeQuest()->title, QStringLiteral("Power Shortage"));

        // Quest 2: 10 naquadah, counted across gains, other resources ignored
        quests.notify("COLLECT", "water", 50);
        quests.notify("COLLECT", "naquadah", 4);
        QVERIFY(quests.objectiveLines().join('\n').contains("(4/10)"));
        quests.notify("COLLECT", "naquadah", 9);
        QCOMPARE(completed.last(), QStringLiteral("Power Shortage"));
        QCOMPARE(lastRewards, (QList<std::pair<QString, int>>{{"naquadah", 5}}));

        // Progress survives a reload
        QuestSystem reloaded;
        QVERIFY(reloaded.load(player));
        QCOMPARE(reloaded.completedCount(), 2);
        QCOMPARE(reloaded.activeQuest()->title, QStringLiteral("Signal in the Dark"));

        // Quest 3: through the gate and back
        quests.notify("EXPLORE");
        quests.notify("SURVIVE");
        QVERIFY(campaignDone);
        QCOMPARE(quests.completedCount(), 3);
        QVERIFY(quests.objectiveLines().isEmpty());
        QVERIFY(!quests.hint().isEmpty());
    }

    void shipLibrary()
    {
        // Simulate a save created before the library: no attacks, no
        // faction data, only the 5 original ship types
        DatabaseManager &db = DatabaseManager::instance();
        QVERIFY(db.exec("DROP TABLE Ship_Attack"));
        QVERIFY(db.exec("UPDATE Ship_Type SET faction = NULL, hostile = 0"));
        QVERIFY(db.exec("DELETE FROM Ship_Type WHERE id > 5"));
        QVERIFY(ShipLibraryRepository::migrate());
        QVERIFY(ShipLibraryRepository::migrate());          // idempotent

        const QList<ShipClass> ships = ShipLibraryRepository::all();
        QCOMPARE(ships.size(), 20);
        QStringList hostileFactions;
        for (const ShipClass &c : ships) {
            QVERIFY2(!c.attacks.isEmpty(), qPrintable(c.name));
            if (c.hostile && c.isMothership())
                hostileFactions << c.faction;
            for (const ShipAttack &a : c.attacks)
                if (a.kind == "launch")      // launched ships must exist
                    QVERIFY2(std::any_of(ships.begin(), ships.end(),
                                         [&](const ShipClass &s) { return s.name == a.payload; }),
                             qPrintable(a.payload));
        }
        hostileFactions.removeDuplicates();
        hostileFactions.sort();
        QCOMPARE(hostileFactions, (QStringList{"Drones", "Goa'uld", "Lucian Alliance", "Wraith"}));

        const auto dart = std::find_if(ships.begin(), ships.end(), [](const ShipClass &c) { return c.name == "Wraith Dart"; });
        QVERIFY(dart != ships.end());
        QCOMPARE(dart->attacks.size(), 2);
        const ShipAttack &dive = dart->attacks.last();        // specials come last
        QCOMPARE(dive.kind, QStringLiteral("kamikaze"));
        QVERIFY(dive.special && dive.interceptable());
        QCOMPARE(dive.trigger, QStringLiteral("damaged"));
    }
};


// ================================================================
//  Conquest mode logic
// ================================================================

class ConquestTest : public QObject
{
    Q_OBJECT

    // Three planets in a row: player capital - neutral - enemy capital
    static QJsonObject smallGalaxy()
    {
        auto planet = [](const char *name, double x, int owner, int defense, bool capital, QJsonArray lanes) {
            return QJsonObject{{"name", name}, {"x", x}, {"y", 100.0}, {"owner", owner}, {"population", 5.0},
                               {"maxPopulation", 8.0}, {"bonus", 0}, {"defense", defense},
                               {"capital", capital}, {"lanes", lanes}};
        };
        return {{"version", 1},
                {"factions", QJsonArray{QJsonObject{{"id", "Tau'ri"}, {"player", true}, {"naquadah", 100.0},
                                                    {"trinium", 50.0}},
                                        QJsonObject{{"id", "Goa'uld"}, {"player", false}, {"naquadah", 100.0},
                                                    {"trinium", 50.0}}}},
                {"planets", QJsonArray{planet("Earth", 100, 0, 5, true, {1}),
                                       planet("Abydos", 240, -1, 1, false, {0, 2}),
                                       planet("Chulak", 380, 1, 5, true, {1})}},
                {"at", 0}, {"previous", 0}, {"route", QJsonArray()}, {"hull", 100.0}};
    }

    // The same galaxy with fleets (owner, motherships, fighters, planet)
    static QJsonObject withFleets(const QList<std::array<int, 4>> &fleets)
    {
        QJsonObject json = smallGalaxy();
        QJsonArray list;
        int id = 1;
        for (const auto &[owner, big, small, at] : fleets)
            list.append(QJsonObject{{"id", id++}, {"owner", owner}, {"motherships", big}, {"fighters", small},
                                    {"at", at}, {"think", 100.0}});
        json["version"] = 2;
        json["fleets"] = list;
        return json;
    }

    static void run(ConquestGame &game, double seconds)
    {
        for (double t = 0; t < seconds; t += 0.1)
            game.tick(0.1);
    }

private slots:
    void generation_data()
    {
        QTest::addColumn<int>("size");
        QTest::addColumn<int>("planets");
        QTest::addColumn<int>("factions");
        QTest::newRow("small") << int(Conquest::Size::Small) << 12 << 2;
        QTest::newRow("medium") << int(Conquest::Size::Medium) << 24 << 3;
        QTest::newRow("large") << int(Conquest::Size::Large) << 40 << 4;
    }

    void generation()
    {
        QFETCH(int, size);
        QFETCH(int, planets);
        QFETCH(int, factions);
        for (quint32 seed : {1u, 7u, 42u, 1234u}) {
            ConquestGame game;
            game.start({QStringLiteral("Wraith"), Conquest::Size(size), seed});
            QCOMPARE(game.planets().size(), planets);
            QCOMPARE(game.factions().size(), factions);
            QCOMPARE(game.player().id, QStringLiteral("Wraith"));
            // Every planet reachable, one capital per faction, the flagship at home
            for (const Conquest::Planet &p : game.planets())
                QVERIFY(p.id == game.flagshipPlanet() || !game.path(game.flagshipPlanet(), p.id).isEmpty());
            int capitals = 0;
            for (const Conquest::Planet &p : game.planets())
                capitals += p.capital ? 1 : 0;
            QCOMPARE(capitals, factions);
            QCOMPARE(game.planets()[game.flagshipPlanet()].owner, game.playerIndex());
            for (int f = 0; f < factions; ++f)
                QVERIFY(game.planetCount(f) >= 1);
        }
        // Same seed, same galaxy
        ConquestGame a, b;
        a.start({QStringLiteral("Tau'ri"), Conquest::Size(size), 99});
        b.start({QStringLiteral("Tau'ri"), Conquest::Size(size), 99});
        QCOMPARE(a.toJson(), b.toJson());
    }

    void captureAndVictory()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(smallGalaxy()));
        QSignalSpy battles(&game, &ConquestGame::battleStarted);
        QSignalSpy over(&game, &ConquestGame::gameOver);

        // Neutral world: taken after a stay in orbit (4 + 3 x defense s)
        QVERIFY(game.orderTravel(1));
        run(game, 2.5);                         // 140 units at 70/s
        QCOMPARE(game.flagshipPlanet(), 1);
        QCOMPARE(game.planets()[1].owner, -1);
        run(game, 7.5);
        QCOMPARE(game.planets()[1].owner, 0);

        // Enemy world: battle, then capture and victory
        QVERIFY(game.orderTravel(2));
        run(game, 2.5);
        QCOMPARE(battles.size(), 1);
        QCOMPARE(battles[0][1].toString(), QStringLiteral("Goa'uld"));
        QCOMPARE(battles[0][2].toInt(), 3);     // defense 5 -> 3 motherships
        QVERIFY(game.inBattle());
        QVERIFY(!game.orderTravel(0));          // no escape during a battle
        game.battleWon();
        QCOMPARE(game.planets()[2].owner, 0);
        QCOMPARE(over.size(), 1);
        QCOMPARE(over[0][0].toBool(), true);
    }

    void retreatAfterDefeat()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(smallGalaxy()));
        QVERIFY(game.orderTravel(2));           // through the neutral world
        run(game, 5.0);
        QVERIFY(game.inBattle());
        game.battleLost();
        QVERIFY(!game.inBattle());
        QCOMPARE(game.flagshipPlanet(), 1);     // back where it came from
        QCOMPARE(game.planets()[2].owner, 1);
    }

    void ordersAndEconomy()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(smallGalaxy()));
        QVERIFY(!game.fortify(0));              // capital already at 5
        QVERIFY(!game.fortify(2));              // not ours
        QVERIFY(!game.repairFlagship());        // intact hull
        game.setFlagshipHull(60);
        QVERIFY(game.repairFlagship());         // 40 points = 20 trinium
        QCOMPARE(qRound(game.flagshipHull()), 100);
        QCOMPARE(qRound(game.player().trinium), 30);

        const double before = game.player().naquadah;
        run(game, 10);
        QVERIFY(game.player().naquadah > before);
        QCOMPARE(game.findPlanet("abyd"), 1);
        QCOMPARE(game.findPlanet("3"), 2);
        QCOMPARE(game.findPlanet("nowhere"), -1);
    }

    void shipyardsLaunchFleets()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(smallGalaxy()));
        game.setSeed(3);
        QCOMPARE(game.fleetCount(0), 0);
        run(game, 200);                         // Earth (capital) fills its shipyard
        QVERIFY(game.fleetCount(0) + game.planetCount(0) > 1);
        QVERIFY(game.fleetCount(1) > 0 || game.planets()[2].dockFighters > 0);
        QString error;
        QVERIFY(!game.build(1, false, &error));   // Abydos: no shipyard
        QVERIFY(game.build(0, false, &error));    // 30 naquadah
    }

    void strongFleetTakesWeakWorld()
    {
        ConquestGame game;
        // Goa'uld fleet (3 motherships, 6 fighters) on the neutral Abydos (militia 2)
        QVERIFY(game.fromJson(withFleets({{1, 3, 6, 1}})));
        game.setSeed(1);
        QSignalSpy changed(&game, &ConquestGame::planetChanged);
        run(game, 0.5);
        QVERIFY(game.skirmishAt(1));            // the battle takes time
        QCOMPARE(game.planets()[1].owner, -1);
        run(game, 15);
        QVERIFY(!game.skirmishAt(1));
        QCOMPARE(game.planets()[1].owner, 1);
        QVERIFY(changed.size() >= 1);
        QCOMPARE(game.fleetCount(1), 1);
    }

    void fleetAttackingTheFlagship()
    {
        ConquestGame game;
        // A Goa'uld fleet waits at Earth, where the flagship is: defense battle
        QVERIFY(game.fromJson(withFleets({{1, 2, 4, 0}})));
        QSignalSpy battles(&game, &ConquestGame::battleStarted);
        run(game, 0.2);
        QCOMPARE(battles.size(), 1);
        QCOMPARE(battles[0][0].toInt(), 0);
        QCOMPARE(battles[0][1].toString(), QStringLiteral("Goa'uld"));
        QCOMPARE(battles[0][2].toInt(), 2);
        QCOMPARE(battles[0][3].toInt(), 4);
        QCOMPARE(game.planets()[0].owner, 0);   // no automatic battle while the flagship fights
        game.battleWon();
        QCOMPARE(game.fleetCount(1), 0);
        QCOMPARE(game.planets()[0].owner, 0);   // still ours
    }

    void defendedWorldHolds()
    {
        ConquestGame game;
        // A weak Goa'uld fleet attacks Earth (defense 5 = 15) while the flagship is away
        QJsonObject json = withFleets({{1, 0, 3, 0}});
        json["at"] = 1;
        QVERIFY(game.fromJson(json));
        game.setSeed(2);
        run(game, 20);
        QCOMPARE(game.planets()[0].owner, 0);
        QCOMPARE(game.fleetCount(1), 0);
        QVERIFY(game.planets()[0].defense >= 1);
    }

    void noVictoryWhileEnemyFleetsRemain()
    {
        ConquestGame game;
        // A Goa'uld fleet at Abydos (it takes the neutral world)
        QVERIFY(game.fromJson(withFleets({{1, 1, 1, 1}})));
        QSignalSpy over(&game, &ConquestGame::gameOver);
        game.setSeed(5);
        QVERIFY(game.orderTravel(2));
        run(game, 6);
        if (game.inBattle())
            game.battleWon();
        QVERIFY(game.fleetCount(1) + game.planetCount(1) > 0);
        QCOMPARE(over.size(), 0);
    }

    void watchAndEngage()
    {
        ConquestGame game;
        // Goa'uld fleet attacking a Lucian world... here: Goa'uld vs neutral Abydos,
        // with a strong neutral militia so that the battle lasts
        QJsonObject json = withFleets({{1, 2, 6, 1}});
        QJsonArray planets = json["planets"].toArray();
        QJsonObject abydos = planets[1].toObject();
        abydos["defense"] = 5;
        planets[1] = abydos;
        json["planets"] = planets;
        QVERIFY(game.fromJson(json));
        game.setSeed(4);
        QSignalSpy battles(&game, &ConquestGame::battleStarted);
        QVERIFY(game.orderTravel(1));
        run(game, 2.5);                         // the flagship arrives during the battle
        QCOMPARE(game.flagshipPlanet(), 1);
        QVERIFY(game.observing());
        QCOMPARE(battles.size(), 0);            // it only watches
        QString error;
        QVERIFY(!game.engage("wraith", &error));
        QVERIFY(game.engage("goa", &error));
        QCOMPARE(battles.size(), 1);
        QCOMPARE(battles[0][1].toString(), QStringLiteral("Goa'uld"));
        QVERIFY(battles[0][3].toInt() <= 6);    // what is left of the fleet
        game.battleWon();
        run(game, 0.5);
        QVERIFY(!game.skirmishAt(1));
        QCOMPARE(game.fleetCount(1), 0);
    }

    // The small galaxy with another enemy faction
    static QJsonObject withEnemy(const QString &faction, const QList<std::array<int, 4>> &fleets = {})
    {
        QJsonObject json = withFleets(fleets);
        QJsonArray factions = json["factions"].toArray();
        QJsonObject enemy = factions[1].toObject();
        enemy["id"] = faction;
        factions[1] = enemy;
        json["factions"] = factions;
        return json;
    }

    static bool said(const QSignalSpy &spy, const QString &text)
    {
        for (const QList<QVariant> &args : spy)
            if (args.value(0).toString().contains(text))
                return true;
        return false;
    }

    void wraithCulling()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(withEnemy("Wraith", {{1, 3, 6, 1}})));
        game.setSeed(1);
        run(game, 15);
        QCOMPARE(game.planets()[1].owner, 1);
        QVERIFY(game.planets()[1].population < 2.0);    // 5 M, -25% battle, then halved
    }

    void goauldSlavery()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(withEnemy("Goa'uld")));
        // Same population (5 M): Chulak (Goa'uld) yields 1.6x Earth (Tau'ri)
        QCOMPARE(qRound(game.naquadahRate(game.planets()[2]) * 1000),
                 qRound(game.naquadahRate(game.planets()[0]) * 1600));

        // A Goa'uld world without a fleet eventually rises
        QJsonObject json = withEnemy("Goa'uld");
        QJsonArray planets = json["planets"].toArray();
        QJsonObject abydos = planets[1].toObject();
        abydos["owner"] = 1;
        planets[1] = abydos;
        json["planets"] = planets;
        ConquestGame revolt;
        QVERIFY(revolt.fromJson(json));
        revolt.setSeed(8);
        QSignalSpy messages(&revolt, &ConquestGame::message);
        run(revolt, 1500);
        QVERIFY(said(messages, "Jaffa revolt"));
    }

    void tauriTechnologyAndDiplomacy()
    {
        QJsonObject json = smallGalaxy();
        QJsonArray planets = json["planets"].toArray();
        QJsonObject earth = planets[0].toObject();
        earth["bonus"] = int(Conquest::Bonus::AncientSite);
        planets[0] = earth;
        json["planets"] = planets;
        ConquestGame game;
        QVERIFY(game.fromJson(json));
        QCOMPARE(game.powerMultiplier(0), 1.08);
        QCOMPARE(game.powerMultiplier(1), 1.0);

        // The neutral neighbour of the Tau'ri joins them peacefully (no
        // shipyard anywhere: only diplomacy can change its owner)
        for (int i : {0, 2}) {
            QJsonObject planet = planets[i].toObject();
            planet["capital"] = false;
            planets[i] = planet;
        }
        json["planets"] = planets;
        // A Goa'uld fleet holding Chulak (no revolt, no AI move)
        json["version"] = 2;
        json["fleets"] = QJsonArray{QJsonObject{{"id", 1}, {"owner", 1}, {"motherships", 0}, {"fighters", 1},
                                                {"at", 2}, {"ordered", true}}};
        ConquestGame peace;
        QVERIFY(peace.fromJson(json));
        peace.setSeed(6);
        QSignalSpy messages(&peace, &ConquestGame::message);
        run(peace, 1800);
        QVERIFY(said(messages, "through diplomacy"));
        QCOMPARE(peace.planets()[1].owner, 0);
    }

    void lucianRaid()
    {
        ConquestGame game;
        // A big Lucian fleet attacks Earth while the flagship is away
        QJsonObject json = withEnemy("Lucian Alliance", {{1, 4, 10, 0}});
        json["at"] = 1;
        QVERIFY(game.fromJson(json));
        game.setSeed(3);
        QSignalSpy messages(&game, &ConquestGame::message);
        run(game, 60);
        QCOMPARE(game.planets()[0].owner, 1);
        QVERIFY(said(messages, "Lucian raid"));
    }

    void research()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(smallGalaxy()));      // 100 naquadah
        QString error;
        QVERIFY(!game.startResearch("weapons2", &error));   // needs weapons1
        QVERIFY(game.startResearch("weapons1", &error));
        QVERIFY(!game.startResearch("defense1", &error));   // one at a time
        QCOMPARE(qRound(game.player().naquadah), 0);
        run(game, 61);
        QVERIFY(game.hasTech(0, "weapons1"));
        QCOMPARE(game.powerMultiplier(0), 1.1);
        QCOMPARE(game.flagshipDamageBonus(), 1.15);
        QCOMPARE(game.findTech("2"), 1);
        QCOMPARE(game.findTech("weapons3"), 2);

        // Saved and loaded with the game
        ConquestGame copy;
        QVERIFY(copy.fromJson(game.toJson()));
        QVERIFY(copy.hasTech(0, "weapons1"));
    }

    void alliances()
    {
        // Three factions: the player (Tau'ri), Goa'uld (1 world) and Wraith (2 worlds)
        QJsonObject json = withFleets({});
        QJsonArray factions = json["factions"].toArray();
        factions.append(QJsonObject{{"id", "Wraith"}, {"player", false}, {"naquadah", 0.0}, {"trinium", 0.0}});
        json["factions"] = factions;
        // Two Wraith worlds off the Earth - Abydos - Chulak route
        QJsonArray planets = json["planets"].toArray();
        planets.append(QJsonObject{{"name", "Sateda"}, {"x", 380.0}, {"y", 300.0}, {"owner", 2},
                                   {"population", 5.0}, {"maxPopulation", 8.0}, {"defense", 3},
                                   {"lanes", QJsonArray{2, 4}}});
        planets.append(QJsonObject{{"name", "Taranis"}, {"x", 520.0}, {"y", 300.0}, {"owner", 2},
                                   {"population", 5.0}, {"maxPopulation", 8.0}, {"defense", 3},
                                   {"lanes", QJsonArray{3}}});
        QJsonObject chulak = planets[2].toObject();
        chulak["lanes"] = QJsonArray{1, 3};
        planets[2] = chulak;
        json["planets"] = planets;
        ConquestGame game;
        QVERIFY(game.fromJson(json));

        QString answer;
        QVERIFY(!game.proposeAlliance("wraith", &answer));   // no enemy stronger than them
        QVERIFY(game.proposeAlliance("goa", &answer));       // the Wraith are stronger
        QVERIFY(game.isAllied(1));
        QVERIFY(!game.hostile(0, 1));
        QVERIFY(game.hostile(1, 2));

        // Allied worlds are crossed without a battle
        QSignalSpy battles(&game, &ConquestGame::battleStarted);
        QVERIFY(game.orderTravel(2));
        run(game, 6);
        QCOMPARE(game.flagshipPlanet(), 2);
        QCOMPARE(battles.size(), 0);
        QVERIFY(!game.proposeAlliance("goa", &answer));      // already allies
        QVERIFY(game.breakAlliance("goa", &answer));
        QVERIFY(game.hostile(0, 1));
    }

    void fleetLossesAfterBattle()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(withFleets({{0, 2, 8, 0}, {0, 1, 3, 1}})));
        game.setFleetStrength(1, 1, 5);         // survivors of a 3D battle
        QCOMPARE(game.fleets()[0].motherships, 1);
        QCOMPARE(game.fleets()[0].fighters, 5);
        game.setFleetStrength(2, 0, 0);         // wiped out
        QCOMPARE(game.fleetCount(0), 1);
        QCOMPARE(ConquestGame::hangarFighter("Wraith"), QStringLiteral("Wraith Dart"));
        QCOMPARE(ConquestGame::hangarFighter("Tau'ri"), QStringLiteral("F-302"));
    }

    void orderedFleet()
    {
        ConquestGame game;
        QVERIFY(game.fromJson(withFleets({{0, 1, 3, 0}})));
        QString error;
        QVERIFY(!game.orderFleet(99, 1, &error));
        QVERIFY(game.orderFleet(1, 1, &error));
        QVERIFY(game.fleets().first().moving());
        QCOMPARE(game.fleets().first().target, 1);
    }

    void saveRoundTrip()
    {
        ConquestGame game;
        game.start({QStringLiteral("Goa'uld"), Conquest::Size::Medium, 5});
        game.orderTravel(game.planets()[game.flagshipPlanet()].lanes.first());
        run(game, 0.5);
        ConquestGame copy;
        QVERIFY(copy.fromJson(game.toJson()));
        QCOMPARE(copy.toJson(), game.toJson());
        QVERIFY(!copy.fromJson(QJsonObject{{"version", 2}}));
    }
};

// ================================================================

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    {
        CommandParserTest t;
        failures += QTest::qExec(&t, argc, argv);
    }
    {
        WorldTest t;
        failures += QTest::qExec(&t, argc, argv);
    }
    {
        DatabaseTest t;
        failures += QTest::qExec(&t, argc, argv);
    }
    {
        ConquestTest t;
        failures += QTest::qExec(&t, argc, argv);
    }
    return failures;
}

#include "tst_game.moc"
