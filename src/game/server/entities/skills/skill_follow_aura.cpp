#include "skill_follow_aura.h"
#include "skill_vfx_common.h"

#include <generated/protocol.h>
#include <generated/server_data.h>

#include <game/server/gamecontext.h>
#include <base/math.h>

#include "../character.h"

CSkillFollowAura::CSkillFollowAura(CGameWorld *pGameWorld, int OwnerCID, int DurationTicks, ESkillVisualStyle Style, float Radius)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, vec2(0, 0), (int)maximum(32.f, Radius))
{
	m_Owner = OwnerCID;
	m_MaxRadius = maximum(32.f, Radius);
	m_Radius = m_MaxRadius;
	m_LifeSpan = maximum(1, DurationTicks);
	m_StartTick = Server()->Tick();
	m_Style = Style;
	m_PulseInterval = maximum(1, Server()->TickSpeed() / 3);
	m_NextPulseTick = Server()->Tick() + m_PulseInterval;

	AddSnappingGroupIds(SNAP_GROUP_RING, NUM_RING_SEGMENTS);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);
	AddSnappingGroupIds(SNAP_GROUP_SPIDER_LEGS, NUM_ORBIT_DOTS);

	GameWorld()->InsertEntity(this);
}

void CSkillFollowAura::PulseEffect()
{
	switch(m_Style)
	{
	case SKILL_VFX_FROST:
		GameWorld()->CreateDeath(m_Pos, -1);
		break;
	case SKILL_VFX_FIRE:
		GameWorld()->CreateHammerHit(m_Pos);
		break;
	case SKILL_VFX_POISON:
		GameWorld()->CreateDeath(m_Pos, -1);
		break;
	case SKILL_VFX_HOLY:
	case SKILL_VFX_HEAL:
		GameWorld()->CreatePlayerSpawn(m_Pos);
		break;
	case SKILL_VFX_ARCANE:
		GameWorld()->CreateHammerHit(m_Pos);
		break;
	case SKILL_VFX_PHYSICAL:
		GameWorld()->CreateHammerHit(m_Pos);
		break;
	case SKILL_VFX_SHADOW:
		GameWorld()->CreateDeath(m_Pos, -1);
		break;
	}
}

void CSkillFollowAura::Tick()
{
	CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner);
	if(!pOwner || !pOwner->IsAlive())
	{
		MarkForDestroy();
		return;
	}

	m_Pos = pOwner->GetPos();

	const int Elapsed = Server()->Tick() - m_StartTick;
	const float Pulse = 0.85f + 0.15f * sinf((float)Elapsed / (float)maximum(1, Server()->TickSpeed() / 2) * pi);
	m_Radius = m_MaxRadius * Pulse;

	if(Server()->Tick() >= m_NextPulseTick)
	{
		PulseEffect();
		m_NextPulseTick = Server()->Tick() + m_PulseInterval;
	}

	if(--m_LifeSpan <= 0)
		MarkForDestroy();
}

void CSkillFollowAura::SnapCenter(int SnappingClient)
{
	const array<int> *pCenterIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(!pCenterIds || pCenterIds->size() == 0)
		return;
	SnapLaserDot(Server(), (*pCenterIds)[0], m_Pos, m_StartTick);
}

void CSkillFollowAura::SnapRing(int SnappingClient)
{
	const array<int> *pRingIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pRingIds || (int)pRingIds->size() < NUM_RING_SEGMENTS)
		return;

	const float DrawRadius = maximum(16.f, m_Radius);
	const float AngleStep = 2.f * pi / (float)NUM_RING_SEGMENTS;
	for(int i = 0; i < NUM_RING_SEGMENTS; i++)
	{
		const int NextIndex = (i + 1) % NUM_RING_SEGMENTS;
		const vec2 CurrentPos = m_Pos + vec2(DrawRadius * cosf(AngleStep * i), DrawRadius * sinf(AngleStep * i));
		const vec2 NextPos = m_Pos + vec2(DrawRadius * cosf(AngleStep * NextIndex), DrawRadius * sinf(AngleStep * NextIndex));
		CNetObj_Laser *pRing = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pRingIds)[i], sizeof(CNetObj_Laser)));
		if(pRing)
		{
			pRing->m_X = round_to_int(CurrentPos.x);
			pRing->m_Y = round_to_int(CurrentPos.y);
			pRing->m_FromX = round_to_int(NextPos.x);
			pRing->m_FromY = round_to_int(NextPos.y);
			pRing->m_StartTick = m_StartTick;
		}
	}
}

void CSkillFollowAura::SnapOrbit(int SnappingClient)
{
	const array<int> *pOrbitIds = FindSnappingGroupIds(SNAP_GROUP_SPIDER_LEGS);
	if(!pOrbitIds || (int)pOrbitIds->size() < NUM_ORBIT_DOTS)
		return;

	const int Elapsed = Server()->Tick() - m_StartTick;
	const float Spin = (float)Elapsed / (float)maximum(1, Server()->TickSpeed()) * 2.f;
	const float OrbitRadius = m_Radius * 0.65f;
	const float AngleStep = 2.f * pi / (float)NUM_ORBIT_DOTS;
	for(int i = 0; i < NUM_ORBIT_DOTS; i++)
	{
		const float Angle = Spin + AngleStep * (float)i;
		const vec2 DotPos = m_Pos + vec2(OrbitRadius * cosf(Angle), OrbitRadius * sinf(Angle));
		SnapLaserDot(Server(), (*pOrbitIds)[i], DotPos, m_StartTick);
	}
}

void CSkillFollowAura::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	SnapCenter(SnappingClient);
	if(m_Radius > 8.f)
	{
		SnapRing(SnappingClient);
		SnapOrbit(SnappingClient);
	}
}
