#include "commandmanager.h"

CommandManager::CommandManager(QObject *parent)
    : QObject(parent)
{
}

bool CommandManager::loadCommandsFromFile(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Could not open file:" << filename;
        return false;
    }

    QByteArray data = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);

    if (doc.isNull()) {
        qDebug() << "Failed to parse JSON";
        return false;
    }

    m_commands = doc.object()["commands"].toObject();
    return !m_commands.isEmpty();
}

bool CommandManager::validateCommand(const QString &commandStr)
{
    QString commandName = extractCommandName(commandStr);
    QString category = extractCommandCategory(commandName);
    QStringList parameters = extractParameters(commandStr);

    if (category.isEmpty()) {
        emit commandValidated(false, "Command not found in any category");
        return false;
    }

    if (!validateParameters(category, commandName, parameters)) {
        emit commandValidated(false, "Invalid parameters for command: " + commandName);
        return false;
    }

    emit commandValidated(true, "Valid command: " + commandName);
    return true;
}

QString CommandManager::extractCommandName(const QString &commandStr) const
{
    // Extract the command name before any parameters
    return commandStr.split(" ").first().toLower();
}

QString CommandManager::extractCommandCategory(const QString &commandName) const
{
    // Check each category for the command
    QStringList categories = {"combat", "movement", "gestion"};

    for (const QString &category : categories) {
        QJsonObject categoryObj = m_commands[category].toObject()["subcommands"].toObject();
        for (auto it = categoryObj.begin(); it != categoryObj.end(); ++it) {
            if (it.value().toObject()["name"].toString().toLower() == commandName) {
                return category;
            }
        }
    }

    return QString();
}

QStringList CommandManager::extractParameters(const QString &commandStr) const
{
    QStringList parts = commandStr.split(" ", Qt::SkipEmptyParts);
    return parts.size() > 1 ? parts.mid(1) : QStringList();
}

bool CommandManager::validateParameters(const QString &category, const QString &commandName, const QStringList &params) const
{
    QJsonObject categoryObj = m_commands[category].toObject();
    QJsonObject subcommands = categoryObj["subcommands"].toObject();

    // Find the command in the subcommands
    for (auto it = subcommands.begin(); it != subcommands.end(); ++it) {
        QJsonObject cmdObj = it.value().toObject();
        if (cmdObj["name"].toString().toLower() == commandName) {
            // Check if command has parameters
            QJsonArray expectedParams = cmdObj["params"].toArray();
            return params.size() == expectedParams.size();
        }
    }

    return false;
}

QStringList CommandManager::getAvailableCommands() const
{
    QStringList commands;
    QStringList categories = {"combat", "movement", "gestion"};

    for (const QString &category : categories) {
        QJsonObject categoryObj = m_commands[category].toObject();
        QJsonObject subcommands = categoryObj["subcommands"].toObject();

        for (auto it = subcommands.begin(); it != subcommands.end(); ++it) {
            commands << it.value().toObject()["name"].toString();
        }
    }

    return commands;
}
