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
// Vaisseaux wraith : pas de bouclier, mais une coque organique très épaisse.
// 1.35 = 35 % de points de coque en plus pour les ruches, croiseurs et Darts.
constexpr float kWraithHullScale = 1.35f;

// Ton vaisseau amiral : a-t-il un bouclier ? (la ruche wraith n'en a pas)
inline bool flagshipShield(const QString &faction)
{
    return faction != "Wraith";
}

// Dégâts reçus par ton vaisseau amiral selon sa coque (1.0 = normal).
// Ruche wraith : 0.3 = elle encaisse ~3 fois plus de tirs, pour compenser
// l'absence de bouclier (et elle se régénère, voir hullRegen plus bas).
inline float flagshipArmor(const QString &faction)
{
    return faction == "Wraith" ? 0.3f : 1.0f;
}

// Missiles et drones : munitions limitées (nombre de salves par vaisseau).
// Les drones des Anciens traversent les boucliers : arme très forte, donc rare.
constexpr int kMissileSalvos = 6;
constexpr int kDroneSalvos = 3;
// Drones de ton vaisseau amiral (Anciens / Aurora) : commande 'drones <n>'
constexpr int kPlayerDroneSalvos = 4;           // salves en réserve
constexpr int kPlayerDronesPerSalvo = 6;        // drones par salve
constexpr float kPlayerDroneDamage = 35.0f;     // dégâts par drone (traversent les boucliers)
// Les drones ne se rechargent plus seuls : on en trouve dans les caches des Anciens
// (campagne) ou sur tes mondes avec un site ancien (conquête).
constexpr float kShipShieldRegen = 0.05f;       // part du bouclier rechargée par seconde…
constexpr float kShipShieldDelay = 4.0f;        // …après tant de secondes sans être touché

// ----------------------------------------------------------------
//  4b. ARMEMENT DU VAISSEAU AMIRAL, PAR FACTION (conquête)
//  Chaque vaisseau a ses armes : pas d'arme = la commande est refusée
//  et le système ne consomme pas d'énergie.
// ----------------------------------------------------------------

struct FlagshipLoadout
{
    const char *mainName;       // nom de l'arme principale ('attack <n>')
    float mainDamage;           // dégâts d'un tir principal (Destiny : 160)
    bool pulse;                 // salve à impulsion ('pulseweapon')
    bool towers;                // tours secondaires (tir auto + 'secondarytower')
    bool railguns;              // défense rapprochée contre missiles et chasseurs
    int hangar;                 // chasseurs dans le hangar ('launch')
    int drones;                 // salves de drones ('drones <n>')
    float hullRegen;            // % de coque réparé par seconde en combat (organique)
};

inline FlagshipLoadout flagshipLoadout(const QString &faction)
{
    if (faction == "Tau'ri")            // BC-304 : railguns, missiles, F-302
        return {QT_TRANSLATE_NOOP("Balance", "Asgard beam"), 150.0f, false, true, true, 4, 0, 0.0f};
    if (faction == "Goa'uld")           // Ha'tak : canons à plasma, beaucoup de planeurs
        return {QT_TRANSLATE_NOOP("Balance", "Staff cannons"), 140.0f, true, true, false, 10, 0, 0.0f};
    if (faction == "Lucian Alliance")   // Ha'tak récupéré, moins bien armé
        return {QT_TRANSLATE_NOOP("Balance", "Staff cannons"), 120.0f, false, true, false, 8, 0, 0.0f};
    if (faction == "Wraith")            // Ruche : nuées de Darts, coque organique qui se régénère
        return {QT_TRANSLATE_NOOP("Balance", "Energy cannons"), 130.0f, false, true, false, 12, 0, 0.8f};
    if (faction == "Asgard")            // O'Neill : rayons très puissants, peu de chasseurs
        return {QT_TRANSLATE_NOOP("Balance", "Asgard beam"), 230.0f, true, false, false, 2, 0, 0.0f};
    if (faction == "Ancients")          // Aurora : armes à énergie et drones
        return {QT_TRANSLATE_NOOP("Balance", "Energy weapons"), 160.0f, true, true, false, 4, kPlayerDroneSalvos, 0.0f};
    if (faction == "Ori")               // Vaisseau ori : rayon dévastateur
        return {QT_TRANSLATE_NOOP("Balance", "Ori beam"), 240.0f, false, true, false, 4, 0, 0.0f};
    // Destiny
    return {QT_TRANSLATE_NOOP("Balance", "Main tower"), 160.0f, true, true, true, 4, 0, 0.0f};
}

