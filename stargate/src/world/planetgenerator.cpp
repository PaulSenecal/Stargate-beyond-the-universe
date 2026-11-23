#include "planetgenerator.h"

#include <QCoreApplication>
#include <QRandomGenerator>
#include <QStringList>

// ================================================================
//  Planet helpers
// ================================================================

QString Planet::gateAddress() const
{
    return QString(code).remove('-');
}

QString Planet::classificationName() const
{
    switch (classification) {
    case Classification::Viable:       return QCoreApplication::translate("Planet", "Viable");
    case Classification::Hostile:      return QCoreApplication::translate("Planet", "Hostile");
    case Classification::Barren:       return QCoreApplication::translate("Planet", "Barren");
    case Classification::ResourceRich: return QCoreApplication::translate("Planet", "Resource rich");
    case Classification::Ancient:      return QCoreApplication::translate("Planet", "Ancient site");
    case Classification::Unknown:      break;
    }
    return QCoreApplication::translate("Planet", "Unknown");
}

// ================================================================
//  Generation
// ================================================================

quint64 PlanetGenerator::makeSeed(int galaxy, int sequence, int variant)
{
    return (quint64(galaxy) << 40) | (quint64(sequence) << 8) | quint64(variant);
}

Planet PlanetGenerator::generate(int galaxy, int sequence, int variant)
{
    const quint64 seed = makeSeed(galaxy, sequence, variant);

    Planet p;
    p.galaxy = galaxy;
    p.sequence = sequence;
    p.variant = variant;
    p.code = QStringLiteral("G%1-%2-%3")
                 .arg(galaxy, 2, 10, QChar('0'))
                 .arg(sequence, 4, 10, QChar('0'))
                 .arg(QChar('A' + variant));

    p.conditions = generateConditions(seed);
    p.nickname = generateNickname(p.conditions, seed);
    p.classification = classify(p.conditions);

    if (rollAncientSite(seed)) {
        p.ancientName = generateAncientName(seed);
        p.classification = Classification::Ancient;
    }
    return p;
}

PlanetConditions PlanetGenerator::generateConditions(quint64 seed)
{
    QRandomGenerator rng(seed + 1);

    PlanetConditions c;
    c.water = float(rng.generateDouble());
    c.oxygen = float(rng.generateDouble());
    c.minerals = float(rng.generateDouble());
    c.energy = float(rng.generateDouble() * rng.generateDouble());   // squared: rare
    c.organics = float(rng.generateDouble());
    // Dangers are weighted so not everything is uninhabitable
    c.toxicity = float(rng.generateDouble() * 0.75);
    c.radiation = float(rng.generateDouble() * 0.55);
    // Life needs enough water and oxygen
    c.hasLife = c.water > 0.35f && c.oxygen > 0.25f && rng.generateDouble() > 0.35;
    return c;
}

QString PlanetGenerator::generateNickname(const PlanetConditions &c, quint64 seed)
{
    // Most extreme conditions first
    if (c.radiation > 0.45f && c.toxicity > 0.45f) return QStringLiteral("Blight");
    if (c.toxicity > 0.65f)                        return QStringLiteral("Ashen");
    if (c.radiation > 0.50f)                       return QStringLiteral("Glare");
    if (c.water > 0.85f && c.hasLife)              return QStringLiteral("Eden");
    if (c.water > 0.75f)                           return QStringLiteral("Still");
    if (c.energy > 0.25f)                          return QStringLiteral("Lode");
    if (c.minerals > 0.80f)                        return QStringLiteral("Crag");
    if (c.organics > 0.75f && c.hasLife)           return QStringLiteral("Veld");
    if (c.water < 0.08f && c.oxygen < 0.15f)       return QStringLiteral("Dust");
    if (c.oxygen < 0.12f)                          return QStringLiteral("Pale");
    if (c.water > 0.5f && c.toxicity > 0.35f)      return QStringLiteral("Mire");

    static const QStringList fallbacks = {
        "Ember", "Howl", "Veil", "Drift", "Cinder", "Haze",
        "Frost", "Gale", "Shelf", "Flint", "Murk", "Shard",
        "Shroud", "Pyre", "Hollow", "Wend", "Gravel", "Fen"};
    QRandomGenerator rng(seed ^ 0xDEADBEEF);
    return fallbacks.at(int(rng.bounded(quint32(fallbacks.size()))));
}

Classification PlanetGenerator::classify(const PlanetConditions &c)
{
    const float viability = c.oxygen * 0.35f + c.water * 0.30f - c.toxicity * 0.45f - c.radiation * 0.35f;
    const float richness = c.minerals + c.energy * 3.f + c.organics * 0.5f;

    if (c.toxicity > 0.65f || c.radiation > 0.55f)
        return Classification::Hostile;
    if (viability > 0.1f && richness > 1.4f)
        return Classification::ResourceRich;
    if (viability > 0.05f)
        return Classification::Viable;
    if (richness < 0.4f)
        return Classification::Barren;
    return Classification::Unknown;
}

bool PlanetGenerator::rollAncientSite(quint64 seed)
{
    QRandomGenerator rng(seed ^ 0xC0FFEE);
    return rng.bounded(100u) < 5;    // 5 %
}

QString PlanetGenerator::generateAncientName(quint64 seed)
{
    static const QStringList prefixes = {
        "Ara", "Prima", "Nova", "Terra", "Lux", "Via", "Caeli",
        "Ignis", "Aqua", "Pax", "Vita", "Tempus", "Stella"};
    static const QStringList suffixes = {
        "Nova", "Mortem", "Vitae", "Aeterna", "Prima", "Ultima",
        "Sancta", "Vera", "Magna", "Perdita", "Obscura"};

    QRandomGenerator rng(seed ^ 0xAC1EC0DEu);
    const int pi = int(rng.bounded(quint32(prefixes.size())));
    int si = int(rng.bounded(quint32(suffixes.size())));
    if (prefixes.at(pi).compare(suffixes.at(si), Qt::CaseInsensitive) == 0)   // avoid "Nova Nova"
        si = (si + 1) % int(suffixes.size());
    return prefixes.at(pi) + ' ' + suffixes.at(si);
}
