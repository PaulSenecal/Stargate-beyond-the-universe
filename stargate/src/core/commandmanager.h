#pragma once

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLineEdit>
#include <QString>
#include <QFile>
#include <QDebug>

class CommandManager : public QObject
{
    Q_OBJECT

public:
    explicit CommandManager(QObject *parent = nullptr);
    bool loadCommandsFromFile(const QString &filename);
    bool validateCommand(const QString &commandStr);
    QStringList getAvailableCommands() const;

private:
    QJsonObject m_commands;
    QString extractCommandName(const QString &commandStr) const;
    QString extractCommandCategory(const QString &commandName) const;
    QStringList extractParameters(const QString &commandStr) const;
    bool validateParameters(const QString &category, const QString &command, const QStringList &params) const;

signals:
    void commandValidated(bool valid, const QString &message);
};
