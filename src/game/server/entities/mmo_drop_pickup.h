#ifndef GAME_SERVER_ENTITIES_MMO_DROP_PICKUP_H
#define GAME_SERVER_ENTITIES_MMO_DROP_PICKUP_H

#include <game/server/entity.h>
#include <game/server/entities/pickup.h>

enum
{
	MOBDROP_EXP = 0,
	MOBDROP_HEALTH = 1,
	MOBDROP_MANA = 2,
	MOBDROP_AMMO = 3,
};

class CEntityDropPickup : public CEntity
{
	vec2 m_Vel;
	int m_Kind;
	int m_Subtype;
	int m_Value;
	int m_LifeSpan;
	int m_OwnerClientID;

public:
	CEntityDropPickup(CGameWorld *pGameWorld, vec2 Pos, vec2 Vel, int Kind, int Subtype, int Value, int OwnerClientID = -1);

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
