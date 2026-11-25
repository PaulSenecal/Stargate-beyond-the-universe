#ifndef SHIPCREW_H
#define SHIPCREW_H

#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <utility>

#include "src/core/interior.h"
#include "commandconsole.h"

class BattleView;
class ShipPlanDialog;
class QWidget;

// ================================================================
//  ShipCrew
//  The console orders for the inside of the player's ship, shared by
//  the campaign (the Destiny) and the conquest (the faction flagship):
//  plan, repairs as crew jobs, doors, crew rest, modules, defence, and
//  the actions against an enemy ship: its plan, docking a shuttle,
//  boarding it, sabotaging one of its rooms.
//
//  A team goes aboard an enemy ship:
//   - by teleportation (Asgard and Tau'ri beams, Wraith culling beam)
//     once its shield is down;
//   - through a shuttle latched on it ('dock <n>');
//   - through the Stargate when both ships carry one: the address is
//     dialed, it takes a few seconds.
//  The host (game screen) gives the payment, the costs and the reactions.
// ================================================================

class ShipCrew
{
    Q_DECLARE_TR_FUNCTIONS(GameMenu)

public:
    using Cost = QList<std::pair<QString, int>>;

    ShipCrew(Interior *interior, BattleView *battle, CommandConsole *console, QWidget *parent);

    // --- Host hooks
    std::function<bool(const Cost &)> pay;                  // false: prints what is missing
    std::function<Cost(const QString &system)> systemCost;  // repair cost of a system
    std::function<void(const QString &type)> onQuest;       // PLAN, JOB, SEAL, CREW, BOARD
    std::function<void(const QString &faction)> onCapture;  // an enemy ship taken
    std::function<void(const QString &key, float health)> onSystemChanged;
    std::function<int()> crewCount;

    // Handles the order; false if it is not one of ours
    bool handle(const QString &first, const QStringList &words);
    // Simulation of the inside: systems, hull, fires drawn on the hull.
    // Returns the effects for the host (food, crew lost, fires put out)
    Interior::Effects tick(float seconds);
    // Bonuses of the crew and the modules, on top of the host's ones
    void applyBonuses(float damage = 1.0f, float shieldRegen = 1.0f, float accuracy = 1.0f, int power = 0);
    void flushMessages();
    // What needs an action inside the ship and in the battle (alert strip)
    QList<CommandConsole::Alert> alerts() const;
    // Completion of our orders' arguments
    QStringList argumentChoices(const QStringList &before) const;
    static QStringList commands();

private:
    void repair(const QString &what);
    void showCrew();
    void showPlan(int enemyIndex);
    // How a team can go to enemy n ("" if it cannot; *why says why)
    QString wayAboard(int enemyIndex, bool wantGate, QString *why) const;
    void board(int enemyIndex, const QString &way);
    void sabotage(int enemyIndex, const QString &room, const QString &way);
    void say(const QString &text, int tone = 1);

    Interior *m_interior = nullptr;
    BattleView *m_battle = nullptr;
    CommandConsole *m_console = nullptr;
    QWidget *m_parent = nullptr;
    QPointer<ShipPlanDialog> m_planDialog;
    double m_teamBusyUntil = 0;     // the away team is not back yet
};

#endif // SHIPCREW_H
