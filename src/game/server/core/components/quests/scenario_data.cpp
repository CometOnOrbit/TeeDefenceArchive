#include "scenario_data.h"
#include <base/system.h>

array<CScenarioData*> CScenarioData::ms_aData;

void CScenarioData::Init(int ID, const char* pName)
{
	m_ID = ID;
	str_copy(m_Name, pName, sizeof(m_Name));
}

void CScenarioData::AddSpawnPoint(vec2 Position, int WorldID)
{
	SScenarioSpawnPoint Spawn{};
	Spawn.m_Position = Position;
	Spawn.m_WorldID = WorldID;
	m_vSpawnPoints.add(Spawn);
}

void CScenarioData::AddWaveConfig(const SScenarioWaveConfig& Wave)
{
	m_vWaveConfigs.add(Wave);
}

void CScenarioData::AddObjective(EScenarioType Type, int TargetID, int RequiredCount, const char* pDesc)
{
	SScenarioObjective Obj{};
	Obj.m_Type = Type;
	Obj.m_TargetID = TargetID;
	Obj.m_RequiredCount = RequiredCount;
	Obj.m_CurrentCount = 0;
	Obj.m_Complete = false;
	str_copy(Obj.m_Description, pDesc, sizeof(Obj.m_Description));
	m_vObjectives.add(Obj);
}

void CScenarioData::SetReward(int Gold, const array<int>& ItemIDs, const array<int>& ItemCounts)
{
	m_Reward.m_Gold = Gold;
	for(int ID : ItemIDs)
		m_Reward.m_ItemIDs.add(ID);
	for(int Count : ItemCounts)
		m_Reward.m_ItemCounts.add(Count);
}

int CScenarioData::GetSpawnWorldID() const
{
	if(m_vSpawnPoints.size() > 0)
		return m_vSpawnPoints[0].m_WorldID;
	return 0;
}

vec2 CScenarioData::GetSpawnPosition() const
{
	if(m_vSpawnPoints.size() > 0)
		return m_vSpawnPoints[0].m_Position;
	return vec2(0.0f, 0.0f);
}

CScenarioData* CScenarioData::GetByID(int ID)
{
	for(CScenarioData* pData : ms_aData)
	{
		if(pData && pData->GetID() == ID)
			return pData;
	}
	return nullptr;
}
