#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_BOMB_SENTINEL_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_BOMB_SENTINEL_H

#include <game/server/entity.h>

class CSentinelBombProjectile : public CChildEntity
{
	vec2 m_Dir;
	int m_Damage;
	int m_LifeSpan;
	int m_StartTick;

public:
	CSentinelBombProjectile(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void Explode();
};

class CSoldierBombSentinel : public CChildEntity
{
	int m_ShotsLeft;
	int m_MaxShots;
	int m_Damage;
	float m_Range;
	int m_IntervalTicks;
	int m_NextFireTick;
	int m_StartTick;
	float m_OrbitAngle;

public:
	CSoldierBombSentinel(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, int MaxShots, int Damage, float Range, float IntervalSec);

	void Tick() override;
	void Snap(int SnappingClient) override;

	int ShotsLeft() const { return m_ShotsLeft; }
};

#endif
