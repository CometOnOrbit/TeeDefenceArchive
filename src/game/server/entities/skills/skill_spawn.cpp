#include "skill_spawn.h"

#include "skill_fireball.h"
#include "skill_magic_bolt.h"
#include "skill_poison_cloud.h"
#include "skill_cast_ring.h"
#include "skill_hit_burst.h"
#include "skill_meteor.h"
#include "skill_beam_sweep.h"
#include "skill_void_vortex.h"
#include "skill_fork_lightning.h"
#include "skill_super_nova.h"
#include "skill_smoke_veil.h"
#include "skill_spin_laser.h"
#include "skill_gravity_well.h"
#include "skill_scatter_grenade.h"
#include "skill_detonation_beam.h"
#include "skill_homing_plasma.h"
#include "skill_ricochet_shot.h"
#include "skill_acid_pool.h"
#include "skill_laser_trap.h"
#include "skill_bomb_sentinel.h"
#include "skill_detonation_beam.h"
#include "skill_follow_aura.h"
#include "skill_zone_aura.h"
#include "skill_link_beam.h"

#include <game/server/entities/electro.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gameworld.h>
#include <game/server/gamecontext.h>

#include "../character.h"
#include <game/server/gamecontext.h>
#include <game/server/entities/mmo/tesla_chain.h>
#include <base/math.h>
#include <base/system.h>
#include <generated/server_data.h>

void SpawnSkillFireball(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, bool ApplyBurn)
{
	if(!pWorld)
		return;
	new CSkillFireball(pWorld, OwnerCID, Pos, Dir, Damage, ApplyBurn);
}

void SpawnSkillChainLightning(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, float ChainRange, int MaxTargets, float DamageFalloff)
{
	if(!pWorld)
		return;
	new CMMOTeslaChain(pWorld, OwnerCID, Pos, Dir, Damage, ChainRange, MaxTargets, DamageFalloff);
}

void SpawnSkillMagicBolt(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int Effect, int PreferredTargetCID)
{
	if(!pWorld)
		return;
	new CSkillMagicBolt(pWorld, OwnerCID, Pos, Dir, Damage, (ESkillBoltEffect)Effect, PreferredTargetCID);
}

void SpawnSkillPoisonCloud(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int PoisonStacks)
{
	if(!pWorld)
		return;
	const int Duration = pWorld->GameServer()->Server()->TickSpeed() * 2;
	new CSkillPoisonCloud(pWorld, OwnerCID, Pos, Radius, PoisonStacks, Duration);
}

void SpawnSkillArcaneMissiles(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int DamagePerMissile, int PreferredTargetCID)
{
	if(!pWorld)
		return;
	if(length(Dir) < 0.01f)
		Dir = vec2(1.f, 0.f);
	Dir = normalize(Dir);
	const float BaseAngle = angle(Dir);
	const float Offsets[] = {-0.12f, 0.f, 0.12f};
	for(int i = 0; i < 3; i++)
	{
		const vec2 MissileDir = direction(BaseAngle + Offsets[i]);
		new CSkillMagicBolt(pWorld, OwnerCID, Pos, MissileDir, DamagePerMissile, SKILL_BOLT_DAMAGE, PreferredTargetCID);
	}
}

void SpawnSkillMeteor(CGameWorld *pWorld, int OwnerCID, vec2 Pos, int Damage, float Radius, int WarningTicks)
{
	if(!pWorld)
		return;
	new CSkillMeteor(pWorld, OwnerCID, Pos, Damage, Radius, WarningTicks);
}

void SpawnSkillBeamSweep(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float StartAngle, float BeamLength, int Damage, int DurationTicks)
{
	if(!pWorld)
		return;
	new CSkillBeamSweep(pWorld, OwnerCID, Pos, StartAngle, BeamLength, Damage, DurationTicks);
}

void SpawnSkillVoidVortex(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int Damage, int PullTicks)
{
	if(!pWorld)
		return;
	new CSkillVoidVortex(pWorld, OwnerCID, Pos, Radius, Damage, PullTicks);
}

void SpawnSkillForkLightning(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, float Range, int MaxForks)
{
	if(!pWorld)
		return;
	if(length(Dir) < 0.01f)
		Dir = vec2(1.f, 0.f);
	new CSkillForkLightning(pWorld, OwnerCID, Pos, Dir, maximum(160.f, Range), 80.f, Damage, clamp(MaxForks, 2, 3));
}

