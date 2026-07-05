#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_SCATTER_GRENADE_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_SCATTER_GRENADE_H

#include <game/server/entity.h>

class CSkillScatterGrenade : public CChildEntity
{
	vec2 m_SpawnPos;
	vec2 m_Direction;
	int m_Damage;
	int m_ExplosionRadiusTiles;
	int m_StartTick;
	int m_BouncesLeft;
	bool m_ExplodeOnContact;

	vec2 GetPos(float Time);

public:
	CSkillScatterGrenade(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, int ExplosionRadiusTiles);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Explode(vec2 Pos);
	void OnCollided(vec2 At);
};

#endif
