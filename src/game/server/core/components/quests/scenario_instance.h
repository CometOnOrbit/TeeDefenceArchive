#ifndef GAME_SERVER_COMPONENT_QUEST_SCENARIO_INSTANCE_H
#define GAME_SERVER_COMPONENT_QUEST_SCENARIO_INSTANCE_H

#include "scenario_data.h"
#include <base/tl/array.h>
#include <engine/shared/protocol.h>

class CPlayer;
class CGameContext;

class CScenarioInstance
{
CGameContext* m_pGameServer{};
CGameContext* GS() const { return m_pGameServer; }

CPlayer* m_pPlayer{};
CScenarioData* m_pScenarioData{};

array<SScenarioObjective> m_vObjectives{};
int m_CurrentWave{};
int m_SpawnedCount{};
int m_NextSpawnTick{};

bool m_Running{};
bool m_Complete{};
bool m_Failed{};

int m_SpawnedBotIDs[MAX_CLIENTS]{};
int m_NumSpawnedBots{};

// 副本传送相关
vec2 m_PreEnterPos{};        // 进入副本前玩家的位置
int m_PreEnterWorldID{};     // 进入副本前玩家的世界
int m_WorldID{};             // 副本所在世界
vec2 m_SpawnPos{};           // 副本出生点（玩家传送 + 刷怪用）

// 按 DefID 统计击杀
int m_aKillCountByDefID[256]{};

public:
CScenarioInstance(CGameContext* pGameServer, CPlayer* pPlayer, CScenarioData* pScenarioData);
~CScenarioInstance();

void Start();
void Stop();
void Update();

bool IsRunning() const { return m_Running; }
bool IsComplete() const { return m_Complete; }
bool IsFailed() const { return m_Failed; }

void OnBotSpawned(int BotID);
void OnBotKilled(int DefID);
void OnPlayerMove();
void OnItemCollected(int ItemID, int Count);

void SetWorldID(int WorldID);
void SetSpawnPos(vec2 Pos);
void RemoveBotByCID(int BotID);
void CleanupBots();

void UpdateObjectiveProgress(EScenarioType Type, int TargetID, int Delta);
bool CheckAllObjectivesComplete();
void GrantRewards();

CPlayer* GetPlayer() const { return m_pPlayer; }
CScenarioData* GetScenarioData() const { return m_pScenarioData; }

void SendProgressMessage(const char* pMsg);
void SendStatusMessage(const char* pMsg);
};

#endif
