#include "quest_step_data.h"
#include "quest_manager.h"
#include "scenario_manager.h"
#include "entities/dir_navigator.h"
#include <game/server/gamecontext.h>
#include <game/server/entity.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/mmo/mmo_types.h>
#include <game/server/data_center.h>
#include <game/server/item_system.h>
#include <engine/shared/jsonparser.h>

void CQuestStepBase::UpdateBot() const
{
}

bool CQuestStepBase::IsActiveStep() const
{
	return false;
}

CPlayer* CQuestStep::GetPlayer() const
{
	return GS()->m_apPlayers[m_ClientID];
}

CQuestStep::~CQuestStep()
{
	m_aMobProgress.clear();
	m_aMoveActionProgress.clear();
	if(m_pEntNavigator)
	{
		GS()->m_World.DestroyEntity(static_cast<CEntity*>(m_pEntNavigator));
		m_pEntNavigator = nullptr;
	}
}

bool CQuestStep::IsComplete()
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return false;

	if(m_Bot.m_ScenarioJson[0] != '\0')
	{
		if(TWorldController* pCore = GS()->Core())
		{
			if(CQuestManager* pQuestMgr = pCore->QuestManager())
			{
				CScenarioManager* pScenarioMgr = pQuestMgr->GetScenarioManager();
				if(pScenarioMgr)
				{
					CScenarioInstance* pInstance = pScenarioMgr->GetInstance(pPlayer);
					if(pInstance && pInstance->IsComplete())
						return true;
				}
			}
		}
		return false;
	}

	for(auto& p : m_Bot.m_vRequiredItems)
	{
		if(pPlayer->m_MMOInventory.CountByID(p.m_ItemID) < p.m_Value)
			return false;
	}

	for(auto& p : m_Bot.m_vRequiredDefeats)
	{
		if(m_aMobProgress[p.m_BotID].m_Count < p.m_RequiredCount)
			return false;
	}

	for(bool F : m_aMoveActionProgress)
	{
		if(!F)
			return false;
	}

	// Check dialog requirements
	for(auto& d : m_Bot.m_vRequiredDialogs)
	{
		const char* pNpcId = d.m_aNpcId;
		auto it = m_aDialogProgress.find(pNpcId);
		if(it == m_aDialogProgress.end() || !it->second.m_DialogDone)
			return false;
	}

	return true;
}

bool CQuestStep::Finish(bool Force)
{
	if(!Force && !IsComplete())
		return false;

	m_StepComplete = true;

	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
	{
		return false;
	}

	PostFinish();
	return true;
}

void CQuestStep::PostFinish()
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return;

	if(m_Bot.m_ScenarioJson[0] != '\0')
	{
		if(TWorldController* pCore = GS()->Core())
		{
			if(CQuestManager* pQuestMgr = pCore->QuestManager())
			{
				CScenarioManager* pScenarioMgr = pQuestMgr->GetScenarioManager();
				if(pScenarioMgr)
				{
					CScenarioInstance* pInstance = pScenarioMgr->GetInstance(pPlayer);
					if(pInstance)
						pScenarioMgr->RemoveInstance(pInstance);
				}
			}
		}
	}

	for(auto& pRequired : m_Bot.m_vRequiredItems)
	{
		if(pRequired.m_Type == SQuestBotInfo::SRequiredItem::TYPE_GIVE)
		{
			if(pPlayer->m_MMOInventory.CountByID(pRequired.m_ItemID) >= pRequired.m_Value)
			{
				pPlayer->m_MMOInventory.RemoveByID(pRequired.m_ItemID, pRequired.m_Value);
				GS()->SendChatTo(pPlayer->GetCID(), "[完成] 交给物品");
				pPlayer->m_MMODirty = true;
			}
		}
	}

	for(int ItemID : m_Bot.m_RewardItems)
	{
		if(ItemID > 0)
		{
			pPlayer->m_MMOInventory.Add(ItemID, 1, 0);
			pPlayer->m_MMODirty = true;
		}
	}
}

