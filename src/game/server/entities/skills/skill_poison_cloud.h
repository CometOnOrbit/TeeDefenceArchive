#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_POISON_CLOUD_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_POISON_CLOUD_H

#include <game/server/entity.h>

class CSkillPoisonCloud : public CChildEntity
{
	int m_Owner;
	int m_Damage;
	float m_Radius;
	int m_LifeSpan;
	int m_TickInterval;
	int m_NextPulseTick;
	int m_PoisonStacks;
	int m_StartTick;

public:
	CSkillPoisonCloud(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int PoisonStacks, int DurationTicks);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Pulse();
};

#endif
