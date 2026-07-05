#include "skill_hit_burst.h"

#include "skill_vfx_common.h"

#include <generated/server_data.h>

#include <game/server/gamecontext.h>
#include <base/math.h>

CSkillHitBurst::CSkillHitBurst(CGameWorld *pGameWorld, vec2 Pos, float MaxRadius, ESkillVisualStyle Style, int DurationTicks, int NumDots)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, (int)maximum(32.f, MaxRadius))
{
	m_Pos = Pos;
	m_MaxRadius = maximum(24.f, MaxRadius);
	m_Radius = 8.f;
	m_Rotation = random_float() * 2.f * pi;
	m_RotationSpeed = 0.28f;
	m_NumDots = clamp(NumDots, 6, 24);
	m_LifeSpan = DurationTicks > 0 ? DurationTicks : maximum(1, Server()->TickSpeed() / 3);
	m_StartTick = Server()->Tick();
	m_GrowDuration = maximum(1, m_LifeSpan * 2 / 3);
	m_Style = Style;

	AddSnappingGroupIds(SNAP_GROUP_RING, m_NumDots);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	switch(m_Style)
	{
	case SKILL_VFX_FROST:
		GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
		break;
	case SKILL_VFX_FIRE:
		GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
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

void CSkillHitBurst::Tick()
{
	const int Elapsed = Server()->Tick() - m_StartTick;
	const float T = minimum(1.f, (float)Elapsed / (float)m_GrowDuration);
	m_Radius = 8.f + (m_MaxRadius - 8.f) * T;
	m_Rotation += m_RotationSpeed;

	if(--m_LifeSpan <= 0)
		MarkForDestroy();
}

void CSkillHitBurst::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const array<int> *pCenterIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pCenterIds && pCenterIds->size() > 0)
		SnapLaserDot(Server(), (*pCenterIds)[0], m_Pos, m_StartTick);

	const array<int> *pDotIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pDotIds || (int)pDotIds->size() < m_NumDots)
		return;

	const float AngleStep = 2.f * pi / (float)m_NumDots;
	for(int i = 0; i < m_NumDots; i++)
	{
		const float Angle = m_Rotation + AngleStep * (float)i;
		const vec2 DotPos = m_Pos + vec2(m_Radius * cosf(Angle), m_Radius * sinf(Angle));
		SnapLaserDot(Server(), (*pDotIds)[i], DotPos, m_StartTick);
	}
}
