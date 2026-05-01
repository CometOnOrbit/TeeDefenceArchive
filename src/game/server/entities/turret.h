/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ENTITIES_TURRET_H
#define GAME_SERVER_ENTITIES_TURRET_H

#include <game/server/entity.h>

class CTurret : public CEntity
{
	static const int NUM_RING_LASERS = 6;

	int m_Owner;
	int m_ItemDefId;
	int m_aRingIds[NUM_RING_LASERS];
	int m_LastShotTick;

public:
	CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int ItemDefId);
	~CTurret() override;

	void Tick() override;
	void Snap(int SnappingClient) override;

	int GetOwner() const { return m_Owner; }
	int GetItemDefId() const { return m_ItemDefId; }
	int GetVisualLevel() const;
};

#endif
