/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ENTITIES_TURRET_H
#define GAME_SERVER_ENTITIES_TURRET_H

#include <game/server/entity.h>

class CTurret : public CEntity
{
	static const int NUM_RING_LASERS = 6;

	int m_Owner;
	int m_ItemDefId;
	int m_aCenterId;
	int m_aRingIds[NUM_RING_LASERS];
	int m_LastShotTick;
	int m_LastAmmoWarnTick;

public:
	CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int ItemDefId);
	~CTurret() override;

	void Tick() override;
	void Snap(int SnappingClient) override;

	int GetOwner() const { return m_Owner; }
	int GetItemDefId() const { return m_ItemDefId; }
	int GetVisualLevel() const;
};

/** Ghost ring shown while the owner picks a deploy spot (vote menu placement page). */
class CTurretPreview : public CEntity
{
	static const int NUM_RING_LASERS = 6;

	int m_Owner;
	int m_ItemDefId;
	bool m_Valid;
	int m_aCenterId;
	int m_aRingIds[NUM_RING_LASERS];

public:
	CTurretPreview(CGameWorld *pGameWorld, vec2 Pos, int Owner, int ItemDefId, bool Valid);
	~CTurretPreview() override;

	void SetPreviewPos(vec2 Pos);
	void SetValid(bool Valid);
	void Snap(int SnappingClient) override;

private:
	int GetVisualLevel() const;
};

#endif
