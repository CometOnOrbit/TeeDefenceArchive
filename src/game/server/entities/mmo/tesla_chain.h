#ifndef GAME_SERVER_ENTITIES_MMO_TESLA_CHAIN_H
#define GAME_SERVER_ENTITIES_MMO_TESLA_CHAIN_H

#include <base/tl/array.h>
#include <game/server/entity.h>

class CMMOTeslaChain : public CChildEntity
{
	static constexpr int MAX_CHAIN_SEGMENTS = 3;
	static constexpr int NUM_SUB_SEGMENTS_PER_BOLT = 3;

	vec2 m_InitialDir;
	int m_LifeSpanTicks;
	int m_Damage;
	float m_ChainRange;
	int m_MaxTargetsInChain;
	float m_DamageFalloff;
	bool m_ChainCalculationDone;

	array<int> m_aTargetsHitThisShot;
	array<vec2> m_aChainSegmentEndPoints;

public:
	CMMOTeslaChain(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Direction, int Damage, float ChainRange, int MaxTargets, float DamageFalloff);

	void Tick() override;
	void Snap(int SnappingClient) override;

private:
	void CalculateChain();
};

#endif
