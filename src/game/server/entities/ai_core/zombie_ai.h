#ifndef GAME_SERVER_ENTITIES_AI_CORE_ZOMBIE_AI_H
#define GAME_SERVER_ENTITIES_AI_CORE_ZOMBIE_AI_H

#include <base/vmath.h>
#include <generated/protocol.h>
#include <engine/shared/protocol.h>

class CGameController;
class CGameContext;
class CPlayer;
class CCharacter;

// ─── CZombieAI ───────────────────────────────────────────────────────
// Replaces CZombieBot. Standalone class (not CBaseAI) because TD zombies
// use CPlayer::OnPredictedInput() flow instead of CCharacterBotAI.
// Merges logic from zombie_bot.cpp; stores GS pointer to avoid
// accessing protected CGameController::GameServer().

class CZombieAI final
{
public:
	CZombieAI(CPlayer *pPlayer, CGameController *pCtrl, CGameContext *pGS);
	~CZombieAI() = default;

	void TickBrain();
	const CNetObj_PlayerInput &GetInput() const { return m_InputData; }

	void SetMarchGoal(vec2 Goal) { m_MarchGoal = Goal; }
	void SetAiMul(float Mul) { m_AiMul = Mul; }

private:
	CPlayer *m_pPlayer{};
	CGameController *m_pCtrl{};
	CGameContext *m_pGS{};

	// State
	int m_Flags{};
	vec2 m_Target{};
	vec2 m_RealTarget{};
	vec2 m_MarchGoal{};
	float m_AiMul{1.0f};
	int m_LowSpeedTicks{};
	bool m_McJumpTried{};
	int m_StuckFlipCooldown{};
	int m_HookCooldown{};
	CNetObj_PlayerInput m_InputData{};
	CNetObj_PlayerInput m_LastData{};

	// Target info
	enum { TARGET_MARCH, TARGET_PLAYER };
	struct SZTarget
	{
		int m_Type{TARGET_MARCH};
		vec2 m_Pos{};
		int m_PlayerCID{-1};
		bool m_NeedUpdate{true};
	} m_TargetInfo{};

	// ─── Helpers ───────────────────────────────────────────────────
	int GetTile(vec2 Pos) const;
	int ZombieFirstSlot() const;
	int CountNearbyZombies(vec2 Pos, float Radius) const;
	void ApplyCrowdSteering(vec2 Pos, vec2 *pTargetOff) const;
	bool ZombieBlockedAhead(vec2 Pos, int Dir) const;

	// ─── Target / Navigation ──────────────────────────────────────
	vec2 GetPersonalMarchGoal() const;
	void UpdateTarget();
	void UpdateNavigation();
	void EnsureMarchDrive(vec2 Pos, vec2 Goal);

	// ─── Movement ─────────────────────────────────────────────────
	void MakeChoice();
	void HandleStuck(int *pMoveDir);

	// ─── Hook ─────────────────────────────────────────────────────
	bool AllowTerrainHook(int CurTile, int MoveDir) const;
	bool WantSpeedHook(int CurTile, float DistMarch) const;
	bool ShouldTryTerrainHook(int CurTile, int MoveDir, float DistMarch) const;
	void HandleHook(bool SeeTarget, int MoveDir, float DistMarch);

	// ─── Difficulty & Zombie Type ────────────────────────────────
	float GetAiScale() const;
	float GetCombatRadius() const;
	float GetAggroRadius() const;
	int GetActiveZombType() const;
	float GetTypeAttackRange(int ZombType) const;

	// ─── Special Abilities ───────────────────────────────────────
	bool TryZamerDetonate(float DistTower, float DistHuman, bool InSight);
	void ApplySpecialMovement(bool InSight, float DistHuman);
	void TryZeleTeleport(bool InSight, float DistHuman);
	void TryZeaterConsume();
	void UpdateZinvisState(bool HumanCombat, bool HumanAggro);

	// ─── Weapon ──────────────────────────────────────────────────
	void HandleWeapon(bool InSight, bool HumanCombat, bool HumanAggro,
		bool HasStructure, float DistStructure, vec2 StructurePos, float DistHuman);
};

#endif
