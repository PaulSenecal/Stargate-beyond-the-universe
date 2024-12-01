#include "logger.h"

Logger::Logger(QObject *parent) : QObject(parent) {
    m_logFilePath = QDir::currentPath() + "/application.log";
    initLogFile();
}

Logger::~Logger() {
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void Logger::setLogFile(const QString& filePath) {
    QMutexLocker locker(&m_mutex);
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
    m_logFilePath = filePath;
    initLogFile();
}

bool Logger::initLogFile() {
    QDir().mkpath(QFileInfo(m_logFilePath).path());
    m_logFile.setFileName(m_logFilePath);

    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qDebug() << "Impossible d'ouvrir le fichier de log:" << m_logFilePath;
        return false;
    }

    m_textStream.setDevice(&m_logFile);
    return true;
}

QString Logger::levelToString(LogLevel level) {
    switch (level) {
    case Debug:    return "DEBUG";
    case Info:     return "INFO";
    case Warning:  return "WARNING";
    case Error:    return "ERROR";
    case Critical: return "CRITICAL";
    default:       return "UNKNOWN";
    }
}

void Logger::log(LogLevel level, const QString& message) {
    QMutexLocker locker(&m_mutex);

    QString timestamp = QDateTime::currentDateTime().toString("dd/MM/yyyy hh:mm:ss.zzz");
    QString logEntry = QString("[%1] [%2] %3\n")
                           .arg(timestamp)
                           .arg(levelToString(level))
                           .arg(message);

    m_textStream << logEntry;
    m_textStream.flush();

    // Afficher également dans la console
    qDebug().noquote() << logEntry.trimmed();
}


