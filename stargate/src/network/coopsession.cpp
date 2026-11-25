#include "coopsession.h"

#include <QJsonArray>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

using namespace CoopProtocol;

// ================================================================
//  CoopHost
// ================================================================

CoopHost::CoopHost(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, &CoopHost::onConnection);
}

CoopHost::~CoopHost()
{
    stop();
}

bool CoopHost::listen(quint16 port, Role guestRole, QString *error, bool localOnly)
{
    stop();
    m_role = guestRole;
    if (!m_server->listen(localOnly ? QHostAddress::LocalHost : QHostAddress::Any, port)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    return true;
}

void CoopHost::stop()
{
    if (m_guest) {
        send(QJsonObject{{"type", "bye"}});
        m_guest->flush();
        m_guest->disconnectFromHost();
        m_guest = nullptr;
    }
    m_server->close();
}

bool CoopHost::isListening() const
{
    return m_server->isListening();
}

quint16 CoopHost::port() const
{
    return m_server->serverPort();
}

QStringList CoopHost::localAddresses()
{
    QStringList addresses;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
            addresses << a.toString();
    if (addresses.isEmpty())
        addresses << QStringLiteral("127.0.0.1");
    return addresses;
}

void CoopHost::onConnection()
{
    while (QTcpSocket *socket = m_server->nextPendingConnection()) {
        if (m_guest) {
            // One co-pilot at a time
            socket->write(encode(QJsonObject{{"type", "bye"}, {"text", "the ship already has a co-pilot"}}));
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }
        m_guest = socket;
        m_buffer.clear();
        connect(socket, &QTcpSocket::readyRead, this, &CoopHost::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            socket->deleteLater();
            if (m_guest == socket) {
                m_guest = nullptr;
                emit guestLeft();
            }
        });
    }
}

void CoopHost::onReadyRead()
{
    if (!m_guest)
        return;
    m_buffer += m_guest->readAll();
    for (const QJsonObject &message : decode(m_buffer)) {
        const QString type = message.value("type").toString();
        if (type == "hello") {
            if (message.value("version").toInt() != kVersion) {
                send(QJsonObject{{"type", "bye"}, {"text", "different game versions"}});
                m_guest->disconnectFromHost();
                return;
            }
            send(QJsonObject{{"type", "welcome"}, {"role", roleName(m_role)},
                             {"commands", QJsonArray::fromStringList(commandsOf(m_role))}});
            emit guestJoined(m_guest->peerAddress().toString());
        } else if (type == "command") {
            const QString text = message.value("text").toString().left(200).simplified();
            if (text.isEmpty())
                continue;
            if (allowed(m_role, text)) {
                emit guestCommand(text);
            } else {
                send(QJsonObject{{"type", "denied"}, {"text", text}});
                emit guestDenied(text);
            }
        }
    }
}

void CoopHost::send(const QJsonObject &message)
{
    if (m_guest && m_guest->state() == QAbstractSocket::ConnectedState)
        m_guest->write(encode(message));
}

void CoopHost::sendLog(const QString &text, int tone)
{
    send(QJsonObject{{"type", "log"}, {"text", text}, {"tone", tone}});
}

void CoopHost::sendStatus(const QStringList &lines)
{
    send(QJsonObject{{"type", "status"}, {"lines", QJsonArray::fromStringList(lines)}});
}

// ================================================================
//  CoopClient
// ================================================================

CoopClient::CoopClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        m_socket->write(encode(QJsonObject{{"type", "hello"}, {"version", kVersion}}));
        emit connected();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &CoopClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] { emit disconnected(tr("connection closed")); });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (m_socket->state() != QAbstractSocket::ConnectedState)
            emit disconnected(m_socket->errorString());
    });
}

void CoopClient::connectTo(const QString &host, quint16 port)
{
    m_buffer.clear();
    m_socket->abort();
    m_socket->connectToHost(host, port);
}

void CoopClient::disconnectFromHost()
{
    m_socket->disconnectFromHost();
}

bool CoopClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void CoopClient::sendCommand(const QString &text)
{
    if (isConnected())
        m_socket->write(encode(QJsonObject{{"type", "command"}, {"text", text}}));
}

void CoopClient::onReadyRead()
{
    m_buffer += m_socket->readAll();
    for (const QJsonObject &message : decode(m_buffer)) {
        const QString type = message.value("type").toString();
        if (type == "welcome") {
            QStringList commands;
            for (const QJsonValue &v : message.value("commands").toArray())
                commands << v.toString();
            emit welcomed(message.value("role").toString(), commands);
        } else if (type == "log") {
            emit log(message.value("text").toString(), message.value("tone").toInt());
        } else if (type == "status") {
            QStringList lines;
            for (const QJsonValue &v : message.value("lines").toArray())
                lines << v.toString();
            emit status(lines);
        } else if (type == "denied") {
            emit denied(message.value("text").toString());
        } else if (type == "bye") {
            emit disconnected(message.value("text").toString(tr("the host left")));
            m_socket->disconnectFromHost();
        }
    }
}
