#include "scenario_manager.h"
#include <base/system.h>
#include <engine/server.h>
#include <engine/shared/jsonparser.h>
#include <engine/storage.h>
#include <game/server/player.h>
#include <game/server/gamecontext.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/mmo/mmo_types.h>

CScenarioManager::CScenarioManager(CGameContext* pGameServer) : m_pGameServer(pGameServer)
{
}

CScenarioManager::~CScenarioManager()
{
	Reset();
}

void CScenarioManager::Init()
{
	InitScenarios();
}

void CScenarioManager::Reset()
{
	for(CScenarioInstance* pInstance : m_vInstances)
	{
		if(pInstance)
			delete pInstance;
	}
	m_vInstances.clear();
}

void CScenarioManager::Update()
{
	for(int i = m_vInstances.size() - 1; i >= 0; i--)
	{
		CScenarioInstance* pInstance = m_vInstances[i];
		if(!pInstance)
		{
			m_vInstances.remove_index(i);
			continue;
		}

		// 如果副本已结束或不处于运行状态，移除实例
		if(!pInstance->IsRunning() || pInstance->IsComplete() || pInstance->IsFailed())
		{
			// 确保玩家已被传送回入口
			delete pInstance;
			m_vInstances.remove_index(i);
			continue;
		}

		pInstance->Update();
	}
}

void CScenarioManager::LoadFromJson(const char* pJsonString)
{
	CJsonParser Parser;
	json_value* pRoot = Parser.ParseString(pJsonString, "scenario");
	if(!pRoot || pRoot->type != json_object)
	{
		return;
	}

	const json_value& rScenarios = (*pRoot)["scenarios"];
	if(rScenarios.type != json_array)
	{
		return;
	}

	for(unsigned i = 0; i < rScenarios.u.array.length; i++)
	{
		const json_value& rScenario = rScenarios[(int)i];
		if(rScenario.type != json_object)
			continue;

		int ID = (int)rScenario["id"].u.integer;
		const char* pName = rScenario["name"].type == json_string ? rScenario["name"].u.string.ptr : "";

		CScenarioData* pData = new CScenarioData();
		pData->Init(ID, pName);

		// 加载 spawn points
		const json_value& rSpawnPoints = rScenario["spawn_points"];
		if(rSpawnPoints.type == json_array)
		{
			for(unsigned j = 0; j < rSpawnPoints.u.array.length; j++)
			{
				const json_value& rSpawn = rSpawnPoints[(int)j];
				if(rSpawn.type != json_object)
					continue;
				int X = (int)rSpawn["x"].u.integer;
				int Y = (int)rSpawn["y"].u.integer;
				int WorldID = (int)rSpawn["world_id"].u.integer;
				pData->AddSpawnPoint(vec2((float)X, (float)Y), WorldID);
			}
		}

		// 加载 wave configs
		const json_value& rWaves = rScenario["waves"];
		if(rWaves.type == json_array)
		{
			for(unsigned j = 0; j < rWaves.u.array.length; j++)
			{
				const json_value& rWave = rWaves[(int)j];
				if(rWave.type != json_object)
					continue;

				SScenarioWaveConfig Wave{};
				Wave.m_WaveNumber = (int)rWave["wave_number"].u.integer;
				Wave.m_BotID = (int)rWave["bot_id"].u.integer;
				Wave.m_Count = (int)rWave["count"].u.integer;
				Wave.m_SpawnDelay = (int)rWave["spawn_delay"].u.integer;

				if(rWave["spawn_x"].type == json_integer)
					Wave.m_SpawnPos.x = (float)rWave["spawn_x"].u.integer;
				if(rWave["spawn_y"].type == json_integer)
					Wave.m_SpawnPos.y = (float)rWave["spawn_y"].u.integer;

				pData->AddWaveConfig(Wave);
			}
		}

		// 加载 objectives
		const json_value& rObjectives = rScenario["objectives"];
		if(rObjectives.type == json_array)
		{
			for(unsigned j = 0; j < rObjectives.u.array.length; j++)
			{
				const json_value& rObj = rObjectives[(int)j];
				if(rObj.type != json_object)
					continue;
				int Type = (int)rObj["type"].u.integer;
				int TargetID = (int)rObj["target_id"].u.integer;
				int Count = (int)rObj["count"].u.integer;
				const char* pDesc = rObj["description"].type == json_string ? rObj["description"].u.string.ptr : "";
				pData->AddObjective((EScenarioType)Type, TargetID, Count, pDesc);
			}
		}

		// 加载 reward
		const json_value& rReward = rScenario["reward"];
		if(rReward.type == json_object)
		{
			int Gold = (int)rReward["gold"].u.integer;
			array<int> ItemIDs;
			array<int> ItemCounts;
			const json_value& rItems = rReward["items"];
			if(rItems.type == json_array)
			{
				for(unsigned j = 0; j < rItems.u.array.length; j++)
				{
					const json_value& rItem = rItems[(int)j];
					if(rItem.type != json_object)
						continue;
					int ItemID = (int)rItem["id"].u.integer;
					int Count = (int)rItem["count"].u.integer;
					ItemIDs.add(ItemID);
					ItemCounts.add(Count);
				}
			}
			pData->SetReward(Gold, ItemIDs, ItemCounts);
		}

		CScenarioData::ms_aData.add(pData);
	}

}

