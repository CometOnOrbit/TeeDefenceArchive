#include "skill_cast_ring.h"
#include "skill_vfx_common.h"

#include <generated/protocol.h>
#include <generated/server_data.h>

#include <game/server/gamecontext.h>
#include <base/math.h>

CSkillCastRing::CSkillCastRing(CGameWorld *pGameWorld, vec2 Pos, float MaxRadius, int DurationTicks, ESkillVisualStyle Style,
	ESkillCastRingMode Mode, int GrowDurationTicks)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, (int)MaxRadius)
{
	m_MaxRadius = maximum(32.f, MaxRadius);
	m_Radius = Mode == SKILL_RING_STEADY ? m_MaxRadius : 0.f;
	m_LifeSpan = maximum(1, DurationTicks);
	m_StartTick = Server()->Tick();
	m_GrowDuration = GrowDurationTicks > 0 ? GrowDurationTicks : maximum(1, Server()->TickSpeed() / 4);
	m_Style = Style;
	m_Mode = Mode;
	m_PulseInterval = maximum(1, Server()->TickSpeed() / 3);
	m_NextPulseTick = Server()->Tick() + m_PulseInterval;

	AddSnappingGroupIds(SNAP_GROUP_RING, NUM_RING_SEGMENTS);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	switch(m_Style)
	{
	case SKILL_VFX_FROST:
		GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
		break;
	case SKILL_VFX_FIRE:
		GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
		break;
	case SKILL_VFX_POISON:
		GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
		break;
	case SKILL_VFX_HOLY:
	case SKILL_VFX_HEAL:
		GameWorld()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH);
		break;
	case SKILL_VFX_ARCANE:
		GameWorld()->CreateSound(m_Pos, SOUND_LASER_FIRE);
		break;
	case SKILL_VFX_PHYSICAL:
		GameWorld()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);
		break;
	case SKILL_VFX_SHADOW:
		GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
		break;
	}

	GameWorld()->InsertEntity(this);
}

void CSkillCastRing::PulseEffect()
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

void CSkillCastRing::Tick()
{
	const int Elapsed = Server()->Tick() - m_StartTick;

	if(m_Mode == SKILL_RING_EXPAND_BURST || m_Mode == SKILL_RING_EXPAND_HOLD)
	{
		const float T = minimum(1.f, (float)Elapsed / (float)m_GrowDuration);
		m_Radius = m_MaxRadius * T;
	}
	else if(m_Mode == SKILL_RING_STEADY)
	{
		const float Pulse = 0.85f + 0.15f * sinf((float)Elapsed / (float)maximum(1, Server()->TickSpeed() / 2) * pi);
		m_Radius = m_MaxRadius * Pulse;
	}

	if(Server()->Tick() >= m_NextPulseTick)
	{
		PulseEffect();
		m_NextPulseTick = Server()->Tick() + m_PulseInterval;
	}

	if(--m_LifeSpan <= 0)
		MarkForDestroy();
}

void CSkillCastRing::SnapCenter(int SnappingClient)
{
	const array<int> *pCenterIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(!pCenterIds || pCenterIds->size() == 0)
		return;

	SnapLaserDot(Server(), (*pCenterIds)[0], m_Pos, m_StartTick);
}

void CSkillCastRing::SnapRing(int SnappingClient)
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

void CSkillCastRing::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	SnapCenter(SnappingClient);
	if(m_Radius > 8.f)
		SnapRing(SnappingClient);
}
