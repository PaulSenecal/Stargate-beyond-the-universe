#ifndef BALANCE_H
#define BALANCE_H

#include <QString>

// ================================================================
//  Balance - TOUS LES REGLAGES DU JEU, A AJUSTER ICI
//
//  Modifie une valeur, recompile (Qt Creator : Ctrl+B) et relance.
//  Les plus utiles sont en haut : les multiplicateurs globaux.
//  Repères : 1.0 = valeur normale, 2.0 = deux fois plus, 0.5 = moitié.
// ================================================================

namespace Balance {

// ----------------------------------------------------------------
//  1. DUREE DES COMBATS (les réglages les plus simples)
// ----------------------------------------------------------------

// Coque de tous les vaisseaux ennemis et alliés en bataille 3D.
// 2.0 = il faut deux fois plus de tirs pour les détruire (combats plus longs).
constexpr float kShipHullScale = 1.0f;

// Boucliers de tous les vaisseaux ennemis et alliés (voir 4. plus bas).
// 0.0 = aucun vaisseau n'a de bouclier.
constexpr float kShipShieldScale = 1.0f;

// Dégâts reçus par ton vaisseau (Destiny / vaisseau amiral).
// 0.5 = tu encaisses deux fois mieux.
constexpr float kDamageToPlayerScale = 1.0f;

// Combats automatiques de la conquête (loin du vaisseau amiral) :
// dégâts par seconde et par point de puissance. Plus petit = plus long.
// 0.04 -> 15 à 20 s en moyenne ; 0.02 -> deux fois plus long.
constexpr double kAutoBattleRate = 0.04;

// ----------------------------------------------------------------
//  2. ARMES DE TON VAISSEAU (secondes)
// ----------------------------------------------------------------

constexpr float kMainCooldown = 4.0f;           // tour principale ('attack', 'maintower')
constexpr float kPulseCooldown = 8.0f;          // salve à impulsion ('pulseweapon')
constexpr float kSecondaryCooldown = 2.0f;      // rafale des tours ('secondarytower')
constexpr float kAutoFireDelay = 0.45f;         // tir automatique des tours secondaires
constexpr float kRailgunDelay = 0.3f;           // tir des railguns (défense rapprochée)
constexpr float kSlugVsFighter = 2.5f;          // railguns : x dégâts contre les petits vaisseaux…
constexpr float kSlugVsMothership = 0.3f;       // …mais presque rien contre les vaisseaux-mères
constexpr float kMissileSpeed = 8.0f;           // vitesse des missiles ennemis
constexpr float kSlugSpeed = 70.0f;             // vitesse des obus de railgun
constexpr float kDiveSpeed = 9.0f;              // vitesse d'un kamikaze en piqué

// ----------------------------------------------------------------
//  3. BOUCLIER DE TON VAISSEAU (points par seconde, sur 100)
// ----------------------------------------------------------------

constexpr float kPlayerShieldRegen = 6.0f;          // recharge au calme
constexpr float kPlayerShieldRegenUnderFire = 3.0f; // recharge sous le feu
constexpr float kBeamShieldPierce = 0.5f;           // part d'un rayon qui traverse un bouclier

// ----------------------------------------------------------------
//  4. BOUCLIERS DES AUTRES VAISSEAUX
//  Valeur = part de la coque (0.5 = bouclier de la moitié de la coque).
//  Les chasseurs n'en ont pas (sauf l'éclaireur asgard).
// ----------------------------------------------------------------

inline float shipShieldShare(const QString &faction, bool mothership)
{
    if (!mothership)
        return faction == "Asgard" ? 0.3f : 0.0f;
    if (faction == "Asgard")
        return 0.8f;                            // les meilleurs boucliers
    if (faction == "Ancients" || faction == "Ori")
        return 0.7f;
    if (faction == "Tau'ri")
        return 0.5f;                            // boucliers asgard sur les BC-304
    if (faction == "Goa'uld")
        return 0.4f;
    if (faction == "Lucian Alliance")
        return 0.25f;                           // Ha'tak récupérés, mal entretenus
    return 0.0f;                                // Wraith (coque organique), Drones
}
constexpr float kShipShieldRegen = 0.05f;       // part du bouclier rechargée par seconde…
constexpr float kShipShieldDelay = 4.0f;        // …après tant de secondes sans être touché

// ----------------------------------------------------------------
//  5. VAGUES (campagne), ALLIES ET HANGAR
// ----------------------------------------------------------------

constexpr float kWaveDelay = 15.0f;             // secondes entre secteur dégagé et nouvelle vague
constexpr int kMaxContacts = 12;                // ennemis en même temps au maximum
constexpr int kMaxAllies = 12;                  // alliés en même temps au maximum
constexpr float kFireOnAllies = 0.45f;          // part des tirs ennemis visant tes alliés
constexpr float kHangarRefill = 20.0f;          // secondes pour reconstruire un chasseur hors combat
constexpr float kAllyFighterSpeed = 15.0f;
constexpr float kAllyMothershipSpeed = 5.0f;

// ----------------------------------------------------------------
//  6. ENERGIE (le cœur produit kCorePower, chaque système consomme)
// ----------------------------------------------------------------

constexpr int kCorePower = 100;
constexpr int kMainTowerPower = 20;
constexpr int kEnginePower = 15;
constexpr int kTurretPower = 15;
constexpr int kRailgunPower = 15;
constexpr int kShieldPower = 25;

// ----------------------------------------------------------------
//  7. CONQUETE
// ----------------------------------------------------------------

constexpr double kFlagshipSpeed = 70.0;         // vitesse du vaisseau amiral sur la carte
constexpr double kFleetSpeed = 28.0;            // vitesse des flottes
constexpr double kCaptureBase = 4.0;            // secondes en orbite pour rallier un monde neutre…
constexpr double kCapturePerDefense = 3.0;      // …plus tant par niveau de défense
constexpr double kHullRegen = 0.3;              // % de coque réparé par seconde sur tes mondes
constexpr double kShipyardRegen = 1.5;          // idem sur un chantier naval
constexpr double kTriniumPerHull = 0.5;         // trinium par point de coque réparé ('repair')
constexpr double kFighterBuild = 40.0;          // secondes pour construire un chasseur
constexpr double kMothershipBuild = 120.0;      // secondes pour construire un vaisseau-mère
constexpr int kDockFighters = 8;                // capacité d'un chantier naval
constexpr int kDockMotherships = 3;
constexpr double kThinkDelay = 4.0;             // secondes entre deux décisions d'une flotte IA
constexpr double kAttackRatio = 1.6;            // l'IA n'attaque qu'avec tant de fois plus de puissance
constexpr double kDefenseRebuild = 60.0;        // secondes par niveau de défense reconstruit
constexpr int kFlagshipPower = 12;              // force estimée du vaisseau amiral par l'IA
constexpr double kEventPeriod = 30.0;           // secondes entre deux événements de faction
constexpr double kRevoltChance = 0.04;          // révolte jaffa, par monde goa'uld et par période
constexpr double kDiplomacyChance = 0.06;       // diplomatie tau'ri, par voisin neutre et par période
constexpr double kConversionChance = 0.03;      // conversion ori, par voisin neutre et par période
constexpr double kPactChance = 0.06;            // deux IA s'allient contre un ennemi fort
constexpr double kPactEndChance = 0.25;         // la menace disparaît : le pacte se dissout
constexpr double kBetrayalChance = 0.04;        // l'allié devenu bien plus fort trahit l'autre
constexpr int kMaxBattleMotherships = 4;        // limites de la bataille 3D (12 contacts)
constexpr int kMaxBattleFighters = 8;

} // namespace Balance

#endif // BALANCE_H
