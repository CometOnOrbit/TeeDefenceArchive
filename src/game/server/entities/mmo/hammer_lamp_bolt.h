#ifndef GAME_SERVER_ENTITIES_MMO_HAMMER_LAMP_BOLT_H
#define GAME_SERVER_ENTITIES_MMO_HAMMER_LAMP_BOLT_H

#include <game/server/entity.h>

class CMMOHammerLampBolt : public CChildEntity
{
	vec2 m_Vel;
	vec2 m_InitialVel;
	float m_InitialAmount;
	int m_TargetCID;
	int m_Damage;

public:
	CMMOHammerLampBolt(CGameWorld *pGameWorld, int OwnerCID, int TargetCID, vec2 Pos, vec2 InitialVel, int Damage);

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
