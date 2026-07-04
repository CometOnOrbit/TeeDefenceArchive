#include <generated/protocol.h>

#include <game/server/gamecontext.h>

#include "../character.h"
#include "mmo_weapon_common.h"
#include "tesla_chain.h"

static constexpr float JITTER_MAGNITUDE = 64.0f;
static constexpr int SNAP_GROUP_TESLA_BASE = 100;

CMMOTeslaChain::CMMOTeslaChain(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int Damage, float ChainRange, int MaxTargets, float DamageFalloff)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 24)
{
	m_Owner = OwnerCID;
	m_InitialDir = normalize(Direction);
	m_LifeSpanTicks = Server()->TickSpeed() / 4;
	m_Damage = Damage;
	m_ChainRange = ChainRange;
	m_MaxTargetsInChain = clamp(MaxTargets, 1, MAX_CHAIN_SEGMENTS + 1);
	m_DamageFalloff = clamp(DamageFalloff, 0.1f, 1.0f);
	m_ChainCalculationDone = false;

	m_aChainSegmentEndPoints.add(m_Pos);
	for(int i = 0; i < MAX_CHAIN_SEGMENTS; i++)
		AddSnappingGroupIds(SNAP_GROUP_TESLA_BASE + i, NUM_SUB_SEGMENTS_PER_BOLT);

	CalculateChain();
	m_ChainCalculationDone = true;

	GameWorld()->InsertEntity(this);
}

void CMMOTeslaChain::CalculateChain()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
		return;

	vec2 CurrentSource = m_Pos;
	vec2 CurrentDir = m_InitialDir;
	float CurrentDamage = (float)m_Damage;

	for(int i = 0; i < m_MaxTargetsInChain; ++i)
	{
		CCharacter *pClosestTarget = nullptr;
		vec2 TargetPos = CurrentSource + CurrentDir * m_ChainRange;
		float MinDist = m_ChainRange;

		vec2 WallPos;
		if(GameServer()->Collision()->IntersectLineWithInvisible(CurrentSource, TargetPos, &WallPos, nullptr))
		{
			TargetPos = WallPos;
			MinDist = distance(CurrentSource, WallPos);
		}

		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pChar = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
				continue;

			const int TargetCID = pChar->GetCID();
			bool AlreadyHit = false;
			for(int HitCID : m_aTargetsHitThisShot)
			{
				if(HitCID == TargetCID)
				{
					AlreadyHit = true;
					break;
				}
			}
			if(AlreadyHit)
				continue;

			const float Dist = distance(CurrentSource, pChar->GetPos());
			if(Dist < MinDist && !GameServer()->Collision()->IntersectLineWithInvisible(CurrentSource, pChar->GetPos(), nullptr, nullptr))
			{
				MinDist = Dist;
				TargetPos = pChar->GetPos();
				pClosestTarget = pChar;
			}
		}

		m_aChainSegmentEndPoints.add(TargetPos);

		GameWorld()->CreateExplosion(TargetPos, pOwnerChar, WEAPON_LASER, maximum(1, round_to_int(CurrentDamage)));
		GameWorld()->CreateSound(TargetPos, SOUND_GRENADE_EXPLODE);
		GameWorld()->CreateSound(TargetPos, SOUND_HOOK_LOOP);

		if(!pClosestTarget)
			break;

		m_aTargetsHitThisShot.add(pClosestTarget->GetCID());
		CurrentSource = TargetPos;
		CurrentDamage *= m_DamageFalloff;
		CurrentDir = vec2(0, 0);

		if(m_aChainSegmentEndPoints.size() > MAX_CHAIN_SEGMENTS + 1)
			break;
	}
}

void CMMOTeslaChain::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	if(--m_LifeSpanTicks < 0)
		MarkForDestroy();
}

void CMMOTeslaChain::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || m_aChainSegmentEndPoints.size() < 2)
		return;

	const int NumSegments = minimum(m_aChainSegmentEndPoints.size() - 1, MAX_CHAIN_SEGMENTS);

	for(int i = 0; i < NumSegments; ++i)
	{
		const vec2 Start = m_aChainSegmentEndPoints[i];
		const vec2 End = m_aChainSegmentEndPoints[i + 1];
		const vec2 Delta = End - Start;
		const float SegLen = length(Delta);

		if(SegLen < 0.001f)
			continue;

		const vec2 Perpendicular = normalize(vec2(-Delta.y, Delta.x));
		const array<int> *pGroupIds = FindSnappingGroupIds(SNAP_GROUP_TESLA_BASE + i);
		if(!pGroupIds)
			continue;

		vec2 SubStart = Start;
		for(int j = 0; j < NUM_SUB_SEGMENTS_PER_BOLT; ++j)
		{
			vec2 SubEnd;
			if(j == NUM_SUB_SEGMENTS_PER_BOLT - 1)
			{
				SubEnd = End;
			}
			else
			{
				const float Lerp = (j + 1.0f) / (float)NUM_SUB_SEGMENTS_PER_BOLT;
				const float Jitter = (random_float() * 2.0f - 1.0f) * JITTER_MAGNITUDE;
				SubEnd = Start + Delta * Lerp + Perpendicular * Jitter;
			}

			CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pGroupIds)[j], sizeof(CNetObj_Laser)));
			if(pLaser)
			{
				pLaser->m_X = round_to_int(SubEnd.x);
				pLaser->m_Y = round_to_int(SubEnd.y);
				pLaser->m_FromX = round_to_int(SubStart.x);
				pLaser->m_FromY = round_to_int(SubStart.y);
				pLaser->m_StartTick = Server()->Tick() - 2;
			}
			SubStart = SubEnd;
		}
	}
}
