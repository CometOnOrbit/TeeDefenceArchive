#ifndef GAME_SERVER_COMPONENT_QUEST_SCENARIO_DATA_H
#define GAME_SERVER_COMPONENT_QUEST_SCENARIO_DATA_H

#include <base/tl/array.h>
#include <base/vmath.h>

class CPlayer;
struct SScenarioBotInfo;

enum EScenarioType
{
SCENARIO_TYPE_DEFEAT,
SCENARIO_TYPE_COLLECT,
SCENARIO_TYPE_REACH,
SCENARIO_TYPE_ESCORT,
SCENARIO_TYPE_WAVE,
SCENARIO_TYPE_MAX
};

struct SScenarioSpawnPoint
{
vec2 m_Position{};
int m_WorldID{};
};

struct SScenarioWaveConfig
{
int m_WaveNumber{};
int m_BotID{};
int m_Count{};
int m_SpawnDelay{};
vec2 m_SpawnPos{};
};

struct SScenarioObjective
{
EScenarioType m_Type{};
int m_TargetID{};
int m_RequiredCount{};
int m_CurrentCount{};
bool m_Complete{};

char m_Description[128]{};
};

struct SScenarioReward
{
int m_Gold{};
array<int> m_ItemIDs{};
array<int> m_ItemCounts{};
};

class CScenarioData
{
public:
CScenarioData() = default;
~CScenarioData() = default;

void Init(int ID, const char* pName);
void AddSpawnPoint(vec2 Position, int WorldID);
void AddWaveConfig(const SScenarioWaveConfig& Wave);
void AddObjective(EScenarioType Type, int TargetID, int RequiredCount, const char* pDesc);
void SetReward(int Gold, const array<int>& ItemIDs, const array<int>& ItemCounts);

int GetID() const { return m_ID; }
const char* GetName() const { return m_Name; }
const array<SScenarioSpawnPoint>& GetSpawnPoints() const { return m_vSpawnPoints; }
const array<SScenarioWaveConfig>& GetWaveConfigs() const { return m_vWaveConfigs; }
const array<SScenarioObjective>& GetObjectives() const { return m_vObjectives; }
const SScenarioReward& GetReward() const { return m_Reward; }

int GetSpawnWorldID() const;     // 从第一个 spawn point 取 world ID
vec2 GetSpawnPosition() const;   // 从第一个 spawn point 取 position

static array<CScenarioData*> ms_aData;
static CScenarioData* GetByID(int ID);

private:
int m_ID{};
char m_Name[64]{};
array<SScenarioSpawnPoint> m_vSpawnPoints{};
array<SScenarioWaveConfig> m_vWaveConfigs{};
array<SScenarioObjective> m_vObjectives{};
SScenarioReward m_Reward{};
};

#endif
