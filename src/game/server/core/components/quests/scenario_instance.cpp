#include "scenario_instance.h"
#include <base/system.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/item_system.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/entities/character_bot_ai.h>

CScenarioInstance::CScenarioInstance(CGameContext* pGameServer, CPlayer* pPlayer, CScenarioData* pScenarioData)
	: m_pGameServer(pGameServer), m_pPlayer(pPlayer), m_pScenarioData(pScenarioData)
{
	for(const SScenarioObjective& Obj : pScenarioData->GetObjectives())
	{
		m_vObjectives.add(Obj);
	}
	m_CurrentWave = 0;
	m_PreEnterWorldID = -1;
	m_WorldID = -1;
	mem_zero(m_aKillCountByDefID, sizeof(m_aKillCountByDefID));
}

CScenarioInstance::~CScenarioInstance()
{
	Stop();
}

void CScenarioInstance::Start()
{
	if(!m_pPlayer || !m_pScenarioData)
		return;

	// 保存玩家当前位置/世界
	CCharacter* pChr = m_pPlayer->GetCharacter();
	if(pChr)
		m_PreEnterPos = pChr->GetPos();
	else
		m_PreEnterPos = vec2(0.0f, 0.0f);
	m_PreEnterWorldID = m_pPlayer->GetCurrentWorldID();

	// 设置副本世界和出生点
	m_WorldID = m_pScenarioData->GetSpawnWorldID();
	m_SpawnPos = m_pScenarioData->GetSpawnPosition();

	// 如果副本世界有效且与当前世界不同，传送到副本世界
	if(m_WorldID >= 0 && m_WorldID != m_PreEnterWorldID)
	{
		m_pPlayer->ChangeWorld(m_WorldID, &m_SpawnPos);
	}

	// 初始化状态
	m_Running = true;
	m_Complete = false;
	m_Failed = false;
	m_CurrentWave = 0;
	m_SpawnedCount = 0;
	m_NumSpawnedBots = 0;
	m_NextSpawnTick = GS()->Server()->Tick() + GS()->Server()->TickSpeed() * 2; // 2秒后开始刷怪
	mem_zero(m_SpawnedBotIDs, sizeof(m_SpawnedBotIDs));
	mem_zero(m_aKillCountByDefID, sizeof(m_aKillCountByDefID));

	for(SScenarioObjective& Obj : m_vObjectives)
	{
		Obj.m_CurrentCount = 0;
		Obj.m_Complete = false;
	}

	SendStatusMessage("副本开始!");
}

void CScenarioInstance::Stop()
{
	if(!m_Running)
		return;

	m_Running = false;

	// 清理所有 spawned bots
	CleanupBots();

	// 传送玩家回进入副本前的位置
	if(m_pPlayer && m_PreEnterWorldID >= 0)
	{
		m_pPlayer->ChangeWorld(m_PreEnterWorldID, &m_PreEnterPos);
	}
}

void CScenarioInstance::Update()
{
	if(!m_Running || m_Complete || m_Failed)
		return;

	const int Now = GS()->Server()->Tick();
	const int TickSpeed = GS()->Server()->TickSpeed();
	const array<SScenarioWaveConfig>& vWaves = m_pScenarioData->GetWaveConfigs();

	// 检查是否所有波次已完成
	if(m_CurrentWave >= (int)vWaves.size())
	{
		CheckAllObjectivesComplete();
		return;
	}

	const SScenarioWaveConfig& rWave = vWaves[m_CurrentWave];

	// 如果还有怪物要刷且到时间了
	if(m_SpawnedCount < rWave.m_Count)
	{
		if(Now >= m_NextSpawnTick)
		{
			// 计算刷怪位置：使用波次配置的位置，如果没有则使用副本出生点
			vec2 SpawnPos = rWave.m_SpawnPos;
			if(SpawnPos.x == 0.0f && SpawnPos.y == 0.0f)
				SpawnPos = m_SpawnPos;

			// 调用 MMO manager 生成怪物
			CMMOManager* pMMO = GS()->Core()->GetMMOManager();
			if(pMMO)
			{
				int CID = pMMO->SpawnMob(rWave.m_BotID, SpawnPos);
				if(CID >= 0)
				{
					OnBotSpawned(CID);
				}
			}

			m_SpawnedCount++;
			m_NextSpawnTick = Now + rWave.m_SpawnDelay * TickSpeed / 1000; // 转换 ms 到 tick
			if(m_NextSpawnTick <= Now)
				m_NextSpawnTick = Now + TickSpeed; // 至少延迟 1 tick
		}
	}
	else if(m_NumSpawnedBots == 0)
	{
		// 当前波次所有怪已全部刷完且全部被击杀
		m_CurrentWave++;
		m_SpawnedCount = 0;

		// 检查是否所有波次都完成了
		if(m_CurrentWave >= (int)vWaves.size())
		{
			// 所有波次完成，检查所有目标
			CheckAllObjectivesComplete();
		}
		else
		{
			// 立即开始下一波（延迟一小段时间）
			m_NextSpawnTick = Now + TickSpeed;
		}
	}
}