bool CQuestStep::TryAutoFinish()
{
	CPlayer* pPlayer = GetPlayer();
	if(pPlayer && IsComplete())
	{
		Finish();
		return true;
	}
	return false;
}

void CQuestStep::AppendDefeatProgress(int DefeatedBotID, const char *pZoneName)
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer || m_MarkedForDestroy)
		return;

	if(m_Bot.m_ScenarioJson[0] != '\0')
	{
		if(TWorldController* pCore = GS()->Core())
		{
			if(CQuestManager* pQuestMgr = pCore->QuestManager())
			{
				CScenarioManager* pScenarioMgr = pQuestMgr->GetScenarioManager();
				if(pScenarioMgr)
					pScenarioMgr->OnPlayerKill(nullptr, pPlayer, 0);
			}
		}
		return;
	}

	if(m_StepComplete || m_Bot.m_vRequiredDefeats.size() == 0)
		return;

	for(auto& p : m_Bot.m_vRequiredDefeats)
	{
		if(p.m_BotID != DefeatedBotID || m_aMobProgress[DefeatedBotID].m_Count >= p.m_RequiredCount)
			continue;

		// Zone filter: if this defeat requirement has a zone constraint,
		// only count kills in the matching zone
		if(p.m_ZoneName[0])
		{
			if(!pZoneName || str_comp(p.m_ZoneName, pZoneName) != 0)
				continue;
		}

		m_aMobProgress[DefeatedBotID].m_Count++;
		if(m_aMobProgress[DefeatedBotID].m_Count >= p.m_RequiredCount)
		{
			m_aMobProgress[DefeatedBotID].m_Complete = true;
			GS()->SendChatTo(pPlayer->GetCID(), "[完成] 击败敌人!");
		}
	}
}

void CQuestStep::UpdateNavigator()
{
	CPlayer* pPlayer = GetPlayer();
	if(m_MarkedForDestroy || m_StepComplete || !pPlayer || !pPlayer->GetCharacter())
	{
		if(m_pEntNavigator)
		{
			GS()->m_World.DestroyEntity(static_cast<CEntity*>(m_pEntNavigator));
			m_pEntNavigator = nullptr;
		}
		return;
	}

	const vec2 PlayerPos = pPlayer->GetCharacter()->GetPos();

	// ── Clean up old defeat-bot navigators ──
	for(auto* pEnt : m_vpEntitiesDefeatBotNavigator)
	{
		GS()->m_World.DestroyEntity(static_cast<CEntity*>(pEnt));
	}
	m_vpEntitiesDefeatBotNavigator.clear();

	// ── Defeat-bot navigators: point to zone center or known spawn area ──
	for(auto& Defeat : m_Bot.m_vRequiredDefeats)
	{
		int Current = m_aMobProgress[Defeat.m_BotID].m_Count;
		if(Current >= Defeat.m_RequiredCount)
			continue; // Already completed

		vec2 TargetPos(0.f, 0.f);
		int TargetWorld = pPlayer->GetCurrentWorldID();
		bool HasTarget = false;

		// Try zone center first
		if(Defeat.m_ZoneName[0])
		{
			const SMMOZoneDef *pZone = SMMOZoneDef::Find(TargetWorld, Defeat.m_ZoneName);
			if(pZone)
			{
				TargetPos = vec2(
					(float)(pZone->m_X1 + pZone->m_X2) / 2.0f,
					(float)(pZone->m_Y1 + pZone->m_Y2) / 2.0f
				);
				HasTarget = true;
			}
		}

		// Fallback: use main target position
		if(!HasTarget && (m_Bot.m_Position.x != 0.f || m_Bot.m_Position.y != 0.f))
		{
			TargetPos = m_Bot.m_Position;
			TargetWorld = m_Bot.m_WorldID;
			HasTarget = true;
		}

		if(HasTarget && distance(PlayerPos, TargetPos) > 64.f)
		{
			CEntityDirNavigator *pNav = new CEntityDirNavigator(GS(), PlayerPos, TargetPos,
				m_ClientID, TargetWorld, 0, 0, 32.f);
			m_vpEntitiesDefeatBotNavigator.add(pNav);
		}
	}

	// ── Main navigator: point to step's target position ──
	const vec2 StepTargetPos = m_Bot.m_Position;
	const int StepTargetWorld = m_Bot.m_WorldID;
	const bool HasMainTarget = (StepTargetPos.x != 0.f || StepTargetPos.y != 0.f);

	if(HasMainTarget)
	{
		if(!m_pEntNavigator)
		{
			m_pEntNavigator = new CEntityDirNavigator(GS(), PlayerPos, StepTargetPos,
				m_ClientID, StepTargetWorld, 0, 0, 32.f);
		}
	}
	else
	{
		if(m_pEntNavigator)
		{
			GS()->m_World.DestroyEntity(static_cast<CEntity*>(m_pEntNavigator));
			m_pEntNavigator = nullptr;
		}
	}
}

