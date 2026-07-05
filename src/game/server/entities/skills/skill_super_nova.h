#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_SUPER_NOVA_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_SUPER_NOVA_H

#include <game/server/entity.h>

class CSkillSuperNova : public CChildEntity
{
	vec2 m_Pos;
	int m_Owner;
	int m_Damage;
	int m_MaxLife;
	int m_Life;
	int m_NextIn;

public:
	CSkillSuperNova(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, int Damage, int MaxLife);

	void Tick() override;
};

#endif
