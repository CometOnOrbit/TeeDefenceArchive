#include "skill_void_vortex.h"
#include "skill_spawn.h"
#include "skill_vfx_common.h"

#include <game/server/gamecontext.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <generated/protocol.h>
#include <generated/server_data.h>
#include <generated/server_data.h>

#include "../character.h"

CSkillVoidVortex::CSkillVoidVortex(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int Damage, int PullTicks)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, (int)maximum(64.f, Radius))
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_MaxRadius = maximum(96.f, Radius);
	m_Radius = 0.f;
	m_Damage = maximum(1, Damage);
	m_LifeSpan = maximum(1, PullTicks);
	m_StartTick = Server()->Tick();
	m_BurstPhase = false;

	AddSnappingGroupIds(SNAP_GROUP_RING, 16);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	SpawnSkillCastRing(GameWorld(), m_Pos, m_MaxRadius, m_LifeSpan, SKILL_VFX_SHADOW, SKILL_RING_EXPAND_HOLD, m_LifeSpan / 2);
	GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
	GameWorld()->InsertEntity(this);
}

void CSkillVoidVortex::Burst()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	SpawnSkillHitBurst(GameWorld(), m_Pos, m_MaxRadius, SKILL_VFX_SHADOW, 0, 20);
	SpawnSkillCastRing(GameWorld(), m_Pos, m_MaxRadius * 1.1f, Server()->TickSpeed() / 3, SKILL_VFX_ARCANE, SKILL_RING_EXPAND_BURST, Server()->TickSpeed() / 6);

	if(pOwnerChar)
		GameWorld()->CreateExplosion(m_Pos, pOwnerChar, WEAPON_LASER, maximum(2, m_Damage / 2));

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pTarget = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
			continue;
		if(distance(m_Pos, pTarget->GetPos()) > m_MaxRadius)
			continue;

		vec2 Dir = normalize(m_Pos - pTarget->GetPos());
		if(length(Dir) < 0.01f)
			Dir = vec2(1.f, 0.f);
		pTarget->TakeDamage(Dir * 14.f, m_Pos, m_Damage, m_Owner, WEAPON_LASER);
	}
}

void CSkillVoidVortex::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	const int Elapsed = Server()->Tick() - m_StartTick;
	const int GrowDuration = maximum(1, m_LifeSpan / 2);
	m_Radius = minimum(m_MaxRadius, m_MaxRadius * (float)Elapsed / (float)GrowDuration);

	if(!m_BurstPhase)
	{
		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pTarget = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
				continue;

			const float Dist = distance(m_Pos, pTarget->GetPos());
			if(Dist > m_Radius || Dist < 16.f)
				continue;

			const vec2 PullDir = normalize(m_Pos - pTarget->GetPos());
			pTarget->GetCore()->m_Vel += PullDir * (8.f * (1.f - Dist / m_MaxRadius));
		}

		if((Elapsed % maximum(1, Server()->TickSpeed() / 5)) == 0)
			GameWorld()->CreateDeath(m_Pos, -1);
	}

	if(--m_LifeSpan <= 0 && !m_BurstPhase)
	{
		m_BurstPhase = true;
		Burst();
		MarkForDestroy();
	}
}

void CSkillVoidVortex::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const int Elapsed = Server()->Tick() - m_StartTick;
	const float Spin = (float)Elapsed / (float)maximum(1, Server()->TickSpeed()) * 2.f;
	const array<int> *pDotIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pDotIds || pDotIds->size() < 16)
		return;

	const float AngleStep = 2.f * pi / 16.f;
	for(int i = 0; i < 16; i++)
	{
		const float Angle = Spin + AngleStep * (float)i;
		const float R = m_Radius * (0.65f + 0.35f * sinf(Angle * 2.f));
		const vec2 DotPos = m_Pos + vec2(R * cosf(Angle), R * sinf(Angle));
		CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pDotIds)[i], sizeof(CNetObj_Laser)));
		if(!pLaser)
			continue;
		pLaser->m_X = round_to_int(DotPos.x);
		pLaser->m_Y = round_to_int(DotPos.y);
		pLaser->m_FromX = round_to_int(m_Pos.x);
		pLaser->m_FromY = round_to_int(m_Pos.y);
		pLaser->m_StartTick = m_StartTick;
	}
}
