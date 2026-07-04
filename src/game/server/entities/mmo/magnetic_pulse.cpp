#include <generated/protocol.h>
#include <generated/server_data.h>

#include <game/server/gamecontext.h>

#include "../character.h"
#include "magnetic_pulse.h"
#include "mmo_weapon_common.h"

CMMOMagneticPulse::CMMOMagneticPulse(CGameWorld *pGameWorld, int OwnerCID, float Radius, vec2 Pos, vec2 Direction)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 24)
{
	m_Owner = OwnerCID;
	m_Direction = Direction;
	m_PosTo = Pos;
	m_LastPhase = false;
	m_LifeTick = Server()->TickSpeed() * 2;
	m_MaxRadius = Radius;
	m_Radius = 0.f;
	m_RadiusGrowStartTick = 0;

	GameWorld()->CreateSound(m_Pos, SOUND_LASER_FIRE);
	GameWorld()->InsertEntity(this);
}

void CMMOMagneticPulse::RunLastPhase()
{
	if(m_LastPhase)
		return;

	m_LastPhase = true;
	m_RadiusGrowStartTick = Server()->Tick();
	AddSnappingGroupIds(SNAP_GROUP_RING, 8);
	GameWorld()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);
}

void CMMOMagneticPulse::TickFirstPhase()
{
	const vec2 NormalizedDirection = normalize(m_Direction);
	const vec2 OldPos = m_Pos;
	m_Pos += NormalizedDirection * 10.f;
	m_PosTo = OldPos;

	if(GameServer()->Collision()->CheckPoint(m_Pos))
	{
		RunLastPhase();
		return;
	}

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
			continue;

		if(distance(m_Pos, pChar->GetPos()) <= 64.f)
		{
			RunLastPhase();
			return;
		}
	}
}

void CMMOMagneticPulse::TickLastPhase()
{
	if(--m_LifeTick <= 0)
	{
		MarkForDestroy();
		return;
	}

	const int GrowDuration = maximum(1, Server()->TickSpeed() / 5);
	const int Elapsed = Server()->Tick() - m_RadiusGrowStartTick;
	m_Radius = minimum(m_MaxRadius, m_MaxRadius * (float)Elapsed / (float)GrowDuration);

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
			continue;

		const float Dist = distance(m_Pos, pChar->GetPos());
		if(Dist > m_Radius || Dist < 24.f)
			continue;

		const vec2 Dir = normalize(pChar->GetPos() - m_Pos);
		pChar->GetCore()->m_Vel += -Dir * 5.f;
	}
}

void CMMOMagneticPulse::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	if(!m_LastPhase)
		TickFirstPhase();
	else
		TickLastPhase();
}

void CMMOMagneticPulse::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	if(m_LastPhase)
	{
		const array<int> *pRingIds = FindSnappingGroupIds(SNAP_GROUP_RING);
		if(pRingIds && pRingIds->size() >= 8)
		{
			const float AngleStep = 2.0f * pi / 8.f;
			for(int i = 0; i < 8; i++)
			{
				const int NextIndex = (i + 1) % 8;
				const vec2 CurrentPos = m_Pos + vec2(m_Radius * cosf(AngleStep * i), m_Radius * sinf(AngleStep * i));
				const vec2 NextPos = m_Pos + vec2(m_Radius * cosf(AngleStep * NextIndex), m_Radius * sinf(AngleStep * NextIndex));
				CNetObj_Laser *pRing = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pRingIds)[i], sizeof(CNetObj_Laser)));
				if(pRing)
				{
					pRing->m_X = round_to_int(CurrentPos.x);
					pRing->m_Y = round_to_int(CurrentPos.y);
					pRing->m_FromX = round_to_int(NextPos.x);
					pRing->m_FromY = round_to_int(NextPos.y);
					pRing->m_StartTick = Server()->Tick() - 1;
				}
			}
		}
	}

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = round_to_int(m_Pos.x);
	pObj->m_Y = round_to_int(m_Pos.y);
	pObj->m_FromX = round_to_int(m_PosTo.x);
	pObj->m_FromY = round_to_int(m_PosTo.y);
	pObj->m_StartTick = Server()->Tick() - 1;
}
