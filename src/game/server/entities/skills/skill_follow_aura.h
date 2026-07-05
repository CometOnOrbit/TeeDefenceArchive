#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_FOLLOW_AURA_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_FOLLOW_AURA_H

#include "skill_cast_ring.h"
#include <game/server/entity.h>

class CSkillFollowAura : public CChildEntity
{
	static constexpr int NUM_RING_SEGMENTS = 12;
	static constexpr int NUM_ORBIT_DOTS = 6;

	int m_Owner;
	int m_LifeSpan;
	int m_StartTick;
	float m_MaxRadius;
	float m_Radius;
	ESkillVisualStyle m_Style;
	int m_PulseInterval;
	int m_NextPulseTick;

public:
	CSkillFollowAura(CGameWorld *pGameWorld, int OwnerCID, int DurationTicks, ESkillVisualStyle Style, float Radius);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void PulseEffect();
	void SnapRing(int SnappingClient);
	void SnapCenter(int SnappingClient);
	void SnapOrbit(int SnappingClient);
};

#endif
