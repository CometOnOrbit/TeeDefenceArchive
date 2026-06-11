#ifndef GAME_SERVER_ZOMBIE_BOT_H
#define GAME_SERVER_ZOMBIE_BOT_H

#include <base/vmath.h>

#include <generated/protocol.h>

#include "botengine.h"

class CGameContext;
class CGameController;
class CPlayer;

class CZombieBot
{
	enum
	{
		BFLAG_LEFT = 1,
		BFLAG_RIGHT = 2,
		BFLAG_JUMP = 4,
		TARGET_MARCH = 0,
		TARGET_PLAYER = 1,
	};

	CBotEngine *m_pBotEngine;
	CGameContext *m_pGameServer;
	CPlayer *m_pPlayer;
	CGameController *m_pCtrl;

	struct CComputeTarget
	{
		vec2 m_Pos;
		int m_Type;
		int m_PlayerCID;
		bool m_NeedUpdate;
	} m_ComputeTarget;

	int m_Flags;
	vec2 m_Target;
	vec2 m_RealTarget;
	int m_LowSpeedTicks;
	bool m_McJumpTried;
	int m_StuckFlipCooldown;
	int m_HookCooldown;

	CNetObj_PlayerInput m_InputData;
	CNetObj_PlayerInput m_LastData;

	class CCollision *Collision() const;
	class CTuningParams *Tuning() const;
	int GetTile(vec2 Pos) const;
	int ZombieFirstSlot() const;

	vec2 GetPersonalMarchGoal() const;
	int CountNearbyZombies(vec2 Pos, float Radius) const;
	void ApplyCrowdSteering(vec2 Pos, vec2 *pTargetOff) const;
	bool ZombieBlockedAhead(vec2 Pos, int Dir) const;

	void UpdateZombieTarget();
	void UpdateMarchNavigation();
	void EnsureMarchDrive(vec2 Pos, vec2 Goal);
	void MakeChoice(bool UseTarget);
	bool AllowTerrainHook(int CurTile, int MoveDir) const;
	bool WantSpeedHook(int CurTile, float DistMarch) const;
	bool ShouldTryTerrainHook(int CurTile, int MoveDir, float DistMarch) const;
	void HandleHook(bool SeeTarget, int MoveDir, float DistMarch);
	void HandleZombieWeapon(bool InSight, bool HumanCombat, bool HumanAggro, bool HasStructure, float DistStructure,
		vec2 StructurePos, float DistHuman);
	int GetActiveZombType() const;
	bool TryZamerDetonate(float DistTower, float DistHuman, bool InSight);
	void ApplySpecialMovement(bool InSight, float DistHuman);
	void TryZeleTeleport(bool InSight, float DistHuman);
	void TryZeaterConsume();
	void UpdateZinvisState(bool HumanCombat, bool HumanAggro);
	float GetAiScale() const;
	float GetCombatRadius() const;
	float GetAggroRadius() const;
	float GetTypeAttackRange(int ZombType) const;
	bool IsGrounded();

public:
	CZombieBot(CBotEngine *pBotEngine, CPlayer *pPlayer, CGameController *pCtrl);
	~CZombieBot();

	void Tick();
	const CNetObj_PlayerInput &Input() const { return m_InputData; }
	CPlayer *Player() const { return m_pPlayer; }
};

#endif
