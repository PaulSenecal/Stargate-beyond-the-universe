#ifndef SOUNDCUE_H
#define SOUNDCUE_H

// ================================================================
//  SoundCue
//  Battle events that make a sound. Emitted by BattleView, played by
//  SoundSystem: the simulation itself has no audio dependency.
// ================================================================

enum class SoundCue
{
    DestinyMainGun,     // main tower shot
    DestinyTurret,      // secondary tower / pulse weapon bolt
    DestinyRailgun,     // point-defense railgun slug
    EnemyLightFire,     // fighter / light battery shot (per faction)
    EnemyHeavyFire,     // mothership battery shot (per faction)
    EnemyMissile,       // missile launch
    EnemyBeam,          // heavy beam
    Launch,             // fighters leaving a hangar
    Explosion,          // fighter destroyed, kamikaze impact
    BigExplosion,       // mothership destroyed
    ShieldHit,
    HullHit
};

#endif // SOUNDCUE_H
