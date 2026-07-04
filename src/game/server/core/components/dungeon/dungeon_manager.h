#ifndef GAME_SERVER_CORE_COMPONENTS_DUNGEON_DUNGEON_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_DUNGEON_DUNGEON_MANAGER_H

#include <engine/console.h>
#include <base/system.h>
#include <base/tl/array.h>
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/server/core/tools/event_listener.h>

class CPlayer;
class CGameContext;
class CCommandManager;

static constexpr int MAX_DUNGEON_INSTANCES = 16;
static constexpr int MAX_DUNGEON_PLAYERS = 8;
static constexpr int MAX_DUNGEON_ROOMS = 16;
static constexpr int MAX_ROOM_SPAWNS = 32;

// ── Room configuration (per template) ──────────────────────────
struct SDungeonRoomSpawn
{
	vec2 m_Position{};
	int m_MobDefID{};
};

struct SDungeonRoom
{
	int m_RoomNumber{};
	vec2 m_PlayerSpawnPos{};        // 玩家进入该房间的传送点
	SDungeonRoomSpawn m_aSpawns[MAX_ROOM_SPAWNS];
	int m_NumSpawns{};
	bool m_IsBossRoom{};
	int m_BossMobDefID{};
};

struct SDungeonTemplate
{
	int m_ID;
	char m_aName[32];
	int m_WorldID;
	int m_MinLevel;
	int m_MaxLevel;
	int m_MaxPlayers;
	int m_TimeLimitTicks;
	int m_RewardGold;
	int m_RewardExp;
	array<int> m_RewardItemIDs;
	array<int> m_RewardItemCounts;
	SDungeonRoom m_aRooms[MAX_DUNGEON_ROOMS];
	int m_NumRooms;
};

// ── Runtime room state (per instance) ──────────────────────────
struct SDungeonRoomState
{
	bool m_Spawned{};                // 怪物已生成
	int m_aSpawnedBotCIDs[MAX_ROOM_SPAWNS];
	int m_NumSpawnedBots{};
	bool m_Cleared{};                // 该房间所有怪物已消灭
};

struct SDungeonInstance
{
	int m_ID;
	int m_TemplateID;
	int m_WorldID;
	int m_ArenaWorldID{-1}; // 绑定的动态世界 ID（-1 表示未创建）
	int m_MinLevel;
	int m_MaxLevel;
	int m_MaxPlayers;
	int m_aPlayerCIDs[MAX_DUNGEON_PLAYERS];
	int m_NumPlayers;
	bool m_Active;
	int m_TimeLimitTicks;
	int m_ElapsedTicks;
	int m_CurrentRoom{};             // 当前房间（0-based）
	SDungeonRoomState m_aRoomStates[MAX_DUNGEON_ROOMS];
	bool m_Completed{};
	bool m_RewardsGiven{};
};

class CDungeonManager : public IGameEventListener
{
public:
	CDungeonManager();
	~CDungeonManager() override;
	CDungeonManager(const CDungeonManager&) = delete;
	CDungeonManager &operator=(const CDungeonManager&) = delete;

	void OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon) override;

	void Init(CGameContext *pGameServer);

	// Template access
	int NumTemplates() const { return m_NumTemplates; }
	const SDungeonTemplate* GetTemplate(int Index) const;
	const SDungeonTemplate* FindTemplateByName(const char *pName) const;

	// Instance operations
	int CreateDungeon(int TemplateID, int CreatorCID);
	bool JoinDungeon(int PlayerCID, int DungeonID);
	bool LeaveDungeon(int PlayerCID);
	bool IsInDungeon(int PlayerCID) const;
	int GetPlayerDungeonID(int PlayerCID) const;
	SDungeonInstance* GetDungeonByIndex(int Index);
	SDungeonInstance* GetDungeon(int DungeonID);
	int NumInstances() const { return m_NumInstances; }

	// Gameplay
	void OnMobKilled(int VictimCID, CPlayer *pKiller);
	void Tick();

	// Commands
	void RegisterChatCommands(CCommandManager *pManager);
	static void ConDungeonCmd(IConsole::IResult *pResult, void *pUser);

private:
	CGameContext *m_pGS;
	SDungeonTemplate m_aTemplates[8];
	int m_NumTemplates;
	SDungeonInstance m_aInstances[MAX_DUNGEON_INSTANCES];
	int m_NumInstances;
	int m_NextInstanceID;

	void LoadTemplates();
	void SpawnRoomMobs(SDungeonInstance &D, SDungeonRoomState &RState, const SDungeonRoom &Room);
	void CheckRoomCleared(SDungeonInstance &D, int RoomIdx);
	void AdvanceToNextRoom(SDungeonInstance &D);
	void TransportPlayers(SDungeonInstance &D, vec2 Pos);
	void GrantRewards(SDungeonInstance &D);
	void CleanupInstance(SDungeonInstance &D);

	int FindEmptySlot();
	int FindWorldHub() const;
};

#endif
