#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_HIT_BURST_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_HIT_BURST_H

#include "skill_cast_ring.h"

#include <game/server/entity.h>

// MRPG-style hit VFX: laser dots on a ring that expands and rotates outward.
class CSkillHitBurst : public CChildEntity
{
	static constexpr int DEFAULT_NUM_DOTS = 12;

	vec2 m_Pos;
	float m_MaxRadius;
	float m_Radius;
	float m_Rotation;
	float m_RotationSpeed;
	int m_NumDots;
	int m_LifeSpan;
	int m_StartTick;
	int m_GrowDuration;
	ESkillVisualStyle m_Style;

public:
	CSkillHitBurst(CGameWorld *pGameWorld, vec2 Pos, float MaxRadius, ESkillVisualStyle Style, int DurationTicks = 0, int NumDots = DEFAULT_NUM_DOTS);

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