void CScenarioManager::LoadFromFile(const char* pFilename)
{
	// 读取 JSON 文件
	IOHANDLE File = GS()->Storage()->OpenFile(pFilename, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
	{
		dbg_msg("scenario", "Failed to open scenario file: %s", pFilename);
		return;
	}

	// 读取文件内容
	char aBuf[65536];
	int Len = io_read(File, aBuf, sizeof(aBuf) - 1);
	io_close(File);

	if(Len <= 0)
	{
		dbg_msg("scenario", "Empty scenario file: %s", pFilename);
		return;
	}
	aBuf[Len] = '\0';

	// 复用 JSON 解析逻辑
	LoadFromJson(aBuf);
}

CScenarioInstance* CScenarioManager::CreateInstance(int ScenarioID, CPlayer* pPlayer)
{
	CScenarioData* pData = CScenarioData::GetByID(ScenarioID);
	if(!pData || !pPlayer)
		return nullptr;

	// 如果玩家已有活跃的副本实例，先移除旧的
	CScenarioInstance* pExisting = GetInstance(pPlayer);
	if(pExisting)
	{
		RemoveInstance(pExisting);
	}

	CScenarioInstance* pInstance = new CScenarioInstance(m_pGameServer, pPlayer, pData);
	m_vInstances.add(pInstance);
	return pInstance;
}

void CScenarioManager::RemoveInstance(CScenarioInstance* pInstance)
{
	for(int i = 0; i < (int)m_vInstances.size(); i++)
	{
		if(m_vInstances[i] == pInstance)
		{
			if(pInstance->IsRunning())
				pInstance->Stop();
			delete pInstance;
			m_vInstances.remove_index(i);
			break;
		}
	}
}

CScenarioInstance* CScenarioManager::GetInstance(int ClientID)
{
	for(CScenarioInstance* pInstance : m_vInstances)
	{
		if(pInstance && pInstance->GetPlayer() && pInstance->GetPlayer()->GetCID() == ClientID)
			return pInstance;
	}
	return nullptr;
}

CScenarioInstance* CScenarioManager::GetInstance(CPlayer* pPlayer) const
{
	for(CScenarioInstance* pInstance : m_vInstances)
	{
		if(pInstance && pInstance->GetPlayer() == pPlayer)
			return pInstance;
	}
	return nullptr;
}

void CScenarioManager::OnPlayerKill(CPlayer* pVictim, CPlayer* pKiller, int Weapon)
{
	(void)Weapon;

	if(!pVictim || !pKiller)
		return;

	// 获取击杀者的活跃副本实例
	CScenarioInstance* pInstance = GetInstance(pKiller);
	if(!pInstance)
		return;

	// 只有击杀 bot (有 m_pMMOBotData) 才计入副本进度
	if(pVictim->m_pMMOBotData)
	{
		int DefID = pVictim->m_pMMOBotData->m_DefID;

		// 从 spawned bot list 中移除
		pInstance->RemoveBotByCID(pVictim->GetCID());

		// 按 DefID 追踪击杀
		pInstance->OnBotKilled(DefID);
	}
}

void CScenarioManager::OnPlayerMove(CPlayer* pPlayer)
{
	if(!pPlayer)
		return;

	CScenarioInstance* pInstance = GetInstance(pPlayer);
	if(!pInstance)
		return;

	pInstance->OnPlayerMove();
}

void CScenarioManager::OnPlayerCollectItem(CPlayer* pPlayer, int ItemID, int Count)
{
	if(!pPlayer)
		return;

	CScenarioInstance* pInstance = GetInstance(pPlayer);
	if(!pInstance)
		return;

	pInstance->OnItemCollected(ItemID, Count);
}

TWorldController* CScenarioManager::GetCore() const
{
	return GS()->Core();
}

CMMOManager* CScenarioManager::GetMMO() const
{
	if(GS()->Core())
		return GS()->Core()->GetMMOManager();
	return nullptr;
}

void CScenarioManager::InitScenarios()
{
	// 先清理已有数据
	for(CScenarioData* pData : CScenarioData::ms_aData)
	{
		delete pData;
	}
	CScenarioData::ms_aData.clear();

	// 尝试从文件加载
	LoadFromFile("server_content/scenarios.json");

	// 如果文件不存在或加载后没有数据，添加一个测试 scenario
	if(CScenarioData::ms_aData.size() == 0)
	{
		dbg_msg("scenario", "No scenarios loaded from file, adding default test scenario");

		CScenarioData* pData = new CScenarioData();
		pData->Init(1, "哥布林洞穴");

		// 出生点
		pData->AddSpawnPoint(vec2(400.0f, 400.0f), 0);

		// 波次配置
		SScenarioWaveConfig Wave1{};
		Wave1.m_WaveNumber = 1;
		Wave1.m_BotID = 1;
		Wave1.m_Count = 3;
		Wave1.m_SpawnDelay = 60; // 60 ticks (~1秒)
		Wave1.m_SpawnPos = vec2(400.0f, 300.0f);
		pData->AddWaveConfig(Wave1);

		SScenarioWaveConfig Wave2{};
		Wave2.m_WaveNumber = 2;
		Wave2.m_BotID = 1;
		Wave2.m_Count = 5;
		Wave2.m_SpawnDelay = 45; // 45 ticks (~0.75秒)
		Wave2.m_SpawnPos = vec2(450.0f, 350.0f);
		pData->AddWaveConfig(Wave2);

		// 目标
		pData->AddObjective(SCENARIO_TYPE_DEFEAT, 1, 8, "击败哥布林 %d/%d");

		// 奖励
		array<int> ItemIDs;
		array<int> ItemCounts;
		pData->SetReward(100, ItemIDs, ItemCounts);

		CScenarioData::ms_aData.add(pData);

		dbg_msg("scenario", "Added test scenario: 哥布林洞穴 (ID=%d, waves=%d)", pData->GetID(), pData->GetWaveConfigs().size());
	}
	else
	{
		dbg_msg("scenario", "Loaded %d scenarios from file", CScenarioData::ms_aData.size());
	}
}
