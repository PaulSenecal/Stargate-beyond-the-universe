#include "DatabaseManager.h"

#include <QFile>
#include <QTextStream>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

// ================================================================
//  Singleton
// ================================================================

DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager s_instance;
    return s_instance;
}

DatabaseManager::~DatabaseManager()
{
    if (m_db.isOpen()) {
        m_db.close();
        qDebug() << "[DB] Connection closed.";
    }
}

// ================================================================
//  init
//  Opens the SQLite file and bootstraps the schema on first run.
//  Call once from main() or Game::init().
// ================================================================

bool DatabaseManager::init(const QString &dbPath)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "stargate");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qCritical() << "[DB] Failed to open:" << m_db.lastError().text();
        return false;
    }

    // SQLite: foreign keys are disabled by default — always enable them
    if (!exec("PRAGMA foreign_keys = ON;")) {
        qCritical() << "[DB] Could not enable foreign keys.";
        return false;
    }

    // Bootstrap: create tables only if they don't exist yet
    if (!tableExists("Ship")) {
        qDebug() << "[DB] First run — loading schema...";
        // Inline schema bootstrap: in a real project you would load
        // stargate_full_schema.sql from resources (see loadSchema()).
        // For now we trust the schema has been applied manually or via
        // loadSchema(":/sql/stargate_full_schema.sql").
        qWarning() << "[DB] Schema not found. Run loadSchema() or apply SQL manually.";
    } else {
        qDebug() << "[DB] Schema OK.";
    }

    qDebug() << "[DB] Connected to" << dbPath;
    return true;
}

// ================================================================
//  loadSchema
//  Reads a .sql file and executes each statement.
//  Statements are split on ';' — works for our schema with no
//  embedded semicolons inside string literals.
// ================================================================

bool DatabaseManager::loadSchema(const QString &schemaPath)
{
    QFile file(schemaPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "[DB] Cannot open schema file:" << schemaPath;
        return false;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    // Split on ';' and execute each non-empty statement
    QStringList statements = content.split(';', Qt::SkipEmptyParts);
    for (const QString &raw : std::as_const(statements)) {
        QString stmt = raw.trimmed();
        if (stmt.isEmpty() || stmt.startsWith("--"))
            continue;
        if (!exec(stmt)) {
            qCritical() << "[DB] Schema error on statement:" << stmt.left(80);
            return false;
        }
    }

    qDebug() << "[DB] Schema loaded from" << schemaPath;
    return true;
}

// ================================================================
//  exec  — fire-and-forget SQL
// ================================================================

bool DatabaseManager::exec(const QString &sql)
{
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        qWarning() << "[DB] exec() failed:" << q.lastError().text()
        << "\n  SQL:" << sql.left(120);
        return false;
    }
    return true;
}

// ================================================================
//  query  — returns a prepared QSqlQuery ready for binding
// ================================================================

QSqlQuery DatabaseManager::query(const QString &sql)
{
    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        qWarning() << "[DB] prepare() failed:" << q.lastError().text()
        << "\n  SQL:" << sql.left(120);
    }
    return q;
}

// ================================================================
//  Helpers
// ================================================================

QString DatabaseManager::lastError() const
{
    return m_db.lastError().text();
}

bool DatabaseManager::isOpen() const
{
    return m_db.isOpen();
}

bool DatabaseManager::tableExists(const QString &tableName)
{
    QSqlQuery q = query("SELECT name FROM sqlite_master "
                        "WHERE type='table' AND name=:name;");
    q.bindValue(":name", tableName);
    if (q.exec() && q.next())
        return true;
    return false;
}