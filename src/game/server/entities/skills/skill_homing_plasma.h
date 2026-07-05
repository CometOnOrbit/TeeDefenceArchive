#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_HOMING_PLASMA_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_HOMING_PLASMA_H

#include <game/server/entity.h>

class CSkillHomingPlasma : public CChildEntity
{
	vec2 m_Dir;
	int m_Damage;
	int m_ExplosionRadiusTiles;
	int m_TrackedCID;
	int m_LifeSpan;
	int m_StartTick;
	float m_TrackingStrength;

public:
	CSkillHomingPlasma(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int ExplosionRadiusTiles, int TrackedCID, float TrackingStrength);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Explode();
};

#endif
