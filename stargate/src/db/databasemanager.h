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

    // Open the player's save database, stored in the user data folder
    // (macOS: ~/Library/Application Support/<app>/). On first launch it
    // is created as a copy of templatePath (the database shipped in the
    // resources), so the template itself is never modified.
    bool openRuntimeCopy(const QString &templatePath, const QString &fileName);
    QString databasePath() const;

    bool beginTransaction();
    bool commit();
    bool rollback();

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
