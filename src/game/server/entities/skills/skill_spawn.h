#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_SPAWN_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_SPAWN_H

#include "skill_cast_ring.h"

#include <base/vmath.h>

class CGameWorld;
class CGameContext;
class CCharacter;

void SpawnSkillFireball(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, bool ApplyBurn = true);
void SpawnSkillChainLightning(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, float ChainRange, int MaxTargets, float DamageFalloff = 0.65f);
void SpawnSkillMagicBolt(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int Effect, int PreferredTargetCID = -1);
void SpawnSkillPoisonCloud(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int PoisonStacks);
void SpawnSkillArcaneMissiles(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int DamagePerMissile, int PreferredTargetCID = -1);
void SpawnSkillMeteor(CGameWorld *pWorld, int OwnerCID, vec2 Pos, int Damage, float Radius, int WarningTicks);
void SpawnSkillBeamSweep(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float StartAngle, float BeamLength, int Damage, int DurationTicks);
void SpawnSkillVoidVortex(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int Damage, int PullTicks);
void SpawnSkillForkLightning(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, float Range, int MaxForks);
void SpawnSkillElectroArc(CGameWorld *pWorld, int OwnerCID, vec2 From, vec2 To, int Damage);
void SpawnSkillSuperNova(CGameWorld *pWorld, int OwnerCID, vec2 Pos, int Damage, int MaxRings);
void SpawnSkillSmokeVeil(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int DurationTicks, int SlowTicks, float SlowFactor);
void SpawnSkillSpinLaser(CGameWorld *pWorld, int OwnerCID, float StartAngle, float OrbitRadius, float BeamEnergy, int Damage, int DurationTicks);
void SpawnSkillGravityWell(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int PullTicks, int ExplosionRadiusTiles, int ExplosionDamage, bool FinalExplosion = true);
void SpawnSkillScatterBlast(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int ExplosionRadiusTiles, int NumShots = 3);
void SpawnSkillHomingPlasma(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int ExplosionRadiusTiles, int TrackedCID, float TrackingStrength);
void SpawnSkillRicochetShot(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage);
void SpawnSkillAcidPool(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int PoisonStacks, int DurationTicks, float SlowMul);
void SpawnSkillLaserTrap(CGameWorld *pWorld, int OwnerCID, vec2 From, vec2 To, int NumBeams, int Damage, int DurationTicks);
void SpawnSkillBombSentinel(CGameWorld *pWorld, int OwnerCID, vec2 Pos, int MaxShots, int Damage, float Range, float IntervalSec);

void SpawnSkillCastRing(CGameWorld *pWorld, vec2 Pos, float MaxRadius, int DurationTicks, ESkillVisualStyle Style,
	ESkillCastRingMode Mode = SKILL_RING_EXPAND_BURST, int GrowDurationTicks = 0);
void SpawnSkillBlinkVisual(CGameWorld *pWorld, vec2 From, vec2 To);
void SpawnSkillCastFlash(CGameWorld *pWorld, vec2 Pos, ESkillVisualStyle Style);
void SpawnSkillFrostNovaVisual(CGameWorld *pWorld, vec2 Pos, float Radius);
void SpawnSkillCastFeedback(CGameWorld *pWorld, const char *pSkillKey, vec2 Pos, float Radius, vec2 BlinkFrom = vec2(0, 0), vec2 BlinkTo = vec2(0, 0));
void SpawnSkillHitBurst(CGameWorld *pWorld, vec2 Pos, float MaxRadius, ESkillVisualStyle Style, int DurationTicks = 0, int NumDots = 12);

void SpawnSkillFollowAura(CGameWorld *pWorld, int OwnerCID, int DurationTicks, ESkillVisualStyle Style, float Radius);
void SpawnSkillZoneAura(CGameWorld *pWorld, vec2 Pos, int DurationTicks, ESkillVisualStyle Style, float Radius);
void SpawnSkillDashTrail(CGameWorld *pWorld, vec2 From, vec2 To, ESkillVisualStyle Style, int DurationTicks);
void SpawnSkillLinkBeam(CGameWorld *pWorld, vec2 From, vec2 To, int DurationTicks, ESkillVisualStyle Style, int TargetCID = -1);

#endif
