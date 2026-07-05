#ifndef GAME_SERVER_ENTITIES_RPG_CK_H
#define GAME_SERVER_ENTITIES_RPG_CK_H

#include <game/collision.h>
#include <game/server/entity.h>

class CPlayer;

enum class ERpgCkKind
{
	ORE,
	PLANT,
};

// RPG-mode gathering CK — switch-layer #node_ore / #node_plant, MMO inventory drops.
class CRpgCk : public CEntity
{
	GatheringNode *m_pNode;
	ERpgCkKind m_Kind;
	int m_CurrentHealth;
	int m_RespawnEndTick;
	int m_LastHudClientId{-1};
	int m_LastHudTick{};

public:
	static int const ms_PhysSize = 14;

	CRpgCk(CGameWorld *pGameWorld, GatheringNode *pNode, vec2 Pos, ERpgCkKind Kind);

	void Reset() override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	bool TakeHit(CPlayer *pPlayer);

private:
	void SnapToWall();
	void GrantLoot(CPlayer *pPlayer);
	int ComputeDamage(CPlayer *pPlayer);
	bool IsActive() const { return m_RespawnEndTick < 0; }
};

#endif