void CQuestStep::UpdateObjectives()
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer || !pPlayer->GetCharacter() || m_MarkedForDestroy)
	{
		return;
	}

	// Check move action positions (player reaching target = complete)
	for(unsigned i = 0; i < m_Bot.m_vRequiredMoveAction.size(); i++)
	{
		if(i >= m_aMoveActionProgress.size())
			break;
		if(m_aMoveActionProgress[i])
			continue;

		const auto& MoveAction = m_Bot.m_vRequiredMoveAction[i];
		const vec2 PlayerPos = pPlayer->GetCharacter()->GetPos();
		if(distance(PlayerPos, MoveAction.m_Position) <= 64.0f)
		{
			m_aMoveActionProgress[i] = true;
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "[完成] %s", MoveAction.m_TaskName);
			GS()->SendChatTo(pPlayer->GetCID(), aBuf);
			pPlayer->m_MMODirty = true;
		}
	}

	if(m_Bot.m_ScenarioJson[0] != '\0')
	{
		if(TWorldController* pCore = GS()->Core())
		{
			if(CQuestManager* pQuestMgr = pCore->QuestManager())
			{
				CScenarioManager* pScenarioMgr = pQuestMgr->GetScenarioManager();
				if(pScenarioMgr)
					pScenarioMgr->OnPlayerMove(pPlayer);
			}
		}
	}
}

void CQuestStep::Update()
{
	UpdateNavigator();
	UpdateObjectives();

	if(m_Bot.m_ScenarioJson[0] != '\0')
	{
		if(TWorldController* pCore = GS()->Core())
		{
			if(CQuestManager* pQuestMgr = pCore->QuestManager())
			{
				CScenarioManager* pScenarioMgr = pQuestMgr->GetScenarioManager();
				if(pScenarioMgr)
					pScenarioMgr->Update();
			}
		}
	}
}

void CQuestStep::MarkForDestroy()
{
	m_MarkedForDestroy = true;
}

void CQuestStep::ClearObjectives()
{
	for(auto* pEnt : m_vpEntitiesMoveAction)
	{
		GS()->m_World.DestroyEntity(pEnt);
	}
	m_vpEntitiesMoveAction.clear();

	for(auto* pEnt : m_vpEntitiesDefeatBotNavigator)
	{
		GS()->m_World.DestroyEntity(static_cast<CEntity*>(pEnt));
	}
	m_vpEntitiesDefeatBotNavigator.clear();
}

