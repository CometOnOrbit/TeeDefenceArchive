#include "skill_gravity_well.h"
#include "skill_spawn.h"
#include "skill_vfx_common.h"

#include <game/server/entities/growingexplosion.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <generated/protocol.h>
#include <generated/server_data.h>

#include "../character.h"

CSkillGravityWell::CSkillGravityWell(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int PullTicks, int ExplosionRadiusTiles, int ExplosionDamage, bool FinalExplosion)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, (int)maximum(64.f, Radius))
{
	m_Center = Pos;
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_MaxRadius = maximum(96.f, Radius);
	m_Radius = 0.f;
	m_LifeSpan = maximum(1, PullTicks);
	m_StartTick = Server()->Tick();
	m_ExplosionRadiusTiles = maximum(1, ExplosionRadiusTiles);
	m_ExplosionDamage = maximum(1, ExplosionDamage);
	m_FinalExplosion = FinalExplosion;
	m_Ended = false;

	AddSnappingGroupIds(SNAP_GROUP_RING, 12);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);
	AddSnappingGroupIds(SNAP_GROUP_TOWER_BODY, 8);

	SpawnSkillCastRing(GameWorld(), m_Center, m_MaxRadius, m_LifeSpan, SKILL_VFX_HOLY, SKILL_RING_EXPAND_HOLD, m_LifeSpan / 2);
	GameWorld()->CreateSound(m_Center, SOUND_WEAPON_SPAWN);
	GameWorld()->InsertEntity(this);
}

void CSkillGravityWell::Finish()
{
	if(m_Ended)
		return;
	m_Ended = true;

	if(m_FinalExplosion)
	{
		GameWorld()->CreateSound(m_Center, SOUND_GRENADE_EXPLODE);
		SpawnSkillHitBurst(GameWorld(), m_Center, m_MaxRadius, SKILL_VFX_HOLY, 0, 16);
		new CGrowingExplosion(GameWorld(), m_Center, vec2(0.f, -1.f), m_Owner, m_ExplosionRadiusTiles, GROWINGEXPLOSIONEFFECT_BOOM, false, GE_TARGET_MMO_HOSTILE, m_ExplosionDamage);
	}
}

void CSkillGravityWell::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	const int Elapsed = Server()->Tick() - m_StartTick;
	const int GrowDuration = maximum(1, m_LifeSpan / 3);
	m_Radius = minimum(m_MaxRadius, m_MaxRadius * (float)Elapsed / (float)GrowDuration);

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pTarget = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
			continue;

		const float Dist = distance(m_Center, pTarget->GetPos());
		if(Dist > m_Radius || Dist < 12.f)
			continue;

		const vec2 PullDir = normalize(m_Center - pTarget->GetPos());
		const float Intensity = 10.f * (1.f - Dist / m_MaxRadius);
		pTarget->GetCore()->m_Vel += PullDir * Intensity;
	}

	if((Elapsed % maximum(1, Server()->TickSpeed() / 6)) == 0)
		GameWorld()->CreateHammerHit(m_Center + vec2((float)(random_int() % 32 - 16), (float)(random_int() % 32 - 16)));

	if(--m_LifeSpan <= 0)
	{
		Finish();
		MarkForDestroy();
	}
}

void CSkillGravityWell::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const int Elapsed = Server()->Tick() - m_StartTick;
	const float Spin = (float)Elapsed / (float)maximum(1, Server()->TickSpeed()) * 1.5f;
	const array<int> *pDotIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pDotIds || pDotIds->size() < 12)
		return;

	const float AngleStep = 2.f * pi / 12.f;
	for(int i = 0; i < 12; i++)
	{
		const float Angle = Spin + AngleStep * (float)i;
		const float R = m_Radius * (0.7f + 0.3f * sinf(Angle * 3.f));
		const vec2 DotPos = m_Center + vec2(R * cosf(Angle), R * sinf(Angle));
		CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pDotIds)[i], sizeof(CNetObj_Laser)));
		if(!pLaser)
			continue;
		pLaser->m_X = round_to_int(DotPos.x);
		pLaser->m_Y = round_to_int(DotPos.y);
		pLaser->m_FromX = round_to_int(m_Center.x);
		pLaser->m_FromY = round_to_int(m_Center.y);
		pLaser->m_StartTick = m_StartTick;
	}

	const array<int> *pSpiralIds = FindSnappingGroupIds(SNAP_GROUP_TOWER_BODY);
	if(pSpiralIds && pSpiralIds->size() >= 8)
	{
		const float GrowT = minimum(1.f, (float)Elapsed / (float)maximum(1, m_LifeSpan));
		const float Inward = 1.f - GrowT * 0.85f;
		for(int i = 0; i < 8; i++)
		{
			const float Angle = Spin * 2.f + 2.f * pi * (float)i / 8.f;
			const float R = m_MaxRadius * Inward * (0.5f + 0.5f * (float)(i + 1) / 8.f);
			const vec2 DotPos = m_Center + vec2(R * cosf(Angle), R * sinf(Angle));
			SnapLaserDot(Server(), (*pSpiralIds)[i], DotPos, m_StartTick);
		}
	}
}
