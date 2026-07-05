#include "skill_homing_plasma.h"
#include "skill_spawn.h"

#include <game/server/entities/growingexplosion.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <generated/protocol.h>
#include <generated/server_data.h>

#include "../character.h"

CSkillHomingPlasma::CSkillHomingPlasma(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int ExplosionRadiusTiles, int TrackedCID, float TrackingStrength)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 16)
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_Dir = normalize(Dir);
	if(length(m_Dir) < 0.01f)
		m_Dir = vec2(1.f, 0.f);
	m_Damage = maximum(1, Damage);
	m_ExplosionRadiusTiles = maximum(2, ExplosionRadiusTiles);
	m_TrackedCID = TrackedCID;
	m_TrackingStrength = clamp(TrackingStrength, 0.5f, 24.f);
	m_LifeSpan = Server()->TickSpeed() * 3;
	m_StartTick = Server()->Tick();
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);
	GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
	GameWorld()->InsertEntity(this);
}

void CSkillHomingPlasma::Explode()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	SpawnSkillHitBurst(GameWorld(), m_Pos, (float)m_ExplosionRadiusTiles * 32.f, SKILL_VFX_ARCANE, 0, 10);
	new CGrowingExplosion(GameWorld(), m_Pos, m_Dir, m_Owner, m_ExplosionRadiusTiles, GROWINGEXPLOSIONEFFECT_BOOM, false, GE_TARGET_MMO_HOSTILE, m_Damage);
	MarkForDestroy();
}

void CSkillHomingPlasma::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	CCharacter *pTarget = nullptr;
	if(m_TrackedCID >= 0 && m_TrackedCID < MAX_CLIENTS)
	{
		CPlayer *pPl = GameServer()->m_apPlayers[m_TrackedCID];
		if(pPl && pPl->GetCharacter() && MMOWeaponTargetValid(GameServer(), m_Owner, pPl->GetCharacter()))
			pTarget = pPl->GetCharacter();
	}

	if(!pTarget)
	{
		float BestDist = 2400.f;
		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pChar = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
				continue;
			const float Dist = distance(m_Pos, pChar->GetPos());
			if(Dist < BestDist)
			{
				BestDist = Dist;
				pTarget = pChar;
				m_TrackedCID = pChar->GetCID();
			}
		}
	}

	if(pTarget)
	{
		const float Dist = distance(m_Pos, pTarget->GetPos());
		if(Dist < 24.f)
		{
			Explode();
			return;
		}

		m_Dir = normalize(pTarget->GetPos() - m_Pos);
		const float Speed = minimum(Dist, m_TrackingStrength);
		m_Pos += m_Dir * Speed;
	}
	else
	{
		m_Pos += m_Dir * 12.f;
	}

	if(GameServer()->Collision()->CheckPoint(m_Pos))
	{
		Explode();
		return;
	}

	if(--m_LifeSpan <= 0)
		Explode();
}

void CSkillHomingPlasma::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	vec2 From = m_Pos - m_Dir * 20.f;
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;

	pLaser->m_X = round_to_int(m_Pos.x);
	pLaser->m_Y = round_to_int(m_Pos.y);
	pLaser->m_FromX = round_to_int(From.x);
	pLaser->m_FromY = round_to_int(From.y);
	pLaser->m_StartTick = m_StartTick;

	const array<int> *pCoreIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pCoreIds && pCoreIds->size() > 0)
	{
		const vec2 CoreFrom = m_Pos + vec2(0.f, -8.f);
		CNetObj_Laser *pCore = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pCoreIds)[0], sizeof(CNetObj_Laser)));
		if(pCore)
		{
			pCore->m_X = round_to_int(m_Pos.x);
			pCore->m_Y = round_to_int(m_Pos.y);
			pCore->m_FromX = round_to_int(CoreFrom.x);
			pCore->m_FromY = round_to_int(CoreFrom.y);
			pCore->m_StartTick = m_StartTick;
		}
	}
}
