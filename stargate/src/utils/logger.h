#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMutex>

class Logger : public QObject {
    Q_OBJECT

public:
    enum LogLevel {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };

    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void log(LogLevel level, const QString& message);
    void setLogFile(const QString& filePath);

private:
    Logger(QObject *parent = nullptr);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool initLogFile();
    QString levelToString(LogLevel level);

    QFile m_logFile;
    QTextStream m_textStream;
    QString m_logFilePath;
    QMutex m_mutex;
};

#define LOG_DEBUG(message)    Logger::getInstance().log(Logger::Debug, message)
#define LOG_INFO(message)     Logger::getInstance().log(Logger::Info, message)
#define LOG_WARNING(message)  Logger::getInstance().log(Logger::Warning, message)
#define LOG_ERROR(message)    Logger::getInstance().log(Logger::Error, message)
#define LOG_CRITICAL(message) Logger::getInstance().log(Logger::Critical, message)

#endif // LOGGER_H