void SpawnSkillElectroArc(CGameWorld *pWorld, int OwnerCID, vec2 From, vec2 To, int Damage)
{
	if(!pWorld)
		return;

	vec2 Delta = To - From;
	if(length(Delta) < 1.f)
		Delta = vec2(64.f, 0.f);
	vec2 Offset(-Delta.y * 0.5f, Delta.x * 0.5f);
	new CElectro(pWorld, From, To, Offset, 3);

	CGameContext *pGS = pWorld->GameServer();
	if(!pGS)
		return;

	const int Dmg = maximum(1, Damage);
	const vec2 SamplePoints[] = {From, (From + To) * 0.5f, To};
	for(int s = 0; s < 3; s++)
	{
		for(CGameWorld::TypeRange r = pWorld->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pTarget = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(pGS, OwnerCID, pTarget))
				continue;
			if(distance(SamplePoints[s], pTarget->GetPos()) > 56.f)
				continue;
			vec2 HitDir = normalize(pTarget->GetPos() - SamplePoints[s]);
			if(length(HitDir) < 0.01f)
				HitDir = vec2(0.f, -1.f);
			pTarget->TakeDamage(HitDir * 6.f, SamplePoints[s], Dmg, OwnerCID, WEAPON_LASER);
		}
	}
	pWorld->CreateSound(To, SOUND_LASER_BOUNCE);
}

void SpawnSkillSuperNova(CGameWorld *pWorld, int OwnerCID, vec2 Pos, int Damage, int MaxRings)
{
	if(!pWorld)
		return;
	new CSkillSuperNova(pWorld, OwnerCID, Pos, Damage, maximum(2, MaxRings));
}

void SpawnSkillSmokeVeil(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int DurationTicks, int SlowTicks, float SlowFactor)
{
	if(!pWorld)
		return;
	new CSkillSmokeVeil(pWorld, OwnerCID, Pos, Radius, DurationTicks, SlowTicks, SlowFactor);
}

void SpawnSkillSpinLaser(CGameWorld *pWorld, int OwnerCID, float StartAngle, float OrbitRadius, float BeamEnergy, int Damage, int DurationTicks)
{
	if(!pWorld)
		return;
	new CSkillSpinLaser(pWorld, OwnerCID, StartAngle, OrbitRadius, BeamEnergy, Damage, DurationTicks);
}

void SpawnSkillGravityWell(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int PullTicks, int ExplosionRadiusTiles, int ExplosionDamage, bool FinalExplosion)
{
	if(!pWorld)
		return;
	new CSkillGravityWell(pWorld, OwnerCID, Pos, Radius, PullTicks, ExplosionRadiusTiles, ExplosionDamage, FinalExplosion);
}

void SpawnSkillScatterBlast(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int ExplosionRadiusTiles, int NumShots)
{
	if(!pWorld)
		return;
	if(length(Dir) < 0.01f)
		Dir = vec2(1.f, 0.f);
	Dir = normalize(Dir);
	const float BaseAngle = angle(Dir);
	const int Shots = clamp(NumShots, 1, 5);
	const float Spread = 0.35f;
	for(int i = 0; i < Shots; i++)
	{
		const float T = Shots > 1 ? (float)i / (float)(Shots - 1) - 0.5f : 0.f;
		const vec2 ShotDir = direction(BaseAngle + T * Spread);
		new CSkillScatterGrenade(pWorld, OwnerCID, Pos, ShotDir, Damage, ExplosionRadiusTiles);
	}
}

void SpawnSkillHomingPlasma(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int ExplosionRadiusTiles, int TrackedCID, float TrackingStrength)
{
	if(!pWorld)
		return;
	new CSkillHomingPlasma(pWorld, OwnerCID, Pos, Dir, Damage, ExplosionRadiusTiles, TrackedCID, TrackingStrength);
}

static void DestroySkillEntitiesForOwner(CGameWorld *pWorld, int OwnerCID, int EntityKind)
{
	if(!pWorld)
		return;

	for(CGameWorld::TypeRange r = pWorld->DoTypeRange(CGameWorld::ENTTYPE_LASER); !r.empty(); r.pop_front())
	{
		CEntity *pEnt = r.front();
		if(EntityKind == 0)
		{
			CSkillLaserTrap *pTrap = dynamic_cast<CSkillLaserTrap *>(pEnt);
			if(pTrap && pTrap->GetOwner() == OwnerCID)
				pTrap->MarkForDestroy();
		}
		else if(EntityKind == 1)
		{
			CSoldierBombSentinel *pSentinel = dynamic_cast<CSoldierBombSentinel *>(pEnt);
			if(pSentinel && pSentinel->GetOwner() == OwnerCID)
				pSentinel->MarkForDestroy();
		}
	}
}