void CScenarioInstance::OnBotSpawned(int BotID)
{
	if(m_NumSpawnedBots < MAX_CLIENTS)
	{
		m_SpawnedBotIDs[m_NumSpawnedBots++] = BotID;
	}
}

void CScenarioInstance::OnBotKilled(int DefID)
{
	if(DefID >= 0 && DefID < 256)
	{
		m_aKillCountByDefID[DefID]++;
	}

	// 更新击杀类型的副本目标
	UpdateObjectiveProgress(SCENARIO_TYPE_DEFEAT, DefID, 1);
}

void CScenarioInstance::RemoveBotByCID(int BotID)
{
	for(int i = 0; i < m_NumSpawnedBots; i++)
	{
		if(m_SpawnedBotIDs[i] == BotID)
		{
			for(int j = i; j < m_NumSpawnedBots - 1; j++)
			{
				m_SpawnedBotIDs[j] = m_SpawnedBotIDs[j + 1];
			}
			m_NumSpawnedBots--;
			break;
		}
	}
}

void CScenarioInstance::OnPlayerMove()
{
	if(!m_Running || !m_pPlayer || !m_pPlayer->GetCharacter())
		return;

	// 检查是否有到达类型的副本目标
	for(const SScenarioObjective& Obj : m_vObjectives)
	{
		if(!Obj.m_Complete && Obj.m_Type == SCENARIO_TYPE_REACH)
		{
			// 检查玩家是否到达目标位置
			vec2 TargetPos((float)Obj.m_TargetID, 0.0f);
			// 使用 TargetID 作为位置参考，检查距离
			vec2 PlayerPos = m_pPlayer->GetCharacter()->GetPos();
			// 注意：TargetID 在此场景中应编码为目标位置的 x 坐标
			// 实际项目中可能需要更精确的位置跟踪
			if(distance(PlayerPos, TargetPos) <= 64.0f)
			{
				UpdateObjectiveProgress(SCENARIO_TYPE_REACH, Obj.m_TargetID, 1);
			}
		}
	}
}

void CScenarioInstance::OnItemCollected(int ItemID, int Count)
{
	UpdateObjectiveProgress(SCENARIO_TYPE_COLLECT, ItemID, Count);
}

void CScenarioInstance::UpdateObjectiveProgress(EScenarioType Type, int TargetID, int Delta)
{
	for(SScenarioObjective& Obj : m_vObjectives)
	{
		if(!Obj.m_Complete && Obj.m_Type == Type && Obj.m_TargetID == TargetID)
		{
			Obj.m_CurrentCount += Delta;
			if(Obj.m_CurrentCount >= Obj.m_RequiredCount)
			{
				Obj.m_CurrentCount = Obj.m_RequiredCount;
				Obj.m_Complete = true;

				char aMsg[128];
				str_format(aMsg, sizeof(aMsg), "目标完成: %s", Obj.m_Description);
				SendProgressMessage(aMsg);
			}
		}
	}
}

