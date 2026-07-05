#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_BEAM_SWEEP_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_BEAM_SWEEP_H

#include <game/server/entity.h>

class CSkillBeamSweep : public CChildEntity
{
	vec2 m_Pos;
	int m_Owner;
	float m_BeamLength;
	float m_Angle;
	float m_AngleSpeed;
	int m_Damage;
	int m_LifeSpan;
	int m_StartTick;
	int m_NextHitTick;

public:
	CSkillBeamSweep(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float StartAngle, float BeamLength, int Damage, int DurationTicks);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void DamageAlongBeam();
};

#endif
