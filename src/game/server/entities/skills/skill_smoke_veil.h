#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_SMOKE_VEIL_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_SMOKE_VEIL_H

#include <game/server/entity.h>

class CSkillSmokeVeil : public CChildEntity
{
	vec2 m_Pos;
	int m_Owner;
	float m_Radius;
	int m_MaxLife;
	int m_Life;
	int m_SlowTicks;
	float m_SlowFactor;

public:
	CSkillSmokeVeil(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int DurationTicks, int SlowTicks, float SlowFactor);

	void Tick() override;
};

#endif
