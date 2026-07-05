#include "quest_data.h"
#include "quest_manager.h"
#include <generated/server_data.h>
#include <game/server/gamecontext.h>
#include <game/server/entity_manager.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/entities/character.h>

array<CQuestDescription*> CQuestDescription::ms_aData;
hash_table<QuestIdentifier, CPlayerQuest*, 16> CPlayerQuest::ms_aPlayerQuests[MAX_CLIENTS];

void CQuestDescription::CReward::ApplyReward(CGameContext* pGameServer, CPlayer* pPlayer) const
{
	if(!pPlayer)
		return;

	if(m_Gold > 0)
	{
		pPlayer->SetStat(AttributeIdentifier::Gold, pPlayer->GetStat(AttributeIdentifier::Gold) + m_Gold);
		pPlayer->m_MMODirty = true;
	}

	if(m_Experience > 0)
	{
		pPlayer->AddMMOExperience(m_Experience);
	}

	if(m_Reputation > 0)
	{
		pPlayer->SetStat(AttributeIdentifier::Reputation, pPlayer->GetStat(AttributeIdentifier::Reputation) + m_Reputation);
		pPlayer->m_MMODirty = true;
	}

	// Give item rewards
	for(auto& ri : m_RewardItems)
	{
		if(ri.m_ItemID > 0 && ri.m_Count > 0)
		{
			pPlayer->m_MMOInventory.Add(ri.m_ItemID, ri.m_Count, 0);
			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "获得物品: %d x%d", ri.m_ItemID, ri.m_Count);
			pGameServer->SendChatTo(pPlayer->GetCID(), aBuf);
			pPlayer->m_MMODirty = true;
		}
	}
}

int CQuestDescription::GetChainLength() const
{
	int Value = 1;

	CQuestDescription* pPrevious = GetPreviousQuest();
	while(pPrevious)
	{
		pPrevious = pPrevious->GetPreviousQuest();
		Value++;
	}

	CQuestDescription* pNext = GetNextQuest();
	while(pNext)
	{
		pNext = pNext->GetNextQuest();
		Value++;
	}

	return Value;
}

int CQuestDescription::GetCurrentChainPos() const
{
	int Value = 1;

	CQuestDescription* pPrevious = GetPreviousQuest();
	while(pPrevious)
	{
		pPrevious = pPrevious->GetPreviousQuest();
		Value++;
	}

	return Value;
}

CQuestDescription* CQuestDescription::GetNextQuest() const
{
	if(m_NextQuestID)
		return Find(*m_NextQuestID);
	return nullptr;
}

CQuestDescription* CQuestDescription::GetPreviousQuest() const
{
	if(m_PreviousQuestID)
		return Find(*m_PreviousQuestID);
	return nullptr;
}

bool CQuestDescription::HasObjectives(int Step)
{
	return m_vObjectives.count(Step) && !m_vObjectives[Step].empty();
}

void CQuestDescription::PreparePlayerObjectives(class CGameContext* pGameServer, int Step, int ClientID, std::deque<CQuestStep*>& pElem)
{
	ResetPlayerObjectives(pElem);
	for(const auto& StepDesc : m_vObjectives[Step])
		pElem.emplace_back(new CQuestStep(pGameServer, ClientID, StepDesc.m_Bot));
}

void CQuestDescription::ResetPlayerObjectives(std::deque<CQuestStep*>& pElem)
{
	for(auto* pPtr : pElem)
	{
		pPtr->MarkForDestroy();
		pPtr->Update();
		delete pPtr;
	}
	pElem.clear();
}

CQuestDescription* CQuestDescription::Find(int ID)
{
	for(CQuestDescription* pDesc : ms_aData)
	{
		if(pDesc && pDesc->GetID() == ID)
			return pDesc;
	}
	return nullptr;
}

CPlayer* CPlayerQuest::GetPlayer() const
{
	return GS()->m_apPlayers[m_ClientID];
}

CQuestDescription* CPlayerQuest::Info() const
{
	return CQuestDescription::Find(m_ID);
}

CPlayerQuest::~CPlayerQuest()
{
	// Despawn quest mobs for this quest before cleanup
	CPlayer* pPlayer = GetPlayer();
	if(pPlayer && GS()->Core() && GS()->Core()->GetMMOManager())
		GS()->Core()->GetMMOManager()->DespawnQuestMobs(m_ID, m_ClientID);

	Info()->ResetPlayerObjectives(m_vObjectives);
	ms_aPlayerQuests[m_ClientID].remove(m_ID);
}

bool CPlayerQuest::HasUnfinishedObjectives() const
{
	for(CQuestStep* pPtr : m_vObjectives)
	{
		if(!pPtr->m_StepComplete && pPtr->m_Bot.HasAction())
			return true;
	}
	return false;
}

