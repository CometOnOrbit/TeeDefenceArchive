#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_FIREBALL_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_FIREBALL_H

#include <game/server/entity.h>

class CSkillFireball : public CChildEntity
{
	vec2 m_Pos;
	vec2 m_Direction;
	int m_Owner;
	int m_Damage;
	int m_LifeSpan;
	int m_StartTick;
	bool m_ApplyBurn;

public:
	CSkillFireball(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, bool ApplyBurn = true);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	vec2 GetPos(float Time);
	void Impact(vec2 Pos, CEntity *pHitEnt);
};

#endif
