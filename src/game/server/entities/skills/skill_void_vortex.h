#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_VOID_VORTEX_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_VOID_VORTEX_H

#include <game/server/entity.h>

class CSkillVoidVortex : public CChildEntity
{
	vec2 m_Pos;
	int m_Owner;
	float m_MaxRadius;
	float m_Radius;
	int m_Damage;
	int m_LifeSpan;
	int m_StartTick;
	bool m_BurstPhase;

public:
	CSkillVoidVortex(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int Damage, int PullTicks);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Burst();
};

#endif
