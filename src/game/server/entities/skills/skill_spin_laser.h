#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_SPIN_LASER_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_SPIN_LASER_H

#include <game/server/entity.h>

class CSkillSpinLaser : public CChildEntity
{
	vec2 m_From;
	vec2 m_Dir;
	float m_OrbitAngle;
	float m_OrbitRadius;
	float m_BeamEnergy;
	int m_Damage;
	int m_EvalTick;
	int m_DamageTimer;
	int m_LifeSpan;
	int m_Owner;

public:
	CSkillSpinLaser(CGameWorld *pGameWorld, int OwnerCID, float StartAngle, float OrbitRadius, float BeamEnergy, int Damage, int DurationTicks);

	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;

private:
	bool HitAlongBeam(vec2 From, vec2 To);
	void SpinStep();
};

#endif
