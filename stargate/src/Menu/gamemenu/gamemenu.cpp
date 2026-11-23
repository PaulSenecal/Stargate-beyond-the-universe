#include "gamemenu.h"

#include <QHBoxLayout>
#include <QTimer>
#include <QVBoxLayout>

#include "commandconsole.h"
#include "hudimagepanel.h"
#include "src/core/commandmanager.h"

GameMenu::GameMenu(QWidget *parent)
    : QWidget(parent)
    , m_battle(new BattleView(this))
    , m_console(new CommandConsole(this))
    , m_commands(new CommandManager(this))
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(6, 6, 8));
    setPalette(pal);

    // --- Left column: legend + HUD screens
    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    left->addWidget(new HudImagePanel(":/hud/maps.png", this), 1);
    left->addWidget(new HudImagePanel(":/hud/navigation.png", this), 1);
    left->addWidget(new HudImagePanel(":/hud/shipStatus.png", this), 1);

    auto *top = new QHBoxLayout;
    top->setSpacing(12);
    top->addLayout(left, 27);
    top->addWidget(m_battle, 73);

    // --- Bottom row: console + weapon status
    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(12);
    bottom->addWidget(m_console, 72);
    bottom->addWidget(new HudImagePanel(":/hud/weaponStatus.png", this), 28);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    root->setSpacing(10);
    root->addLayout(top, 76);
    root->addLayout(bottom, 24);

    // --- Wiring
    connect(m_console, &CommandConsole::commandEntered, this, &GameMenu::onCommandEntered);
    connect(m_battle, &BattleView::logMessage, this, &GameMenu::onBattleLog);
    connect(m_commands, &CommandManager::commandValidated, this, [this](bool valid, const QString &message) {
        if (!valid)
            m_console->appendLine(message, CommandConsole::Tone::Critical);
    });

    if (m_commands->loadCommandsFromFile(":/commandList/commands.json")) {
        QStringList words;
        for (const QString &cmd : m_commands->getAvailableCommands())
            words << cmd.toLower();
        words << "help" << "clear";
        words.removeDuplicates();
        words.sort();
        m_console->setCompletions(words);
    } else {
        m_console->appendLine(tr("Command database unavailable"), CommandConsole::Tone::Critical);
    }

    m_console->appendLine(tr("Destiny tactical console online"), CommandConsole::Tone::Success);
    m_console->appendLine(tr("Hostile ships detected - shields up"), CommandConsole::Tone::Warning);
    m_console->appendLine(tr("Type 'help' for the command list"));

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

    const QStringList parts = command.split(' ', Qt::SkipEmptyParts);
    const QString name = parts.first().toLower();

    if (name == "help" || name == "?") {
        showHelp();
        return;
    }
    if (name == "clear") {
        m_console->clearLog();
        return;
    }
    if (!m_commands->validateCommand(command))
        return;   // error already printed through commandValidated

    dispatch(name, parts.mid(1));
}

void GameMenu::dispatch(const QString &name, const QStringList &args)
{
    auto targetIndex = [&args]() {
        bool ok = false;
        const int n = args.value(0).toInt(&ok);
        return ok ? n - 1 : -1;
    };

    if (name == "attack") {
        const int index = targetIndex();
        if (index < 0)
            m_console->appendLine(tr("Usage: attack <target number>, e.g. attack 1"), CommandConsole::Tone::Warning);
        else
            m_battle->fireMainWeapon(index);
    } else if (name == "maintower") {
        m_battle->fireMainWeapon();
    } else if (name == "pulseweapon") {
        m_battle->firePulseVolley();
    } else if (name == "secondarytower") {
        m_battle->fireSecondaryBurst();
    } else if (name == "shield") {
        const QString state = args.value(0).toLower();
        if (state == "on" || state == "up" || state == "activate" || state == "1")
            m_battle->setShieldActive(true);
        else if (state == "off" || state == "down" || state == "desactivate" || state == "deactivate" || state == "0")
            m_battle->setShieldActive(false);
        else
            m_console->appendLine(tr("Usage: shield on | shield off"), CommandConsole::Tone::Warning);
    } else if (name == "scan") {
        const int index = targetIndex();
        if (index < 0)
            logReport(m_battle->sectorReport());
        else
            m_battle->lockTarget(index);
    } else if (name == "scansector") {
        logReport(m_battle->sectorReport());
    } else if (name == "destinystatus") {
        logReport(m_battle->destinyReport());
    } else if (name == "weaponstatus") {
        logReport(m_battle->weaponReport());
    } else if (name == "repair") {
        m_battle->repairSubsystem(args.value(0));
    } else {
        m_console->appendLine(tr("Order acknowledged: %1 (not simulated yet)").arg(name),
                              CommandConsole::Tone::Info);
    }
}

void GameMenu::showHelp()
{
    QStringList names;
    for (const QString &cmd : m_commands->getAvailableCommands())
        names << cmd.toLower();
    names.removeDuplicates();
    m_console->appendLine(tr("Commands: %1").arg(names.join(", ")));
    m_console->appendLine(tr("Combat: attack <n>, scan <n>, maintower, secondarytower, pulseweapon, shield on|off"));
    m_console->appendLine(tr("Status: destinystatus, weaponstatus, scansector — repair <engines|shields|weapons|lifesupport|hull>"));
}

void GameMenu::logReport(const QString &report)
{
    for (const QString &line : report.split('\n'))
        m_console->appendLine(line);
}
