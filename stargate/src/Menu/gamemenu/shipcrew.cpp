#include "shipcrew.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>
#include <QWidget>
#include <QVector3D>

#include "commandconsole.h"
#include "shipplandialog.h"
#include "src/core/gameconfig.h"
#include "src/db/dbtext.h"
#include "src/render/battleview.h"

namespace {

QString roomName(const Interior::Compartment &c)
{
    return QCoreApplication::translate("Interior", c.name.toUtf8().constData());
}

double now()
{
    return double(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
}

} // namespace

ShipCrew::ShipCrew(Interior *interior, BattleView *battle, CommandConsole *console, QWidget *parent)
    : m_interior(interior)
    , m_battle(battle)
    , m_console(console)
    , m_parent(parent)
{
}

QStringList ShipCrew::commands()
{
    return {"plan", "crew", "jobs", "cancel", "seal", "unseal", "vent", "unlock", "install", "blueprints",
            "defend", "repair", "board", "dock", "sabotage"};
}

void ShipCrew::say(const QString &text, int tone)
{
    const CommandConsole::Tone t = tone == 1 ? CommandConsole::Tone::Success
                                 : tone == 2 ? CommandConsole::Tone::Warning
                                 : tone == 3 ? CommandConsole::Tone::Critical
                                             : CommandConsole::Tone::Info;
    m_console->appendLine(text, t);
}

void ShipCrew::flushMessages()
{
    for (const Interior::Message &m : m_interior->takeMessages())
        say(m.text, m.tone);
}

// ================================================================
//  Orders
// ================================================================

bool ShipCrew::handle(const QString &first, const QStringList &words)
{
    const QString arg = words.mid(1).join(' ');
    QString error;
    auto failed = [this, &error] { say(error, 2); };

    if (first == "repair") {
        repair(arg);
        return true;
    }
    if (first == "plan" || first == "ship") {
        // 'plan T2' or 'plan 2': the enemy ship, as far as the sensors see it
        QString target = arg.trimmed();
        if (target.startsWith('t', Qt::CaseInsensitive))
            target = target.mid(1);
        bool number = false;
        const int enemy = target.toInt(&number);
        if (number && enemy >= 1) {
            showPlan(enemy - 1);
            return true;
        }
        showPlan(-1);
        if (!arg.isEmpty()) {
            const int room = m_interior->find(arg);
            for (const QString &line : ShipPlanDialog::describe(*m_interior, room))
                m_console->appendLine(line);
        }
        return true;
    }
    if (first == "crew") {
        const QString sub = words.value(1).toLower();
        if (sub == "rest") {
            if (m_interior->rest(words.mid(2).join(' '), &error))
                say(tr("%1 goes to rest").arg(words.mid(2).join(' ')), 0);
            else
                failed();
        } else if (sub == "rotate") {
            say(tr("Rotation: %1 tired crew member(s) sent to rest").arg(m_interior->rotate()), 0);
        } else {
            showCrew();
        }
        return true;
    }
    if (first == "jobs") {
        if (m_interior->jobs().isEmpty())
            say(tr("No repair job under way"), 0);
        for (const Interior::Job &j : m_interior->jobs()) {
            QStringList names;
            for (int w : j.workers)
                names << m_interior->people()[w].name;
            say(tr("#%1 %2 %3 - %4% - %5")
                    .arg(j.id)
                    .arg(j.kind, j.target)
                    .arg(qRound(100.0f * (1.0f - j.work / std::max(0.01f, j.total))))
                    .arg(names.isEmpty() ? tr("waiting for free hands") : names.join(", ")),
                0);
        }
        return true;
    }
    if (first == "cancel") {
        if (!m_interior->cancelJob(arg.toInt()))
            say(tr("No job #%1 ('jobs' for the list)").arg(arg), 2);
        return true;
    }
    if (first == "seal" || first == "unseal") {
        if (!m_interior->setSealed(arg, first == "seal", &error))
            failed();
        else if (first == "seal" && onQuest)
            onQuest("SEAL");
        flushMessages();
        return true;
    }
    if (first == "vent") {
        if (!m_interior->vent(arg, &error))
            failed();
        flushMessages();
        return true;
    }
    if (first == "unlock") {
        const int room = m_interior->find(arg);
        if (room < 0 || !m_interior->compartments()[room].locked) {
            m_interior->orderUnlock(arg, &error);
            failed();
            return true;
        }
        if (pay && !pay(m_interior->compartments()[room].unlockCost))
            return true;
        if (m_interior->orderUnlock(arg, &error) < 0)
            failed();
        else
            say(tr("Teams sent to open %1").arg(arg), 0);
        return true;
    }
    if (first == "install") {
        const InteriorDef::Module *module = InteriorDef::module(words.value(1));
        const QString room = words.mid(2).join(' ');
        // Check everything before paying
        if (!module || !m_interior->blueprints().contains(module->id)) {
            m_interior->orderInstall(words.value(1), room, &error);
            failed();
            return true;
        }
        Interior probe = *m_interior;
        if (probe.orderInstall(module->id, room, &error) < 0) {
            failed();
            return true;
        }
        if ((!pay || pay(module->cost)) && m_interior->orderInstall(module->id, room, &error) >= 0)
            say(tr("Installation of the %1 started")
                    .arg(QCoreApplication::translate("Interior", module->name.toUtf8().constData())),
                0);
        return true;
    }
    if (first == "blueprints") {
        if (m_interior->blueprints().isEmpty())
            say(tr("No blueprint yet - explore Ancient sites, open the archives, capture ships"), 0);
        for (const QString &id : m_interior->blueprints())
            if (const InteriorDef::Module *m = InteriorDef::module(id)) {
                QStringList cost;
                for (const auto &[resource, quantity] : m->cost)
                    cost << QStringLiteral("%1 %2").arg(quantity).arg(DbText::tr(resource));
                say(QStringLiteral("%1 (%2): %3 - %4")
                        .arg(QCoreApplication::translate("Interior", m->name.toUtf8().constData()), m->id,
                             QCoreApplication::translate("Interior", m->effect.toUtf8().constData()), cost.join(", ")),
                    0);
            }
        return true;
    }
    if (first == "defend") {
        if (!m_interior->defend(arg, &error))
            failed();
        flushMessages();
        return true;
    }
    if (first == "dock") {
        bool ok = false;
        const int target = words.value(1).toInt(&ok);
        if (!ok || target < 1)
            say(tr("Usage: dock <n> - a ship of your hangar latches onto mothership n (a way in for a team)"), 2);
        else if (!m_battle->dockShuttle(target - 1, &error))
            say(tr("Cannot dock: %1").arg(error), 2);
        return true;
    }
    if (first == "board" || first == "sabotage") {
        // board <n> [gate]   /   sabotage <n> <room> [gate]
        bool ok = false;
        const int target = words.value(1).toInt(&ok);
        if (!ok || target < 1)
            return first == "board" ? (say(tr("Usage: board <n> [gate] - take a crippled mothership"), 2), true)
                                    : false;                 // 'sabotage yard <planet>': the conquest's order
        const bool viaGate = words.last().compare("gate", Qt::CaseInsensitive) == 0;
        const QString room = first == "sabotage" ? words.value(2) : QString();
        if (first == "sabotage" && (room.isEmpty() || room.compare("gate", Qt::CaseInsensitive) == 0)) {
            say(tr("Usage: sabotage <n> <room> [gate] - 'plan %1' shows its rooms").arg(target), 2);
            return true;
        }
        if (now() < m_teamBusyUntil) {
            say(tr("The away team is not back yet (%1 s)").arg(qRound(m_teamBusyUntil - now())), 2);
            return true;
        }
        QString why;
        const QString way = wayAboard(target - 1, viaGate, &why);
        if (way.isEmpty()) {
            say(tr("No way aboard T%1: %2").arg(target).arg(why), 2);
            return true;
        }
        if (first == "board" && !m_battle->canBoard(target - 1, &error)) {
            say(tr("Cannot board: %1").arg(error), 2);
            return true;
        }
        auto go = [this, first, target, room, way] {
            if (first == "board")
                board(target - 1, way);
            else
                sabotage(target - 1, room, way);
        };
        if (way == "gate") {
            // Dialing the other ship's gate takes a few seconds
            const QString cls = m_battle->enemyClass(target - 1);
            say(tr("Dialing the gate of T%1 %2... the team waits in the gate room").arg(target).arg(cls), 0);
            m_teamBusyUntil = now() + 8.0;
            const int delay = qRound(5000.0 / std::max(0.25f, gameConfig().gameSpeed));
            QPointer<CommandConsole> console = m_console;
            QTimer::singleShot(delay, m_parent, [this, console, target, cls, go] {
                if (!console)
                    return;
                if (m_battle->enemyClass(target - 1) != cls) {
                    say(tr("The gate of the target does not answer anymore"), 2);
                    return;
                }
                say(tr("Chevron 7 locked - the team goes through"), 0);
                go();
            });
        } else {
            go();
        }
        return true;
    }
    return false;
}

void ShipCrew::repair(const QString &whatTyped)
{
    const QString what = whatTyped.trimmed().toLower();
    if (what.isEmpty()) {
        say(tr("Usage: repair <system|hull|room> ('help repair')"), 2);
        return;
    }
    const QString key = BattleView::systemKey(what);
    if (!key.isEmpty() && m_battle->systemHealth(key) >= 99.5f) {
        say(tr("%1 already at 100%").arg(key), 0);
        return;
    }
    Cost cost;
    if (!key.isEmpty() && systemCost) {
        cost = systemCost(key);
    } else if (const int room = m_interior->find(what); room >= 0) {
        const Interior::Compartment &c = m_interior->compartments()[room];
        if (c.fire <= 0.0f && c.breach)
            cost.append({QStringLiteral("minerals"), 3});        // hull plates
        else if (c.fire <= 0.0f && !c.leak && c.integrity < 99.0f)
            cost.append({QStringLiteral("minerals"), 2});
    }
    QString error;
    const int job = m_interior->orderRepair(key.isEmpty() ? what : key, &error);
    if (job < 0) {
        say(tr("Cannot repair: %1").arg(error), 2);
        return;
    }
    if (!cost.isEmpty() && pay && !pay(cost)) {
        m_interior->cancelJob(job);
        return;
    }
    if (!cost.isEmpty()) {
        QStringList used;
        for (const auto &[resource, quantity] : cost)
            used << QStringLiteral("%1 %2").arg(quantity).arg(DbText::tr(resource));
        say(tr("Resources used: %1").arg(used.join(", ")), 0);
    }
    const Interior::Job *created = nullptr;
    for (const Interior::Job &j : m_interior->jobs())
        if (j.id == job)
            created = &j;
    if (onQuest)
        onQuest("JOB");
    say(created && created->workers.isEmpty()
            ? tr("Repair job #%1 queued - no free hands yet ('jobs', 'crew rotate')").arg(job)
            : tr("Repair job #%1 started (%2 worker(s))").arg(job).arg(created ? created->workers.size() : 0),
        0);
    if (!key.isEmpty() && onQuest)
        onQuest(QStringLiteral("REPAIR:") + key);
}

void ShipCrew::showCrew()
{
    say(tr("Crew %1 - morale %2% - free workers %3")
            .arg(crewCount ? crewCount() : m_interior->crewCount())
            .arg(qRound(m_interior->morale()))
            .arg(m_interior->freeWorkers()),
        0);
    for (const Interior::Person &p : m_interior->people()) {
        const QString state = p.injury > 0.0f ? tr("INJURED (%1 s)").arg(qRound(p.injury))
                            : p.resting        ? tr("resting")
                            : p.job >= 0       ? tr("repair job #%1").arg(p.job)
                                               : tr("available");
        const int room = m_interior->find(p.location);
        const QString where = room >= 0 ? roomName(m_interior->compartments()[room]) : p.location;
        m_console->appendLine(tr("%1%2 - fatigue %3% - %4 - %5")
                                  .arg(p.name)
                                  .arg(p.bonus.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(p.bonus))
                                  .arg(qRound(p.fatigue))
                                  .arg(state, where),
                              p.injury > 0.0f ? CommandConsole::Tone::Warning : CommandConsole::Tone::Info);
    }
    say(tr("Orders: crew rest <name>, crew rotate (tired ones rest), jobs, cancel <job>"), 0);
    if (onQuest)
        onQuest("CREW");
}

void ShipCrew::showPlan(int enemyIndex)
{
    if (enemyIndex >= 0) {
        const std::shared_ptr<Interior> interior = m_battle->enemyInterior(enemyIndex);
        if (!interior) {
            say(tr("No plan of T%1 (an automated or small ship, or no such contact)").arg(enemyIndex + 1), 2);
            return;
        }
        auto *dialog = new ShipPlanDialog(interior, {},
                                          tr("T%1 %2 - plan seen by the sensors").arg(enemyIndex + 1)
                                              .arg(m_battle->enemyClass(enemyIndex)),
                                          m_parent);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        say(tr("Plan of T%1: aim at a room with 'target %1 <system>', send a team with 'sabotage %1 <room>'")
                .arg(enemyIndex + 1),
            0);
        return;
    }
    if (onQuest)
        onQuest("PLAN");
    if (!m_planDialog)
        m_planDialog = new ShipPlanDialog(std::shared_ptr<const Interior>(m_interior, [](const Interior *) {}),
                                          [this] {
                                              QHash<QString, float> health;
                                              for (const QString &key : BattleView::systemKeys())
                                                  health.insert(key, m_battle->systemHealth(key));
                                              return health;
                                          },
                                          m_interior->style().isEmpty() ? QString() : tr("Flagship - plan"), m_parent);
    m_planDialog->show();
    m_planDialog->raise();
}

// ================================================================
//  Teams aboard an enemy ship
// ================================================================

QString ShipCrew::wayAboard(int enemyIndex, bool wantGate, QString *why) const
{
    const std::shared_ptr<Interior> target = m_battle->enemyInterior(enemyIndex);
    if (!target) {
        *why = tr("no crew to fight there (automated or small ship)");
        return {};
    }
    if (wantGate) {
        if (!m_interior->hasGate())
            *why = tr("no Stargate aboard your ship");
        else if (!target->hasGate())
            *why = tr("that ship carries no Stargate");
        else
            return QStringLiteral("gate");
        return {};
    }
    if (m_battle->shuttleDocked(enemyIndex))
        return QStringLiteral("shuttle");
    if (m_interior->hasBeam()) {
        if (m_battle->enemyShieldDown(enemyIndex))
            return QStringLiteral("beam");
        *why = tr("its shield blocks your transport beam - bring it down");
        return {};
    }
    *why = m_interior->hasGate() && target->hasGate()
               ? tr("dock a shuttle on it ('dock %1') or use the gate ('... gate')").arg(enemyIndex + 1)
               : tr("dock a shuttle on it first ('dock %1')").arg(enemyIndex + 1);
    return {};
}

void ShipCrew::board(int enemyIndex, const QString &way)
{
    const std::shared_ptr<Interior> target = m_battle->enemyInterior(enemyIndex);
    // Their soldiers left standing make it harder
    int soldiers = 0;
    if (target)
        for (const Interior::Person &p : target->people())
            soldiers += p.role == InteriorDef::Role::Military && p.injury <= 0.0f ? 1 : 0;
    float chance = m_interior->boardingChance() / (1.0f + 0.2f * soldiers);
    if (way == "shuttle")
        chance += 0.05f;            // they come in from behind
    chance = std::clamp(chance, 0.05f, 0.9f);
    say(tr("Boarding team away by %1 (success chance %2%)")
            .arg(way == "gate" ? tr("the gate") : way == "beam" ? tr("transport beam") : tr("the docked ship"))
            .arg(qRound(chance * 100)),
        0);
    m_teamBusyUntil = now() + 20.0;
    if (onQuest)
        onQuest("BOARD");
    if (QRandomGenerator::global()->generateDouble() < chance) {
        m_interior->changeMorale(5.0f);
        const QString faction = m_battle->captureEnemy(enemyIndex);
        if (onCapture)
            onCapture(faction);
    } else {
        m_interior->injureRandom(2, 200.0f);
        m_interior->changeMorale(-4.0f);
        say(tr("The boarding failed - the team falls back with wounded"), 3);
    }
    flushMessages();
}

void ShipCrew::sabotage(int enemyIndex, const QString &roomTyped, const QString &way)
{
    const std::shared_ptr<Interior> target = m_battle->enemyInterior(enemyIndex);
    if (!target)
        return;
    const int room = target->find(roomTyped);
    if (room < 0) {
        say(tr("No room '%1' on T%2 ('plan %2')").arg(roomTyped).arg(enemyIndex + 1), 2);
        return;
    }
    const Interior::Compartment &c = target->compartments()[room];
    int guards = c.boarders;
    for (const Interior::Person &p : target->people())
        guards += p.role == InteriorDef::Role::Military && p.injury <= 0.0f && p.location == c.id ? 1 : 0;
    float chance = 0.6f - 0.15f * guards;
    if (m_interior->bonusActive("sabotage"))
        chance += 0.15f;            // SG-1
    if (m_interior->bonusActive("boarding"))
        chance += 0.1f;
    chance = std::clamp(chance, 0.1f, 0.9f);
    m_teamBusyUntil = now() + 15.0;
    if (QRandomGenerator::global()->generateDouble() < chance) {
        m_battle->damageEnemyRoom(enemyIndex, c.id, 55.0f);
        say(tr("Sabotage in the %1 of T%2 (team sent by %3): charges set, the room is on fire")
                .arg(roomName(c)).arg(enemyIndex + 1)
                .arg(way == "gate" ? tr("the gate") : way == "beam" ? tr("transport beam") : tr("the docked ship")),
            1);
    } else {
        m_interior->injureRandom(1, 160.0f);
        say(tr("The sabotage team was spotted in the %1 of T%2 and falls back").arg(roomName(c)).arg(enemyIndex + 1), 2);
    }
    flushMessages();
}

// ================================================================
//  Simulation and bonuses
// ================================================================

Interior::Effects ShipCrew::tick(float seconds)
{
    const bool battle = m_battle->hostileCount() > 0;
    const Interior::Effects e = m_interior->tick(seconds, m_battle->systemHealth("lifesupport"), battle);
    for (const auto &[key, amount] : e.systemDamage) {
        if (!BattleView::systemKeys().contains(key))
            continue;
        const float health = std::max(0.0f, m_battle->systemHealth(key) - amount);
        m_battle->setSubsystemHealth(key, health);
        if (onSystemChanged)
            onSystemChanged(key, health);
    }
    for (const auto &[key, amount] : e.systemRepair)
        m_battle->repairBy(key, amount);
    if (e.hullRepair > 0.0f)
        m_battle->repairBy(QStringLiteral("hull"), e.hullRepair);
    // Burning compartments on the 3D hull
    QList<QVector3D> fires;
    for (const Interior::Compartment &c : m_interior->compartments())
        if (c.fire > 0.0f)
            fires << QVector3D(float(c.rect.center().x()), 0.3f, float(c.rect.center().y()));
    m_battle->setFireSpots(fires);
    flushMessages();
    return e;
}

void ShipCrew::applyBonuses(float damage, float shieldRegen, float accuracy, int power)
{
    const Interior &in = *m_interior;
    m_battle->setPowerLoss(in.powerLoss());
    m_battle->setPowerBonus(power + (in.hasModule("naquadah_generator") ? 15 : 0));
    m_battle->setDamageBonus(damage * (in.bonusActive("combat") ? 1.1f : 1.0f)
                             * (in.hasModule("goauld_cannon") ? 1.15f : 1.0f));
    m_battle->setShieldRegenBonus(shieldRegen * (in.hasModule("asgard_shield") ? 1.3f : 1.0f));
    m_battle->setAccuracyBonus(accuracy * (in.bonusActive("ancient") ? 1.05f : 1.0f)
                               * (in.hasModule("sensor_suite") ? 1.1f : 1.0f)
                               * (in.hasCapability("command") ? 1.0f : 0.9f));
}

// ================================================================
//  Completion
// ================================================================

QStringList ShipCrew::argumentChoices(const QStringList &before) const
{
    const QString command = before.value(0).toLower();
    const int position = int(before.size());
    QStringList rooms;
    for (const Interior::Compartment &c : m_interior->compartments())
        rooms << c.id;
    QStringList targets;
    for (int i = 1; i <= m_battle->hostileCount(); ++i)
        targets << QString::number(i);
    if (position == 1) {
        if (command == "repair")
            return BattleView::systemKeys() + QStringList{"hull"} + rooms;
        if (command == "seal" || command == "unseal" || command == "vent" || command == "defend")
            return rooms;
        if (command == "plan") {
            QStringList choices = rooms;
            for (const QString &t : targets)
                choices << QStringLiteral("T") + t;
            return choices;
        }
        if (command == "unlock") {
            QStringList closed;
            for (const Interior::Compartment &c : m_interior->compartments())
                if (c.locked)
                    closed << c.id;
            return closed;
        }
        if (command == "install")
            return m_interior->blueprints();
        if (command == "crew")
            return {"rest", "rotate"};
        if (command == "cancel") {
            QStringList ids;
            for (const Interior::Job &j : m_interior->jobs())
                ids << QString::number(j.id);
            return ids;
        }
        if (command == "board" || command == "dock" || command == "sabotage")
            return targets;
    }
    if (position == 2) {
        if (command == "install")
            return rooms;
        if (command == "crew" && before.value(1).toLower() == "rest") {
            QStringList names;
            for (const Interior::Person &p : m_interior->people())
                if (!p.generic)
                    names << p.id;
            return names;
        }
        if (command == "board")
            return {"gate"};
        if (command == "sabotage") {
            // The rooms of that enemy ship
            QStringList enemyRooms;
            if (const auto target = m_battle->enemyInterior(before.value(1).toInt() - 1))
                for (const Interior::Compartment &c : target->compartments())
                    enemyRooms << c.id;
            return enemyRooms;
        }
    }
    if (position == 3 && command == "sabotage")
        return {"gate"};
    return {};
}
