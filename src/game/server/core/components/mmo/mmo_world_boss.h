#ifndef GAME_SERVER_CORE_COMPONENTS_MMO_MMO_WORLD_BOSS_H
#define GAME_SERVER_CORE_COMPONENTS_MMO_MMO_WORLD_BOSS_H

#include <game/server/entity.h>
#include <game/server/core/tworld_component.h>
#include <game/server/core/tools/event_listener.h>
#include <engine/shared/protocol.h>
#include <vector>

class CGameContext;
class CPlayer;
class CCharacter;

class CWorldBossManager : public TWorldComponent, public IGameEventListener
{
public:
	CWorldBossManager();
	~CWorldBossManager();

	void OnPreInit() override;
	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnShutdown() override;
	void OnTick() override;

	void OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon) override;

	// Called from CCharacter::TakeDamage to record damage done by players to the boss
	void RecordDamage(int BossCID, int AttackerCID, int Damage);

	bool IsWorldBoss(CPlayer *pPlayer) const;
	bool IsWorldBossCharacter(CCharacter *pChar) const;
	bool IsBossAlive() const { return m_IsAlive; }
	int GetBossHP() const { return m_BossHP; }
	int GetBossMaxHP() const { return m_BossMaxHP; }
	int GetNextSpawnInTicks() const;

	void RegisterBossCommands();

private:
	static constexpr int BOSS_SLOT = 63;         // Fixed bot slot for world boss
	static constexpr int BOSS_MAX_HP = 20000;    // Base HP
	static constexpr int BOSS_BASE_ATTACK = 80;  // Base attack damage
	static constexpr int RESPAWN_INTERVAL = 3600; // 6 minutes in ticks (at 10 tick/s)
	static constexpr float BOSS_SPEED = 2.0f;    // Slower than a normal player (≈4.0)
	static constexpr int BOSS_ATTACK_RANGE = 80; // Attack range in units
	static constexpr int BOSS_ATTACK_COOLDOWN = 30; // Ticks between attacks
	static constexpr int BOSS_AGGRO_RANGE = 800; // Aggro range in units

	// Current boss state
	int m_WorldID = -1;            // Which world the boss spawns in
	bool m_IsAlive = false;
	int m_BossClientID = -1;       // CID of the boss bot
	int m_BossHP = 0;
	int m_BossMaxHP = 0;
	int m_SpawnTick = 0;           // Tick when the boss was spawned
	int m_NextSpawnTick = 0;       // Tick when boss should respawn
	int m_LastBossAttackTick = 0;  // Tick of last boss attack

	vec2 m_BossSpawnPos = vec2(0, 0);

	// Damage tracking per player
	struct SPlayerDamage {
		int ClientID;
		int Damage;
		int64 AccountID; // For persistence
	};
	std::vector<SPlayerDamage> m_aDamage;

	// Internal methods
	void SpawnBoss();
	void DespawnBoss();
	void TickBossAI(CCharacter *pBoss);
	void BroadcastBossStatus();
	void DistributeRewards(CPlayer *pKiller);
	void FindBossSpawnPos();
	int PickRandomWorld() const;
	void ResetDamageTracking();
};

#endif
