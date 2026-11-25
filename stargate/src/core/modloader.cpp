#include "modloader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

ModLoader &ModLoader::instance()
{
    static ModLoader loader;
    return loader;
}

void ModLoader::load(const QString &folder)
{
    m_names.clear();
    m_errors.clear();
    m_ships.clear();
    m_events.clear();
    m_config = QJsonObject();
    m_folder = folder;
    if (m_folder.isEmpty())
        m_folder = qEnvironmentVariable("STARGATE_MODS");
    if (m_folder.isEmpty())
        m_folder = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("mods");
    QDir dir(m_folder);
    if (!dir.exists())
        return;
    for (const QFileInfo &file : dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        QFile f(file.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            m_errors << QStringLiteral("%1: cannot be read").arg(file.fileName());
            continue;
        }
        QJsonParseError parse;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parse);
        if (!doc.isObject()) {
            m_errors << QStringLiteral("%1: invalid JSON (%2, offset %3)").arg(file.fileName(), parse.errorString())
                            .arg(parse.offset);
            continue;
        }
        loadDocument(doc.object(), file.fileName());
    }
}

void ModLoader::loadDocument(const QJsonObject &json, const QString &name)
{
    const QString title = json.value("name").toString(name);
    int ships = 0, events = 0;
    for (const QJsonValue &v : json.value("ships").toArray()) {
        ShipClass ship;
        QString error;
        if (!parseShip(v.toObject(), &ship, &error)) {
            m_errors << QStringLiteral("%1: ship %2").arg(name, error);
            continue;
        }
        m_ships.erase(std::remove_if(m_ships.begin(), m_ships.end(),
                                     [&ship](const ShipClass &s) { return s.name == ship.name; }),
                      m_ships.end());
        m_ships << ship;
        ++ships;
    }
    for (const QJsonValue &v : json.value("events").toArray()) {
        const QJsonObject o = v.toObject();
        Event e;
        e.id = o.value("id").toString();
        e.title = o.value("title").toString();
        e.text = o.value("text").toString();
        for (const QJsonValue &option : o.value("options").toArray()) {
            const QJsonObject opt = option.toObject();
            e.options << opt.value("text").toString();
            e.outcomes << parseOutcome(opt.value("outcome").toObject());
        }
        if (e.id.isEmpty() || e.title.isEmpty() || e.options.size() < 2) {
            m_errors << QStringLiteral("%1: event '%2' needs an id, a title and 2 options or more").arg(name, e.id);
            continue;
        }
        m_events << e;
        ++events;
    }
    const QJsonObject config = json.value("config").toObject();
    for (auto it = config.begin(); it != config.end(); ++it)
        m_config.insert(it.key(), it.value());
    m_names << QStringLiteral("%1 (%2 ship(s), %3 event(s)%4)")
                   .arg(title).arg(ships).arg(events)
                   .arg(config.isEmpty() ? QString() : QStringLiteral(", settings"));
}

bool ModLoader::parseShip(const QJsonObject &o, ShipClass *ship, QString *error)
{
    ShipClass s;
    s.name = o.value("name").toString();
    s.role = o.value("role").toString("Fighter");
    s.faction = o.value("faction").toString();
    s.description = o.value("description").toString();
    s.hull = std::clamp(o.value("hull").toInt(100), 1, 100000);
    s.hostile = o.value("hostile").toBool(true);
    s.color = o.value("color").toString("#808080");
    s.picture = o.value("picture").toString();
    if (s.name.isEmpty() || s.faction.isEmpty()) {
        if (error)
            *error = QStringLiteral("'%1': name and faction are required").arg(s.name);
        return false;
    }
    static const QStringList kinds{"bolt", "slug", "missile", "beam", "kamikaze", "launch"};
    for (const QJsonValue &v : o.value("attacks").toArray()) {
        const QJsonObject a = v.toObject();
        ShipAttack attack;
        attack.name = a.value("name").toString();
        attack.kind = a.value("kind").toString("bolt");
        attack.damage = float(a.value("damage").toDouble(1));
        attack.shots = std::max(1, a.value("shots").toInt(1));
        attack.cooldown = float(std::max(0.2, a.value("cooldown").toDouble(3)));
        attack.chance = float(std::clamp(a.value("chance").toDouble(1), 0.0, 1.0));
        attack.trigger = a.value("trigger").toString("always");
        attack.payload = a.value("payload").toString();
        attack.special = a.value("special").toBool();
        attack.description = a.value("description").toString();
        if (!kinds.contains(attack.kind)) {
            if (error)
                *error = QStringLiteral("'%1': unknown attack kind '%2'").arg(s.name, attack.kind);
            return false;
        }
        s.attacks << attack;
    }
    if (s.attacks.isEmpty()) {
        if (error)
            *error = QStringLiteral("'%1': at least one attack").arg(s.name);
        return false;
    }
    *ship = s;
    return true;
}

Outcome ModLoader::parseOutcome(const QJsonObject &o)
{
    Outcome out;
    out.text = o.value("text").toString();
    const QString tone = o.value("tone").toString();
    out.tone = tone == "good" ? 1 : tone == "warning" ? 2 : tone == "critical" ? 3 : 0;
    const QJsonObject resources = o.value("resources").toObject();
    for (auto it = resources.begin(); it != resources.end(); ++it)
        out.resources.append({it.key(), it.value().toInt()});
    out.crew = o.value("crew").toInt();
    out.morale = float(o.value("morale").toDouble());
    out.power = o.value("power").toDouble();
    out.ftl = o.value("ftl").toDouble();
    out.hullDamage = float(std::max(0.0, o.value("hullDamage").toDouble()));
    out.injuries = std::max(0, o.value("injuries").toInt());
    out.fire = o.value("fire").toBool();
    out.blueprint = o.value("blueprint").toBool();
    out.hostiles = o.value("hostiles").toString();
    out.boarders = std::max(0, o.value("boarders").toInt());
    out.missiles = o.value("missiles").toInt();
    out.drones = o.value("drones").toInt();
    out.reveal = o.value("reveal").toString();
    return out;
}

QList<ShipClass> ModLoader::merged(const QList<ShipClass> &library) const
{
    QList<ShipClass> result = library;
    for (const ShipClass &ship : m_ships) {
        auto it = std::find_if(result.begin(), result.end(), [&ship](const ShipClass &s) { return s.name == ship.name; });
        if (it != result.end())
            *it = ship;
        else
            result << ship;
    }
    return result;
}
