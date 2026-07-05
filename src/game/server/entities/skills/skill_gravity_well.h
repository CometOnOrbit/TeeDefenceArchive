#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_GRAVITY_WELL_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_GRAVITY_WELL_H

#include <game/server/entity.h>

class CSkillGravityWell : public CChildEntity
{
	vec2 m_Center;
	int m_Owner;
	float m_MaxRadius;
	float m_Radius;
	int m_LifeSpan;
	int m_StartTick;
	int m_ExplosionRadiusTiles;
	int m_ExplosionDamage;
	bool m_FinalExplosion;
	bool m_Ended;

public:
	CSkillGravityWell(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int PullTicks, int ExplosionRadiusTiles, int ExplosionDamage, bool FinalExplosion = true);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Finish();
};

#endif
