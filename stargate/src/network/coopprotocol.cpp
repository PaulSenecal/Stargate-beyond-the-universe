#include "coopprotocol.h"

#include <QJsonDocument>

namespace CoopProtocol {

QString roleName(Role role)
{
    return role == Role::Tactical ? QStringLiteral("tactical") : QStringLiteral("engineering");
}

bool roleFromName(const QString &name, Role *role)
{
    const QString n = name.trimmed().toLower();
    if (n.startsWith("tac") || n.startsWith("weap")) {
        *role = Role::Tactical;
        return true;
    }
    if (n.startsWith("eng") || n.startsWith("rep")) {
        *role = Role::Engineering;
        return true;
    }
    return false;
}

QStringList commandsOf(Role role)
{
    // Reports both stations can ask for
    const QStringList shared{"help", "destinystatus", "scansector", "weaponstatus", "stats", "quests", "hint",
                             "showressource", "clear"};
    if (role == Role::Tactical)
        return shared + QStringList{"attack", "maintower", "pulseweapon", "secondarytower", "scan", "target", "aim",
                                    "missiles", "missile", "drones", "launch", "shield", "evade", "evasive", "auto"};
    return shared + QStringList{"repair", "jobs", "cancel", "crew", "seal", "unseal", "vent", "unlock", "install",
                                "blueprints", "defend", "power", "turrets", "railguns"};
}

bool allowed(Role role, const QString &command)
{
    const QString first = command.section(' ', 0, 0, QString::SectionSkipEmpty).toLower();
    return !first.isEmpty() && commandsOf(role).contains(first);
}

QByteArray encode(const QJsonObject &message)
{
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

QList<QJsonObject> decode(QByteArray &buffer)
{
    QList<QJsonObject> messages;
    int end;
    while ((end = int(buffer.indexOf('\n'))) >= 0) {
        const QByteArray line = buffer.left(end).trimmed();
        buffer.remove(0, end + 1);
        if (line.isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (doc.isObject())
            messages << doc.object();
    }
    // A peer that never sends a newline must not fill the memory
    if (buffer.size() > 64 * 1024)
        buffer.clear();
    return messages;
}

} // namespace CoopProtocol
