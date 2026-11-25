#ifndef COOPSESSION_H
#define COOPSESSION_H

#include <QObject>
#include <QPointer>
#include <QStringList>

#include "coopprotocol.h"

class QTcpServer;
class QTcpSocket;

// ================================================================
//  CoopHost
//  Runs in the host's game: waits for one co-pilot on a TCP port,
//  sends it the console and the ship status, receives its commands
//  (already checked against its station).
// ================================================================

class CoopHost : public QObject
{
    Q_OBJECT

public:
    explicit CoopHost(QObject *parent = nullptr);
    ~CoopHost() override;

    // localOnly: this machine only (tests), otherwise the local network
    bool listen(quint16 port, CoopProtocol::Role guestRole, QString *error = nullptr, bool localOnly = false);
    void stop();
    bool isListening() const;
    bool hasGuest() const { return m_guest != nullptr; }
    quint16 port() const;
    CoopProtocol::Role guestRole() const { return m_role; }
    // Addresses of this machine to give to the co-pilot
    static QStringList localAddresses();

    void sendLog(const QString &text, int tone);
    void sendStatus(const QStringList &lines);

signals:
    void guestJoined(const QString &address);
    void guestLeft();
    void guestCommand(const QString &text);      // allowed for its station
    void guestDenied(const QString &text);       // not its station

private:
    void onConnection();
    void onReadyRead();
    void send(const QJsonObject &message);

    QTcpServer *m_server = nullptr;
    QPointer<QTcpSocket> m_guest;
    QByteArray m_buffer;
    CoopProtocol::Role m_role = CoopProtocol::Role::Engineering;
};

// ================================================================
//  CoopClient
//  The co-pilot's side: connects to the host, sends commands, receives
//  the console lines and the status.
// ================================================================

class CoopClient : public QObject
{
    Q_OBJECT

public:
    explicit CoopClient(QObject *parent = nullptr);

    void connectTo(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    void sendCommand(const QString &text);

signals:
    void connected();
    void welcomed(const QString &role, const QStringList &commands);
    void log(const QString &text, int tone);
    void status(const QStringList &lines);
    void denied(const QString &text);
    void disconnected(const QString &reason);

private:
    void onReadyRead();

    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
};

#endif // COOPSESSION_H
