#ifndef GAME_SERVER_ENTITIES_MMO_HOMING_GRENADE_H
#define GAME_SERVER_ENTITIES_MMO_HOMING_GRENADE_H

#include <game/server/entity.h>

class CMMOHomingGrenade : public CChildEntity
{
	vec2 m_Direction;
	int m_Damage;
	float m_Speed;

public:
	CMMOHomingGrenade(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int Damage, float Speed);

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
