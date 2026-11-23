#include "planetlook.h"

#include <algorithm>
#include <cmath>

PlanetLook lookOf(const Planet &p)
{
    const PlanetConditions &c = p.conditions;
    PlanetLook look;

    // Water: share of ocean (a dry world has none)
    look.waterLevel = c.water < 0.1f ? -1.0f : 0.40f + c.water * 0.22f;
    look.cloudCover = std::clamp(c.oxygen * 0.6f + c.water * 0.6f, 0.0f, 1.0f);

    // Ground, from the most striking condition
    if (c.radiation > 0.45f) {
        look.ground = {0.35f, 0.15f, 0.10f};
        look.groundAlt = {0.55f, 0.25f, 0.12f};
    } else if (c.toxicity > 0.6f) {
        look.ground = {0.35f, 0.33f, 0.12f};
        look.groundAlt = {0.50f, 0.45f, 0.15f};
    } else if (c.hasLife && c.organics > 0.5f) {
        look.ground = {0.12f, 0.22f, 0.08f};
        look.groundAlt = {0.25f, 0.30f, 0.15f};
    } else if (c.water < 0.1f) {
        look.ground = {0.45f, 0.33f, 0.20f};
        look.groundAlt = {0.60f, 0.48f, 0.30f};
    } else if (c.minerals > 0.75f) {
        look.ground = {0.30f, 0.28f, 0.27f};
        look.groundAlt = {0.45f, 0.43f, 0.40f};
    }

    if (c.toxicity > 0.5f)
        look.ocean = {0.10f, 0.12f, 0.03f};

    // Atmosphere
    if (c.oxygen < 0.12f)
        look.atmosphere = {0.10f, 0.10f, 0.12f};
    else if (c.toxicity > 0.6f)
        look.atmosphere = {0.50f, 0.60f, 0.15f};
    else if (c.radiation > 0.45f)
        look.atmosphere = {0.80f, 0.35f, 0.20f};
    else
        look.atmosphere = QVector3D(0.25f, 0.45f, 1.0f) * (0.4f + c.oxygen * 0.6f);

    look.lights = p.isAncientSite() ? 1.0f : 0.0f;
    look.seed = QVector3D(std::fmod(p.galaxy * 1.7f, 17.0f), std::fmod(p.sequence * 0.37f, 13.0f), p.variant * 5.1f);
    return look;
}
