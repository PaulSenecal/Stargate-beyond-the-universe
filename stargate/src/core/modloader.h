#ifndef MODLOADER_H
#define MODLOADER_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "campaignlife.h"
#include "shipclass.h"

// ================================================================
//  ModLoader
//  Mods are plain JSON files in the "mods" folder next to the saves
//  (or in the folder of the STARGATE_MODS environment variable), read
//  at start-up. A mod can add or replace:
//   - "ships":  ship classes and their attacks (same fields as the
//               ship library), new factions included;
//   - "events": decision events of the campaign, with the outcome of
//               each option (resources, crew, morale, hull, hostiles…);
//   - "config": battle values, like the custom config.json.
//  See rcs/mods/example_mod.json. Errors never stop the game: they are
//  listed by the 'mods' command.
// ================================================================

class ModLoader
{
public:
    struct Event {
        QString id, title, text;
        QStringList options;
        QList<Outcome> outcomes;    // one per option
    };

    static ModLoader &instance();

    // Reads every *.json of the folder (default: AppData/mods)
    void load(const QString &folder = QString());
    QString folder() const { return m_folder; }

    QStringList loaded() const { return m_names; }
    QStringList errors() const { return m_errors; }
    const QList<ShipClass> &ships() const { return m_ships; }
    const QList<Event> &events() const { return m_events; }
    QJsonObject config() const { return m_config; }

    // The library with the mod ships added (same name = replaced)
    QList<ShipClass> merged(const QList<ShipClass> &library) const;

    // Parsing helpers (public for the tests)
    static bool parseShip(const QJsonObject &json, ShipClass *ship, QString *error);
    static Outcome parseOutcome(const QJsonObject &json);
    void loadDocument(const QJsonObject &json, const QString &name);

private:
    ModLoader() = default;
    QString m_folder;
    QStringList m_names, m_errors;
    QList<ShipClass> m_ships;
    QList<Event> m_events;
    QJsonObject m_config;
};

#endif // MODLOADER_H