void CQuestStep::FormatStringTasks(char* aBufQuestTask, int Size)
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return;

	char aTemp[512];
	str_copy(aBufQuestTask, "", Size);

	// Defeat objectives
	for(auto& Defeat : m_Bot.m_vRequiredDefeats)
	{
		int Current = m_aMobProgress[Defeat.m_BotID].m_Count;
		const SMMOMobDef *pDef = SMMOMobDef::Get(Defeat.m_BotID);
		const char *pMobName = pDef ? pDef->m_aName : "??";

		if(Defeat.m_ZoneName[0])
		{
			str_format(aTemp, sizeof(aTemp), "在 %s 区域击败 %s (%d/%d)\n",
				Defeat.m_ZoneName, pMobName, Current, Defeat.m_RequiredCount);
		}
		else
		{
			str_format(aTemp, sizeof(aTemp), "击败 %s (%d/%d)\n",
				pMobName, Current, Defeat.m_RequiredCount);
		}
		str_append(aBufQuestTask, aTemp, Size);
	}

	// Item collection objectives
	for(auto& Item : m_Bot.m_vRequiredItems)
	{
		str_format(aTemp, sizeof(aTemp), "收集物品 #%d (%d个)\n",
			Item.m_ItemID, Item.m_Value);
		str_append(aBufQuestTask, aTemp, Size);
	}

	// Dialog objectives
	for(auto& Dialog : m_Bot.m_vRequiredDialogs)
	{
		bool done = m_aDialogProgress[Dialog.m_aNpcId].m_DialogDone;
		str_format(aTemp, sizeof(aTemp), "%s与 %s 对话%s\n",
			done ? "✓ " : "○ ",
			Dialog.m_aNpcId,
			done ? " [完成]" : "");
		str_append(aBufQuestTask, aTemp, Size);
	}

	// Move action objectives
	for(unsigned i = 0; i < m_Bot.m_vRequiredMoveAction.size(); i++)
	{
		bool done = (i < m_aMoveActionProgress.size()) && m_aMoveActionProgress[i];
		str_format(aTemp, sizeof(aTemp), "%s %s%s\n",
			done ? "✓ " : "○ ",
			m_Bot.m_vRequiredMoveAction[i].m_TaskName,
			done ? " [完成]" : "");
		str_append(aBufQuestTask, aTemp, Size);
	}
}

void CQuestStep::CompleteDialog(const char* pNpcId)
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer || m_MarkedForDestroy || m_StepComplete)
		return;

	if(m_Bot.m_vRequiredDialogs.size() == 0)
		return;

	// Check if this step needs a dialog with this NPC
	for(auto& d : m_Bot.m_vRequiredDialogs)
	{
		if(str_comp(d.m_aNpcId, pNpcId) == 0)
		{
			m_aDialogProgress[pNpcId].m_DialogDone = true;
			m_aDialogProgress[pNpcId].m_Complete = true;
			GS()->SendChatTo(pPlayer->GetCID(), "[完成] 与NPC对话!");
			return;
		}
	}
}

int CQuestStep::GetMoveActionCurrentStepPos() const
{
	for(unsigned i = 0; i < m_aMoveActionProgress.size(); i++)
	{
		if(!m_aMoveActionProgress[i])
			return m_Bot.m_vRequiredMoveAction[i].m_Step;
	}
	return -1;
}

void CQuestStep::StartScenario()
{
	if(m_Bot.m_ScenarioJson[0] == '\0')
		return;

	if(!GS()->Core() || !GS()->Core()->QuestManager())
		return;

	CScenarioManager* pScenarioMgr = GS()->Core()->QuestManager()->GetScenarioManager();
	if(!pScenarioMgr)
		return;

	CJsonParser Parser;
	json_value* pRoot = Parser.ParseString(m_Bot.m_ScenarioJson, "scenario");
	if(!pRoot || pRoot->type != json_object)
	{
		json_value_free(pRoot);
		return;
	}

	int ScenarioID = (int)(*pRoot)["id"].u.integer;
	json_value_free(pRoot);

	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return;

	CScenarioInstance* pInstance = pScenarioMgr->CreateInstance(ScenarioID, pPlayer);
	if(pInstance)
		pInstance->Start();
}
