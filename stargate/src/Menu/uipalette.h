#ifndef UIPALETTE_H
#define UIPALETTE_H

#include <QColor>
#include <QFont>

#include "src/core/gameconfig.h"

// ================================================================
//  UiPalette
//  Colors of the HUD that carry a meaning (health, hostile, friendly).
//  With the colour-blind setting, green / red become blue / orange,
//  which stay distinct for every common colour vision deficiency.
// ================================================================

namespace UiPalette {

inline QColor ok()
{
    return gameConfig().colorBlind ? QColor(80, 170, 255) : QColor(70, 230, 100);
}

inline QColor warning()
{
    return gameConfig().colorBlind ? QColor(245, 225, 90) : QColor(240, 190, 60);
}

inline QColor critical()
{
    return gameConfig().colorBlind ? QColor(255, 140, 20) : QColor(255, 70, 60);
}

// Health bars: ok above `good`, warning above `bad`, critical below
inline QColor health(float pct, float good = 60.0f, float bad = 30.0f)
{
    if (pct > good) return ok();
    if (pct > bad) return warning();
    return critical();
}

inline QColor hostile()
{
    return critical();
}

// Text size of the console and the HUD panels (accessibility)
inline int scaled(int pixels)
{
    return qRound(pixels * gameConfig().textScale);
}

} // namespace UiPalette

#endif // UIPALETTE_H
