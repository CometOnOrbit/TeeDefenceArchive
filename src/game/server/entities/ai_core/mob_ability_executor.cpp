#include "mob_ability_executor.h"

#include <game/server/entities/character_bot_ai.h>
#include <game/server/entities/growingexplosion.h>
#include <game/server/entities/skills/skill_spawn.h>
#include <game/server/entities/skills/skill_detonation_beam.h>
#include <game/server/entities/skills/skill_magic_bolt.h>
#include <game/server/entities/skills/skill_cast_ring.h>
#include <game/server/entities/skills/skill_target_query.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/collision.h>
#include <base/math.h>

static bool MobFrostNova(CGameContext *pGS, CCharacterBotAI *pCaster, int MobAttack, float ScaleAttack, float Radius)
{
	if(!pGS || !pCaster || !pGS->Core() || !pGS->Core()->StatusManager())
		return false;

	const vec2 Pos = pCaster->GetPos();
	SpawnSkillCastFeedback(&pGS->m_World, "frost_nova", Pos, Radius);
	const int TickSpeed = pGS->Server()->TickSpeed();
	const int SlowTicks = TickSpeed * 3 + maximum(0, MobAttack / 2);
	SpawnSkillZoneAura(&pGS->m_World, Pos, SlowTicks, SKILL_VFX_FROST, Radius * 0.5f);
	const int FreezeDamage = maximum(4, (int)(MobAttack * ScaleAttack * 2.f));

	ForEachMobHostileInRadius(&pGS->m_World, pCaster->GetPlayer(), Pos, Radius, [&](CCharacter *pTargetChr) {
		if(pGS->Core()->StatusManager()->GetSlowTicks(pTargetChr) > 0)
		{
			pTargetChr->TakeDamage(vec2(0, 0), Pos, FreezeDamage, pCaster->GetPlayer()->GetCID(), WEAPON_GAME);
			SpawnSkillHitBurst(&pGS->m_World, pTargetChr->GetPos(), 48.f, SKILL_VFX_FROST);
			pGS->m_World.CreateSound(pTargetChr->GetPos(), SOUND_GRENADE_EXPLODE);
		}
		else
		{
			pGS->Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, SlowTicks, 0.55f);
			SpawnSkillHitBurst(&pGS->m_World, pTargetChr->GetPos(), 40.f, SKILL_VFX_FROST);
		}
		return true;
	});

	pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + TickSpeed / 2);
	return true;
}

static bool MobBlink(CGameContext *pGS, CCharacterBotAI *pCaster, int MobLevel, float Distance)
{
	if(!pGS || !pCaster)
		return false;

	vec2 Dir(0.f, 0.f);
	CCharacter *pNearest = FindNearestMobHostile(pGS, pCaster->GetPlayer(), pCaster->GetPos(), 2000.f);
	if(pNearest)
	{
		vec2 Away = pCaster->GetPos() - pNearest->GetPos();
		if(length(Away) > 0.01f)
			Dir = normalize(Away);
	}

	if(length(Dir) < 0.01f)
	{
		const float Angle = random_float() * 2.f * pi;
		Dir = vec2(cosf(Angle), sinf(Angle));
	}

	const float Travel = maximum(64.f, Distance + MobLevel * 4.f);
	const vec2 OldPos = pCaster->GetPos();
	vec2 NewPos = OldPos + Dir * Travel;
	CCollision *pColl = pGS->Collision();
	if(pColl)
	{
		NewPos.x = clamp(NewPos.x, 16.0f, (float)(pColl->GetWidth() * 32 - 16));
		NewPos.y = clamp(NewPos.y, 16.0f, (float)(pColl->GetHeight() * 32 - 16));
	}

	pCaster->SetCharacterPos(NewPos);
	SpawnSkillCastFeedback(&pGS->m_World, "blink", NewPos, 0.f, OldPos, NewPos);
	SpawnSkillDashTrail(&pGS->m_World, OldPos, NewPos, SKILL_VFX_ARCANE, pGS->Server()->TickSpeed() / 2);
	pCaster->SetEmote(EMOTE_SURPRISE, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 2);
	return true;
}

