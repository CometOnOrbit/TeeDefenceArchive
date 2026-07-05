#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_ZONE_AURA_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_ZONE_AURA_H

#include "skill_cast_ring.h"
#include <game/server/entity.h>

class CSkillZoneAura : public CChildEntity
{
	static constexpr int NUM_RING_SEGMENTS = 12;
	static constexpr int NUM_ORBIT_DOTS = 6;

	vec2 m_Center;
	int m_LifeSpan;
	int m_StartTick;
	float m_MaxRadius;
	float m_Radius;
	ESkillVisualStyle m_Style;
	int m_PulseInterval;
	int m_NextPulseTick;

public:
	CSkillZoneAura(CGameWorld *pGameWorld, vec2 Pos, int DurationTicks, ESkillVisualStyle Style, float Radius);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void PulseEffect();
	void SnapRing(int SnappingClient);
	void SnapCenter(int SnappingClient);
	void SnapOrbit(int SnappingClient);
};

#endif
