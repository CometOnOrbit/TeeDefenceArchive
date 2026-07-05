#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_CAST_RING_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_CAST_RING_H

#include <game/server/entity.h>

enum ESkillVisualStyle
{
	SKILL_VFX_FROST = 0,
	SKILL_VFX_FIRE,
	SKILL_VFX_POISON,
	SKILL_VFX_HOLY,
	SKILL_VFX_ARCANE,
	SKILL_VFX_PHYSICAL,
	SKILL_VFX_SHADOW,
	SKILL_VFX_HEAL,
};

enum ESkillCastRingMode
{
	SKILL_RING_EXPAND_BURST = 0,
	SKILL_RING_EXPAND_HOLD,
	SKILL_RING_STEADY,
};

class CSkillCastRing : public CChildEntity
{
	static constexpr int NUM_RING_SEGMENTS = 12;

	float m_MaxRadius;
	float m_Radius;
	int m_LifeSpan;
	int m_StartTick;
	int m_GrowDuration;
	ESkillVisualStyle m_Style;
	ESkillCastRingMode m_Mode;
	int m_PulseInterval;
	int m_NextPulseTick;

public:
	CSkillCastRing(CGameWorld *pGameWorld, vec2 Pos, float MaxRadius, int DurationTicks, ESkillVisualStyle Style,
		ESkillCastRingMode Mode = SKILL_RING_EXPAND_BURST, int GrowDurationTicks = 0);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void PulseEffect();
	void SnapRing(int SnappingClient);
	void SnapCenter(int SnappingClient);
};

#endif