void SpawnSkillRicochetShot(CGameWorld *pWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage)
{
	if(!pWorld)
		return;
	new CSkillRicochetShot(pWorld, OwnerCID, Pos, Dir, Damage);
}

void SpawnSkillAcidPool(CGameWorld *pWorld, int OwnerCID, vec2 Pos, float Radius, int PoisonStacks, int DurationTicks, float SlowMul)
{
	if(!pWorld)
		return;
	new CSkillAcidPool(pWorld, OwnerCID, Pos, Radius, PoisonStacks, DurationTicks, SlowMul);
}

void SpawnSkillLaserTrap(CGameWorld *pWorld, int OwnerCID, vec2 From, vec2 To, int NumBeams, int Damage, int DurationTicks)
{
	if(!pWorld)
		return;
	DestroySkillEntitiesForOwner(pWorld, OwnerCID, 0);
	new CSkillLaserTrap(pWorld, OwnerCID, From, To, NumBeams, Damage, DurationTicks);
}

void SpawnSkillBombSentinel(CGameWorld *pWorld, int OwnerCID, vec2 Pos, int MaxShots, int Damage, float Range, float IntervalSec)
{
	if(!pWorld)
		return;
	DestroySkillEntitiesForOwner(pWorld, OwnerCID, 1);
	new CSoldierBombSentinel(pWorld, OwnerCID, Pos, MaxShots, Damage, Range, IntervalSec);
}

void SpawnSkillCastRing(CGameWorld *pWorld, vec2 Pos, float MaxRadius, int DurationTicks, ESkillVisualStyle Style,
	ESkillCastRingMode Mode, int GrowDurationTicks)
{
	if(!pWorld)
		return;
	new CSkillCastRing(pWorld, Pos, MaxRadius, DurationTicks, Style, Mode, GrowDurationTicks);
}

void SpawnSkillCastFlash(CGameWorld *pWorld, vec2 Pos, ESkillVisualStyle Style)
{
	if(!pWorld)
		return;
	const int TickSpeed = pWorld->GameServer()->Server()->TickSpeed();
	SpawnSkillCastRing(pWorld, Pos, 48.f, TickSpeed / 3, Style, SKILL_RING_EXPAND_BURST, TickSpeed / 6);
}

void SpawnSkillBlinkVisual(CGameWorld *pWorld, vec2 From, vec2 To)
{
	if(!pWorld)
		return;
	const int TickSpeed = pWorld->GameServer()->Server()->TickSpeed();
	pWorld->CreatePlayerSpawn(From);
	pWorld->CreatePlayerSpawn(To);
	pWorld->CreateDeath(From, -1);
	SpawnSkillCastRing(pWorld, From, 40.f, TickSpeed / 4, SKILL_VFX_ARCANE, SKILL_RING_EXPAND_BURST, TickSpeed / 8);
	SpawnSkillCastRing(pWorld, To, 56.f, TickSpeed / 3, SKILL_VFX_ARCANE, SKILL_RING_EXPAND_HOLD, TickSpeed / 6);
	pWorld->CreateSound(To, SOUND_SFX_TELEPORT);
}

