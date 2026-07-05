#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_ACID_POOL_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_ACID_POOL_H

#include <game/server/entity.h>

class CSkillAcidPool : public CChildEntity
{
	float m_Radius;
	int m_LifeSpan;
	int m_TickInterval;
	int m_NextPulseTick;
	int m_PoisonStacks;
	int m_StartTick;
	float m_SlowMul;

public:
	CSkillAcidPool(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int PoisonStacks, int DurationTicks, float SlowMul);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Pulse();
};

#endif