bool CPlayerQuest::Accept(int StartStep)
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return false;

	if(m_State != QuestState_NoAccepted)
		return false;

	SetNewState(QuestState_Accepted);
	m_Step = std::max(1, StartStep);
	m_Datafile.Create();

	if(Info()->HasFlag(QUEST_FLAG_TUTORIAL))
		GS()->SendChatLocF(m_ClientID, "quest.accept_tutorial", "教程任务: '%s' 已接受!", Info()->GetName());
	else if(Info()->HasFlag(QUEST_FLAG_TYPE_MAIN))
		GS()->SendChatLocF(m_ClientID, "quest.accept_main", "主线任务: '%s' 已接受!", Info()->GetName());
	else if(Info()->HasFlag(QUEST_FLAG_TYPE_DAILY))
		GS()->SendChatLocF(m_ClientID, "quest.accept_daily", "每日任务: '%s' 已接受!", Info()->GetName());
	else if(Info()->HasFlag(QUEST_FLAG_TYPE_WEEKLY))
		GS()->SendChatLocF(m_ClientID, "quest.accept_weekly", "每周任务: '%s' 已接受!", Info()->GetName());
	else if(Info()->HasFlag(QUEST_FLAG_TYPE_REPEATABLE))
		GS()->SendChatLocF(m_ClientID, "quest.accept_repeatable", "重复任务: '%s' 已接受!", Info()->GetName());
	else
		GS()->SendChatLocF(m_ClientID, "quest.accept_side", "支线任务: '%s' 已接受!", Info()->GetName());

	// MRPG-style effects
	GS()->Broadcast(m_ClientID, CGameContext::BROADCAST_PRIORITY_TITLE, 100, "任务已接受!");
	if(pPlayer->GetCharacter())
		GS()->m_World.CreateSound(pPlayer->GetCharacter()->GetPos(), SOUND_GAME_ACCEPT, CmaskOne(m_ClientID));

	return true;
}

bool CPlayerQuest::Restart(int StartStep)
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return false;

	if(m_State != QuestState_NoAccepted)
		Reset();

	return Accept(StartStep);
}

void CPlayerQuest::Refuse()
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return;

	if(m_State != QuestState_Accepted)
		return;

	if(Info()->HasFlag(QUEST_FLAG_CANT_REFUSE))
	{
		GS()->SendChatLoc(m_ClientID, "quest.cant_refuse", "此任务无法拒绝!");
		return;
	}

	Reset();
	GS()->SendChatLocF(m_ClientID, "quest.refused", "你拒绝了任务 '%s'.", Info()->GetName());
}

void CPlayerQuest::Reset()
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return;

	// Despawn quest mobs for this quest
	if(GS()->Core() && GS()->Core()->GetMMOManager())
		GS()->Core()->GetMMOManager()->DespawnQuestMobs(m_ID, m_ClientID);

	Info()->ResetPlayerObjectives(m_vObjectives);
	m_Step = 1;
	SetNewState(QuestState_NoAccepted);
	m_Datafile.Delete();
}

void CPlayerQuest::UpdateStepProgress()
{
	CPlayer* pPlayer = GetPlayer();
	if(!pPlayer)
		return;

	if(HasUnfinishedObjectives())
		return;

	// Despawn quest mobs for this quest before transitioning to next step
	if(GS()->Core() && GS()->Core()->GetMMOManager())
		GS()->Core()->GetMMOManager()->DespawnQuestMobs(m_ID, m_ClientID);

	Info()->ResetPlayerObjectives(m_vObjectives);
	m_Step++;
	if(Info()->HasObjectives(m_Step))
	{
		m_Datafile.Create();
		return;
	}

	Info()->Reward().ApplyReward(GS(), pPlayer);

	GS()->SendChatLocF(m_ClientID, "quest.completed", "任务完成: '%s'!", Info()->GetName());

	// MRPG-style effects
	GS()->Broadcast(m_ClientID, CGameContext::BROADCAST_PRIORITY_TITLE, 100, "任务完成!");
	if(pPlayer && pPlayer->GetCharacter())
		GS()->m_World.CreateSound(pPlayer->GetCharacter()->GetPos(), SOUND_GAME_DONE, CmaskOne(m_ClientID));

	if(Info()->HasFlag(QUEST_FLAG_TYPE_REPEATABLE))
	{
		GS()->SendChatLoc(-1, "quest.completed_repeatable", "{name} 完成了重复任务 \"{quest}\".");
		Reset();
		return;
	}

	if(Info()->HasFlag(QUEST_FLAG_TUTORIAL))
		GS()->SendChatLoc(-1, "quest.completed_tutorial", "{name} 完成了教程任务 \"{quest}\".");
	else if(Info()->HasFlag(QUEST_FLAG_TYPE_DAILY))
		GS()->SendChatLoc(-1, "quest.completed_daily", "{name} 完成了每日任务 \"{quest}\".");
	else if(Info()->HasFlag(QUEST_FLAG_TYPE_WEEKLY))
		GS()->SendChatLoc(-1, "quest.completed_weekly", "{name} 完成了每周任务 \"{quest}\".");
	else if(Info()->HasFlag(QUEST_FLAG_TYPE_MAIN))
		GS()->SendChatLoc(-1, "quest.completed_main", "{name} 完成了主线任务 \"{quest}\".");
	else
		GS()->SendChatLoc(-1, "quest.completed_side", "{name} 完成了支线任务 \"{quest}\".");

	SetNewState(QuestState_Finished);
	m_Datafile.Delete();

	if(GS()->Core() && GS()->Core()->QuestManager())
		GS()->Core()->QuestManager()->TryAcceptNextQuestChain(pPlayer, m_ID);
}

void CPlayerQuest::Update()
{
	for(CQuestStep* pStep : m_vObjectives)
		pStep->Update();

	UpdateStepProgress();
}

CQuestStep* CPlayerQuest::GetStepByMob(int MobID)
{
	for(CQuestStep* pPtr : m_vObjectives)
	{
		if(pPtr->m_Bot.m_ID == MobID)
			return pPtr;
	}
	return nullptr;
}

void CPlayerQuest::SkipObjectivesAndFinish()
{
	while(!IsCompleted())
	{
		for(CQuestStep* pObjective : m_vObjectives)
			pObjective->Finish(true);
	}
}

void CPlayerQuest::SetNewState(QuestState State)
{
	if(m_State != State)
		m_State = State;
}