#ifndef COOPPROTOCOL_H
#define COOPPROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// ================================================================
//  CoopProtocol
//  Two players aboard the same Destiny over the network: the host runs
//  the game, the guest (co-pilot) has a console and the ship status.
//  Each player has a station: the guest's commands are only accepted
//  when they belong to it (the host can do everything).
//   - tactical:    weapons, targets, shields, hangar, evasion
//   - engineering: repairs, crew, doors, energy, modules
//  Messages are JSON objects, one per line (UTF-8):
//   guest -> host: hello {version}, command {text}
//   host -> guest: welcome {role}, log {text, tone}, status {lines},
//                  denied {text}, bye
// ================================================================

namespace CoopProtocol {

constexpr int kVersion = 1;
constexpr quint16 kDefaultPort = 41414;

enum class Role { Tactical, Engineering };

QString roleName(Role role);
bool roleFromName(const QString &name, Role *role);
// Commands of a station (first word, lower case)
QStringList commandsOf(Role role);
// True when the guest at `role` may type `command` (whole line)
bool allowed(Role role, const QString &command);

QByteArray encode(const QJsonObject &message);
// Splits complete lines out of `buffer` (what is left stays in it)
QList<QJsonObject> decode(QByteArray &buffer);

} // namespace CoopProtocol

#endif // COOPPROTOCOL_H
