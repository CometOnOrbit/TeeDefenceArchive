#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_MAGIC_BOLT_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_MAGIC_BOLT_H

#include <game/server/entity.h>

enum ESkillBoltEffect
{
	SKILL_BOLT_DAMAGE = 0,
	SKILL_BOLT_ENTANGLE,
	SKILL_BOLT_LIFE_DRAIN,
	SKILL_BOLT_SMOKE_HOOK,
	SKILL_BOLT_WAND,
};

class CSkillMagicBolt : public CChildEntity
{
	vec2 m_Pos;
	vec2 m_Direction;
	int m_Owner;
	int m_Damage;
	int m_LifeSpan;
	int m_StartTick;
	int m_TrackedCID;
	ESkillBoltEffect m_Effect;
	float m_Traveled;
	float m_MaxTravel;

public:
	CSkillMagicBolt(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, ESkillBoltEffect Effect, int PreferredTargetCID = -1);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Impact(CCharacter *pTarget);
};

#endif