// Missiles de ton vaisseau ('missiles <n>') selon le vaisseau amiral
// ("" = Destiny, campagne). 0 = pas de baie à missiles.
inline int flagshipMissiles(const QString &faction)
{
    if (faction.isEmpty())
        return 6;                               // baie à missiles du Destiny
    if (faction == "Tau'ri")
        return 8;                               // BC-304
    if (faction == "Lucian Alliance")
        return 4;
    return 0;
}

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
constexpr int kDronePower = 20;             // guidage des drones : il faut ce surplus pour lancer une salve
constexpr int kEvasionPower = 15;           // manœuvres d'évitement ('evade on')
constexpr int kAutoFirePower = 5;           // tir automatique : par arme confiée à l'ordinateur ('auto ...')
constexpr float kAutoFireAim = 0.9f;        // …dont les tirs sont un peu moins précis (x0.9)

// ----------------------------------------------------------------
//  6b. TACTIQUE : précision, rayons, railguns, missiles, saut FTL
//  (la précision selon la distance se règle dans config.json)
// ----------------------------------------------------------------

constexpr float kEvasionDodge = 0.7f;       // 'evade on' : précision ennemie x0.7…
constexpr float kEvasionOwnAim = 0.85f;     // …mais la tienne aussi un peu (x0.85)
constexpr float kSubsystemAim = 0.8f;       // viser un sous-système ennemi : précision x0.8
constexpr float kSubsystemHullShare = 0.7f; // …et la coque ne prend que 70 % des dégâts
constexpr float kSubsystemDamage = 260.0f;  // % de sous-système perdu par point de coque max
constexpr float kBeamDuration = 0.9f;       // durée d'un rayon continu (secondes)
constexpr int kRailgunAmmo = 900;           // obus de railgun (rechargés sur les planètes minérales)
constexpr float kRailgunHeatPerShot = 4.0f; // chaleur par obus (surchauffe à 100)
constexpr float kRailgunCooling = 8.0f;     // refroidissement par seconde (tir continu : surchauffe en ~20 s)
constexpr float kRailgunRestart = 35.0f;    // les railguns repartent sous cette chaleur

constexpr float kPlayerMissileDamage = 120.0f;
constexpr float kAdaptAfterHits = 15.0f;    // drones de commande : s'adaptent après tant de coups…
constexpr float kAdaptedDamage = 0.5f;      // …de ton arme la plus utilisée (dégâts x0.5)
constexpr float kFtlSpoolUnderFire = 10.0f; // secondes pour charger le saut FTL en combat
constexpr float kFtlSpoolDocking = 5.0f;    // …ou pour faire rentrer les chasseurs sortis
constexpr float kDockRange = 30.0f;         // chasseurs plus loin au moment du saut : abandonnés
constexpr float kEnemyDroneShieldCost = 0.25f; // une salve de drones ennemie vide 25 % du bouclier

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
// Espionnage et sabotage par les équipes de porte ('spy', 'sabotage')
constexpr double kSpyCost = 15.0;               // naquadah par mission d'espionnage
constexpr double kSabotageCost = 40.0;          // naquadah par sabotage
constexpr double kGateTeamCooldown = 45.0;      // secondes avant qu'une équipe reparte
constexpr int kGateRange = 2;                   // portée : à tant de couloirs de tes mondes
constexpr double kSabotageChance = 0.6;         // chance de base, -8 % par niveau de défense
constexpr double kYardSabotage = 120.0;         // chantier naval arrêté (secondes)
constexpr double kMineSabotage = 150.0;         // revenus coupés (secondes)
constexpr double kAiSabotageChance = 0.03;      // une IA sabote un de tes mondes, par période
// Événements galactiques (invasion wraith, peste, Ancien élevé, tempête)
constexpr double kGalacticPeriod = 150.0;       // secondes entre deux tirages
constexpr double kGalacticChance = 0.5;         // chance qu'un événement arrive à chaque tirage
constexpr double kUnderdogBoost = 1.3;          // chantiers des factions avec 2 mondes ou moins
constexpr int kMaxBattleMotherships = 4;        // limites de la bataille 3D (12 contacts)
constexpr int kMaxBattleFighters = 8;

} // namespace Balance

#endif // BALANCE_H
