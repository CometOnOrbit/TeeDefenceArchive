#ifndef GAME_SERVER_ENTITIES_RPG_FISHING_H
#define GAME_SERVER_ENTITIES_RPG_FISHING_H

#include <game/collision.h>
#include <game/server/entities/physics/rope.h>
#include <game/server/entity.h>

#include <optional>

class CPlayer;
class CCharacter;

class CEntityFishingRod : public CEntity
{
	struct FishingNow
	{
		enum State
		{
			WAITING,
			HOOKING,
			PULLING,
			SUCCESS,
			AWAY,
		};

		std::optional<vec2> m_FromPoint;
		float m_InterpolatedX{};
		int m_State{};
		int m_Health{};
		int m_HookingTime{};
	};

	enum
	{
		SNAP_ROD = 10,
		NUM_ROD_POINTS = 3,

		SNAP_ROPE = 11,
		NUM_ROPE_POINTS = 8,
	};

	int m_OwnerClientId;
	bool m_FloatInWater{};
	bool m_AutoMode{};
	int m_LastAutoPullTick{};
	int m_LastHudTick{};
	int m_LastWarnTick{};
	int m_PrevFishingState{-1};
	int m_CachedRodItemId{-1};
	int m_CachedSwitchNumber{-1};
	GatheringNode *m_pCachedFishNode{};
	int m_PhysicsTick{};
	vec2 m_EndRodPoint{};
	RopePhysic m_Rope{};
	FishingNow m_Fishing{};

	vec2 CalculateRodPoint(bool FacingRight, size_t Segment) const;
	void UpdateRodEndPoint(const CCharacter *pChar);
	void FishingTick(CPlayer *pPlayer, GatheringNode *pNode, const char *pRodName);

public:
	CEntityFishingRod(CGameWorld *pGameWorld, int ClientID, vec2 Position, vec2 Force, bool AutoMode);
	~CEntityFishingRod() override;

	bool IsWaitingState() const { return m_Fishing.m_State == FishingNow::WAITING; }

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
