#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_METEOR_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_METEOR_H

#include <game/server/entity.h>

class CSkillMeteor : public CChildEntity
{
	vec2 m_Pos;
	int m_Owner;
	int m_Damage;
	float m_Radius;
	int m_WarningTicks;
	int m_StartTick;
	bool m_Impacted;

public:
	CSkillMeteor(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, int Damage, float Radius, int WarningTicks);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Impact();
	bool IsHostileTarget(CCharacter *pOwnerChar, CCharacter *pTarget);
};

#endif