bool CScenarioInstance::CheckAllObjectivesComplete()
{
	// 检查所有非波次目标是否完成
	bool AllComplete = true;
	for(const SScenarioObjective& Obj : m_vObjectives)
	{
		if(!Obj.m_Complete)
		{
			AllComplete = false;
			break;
		}
	}

	// 如果有波次目标：所有波次必须也完成
	const array<SScenarioWaveConfig>& vWaves = m_pScenarioData->GetWaveConfigs();
	bool AllWavesDone = (m_CurrentWave >= (int)vWaves.size()) && m_NumSpawnedBots == 0;

	// 检查是否有任何 WAVE 类型的目标
	bool HasWaveObjective = false;
	for(const SScenarioObjective& Obj : m_vObjectives)
	{
		if(Obj.m_Type == SCENARIO_TYPE_WAVE)
		{
			HasWaveObjective = true;
			break;
		}
	}

	// 如果有波次目标，需要波次也完成
	if(HasWaveObjective && !AllWavesDone)
		AllComplete = false;

	if(AllComplete)
	{
		m_Complete = true;
		m_Running = false;
		GrantRewards();
		SendStatusMessage("副本完成!");

		// 延迟清理并传送回入口
		// 实际项目中可以在 GrantRewards 后立即调用 Stop
		Stop();
		return true;
	}

	return false;
}

void CScenarioInstance::GrantRewards()
{
	if(!m_pPlayer)
		return;

	const SScenarioReward& Reward = m_pScenarioData->GetReward();
	CMMOManager* pMMO = GS()->Core()->GetMMOManager();

	if(!pMMO)
	{
		dbg_msg("scenario", "GrantRewards: MMO manager not available");
		return;
	}

	// 发放金币奖励
	if(Reward.m_Gold > 0)
	{
		pMMO->AddGold(m_pPlayer, Reward.m_Gold);
		char aMsg[128];
		str_format(aMsg, sizeof(aMsg), "获得金币: %d", Reward.m_Gold);
		SendProgressMessage(aMsg);
	}

	// 发放物品奖励
	for(int i = 0; i < (int)Reward.m_ItemIDs.size(); i++)
	{
		int ItemID = Reward.m_ItemIDs[i];
		int Count = Reward.m_ItemCounts[i];
		if(ItemID >= 0)
		{
			if(pMMO->GiveItem(m_pPlayer, ItemID, Count))
			{
				char aMsg[128];
				str_format(aMsg, sizeof(aMsg), "获得物品 #%d: %d个", ItemID, Count);
				SendProgressMessage(aMsg);
			}
		}
	}
}

void CScenarioInstance::SendProgressMessage(const char* pMsg)
{
	if(m_pPlayer)
	{
		GS()->SendChatTo(m_pPlayer->GetCID(), pMsg);
	}
}

void CScenarioInstance::SendStatusMessage(const char* pMsg)
{
	SendProgressMessage(pMsg);
}

void CScenarioInstance::SetWorldID(int WorldID)
{
	m_WorldID = WorldID;
}

void CScenarioInstance::SetSpawnPos(vec2 Pos)
{
	m_SpawnPos = Pos;
}

void CScenarioInstance::CleanupBots()
{
	// 遍历所有 spawned bots 并移除
	for(int i = 0; i < m_NumSpawnedBots; i++)
	{
		int CID = m_SpawnedBotIDs[i];
		if(CID >= 0 && CID < MAX_CLIENTS && GS()->m_apPlayers[CID])
		{
			CPlayer* pBot = GS()->m_apPlayers[CID];

			// 清理 bot data
			if(pBot->m_pMMOBotData)
			{
				delete pBot->m_pMMOBotData;
				pBot->m_pMMOBotData = nullptr;
			}

			// 移除 dummy
			GS()->Server()->DummyRemove(CID);
			delete pBot;
			GS()->m_apPlayers[CID] = nullptr;
		}
	}

	m_NumSpawnedBots = 0;
	mem_zero(m_SpawnedBotIDs, sizeof(m_SpawnedBotIDs));
}