bool ExecuteMobAbility(CCharacterBotAI *pCaster, const SMMOMobAbilityDef &Ability, int MobAttack, int MobLevel)
{
	if(!pCaster || !pCaster->IsAlive())
		return false;

	CGameContext *pGS = pCaster->GameServer();
	if(!pGS)
		return false;

	CPlayer *pMob = pCaster->GetPlayer();
	if(!pMob)
		return false;

	const int OwnerCID = pMob->GetCID();
	const float Radius = maximum(96.f, Ability.m_Range);
	CCharacter *pTargetChr = FindNearestMobHostile(pGS, pMob, pCaster->GetPos(), Radius + 200.f);
	CPlayer *pTarget = pTargetChr ? pTargetChr->GetPlayer() : nullptr;
	vec2 Dir(1.f, 0.f);
	if(pTarget)
	{
		Dir = normalize(pTarget->GetCharacter()->GetPos() - pCaster->GetPos());
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
	}

	if(str_comp(Ability.m_aKey, "frost_nova") == 0)
		return MobFrostNova(pGS, pCaster, MobAttack, Ability.m_ScaleAttack, Radius);

	if(str_comp(Ability.m_aKey, "blink") == 0)
		return MobBlink(pGS, pCaster, MobLevel, Ability.m_Range > 1.f ? Ability.m_Range : 350.f);

	if(str_comp(Ability.m_aKey, "fireball") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.5f));
		SpawnSkillCastFeedback(&pGS->m_World, "fireball", pCaster->GetPos(), 48.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_FIRE, 40.f);
		SpawnSkillFireball(&pGS->m_World, OwnerCID, pCaster->GetPos() + Dir * 28.f, Dir, Dmg, true);
		pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 3);
		return true;
	}

	if(str_comp(Ability.m_aKey, "chain_lightning") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.2f));
		SpawnSkillCastFeedback(&pGS->m_World, "chain_lightning", pCaster->GetPos(), Radius);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 44.f);
		SpawnSkillChainLightning(&pGS->m_World, OwnerCID, pCaster->GetPos(), Dir, Dmg, Radius, 3, 0.65f);
		pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 3);
		return true;
	}

	if(str_comp(Ability.m_aKey, "poison_mist") == 0)
	{
		SpawnSkillCastFeedback(&pGS->m_World, "poison_mist", pCaster->GetPos(), Radius);
		SpawnSkillPoisonCloud(&pGS->m_World, OwnerCID, pCaster->GetPos(), Radius, maximum(1, MobAttack / 6));
		return true;
	}

	if(str_comp(Ability.m_aKey, "entangle") == 0)
	{
		const int TargetCID = pTarget ? pTarget->GetCID() : -1;
		SpawnSkillCastFeedback(&pGS->m_World, "entangle", pCaster->GetPos(), 48.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_POISON, 40.f);
		SpawnSkillMagicBolt(&pGS->m_World, OwnerCID, pCaster->GetPos() + Dir * 24.f, Dir, 1, SKILL_BOLT_ENTANGLE, TargetCID);
		return true;
	}

	if(str_comp(Ability.m_aKey, "life_drain") == 0)
	{
		const int Dmg = maximum(2, (int)(MobAttack * Ability.m_ScaleAttack));
		const int TargetCID = pTarget ? pTarget->GetCID() : -1;
		SpawnSkillCastFeedback(&pGS->m_World, "life_drain", pCaster->GetPos(), 48.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_SHADOW, 40.f);
		SpawnSkillMagicBolt(&pGS->m_World, OwnerCID, pCaster->GetPos() + Dir * 24.f, Dir, Dmg, SKILL_BOLT_LIFE_DRAIN, TargetCID);
		return true;
	}

	if(str_comp(Ability.m_aKey, "ground_slam") == 0)
	{
		const vec2 Pos = pCaster->GetPos();
		const int BaseDmg = maximum(2, (int)(MobAttack * Ability.m_ScaleAttack));
		SpawnSkillCastFeedback(&pGS->m_World, "ground_slam", Pos, Radius);
		SpawnSkillZoneAura(&pGS->m_World, Pos, pGS->Server()->TickSpeed() * 2, SKILL_VFX_PHYSICAL, Radius * 0.45f);
		ForEachMobHostileInRadius(&pGS->m_World, pMob, Pos, Radius, [&](CCharacter *pHitChr) {
			const vec2 TargetPos = pHitChr->GetPos();
			const float Dist = distance(Pos, TargetPos);
			vec2 KnockDir = normalize(TargetPos - Pos);
			if(length(KnockDir) < 0.01f)
				KnockDir = vec2(1.f, 0.f);
			pHitChr->TakeDamage(KnockDir * (8.f * (1.f - Dist / Radius)), Pos, BaseDmg, OwnerCID, WEAPON_HAMMER);
			return true;
		});
		SpawnSkillHitBurst(&pGS->m_World, Pos, Radius * 0.5f, SKILL_VFX_PHYSICAL);
		pGS->m_World.CreateSound(Pos, SOUND_HAMMER_FIRE);
		return true;
	}

	if(str_comp(Ability.m_aKey, "thunder_clap") == 0)
	{
		if(!pGS->Core() || !pGS->Core()->StatusManager())
			return false;
		const vec2 Pos = pCaster->GetPos();
		const int BaseDmg = maximum(2, (int)(MobAttack * Ability.m_ScaleAttack));
		const int RootTicks = pGS->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&pGS->m_World, "thunder_clap", Pos, Radius);
		SpawnSkillZoneAura(&pGS->m_World, Pos, pGS->Server()->TickSpeed() * 2, SKILL_VFX_PHYSICAL, Radius * 0.4f);
		ForEachMobHostileInRadius(&pGS->m_World, pMob, Pos, Radius, [&](CCharacter *pHitChr) {
			pHitChr->TakeDamage(vec2(0, 0), Pos, BaseDmg, OwnerCID, WEAPON_HAMMER);
			pGS->Core()->StatusManager()->ApplyStatus(pHitChr, "frost", 1, RootTicks, 0.08f);
			return true;
		});
		SpawnSkillHitBurst(&pGS->m_World, Pos, Radius * 0.45f, SKILL_VFX_PHYSICAL);
		pGS->m_World.CreateSound(Pos, SOUND_HAMMER_FIRE);
		return true;
	}

	if(str_comp(Ability.m_aKey, "meteor_strike") == 0)
	{
		vec2 TargetPos = pCaster->GetPos() + Dir * (Ability.m_Range > 1.f ? Ability.m_Range : 320.f);
		const int Dmg = maximum(4, (int)(MobAttack * Ability.m_ScaleAttack * 2.f));
		const int WarningTicks = pGS->Server()->TickSpeed();
		SpawnSkillCastFeedback(&pGS->m_World, "meteor_strike", TargetPos, Radius);
		SpawnSkillMeteor(&pGS->m_World, OwnerCID, TargetPos, Dmg, Radius, WarningTicks);
		return true;
	}

	if(str_comp(Ability.m_aKey, "void_rift") == 0 || str_comp(Ability.m_aKey, "black_hole") == 0)
	{
		vec2 TargetPos = pTarget ? pTarget->GetCharacter()->GetPos() : pCaster->GetPos() + Dir * 200.f;
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.5f));
		SpawnSkillCastFeedback(&pGS->m_World, Ability.m_aKey, TargetPos, Radius);
		SpawnSkillVoidVortex(&pGS->m_World, OwnerCID, TargetPos, Radius, Dmg, pGS->Server()->TickSpeed());
		return true;
	}

	if(str_comp(Ability.m_aKey, "celestial_beam") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack));
		const int BeamTicks = pGS->Server()->TickSpeed();
		SpawnSkillCastFeedback(&pGS->m_World, "celestial_beam", pCaster->GetPos(), 96.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, BeamTicks, SKILL_VFX_HOLY, 72.f);
		SpawnSkillBeamSweep(&pGS->m_World, OwnerCID, pCaster->GetPos(), angle(Dir), 280.f, Dmg, BeamTicks);
		return true;
	}

	if(str_comp(Ability.m_aKey, "phoenix_flame") == 0)
	{
		if(!pGS->Core() || !pGS->Core()->StatusManager())
			return false;
		const vec2 Pos = pCaster->GetPos();
		const int BurnDmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack));
		const int BurnDuration = pGS->Server()->TickSpeed() * 4;
		SpawnSkillCastFeedback(&pGS->m_World, "phoenix_flame", Pos, Radius);
		SpawnSkillZoneAura(&pGS->m_World, Pos, BurnDuration, SKILL_VFX_FIRE, Radius * 0.5f);
		ForEachMobHostileInRadius(&pGS->m_World, pMob, Pos, Radius, [&](CCharacter *pHitChr) {
			pHitChr->TakeDamage(vec2(0, 0), Pos, BurnDmg, OwnerCID, WEAPON_GUN);
			pGS->Core()->StatusManager()->ApplyStatus(pHitChr, "burn", 1, pGS->Server()->TickSpeed() * 4);
			SpawnSkillHitBurst(&pGS->m_World, pHitChr->GetPos(), 48.f, SKILL_VFX_FIRE);
			return true;
		});
		SpawnSkillHitBurst(&pGS->m_World, Pos, Radius * 0.55f, SKILL_VFX_FIRE, 0, 14);
		return true;
	}

	if(str_comp(Ability.m_aKey, "forked_lightning") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.2f));
		SpawnSkillCastFeedback(&pGS->m_World, "forked_lightning", pCaster->GetPos(), 64.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 44.f);
		SpawnSkillForkLightning(&pGS->m_World, OwnerCID, pCaster->GetPos(), Dir, Dmg, Radius * 2.f, 2);
		pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 3);
		return true;
	}

	if(str_comp(Ability.m_aKey, "electro_arc") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.3f));
		const vec2 From = pCaster->GetPos();
		vec2 To = pTarget ? pTarget->GetCharacter()->GetPos() : From + Dir * Radius;
		if(pGS->Collision())
			pGS->Collision()->IntersectLine(From, To, 0x0, &To);
		SpawnSkillCastFeedback(&pGS->m_World, "electro_arc", To, 72.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 40.f);
		SpawnSkillElectroArc(&pGS->m_World, OwnerCID, From, To, Dmg);
		return true;
	}

	if(str_comp(Ability.m_aKey, "super_nova") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.5f));
		SpawnSkillCastFeedback(&pGS->m_World, "super_nova", pCaster->GetPos(), Radius);
		SpawnSkillSuperNova(&pGS->m_World, OwnerCID, pCaster->GetPos(), Dmg, 3);
		return true;
	}

	if(str_comp(Ability.m_aKey, "smoke_veil") == 0)
	{
		const int Duration = pGS->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&pGS->m_World, "smoke_veil", pCaster->GetPos(), Radius);
		SpawnSkillSmokeVeil(&pGS->m_World, OwnerCID, pCaster->GetPos(), Radius, Duration, pGS->Server()->TickSpeed() * 2, 0.5f);
		return true;
	}

	if(str_comp(Ability.m_aKey, "death_spiral") == 0)
	{
		const int Dmg = maximum(2, (int)(MobAttack * Ability.m_ScaleAttack));
		const int SpiralTicks = pGS->Server()->TickSpeed();
		SpawnSkillCastFeedback(&pGS->m_World, "death_spiral", pCaster->GetPos(), 80.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, SpiralTicks, SKILL_VFX_ARCANE, 68.f);
		SpawnSkillSpinLaser(&pGS->m_World, OwnerCID, angle(Dir), 48.f, 240.f, Dmg, SpiralTicks);
		return true;
	}

	if(str_comp(Ability.m_aKey, "plasma_field") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.2f));
		const int Radius = maximum(6, (int)(Ability.m_Range / 32.f));
		const int GrowTicks = pGS->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&pGS->m_World, "plasma_field", pCaster->GetPos(), (float)Radius * 32.f);
		SpawnSkillZoneAura(&pGS->m_World, pCaster->GetPos(), GrowTicks, SKILL_VFX_ARCANE, (float)Radius * 28.f);
		new CGrowingExplosion(&pGS->m_World, pCaster->GetPos(), Dir, OwnerCID, Radius, GROWINGEXPLOSIONEFFECT_ELECTRIC, false, GE_TARGET_MMO_HOSTILE, Dmg);
		pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 3);
		return true;
	}

	if(str_comp(Ability.m_aKey, "flash_wave") == 0)
	{
		const int Radius = maximum(6, (int)(Ability.m_Range / 32.f));
		const int GrowTicks = pGS->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&pGS->m_World, "flash_wave", pCaster->GetPos(), (float)Radius * 32.f);
		SpawnSkillZoneAura(&pGS->m_World, pCaster->GetPos(), GrowTicks, SKILL_VFX_FROST, (float)Radius * 28.f);
		new CGrowingExplosion(&pGS->m_World, pCaster->GetPos(), Dir, OwnerCID, Radius, GROWINGEXPLOSIONEFFECT_FREEZE, false, GE_TARGET_MMO_HOSTILE);
		pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 2);
		return true;
	}

	if(str_comp(Ability.m_aKey, "gravity_well") == 0)
	{
		vec2 TargetPos = pTarget ? pTarget->GetCharacter()->GetPos() : pCaster->GetPos() + Dir * 200.f;
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.5f));
		const int ExplosionRadiusTiles = maximum(4, (int)(Radius / 32.f));
		const int PullTicks = (int)(pGS->Server()->TickSpeed() * 2.5f);
		SpawnSkillCastFeedback(&pGS->m_World, "gravity_well", TargetPos, Radius);
		SpawnSkillGravityWell(&pGS->m_World, OwnerCID, TargetPos, Radius, PullTicks, ExplosionRadiusTiles, Dmg);
		return true;
	}

	if(str_comp(Ability.m_aKey, "scatter_blast") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack));
		const int ExplosionRadiusTiles = maximum(3, (int)(Radius / 40.f));
		const vec2 SpawnPos = pCaster->GetPos() + Dir * 32.f;
		SpawnSkillCastFeedback(&pGS->m_World, "scatter_blast", SpawnPos, 64.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_FIRE, 40.f);
		SpawnSkillScatterBlast(&pGS->m_World, OwnerCID, SpawnPos, Dir, Dmg, ExplosionRadiusTiles, 3);
		return true;
	}

	if(str_comp(Ability.m_aKey, "detonation_beam") == 0)
	{
		const int Dmg = maximum(4, (int)(MobAttack * Ability.m_ScaleAttack * 1.3f));
		const int ExplosionRadiusTiles = maximum(3, (int)(Radius / 32.f));
		const vec2 From = pCaster->GetPos();
		vec2 To = pTarget ? pTarget->GetCharacter()->GetPos() : From + Dir * Radius;
		if(pGS->Collision())
			pGS->Collision()->IntersectLine(From, To, 0x0, &To);
		SpawnSkillCastFeedback(&pGS->m_World, "detonation_beam", To, (float)ExplosionRadiusTiles * 32.f);
		SpawnSkillDetonationBeam(&pGS->m_World, OwnerCID, From, To, ExplosionRadiusTiles, Dmg, true);
		return true;
	}

	if(str_comp(Ability.m_aKey, "homing_plasma") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack * 1.2f));
		const int ExplosionRadiusTiles = maximum(3, (int)(Radius / 40.f));
		const float TrackingStrength = 10.f;
		const vec2 From = pCaster->GetPos() + Dir * 24.f;
		int TrackedCID = pTarget ? pTarget->GetCID() : -1;
		SpawnSkillCastFeedback(&pGS->m_World, "homing_plasma", From, 48.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 40.f);
		SpawnSkillHomingPlasma(&pGS->m_World, OwnerCID, From, Dir, Dmg, ExplosionRadiusTiles, TrackedCID, TrackingStrength);
		return true;
	}

	if(str_comp(Ability.m_aKey, "ricochet_shot") == 0)
	{
		const int Dmg = maximum(3, (int)(MobAttack * Ability.m_ScaleAttack));
		const vec2 SpawnPos = pCaster->GetPos() + Dir * 32.f;
		SpawnSkillCastFeedback(&pGS->m_World, "ricochet_shot", SpawnPos, 48.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_ARCANE, 40.f);
		SpawnSkillRicochetShot(&pGS->m_World, OwnerCID, SpawnPos, Dir, Dmg);
		return true;
	}

	if(str_comp(Ability.m_aKey, "laser_trap") == 0)
	{
		const int Dmg = maximum(4, (int)(MobAttack * Ability.m_ScaleAttack * 1.2f));
		const int DurationTicks = pGS->Server()->TickSpeed() * 15;
		const vec2 From = pCaster->GetPos();
		vec2 To = pTarget ? pTarget->GetCharacter()->GetPos() : From + Dir * Radius;
		if(pGS->Collision())
			pGS->Collision()->IntersectLine(From, To, 0x0, &To);
		SpawnSkillCastFeedback(&pGS->m_World, "laser_trap", To, 96.f);
		SpawnSkillLaserTrap(&pGS->m_World, OwnerCID, From, To, 4, Dmg, DurationTicks);
		return true;
	}

	if(str_comp(Ability.m_aKey, "bomb_sentinel") == 0)
	{
		const int MaxShots = 3;
		const int Dmg = maximum(8, (int)(MobAttack * Ability.m_ScaleAttack * 2.f));
		const float Range = maximum(400.f, Ability.m_Range);
		SpawnSkillCastFeedback(&pGS->m_World, "bomb_sentinel", pCaster->GetPos(), 80.f);
		SpawnSkillBombSentinel(&pGS->m_World, OwnerCID, pCaster->GetPos(), MaxShots, Dmg, Range, 1.f);
		return true;
	}

	if(str_comp(Ability.m_aKey, "acid_pool") == 0)
	{
		const int Stacks = maximum(1, (int)(MobAttack * Ability.m_ScaleAttack * 0.5f));
		const int DurationTicks = pGS->Server()->TickSpeed() * 8;
		vec2 TargetPos = pTarget ? pTarget->GetCharacter()->GetPos() : pCaster->GetPos();
		SpawnSkillCastFeedback(&pGS->m_World, "acid_pool", TargetPos, Radius);
		SpawnSkillAcidPool(&pGS->m_World, OwnerCID, TargetPos, Radius, Stacks, DurationTicks, 0.8f);
		return true;
	}

	if(str_comp(Ability.m_aKey, "corpse_burst") == 0)
	{
		if(!pGS->Core() || !pGS->Core()->StatusManager())
			return false;
		const vec2 Pos = pCaster->GetPos();
		const int Dmg = maximum(8, (int)(MobAttack * Ability.m_ScaleAttack * 1.8f));
		const int RadiusTiles = maximum(6, (int)(Radius / 32.f));
		const float RadiusPx = (float)RadiusTiles * 32.f;
		const int SlowTicks = pGS->Server()->TickSpeed();
		const int GrowTicks = pGS->Server()->TickSpeed() * 2;
		SpawnSkillCastFeedback(&pGS->m_World, "corpse_burst", Pos, RadiusPx);
		SpawnSkillZoneAura(&pGS->m_World, Pos, GrowTicks, SKILL_VFX_POISON, RadiusPx * 0.45f);
		new CGrowingExplosion(&pGS->m_World, Pos, Dir, OwnerCID, RadiusTiles, GROWINGEXPLOSIONEFFECT_BOOM, false, GE_TARGET_MMO_HOSTILE, Dmg);
		ForEachMobHostileInRadius(&pGS->m_World, pMob, Pos, RadiusPx, [&](CCharacter *pHitChr) {
			pGS->Core()->StatusManager()->ApplyStatus(pHitChr, "frost", 1, SlowTicks, 0.60f);
			return true;
		});
		pGS->m_World.CreateSound(Pos, SOUND_GRENADE_EXPLODE);
		pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 2);
		return true;
	}

	if(str_comp(Ability.m_aKey, "smoke_hook") == 0)
	{
		if(!pGS->Core() || !pGS->Core()->StatusManager())
			return false;
		const int HookDmg = maximum(2, (int)(MobAttack * Ability.m_ScaleAttack));
		const int TargetCID = pTarget ? pTarget->GetCID() : -1;
		if(TargetCID < 0)
			return false;
		vec2 HookDir = Dir;
		if(pTarget)
		{
			HookDir = normalize(pTarget->GetCharacter()->GetPos() - pCaster->GetPos());
			if(length(HookDir) < 0.01f)
				HookDir = Dir;
		}
		SpawnSkillCastFeedback(&pGS->m_World, "smoke_hook", pCaster->GetPos(), 48.f);
		SpawnSkillFollowAura(&pGS->m_World, OwnerCID, pGS->Server()->TickSpeed() / 2, SKILL_VFX_POISON, 40.f);
		SpawnSkillMagicBolt(&pGS->m_World, OwnerCID, pCaster->GetPos() + HookDir * 24.f, HookDir, HookDmg, SKILL_BOLT_SMOKE_HOOK, TargetCID);
		pCaster->SetEmote(EMOTE_PAIN, pGS->Server()->Tick() + pGS->Server()->TickSpeed() / 3);
		return true;
	}

	if(str_comp(Ability.m_aKey, "web_snare") == 0)
	{
		if(!pGS->Core() || !pGS->Core()->StatusManager() || !pTarget)
			return false;
		const int SnareTicks = pGS->Server()->TickSpeed() * 3 / 2 + maximum(0, MobLevel / 2);
		const int ApplyDmg = maximum(1, (int)(MobAttack * Ability.m_ScaleAttack * 0.5f));
		CCharacter *pTargetChr = pTarget->GetCharacter();
		pGS->Core()->StatusManager()->ApplyStatus(pTargetChr, "frost", 1, SnareTicks, 0.30f);
		if(ApplyDmg > 0)
			pTargetChr->TakeDamage(vec2(0.f, 0.f), pCaster->GetPos(), ApplyDmg, OwnerCID, WEAPON_GAME);
		SpawnSkillCastFeedback(&pGS->m_World, "web_snare", pTargetChr->GetPos(), 56.f);
		SpawnSkillFollowAura(&pGS->m_World, pTarget->GetCID(), SnareTicks, SKILL_VFX_POISON, 48.f);
		SpawnSkillHitBurst(&pGS->m_World, pTargetChr->GetPos(), 48.f, SKILL_VFX_POISON);
		pGS->m_World.CreateSound(pTargetChr->GetPos(), SOUND_HOOK_ATTACH_PLAYER);
		return true;
	}

	return false;
}
