#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_FORK_LIGHTNING_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_FORK_LIGHTNING_H

#include <game/server/entity.h>

class CSkillForkLightning : public CChildEntity
{
	vec2 m_From;
	vec2 m_Dir;
	float m_Energy;
	float m_StartEnergy;
	float m_StepEnergy;
	int m_Damage;
	int m_ForkNum;
	int m_MaxForks;
	int m_EvalTick;
	int m_Owner;

public:
	CSkillForkLightning(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, float StartEnergy, float StepEnergy, int Damage, int MaxForks, int ForkNum = 0);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

private:
	bool HitCharacter(vec2 From, vec2 To);
	void DoBounce();
};

#endif
