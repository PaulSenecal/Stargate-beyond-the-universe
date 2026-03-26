#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

// ================================================================
//  DatabaseManager
//  Singleton — one SQLite connection for the whole application.
//  Usage:
//    DatabaseManager::instance().init("stargate.db");
//    QSqlQuery q = DatabaseManager::instance().query("SELECT ...");
// ================================================================

class DatabaseManager
{
public:
    // Singleton access
    static DatabaseManager &instance();

    // Open the DB and run schema if needed
    bool init(const QString &dbPath);

    // Execute a raw SQL string (DDL, INSERT, UPDATE, DELETE)
    // Returns true on success
    bool exec(const QString &sql);

    // Prepare and return a ready-to-execute QSqlQuery
    QSqlQuery query(const QString &sql);

    // Handy access to the last error
    QString lastError() const;

    bool isOpen() const;

private:
    DatabaseManager() = default;
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager &)            = delete;//interditnla copie
    DatabaseManager &operator=(const DatabaseManager &) = delete; // ointerdit laffectaiton

    bool loadSchema(const QString &schemaPath);
    bool tableExists(const QString &tableName);

    QSqlDatabase m_db;
};

#endif // DATABASEMANAGER_H
