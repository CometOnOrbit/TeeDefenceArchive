#include <generated/protocol.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "../character.h"
#include "mmo_weapon_common.h"
#include "tracked_plasma.h"

CMMOTrackedPlasma::CMMOTrackedPlasma(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int Damage, float SpeedMin, float SpeedMax)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 24)
{
	m_Owner = OwnerCID;
	m_Direction = normalize(Direction);
	m_Damage = Damage;
	m_TrackedCID = -1;
	m_StartTick = Server()->Tick();
	m_SpeedMin = SpeedMin;
	m_SpeedMax = maximum(SpeedMin, SpeedMax);

	GameWorld()->InsertEntity(this);
}

CCharacter *CMMOTrackedPlasma::GetTrackedTarget()
{
	if(m_TrackedCID < 0 || m_TrackedCID >= MAX_CLIENTS)
		return nullptr;
	CPlayer *pPl = GameServer()->m_apPlayers[m_TrackedCID];
	if(!pPl)
		return nullptr;
	CCharacter *pChar = pPl->GetCharacter();
	if(!pChar || !MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
		return nullptr;
	return pChar;
}

void CMMOTrackedPlasma::SearchPotentialTarget()
{
	int MinDistanceCID = -1;
	float MinDistance = 2400.0f;

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
			continue;

		if(GameServer()->Collision()->IntersectLineWithInvisible(m_Pos, pChar->GetPos(), nullptr, nullptr))
			continue;

		const float Dist = distance(pChar->GetPos(), m_Pos);
		if(Dist < MinDistance)
		{
			MinDistance = Dist;
			MinDistanceCID = pChar->GetCID();
		}
	}

	if(MinDistanceCID > -1)
		m_TrackedCID = MinDistanceCID;
}

void CMMOTrackedPlasma::Explode(CCharacter *pOwnerChar)
{
	if(pOwnerChar)
		GameWorld()->CreateExplosion(m_Pos, pOwnerChar, WEAPON_LASER, m_Damage);
	MarkForDestroy();
}

void CMMOTrackedPlasma::Tick()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
	{
		MarkForDestroy();
		return;
	}

	if(GameServer()->Collision()->CheckPoint(m_Pos))
	{
		Explode(pOwnerChar);
		return;
	}

	const float Ramp = minimum(1.f, (Server()->Tick() - m_StartTick) / (float)maximum(1, Server()->TickSpeed()));
	const float Speed = m_SpeedMin + Ramp * (m_SpeedMax - m_SpeedMin);
	m_Pos += normalize(m_Direction) * Speed;

	CCharacter *pTarget = GetTrackedTarget();
	if(!pTarget)
	{
		SearchPotentialTarget();
		return;
	}

	if(distance(m_Pos, pTarget->GetPos()) < 24.0f)
	{
		Explode(pOwnerChar);
		return;
	}

	m_Direction = normalize(pTarget->GetPos() - m_Pos);
}

void CMMOTrackedPlasma::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = round_to_int(m_Pos.x);
	pObj->m_Y = round_to_int(m_Pos.y);
	pObj->m_FromX = round_to_int(m_Pos.x);
	pObj->m_FromY = round_to_int(m_Pos.y);
	pObj->m_StartTick = Server()->Tick();
}
