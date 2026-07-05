#ifndef GAME_SERVER_CORE_COMPONENTS_MMO_MMO_WORLD_BOSS_H
#define GAME_SERVER_CORE_COMPONENTS_MMO_MMO_WORLD_BOSS_H

#include <game/server/core/tworld_component.h>
#include <game/server/core/tools/event_listener.h>
#include <base/vmath.h>
#include <vector>

class CGameContext;
class CPlayer;
class CCharacter;
class CMMOManager;
class CCommandManager;
struct SMMOMobDef;

struct SWorldBossItemReward
{
	int m_ItemID = 0;
	int m_Count = 0;
};

struct SWorldBossRewardTier
{
	int m_RankMin = 1;
	int m_RankMax = 1;
	int m_Gold = 0;
	int m_Exp = 0;
	int m_Reputation = 0;
	char m_aLabel[8] = "";
	std::vector<SWorldBossItemReward> m_vItems;
};

struct SWorldBossKillBonus
{
	int m_Reputation = 0;
	std::vector<SWorldBossItemReward> m_vItems;
};

struct SWorldBossBroadcastLine
{
	char m_aKey[64] = {};
	char m_aFallback[256] = {};
};

struct SWorldBossSchedule
{
	int m_InitialMinSec = 300;
	int m_InitialMaxSec = 900;
	int m_RespawnSec = 3600;
};

struct SWorldBossSpawnDef
{
	char m_aId[48] = {};
	bool m_Enabled = true;
	int m_World = -1;
	int m_MobID = -1;
	vec2 m_SpawnPos = vec2(0, 0);
	int m_Slot = 63;
	char m_aDisplayName[64] = {};
	char m_aDisplayNameKey[64] = {};
	SWorldBossSchedule m_Schedule;
	SWorldBossBroadcastLine m_SpawnMsg;
	SWorldBossBroadcastLine m_KillMsg;
	SWorldBossBroadcastLine m_HpMsg;
	SWorldBossBroadcastLine m_StatusAliveMsg;
	SWorldBossBroadcastLine m_StatusWaitingMsg;
	SWorldBossBroadcastLine m_StatusSoonMsg;
	SWorldBossBroadcastLine m_ClanMsg;
	int m_LeaderboardLines = 5;
	std::vector<SWorldBossRewardTier> m_vRewardTiers;
	SWorldBossKillBonus m_KillBonus;
};

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

	void RecordDamage(int BossCID, int AttackerCID, int Damage);

	bool IsEnabled() const { return m_pSpawn != nullptr && m_pSpawn->m_Enabled && m_pMobDef != nullptr; }
	bool IsWorldBoss(CPlayer *pPlayer) const;
	bool IsWorldBossCharacter(CCharacter *pChar) const;
	bool IsBossAlive() const { return m_IsAlive; }
	int GetBossHP() const { return m_BossHP; }
	int GetBossMaxHP() const { return m_BossMaxHP; }
	int GetNextSpawnInTicks() const;
	int GetBossClientID() const { return m_BossClientID; }
	bool GetBossPos(vec2 *pOut) const;
	const char *GetBossDisplayName(int ClientID) const;

	void RegisterBossCommands();
	void RegisterBossVoteCommands(CCommandManager *pManager);
	void SendBossStatusChat(int ClientID) const;

private:
	struct SPlayerDamage {
		int ClientID;
		int Damage;
		int64 AccountID;
	};

	const SWorldBossSpawnDef *m_pSpawn = nullptr;
	const SMMOMobDef *m_pMobDef = nullptr;

	int m_WorldID = -1;
	bool m_IsAlive = false;
	int m_BossClientID = -1;
	int m_BossHP = 0;
	int m_BossMaxHP = 0;
	int m_NextSpawnTick = 0;

	std::vector<SPlayerDamage> m_aDamage;

	void ResolveSpawnForWorld(int WorldID);
	void ScheduleNextSpawn(bool bInitial);
	int RespawnDelayTicks() const;
	void SpawnBoss();
	void DespawnBoss();
	void BroadcastBossStatus();
	void DistributeRewards(CPlayer *pKiller);
	void ResetDamageTracking();
	void FormatBroadcast(int ClientID, char *pBuf, int BufSize, const SWorldBossBroadcastLine &Line, ...) const;

	const SWorldBossRewardTier *FindRewardTier(int Rank) const;
	void GrantReward(CMMOManager *pMMO, CPlayer *pPlayer, const SWorldBossRewardTier &Tier,
		int Rank, int Damage, int DamagePct) const;
	void GrantKillBonus(CMMOManager *pMMO, CPlayer *pKiller) const;
	void AppendItemRewardSummary(char *pBuf, int BufSize, const std::vector<SWorldBossItemReward> &vItems) const;
};

#endif
