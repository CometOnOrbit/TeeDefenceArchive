#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_LASER_TRAP_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_LASER_TRAP_H

#include <base/tl/array.h>
#include <game/server/entity.h>

class CSkillLaserTrap : public CChildEntity
{
	vec2 m_From;
	vec2 m_To;
	array<vec2> m_aBeamA;
	array<vec2> m_aBeamB;
	int m_Damage;
	int m_LifeSpan;
	int m_DamageInterval;
	int m_NextDamageTick;
	int m_StartTick;

public:
	CSkillLaserTrap(CGameWorld *pGameWorld, int OwnerCID, vec2 From, vec2 To, int NumBeams, int Damage, int DurationTicks);

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
