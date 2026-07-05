#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_RICOCHET_SHOT_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_RICOCHET_SHOT_H

#include <game/server/entity.h>

class CSkillRicochetShot : public CChildEntity
{
	vec2 m_SpawnPos;
	vec2 m_Direction;
	int m_Damage;
	int m_StartTick;
	int m_BouncesLeft;
	float m_DistanceLeft;

	vec2 GetPos(float Time);

public:
	CSkillRicochetShot(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage);

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
