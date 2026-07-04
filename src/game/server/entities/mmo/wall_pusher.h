#ifndef GAME_SERVER_ENTITIES_MMO_WALL_PUSHER_H
#define GAME_SERVER_ENTITIES_MMO_WALL_PUSHER_H

#include <game/server/entity.h>

class CMMOWallPusher : public CChildEntity
{
	vec2 m_Direction;
	vec2 m_PosTo;
	int m_LifeTick;
	int m_Damage;
	int m_ID2;

public:
	CMMOWallPusher(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int LifeTick, int Damage);
	~CMMOWallPusher();

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void TryHitCharacter(const vec2 PrevPos);
};

#endif
