/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ENTITIES_TURRET_H
#define GAME_SERVER_ENTITIES_TURRET_H

#include <game/server/entity.h>

class CTurret : public CHitableEntity
{
	static const int NUM_RING_LASERS = 6;

	int m_Owner;
	int m_ItemDefId;
	int m_aCenterId;
	int m_aRingIds[NUM_RING_LASERS];
	int m_LastShotTick;
	int m_LastAmmoWarnTick;
	int m_LastBrokenWarnTick;
	int m_Health;
	int m_MaxHealth;

	float HitRadius();

public:
	CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int ItemDefId);
	~CTurret() override;

	void Tick() override;
	void Snap(int SnappingClient) override;
	bool TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon) override;

	int GetOwner() const { return m_Owner; }
	int GetItemDefId() const { return m_ItemDefId; }
	int GetVisualLevel() const;
	int GetHealth() const { return m_Health; }
	int GetMaxHealth() const { return m_MaxHealth; }
	bool IsBroken() const { return m_Health <= 0; }
	void TakeDamage(int Dmg);
	void Repair();
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