void SpawnSkillFrostNovaVisual(CGameWorld *pWorld, vec2 Pos, float Radius)
{
	if(!pWorld)
		return;
	const int TickSpeed = pWorld->GameServer()->Server()->TickSpeed();
	SpawnSkillCastRing(pWorld, Pos, Radius, TickSpeed / 2, SKILL_VFX_FROST, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
}

void SpawnSkillHitBurst(CGameWorld *pWorld, vec2 Pos, float MaxRadius, ESkillVisualStyle Style, int DurationTicks, int NumDots)
{
	if(!pWorld)
		return;
	new CSkillHitBurst(pWorld, Pos, MaxRadius, Style, DurationTicks, NumDots);
}

void SpawnSkillFollowAura(CGameWorld *pWorld, int OwnerCID, int DurationTicks, ESkillVisualStyle Style, float Radius)
{
	if(!pWorld)
		return;
	new CSkillFollowAura(pWorld, OwnerCID, DurationTicks, Style, Radius);
}

void SpawnSkillZoneAura(CGameWorld *pWorld, vec2 Pos, int DurationTicks, ESkillVisualStyle Style, float Radius)
{
	if(!pWorld)
		return;
	new CSkillZoneAura(pWorld, Pos, DurationTicks, Style, Radius);
}

void SpawnSkillDashTrail(CGameWorld *pWorld, vec2 From, vec2 To, ESkillVisualStyle Style, int DurationTicks)
{
	if(!pWorld)
		return;
	const int Segments = 4;
	const int PerSegment = maximum(1, DurationTicks / Segments);
	const float Radius = 36.f;
	for(int i = 0; i <= Segments; i++)
	{
		const float T = (float)i / (float)Segments;
		const vec2 Pos = From + (To - From) * T;
		new CSkillZoneAura(pWorld, Pos, PerSegment + i * 2, Style, Radius);
	}
}

void SpawnSkillLinkBeam(CGameWorld *pWorld, vec2 From, vec2 To, int DurationTicks, ESkillVisualStyle Style, int TargetCID)
{
	if(!pWorld)
		return;
	new CSkillLinkBeam(pWorld, From, To, DurationTicks, Style, TargetCID);
}

void SpawnSkillCastFeedback(CGameWorld *pWorld, const char *pSkillKey, vec2 Pos, float Radius, vec2 BlinkFrom, vec2 BlinkTo)
{
	if(!pWorld || !pSkillKey || !pSkillKey[0])
		return;

	const int TickSpeed = pWorld->GameServer()->Server()->TickSpeed();
	const float R = maximum(48.f, Radius);

	if(str_comp(pSkillKey, "frost_nova") == 0)
	{
		SpawnSkillFrostNovaVisual(pWorld, Pos, R);
		return;
	}
	if(str_comp(pSkillKey, "blink") == 0)
	{
		if(length(BlinkFrom) > 0.01f && length(BlinkTo) > 0.01f)
			SpawnSkillBlinkVisual(pWorld, BlinkFrom, BlinkTo);
		return;
	}
	if(str_comp(pSkillKey, "fireball") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "chain_lightning") == 0 || str_comp(pSkillKey, "arcane_missiles") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_ARCANE);
		return;
	}
	if(str_comp(pSkillKey, "poison_mist") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_POISON, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "entangle") == 0 || str_comp(pSkillKey, "life_drain") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_POISON);
		return;
	}
	if(str_comp(pSkillKey, "battle_cry") == 0 || str_comp(pSkillKey, "blessing") == 0 || str_comp(pSkillKey, "enlighten") == 0 || str_comp(pSkillKey, "dispel_wave") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_HOLY, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "holy_light") == 0 || str_comp(pSkillKey, "renew") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, 64.f, TickSpeed / 2, SKILL_VFX_HEAL, SKILL_RING_STEADY, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "ground_slam") == 0 || str_comp(pSkillKey, "thunder_clap") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 3, SKILL_VFX_PHYSICAL, SKILL_RING_EXPAND_BURST, TickSpeed / 6);
		return;
	}
	if(str_comp(pSkillKey, "rage") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, 72.f, TickSpeed / 2, SKILL_VFX_PHYSICAL, SKILL_RING_EXPAND_HOLD, TickSpeed / 5);
		return;
	}
	if(str_comp(pSkillKey, "arcane_shield") == 0 || str_comp(pSkillKey, "iron_will") == 0 || str_comp(pSkillKey, "haste") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, 56.f, TickSpeed, SKILL_VFX_ARCANE, SKILL_RING_STEADY, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "shadow_step") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, 64.f, TickSpeed / 2, SKILL_VFX_SHADOW, SKILL_RING_EXPAND_BURST, TickSpeed / 5);
		pWorld->CreateDeath(Pos, -1);
		return;
	}
	if(str_comp(pSkillKey, "dash") == 0 || str_comp(pSkillKey, "attack_teleport") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_ARCANE);
		pWorld->CreateHammerHit(Pos);
		return;
	}
	if(str_comp(pSkillKey, "meteor_strike") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_FIRE);
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed, SKILL_VFX_FIRE, SKILL_RING_STEADY, TickSpeed / 3);
		return;
	}
	if(str_comp(pSkillKey, "celestial_beam") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, 96.f, TickSpeed / 2, SKILL_VFX_HOLY, SKILL_RING_STEADY, TickSpeed / 4);
		pWorld->CreateSound(Pos, SOUND_LASER_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "void_rift") == 0 || str_comp(pSkillKey, "black_hole") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed, SKILL_VFX_SHADOW, SKILL_RING_EXPAND_HOLD, TickSpeed / 3);
		pWorld->CreateDeath(Pos, -1);
		return;
	}
	if(str_comp(pSkillKey, "phoenix_flame") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_FIRE, SKILL_RING_EXPAND_BURST, TickSpeed / 5);
		return;
	}
	if(str_comp(pSkillKey, "starfall") == 0 || str_comp(pSkillKey, "thunderstorm") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_ARCANE);
		return;
	}
	if(str_comp(pSkillKey, "arcane_vortex") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed, SKILL_VFX_ARCANE, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "forked_lightning") == 0 || str_comp(pSkillKey, "electro_arc") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_ARCANE);
		pWorld->CreateSound(Pos, SOUND_LASER_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "super_nova") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_FIRE, SKILL_RING_EXPAND_BURST, TickSpeed / 5);
		return;
	}
	if(str_comp(pSkillKey, "smoke_veil") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed, SKILL_VFX_SHADOW, SKILL_RING_EXPAND_HOLD, TickSpeed / 3);
		pWorld->CreateDeath(Pos, -1);
		return;
	}
	if(str_comp(pSkillKey, "death_spiral") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, 80.f, TickSpeed, SKILL_VFX_ARCANE, SKILL_RING_STEADY, TickSpeed / 4);
		pWorld->CreateSound(Pos, SOUND_LASER_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "plasma_field") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_ARCANE, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
		pWorld->CreateSound(Pos, SOUND_LASER_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "flash_wave") == 0)
	{
		SpawnSkillFrostNovaVisual(pWorld, Pos, R);
		return;
	}
	if(str_comp(pSkillKey, "toxic_bloom") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_POISON, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "healing_mist") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_HEAL, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "gravity_well") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed + TickSpeed / 2, SKILL_VFX_HOLY, SKILL_RING_EXPAND_HOLD, TickSpeed / 2);
		pWorld->CreateDeath(Pos, -1);
		return;
	}
	if(str_comp(pSkillKey, "scatter_blast") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_FIRE);
		pWorld->CreateHammerHit(Pos);
		return;
	}
	if(str_comp(pSkillKey, "detonation_beam") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_FIRE);
		pWorld->CreateSound(Pos, SOUND_LASER_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "homing_plasma") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_ARCANE);
		pWorld->CreateSound(Pos, SOUND_WEAPON_SPAWN);
		return;
	}
	if(str_comp(pSkillKey, "ricochet_shot") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_ARCANE);
		pWorld->CreateSound(Pos, SOUND_WEAPON_SPAWN);
		return;
	}
	if(str_comp(pSkillKey, "acid_pool") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed, SKILL_VFX_POISON, SKILL_RING_EXPAND_HOLD, TickSpeed / 4);
		return;
	}
	if(str_comp(pSkillKey, "laser_trap") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_ARCANE);
		pWorld->CreateSound(Pos, SOUND_LASER_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "bomb_sentinel") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, 72.f, TickSpeed, SKILL_VFX_FIRE, SKILL_RING_STEADY, TickSpeed / 4);
		pWorld->CreateSound(Pos, SOUND_GRENADE_FIRE);
		return;
	}
	if(str_comp(pSkillKey, "corpse_burst") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_POISON, SKILL_RING_EXPAND_BURST, TickSpeed / 5);
		pWorld->CreateDeath(Pos, -1);
		pWorld->CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		return;
	}
	if(str_comp(pSkillKey, "smoke_hook") == 0)
	{
		SpawnSkillCastFlash(pWorld, Pos, SKILL_VFX_POISON);
		pWorld->CreateSound(Pos, SOUND_HOOK_LOOP);
		return;
	}
	if(str_comp(pSkillKey, "web_snare") == 0)
	{
		SpawnSkillCastRing(pWorld, Pos, R, TickSpeed / 2, SKILL_VFX_POISON, SKILL_RING_STEADY, TickSpeed / 4);
		pWorld->CreateSound(Pos, SOUND_HOOK_ATTACH_PLAYER);
		return;
	}
}
