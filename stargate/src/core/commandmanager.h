#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// ================================================================
//  CommandManager
//  Loads the command catalogue (commands.json) and parses what the
//  player types:
//   - names may contain spaces ("Close door a20"); the compact form
//     works too ("closedoor a20"); matching is case-insensitive
//   - a name can exist in several categories / variants ("activate",
//     "scan"): the input is valid if one variant accepts it
//   - parameters are checked against their definition (int, number,
//     allowed values, regex pattern, optional)
//   - errors are explicit: usage, wrong value, "did you mean …?"
// ================================================================

struct CommandParam
{
    QString name;
    QString type = QStringLiteral("text");   // text | int | number
    QStringList values;                       // allowed words (lower case)
    QString pattern;                          // regular expression
    bool optional = false;
    bool hasMin = false, hasMax = false;
    double min = 0, max = 0;
};

struct CommandDefinition
{
    QString key;          // JSON key, e.g. "close_door"
    QString name;         // display name, e.g. "Close door"
    QString category;     // combat | movement | gestion
    QList<CommandParam> params;

    QString usage() const;    // e.g. "close door <door>"
};

struct ParsedCommand
{
    bool valid = false;
    QString name;         // compact lower-case name, e.g. "closedoor"
    QString category;
    QStringList args;     // validated; allowed words normalized to lower case
    QString error;
};

class CommandManager : public QObject
{
    Q_OBJECT

public:
    explicit CommandManager(QObject *parent = nullptr);

    bool loadCommandsFromFile(const QString &filename);

    ParsedCommand parse(const QString &input) const;

    // Kept for compatibility: parses, emits commandValidated and
    // returns whether the input is valid
    bool validateCommand(const QString &commandStr);

    QStringList getAvailableCommands() const;            // display names
    QStringList usages(const QString &category = QString()) const;
    QStringList categories() const;

    // Tab completion: every full line that can follow `input`
    // (command names, then allowed parameter values). `dynamicValues`
    // adds values known only at runtime for the first parameter of a
    // command, keyed by compact name (e.g. "stargate" -> gate addresses).
    QStringList completions(const QString &input,
                            const QHash<QString, QStringList> &dynamicValues = {}) const;

signals:
    void commandValidated(bool valid, const QString &message);

private:
    QString checkParameters(const CommandDefinition &def, QStringList *args) const;
    QString suggestion(const QString &word) const;

    QList<CommandDefinition> m_definitions;
};
