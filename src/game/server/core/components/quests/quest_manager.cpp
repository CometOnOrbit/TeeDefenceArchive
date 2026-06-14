#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CQuestManager::CQuestManager()
{
	m_NumQuests = 0;
	mem_zero(m_aaState, sizeof(m_aaState));
	mem_zero(m_aaaUnlocks, sizeof(m_aaaUnlocks));
	mem_zero(m_aNumUnlocks, sizeof(m_aNumUnlocks));
}

static void ParseQuestStep(const json_value &S, SQuestStepDef &Step)
{
	mem_zero(&Step, sizeof(Step));
	if(S["type"].type == json_string)
		str_copy(Step.m_aType, S["type"].u.string.ptr, sizeof(Step.m_aType));
	if(S["npc"].type == json_string)
		str_copy(Step.m_aNpc, S["npc"].u.string.ptr, sizeof(Step.m_aNpc));
	if(S["world"].type == json_integer)
		Step.m_World = (int)S["world"].u.integer;
	if(S["x"].type == json_integer)
		Step.m_X = (float)S["x"].u.integer;
	if(S["y"].type == json_integer)
		Step.m_Y = (float)S["y"].u.integer;
	if(S["radius"].type == json_integer)
		Step.m_Radius = (float)S["radius"].u.integer;
	else
		Step.m_Radius = 48.f;
	if(S["count"].type == json_integer)
		Step.m_Count = (int)S["count"].u.integer;
	else
		Step.m_Count = 1;
	if(S["item"].type == json_integer)
		Step.m_ItemId = (int)S["item"].u.integer;
}

void CQuestManager::LoadQuests()
{
	m_NumQuests = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/quests.json", Storage());
	if(!pRoot)
	{
		dbg_msg("quest", "quests.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["quests"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumQuests < MAX_QUESTS; i++)
	{
		const json_value &Q = Arr[(int)i];
		if(Q.type != json_object || Q["id"].type != json_string)
			continue;
		SQuestDef &Def = m_aQuests[m_NumQuests++];
		mem_zero(&Def, sizeof(Def));
		str_copy(Def.m_aId, Q["id"].u.string.ptr, sizeof(Def.m_aId));
		if(Q["title_key"].type == json_string)
			str_copy(Def.m_aTitleKey, Q["title_key"].u.string.ptr, sizeof(Def.m_aTitleKey));
		else
			str_format(Def.m_aTitleKey, sizeof(Def.m_aTitleKey), "quest.%s", Def.m_aId);
		Def.m_AutoGrant = Q["auto_grant"].type == json_boolean && Q["auto_grant"].u.boolean != 0;

		const json_value &Steps = Q["steps"];
		if(Steps.type == json_array)
		{
			for(unsigned s = 0; s < Steps.u.array.length && Def.m_NumSteps < MAX_QUEST_STEPS; s++)
			{
				if(Steps[(int)s].type != json_object)
					continue;
				ParseQuestStep(Steps[(int)s], Def.m_aSteps[Def.m_NumSteps++]);
			}
		}

		const json_value &Unlocks = Q["unlocks"];
		if(Unlocks.type == json_array)
		{
			for(unsigned u = 0; u < Unlocks.u.array.length && Def.m_NumUnlocks < MAX_QUEST_UNLOCKS; u++)
			{
				if(Unlocks[(int)u].type != json_string)
					continue;
				str_copy(Def.m_aaUnlocks[Def.m_NumUnlocks++], Unlocks[(int)u].u.string.ptr, sizeof(Def.m_aaUnlocks[0]));
			}
		}

		const json_value &Rewards = Q["rewards"];
		if(Rewards.type == json_object)
		{
			const json_value &Items = Rewards["items"];
			if(Items.type == json_array && Items.u.array.length > 0 && Items[0].type == json_object)
			{
				if(Items[0]["id"].type == json_integer)
					Def.m_RewardItem = (int)Items[0]["id"].u.integer;
				if(Items[0]["num"].type == json_integer)
					Def.m_RewardNum = (int)Items[0]["num"].u.integer;
			}
		}

		if(Q["next_quest"].type == json_string)
			str_copy(Def.m_aNextQuest, Q["next_quest"].u.string.ptr, sizeof(Def.m_aNextQuest));
	}
	dbg_msg("quest", "loaded %d quests", m_NumQuests);
}

void CQuestManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadQuests();
	if(Core())
		Core()->Events().Register(this);
}

void CQuestManager::OnShutdown()
{
	if(Core())
		Core()->Events().Unregister(this);
}

void CQuestManager::OnPlayerKill(CPlayer *pKiller, int ZombId)
{
	(void)ZombId;
	TryKillProgress(pKiller);
}

void CQuestManager::OnClientReset(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	mem_zero(m_aaState[ClientID], sizeof(m_aaState[ClientID]));
	mem_zero(m_aaaUnlocks[ClientID], sizeof(m_aaaUnlocks[ClientID]));
	m_aNumUnlocks[ClientID] = 0;
}

int CQuestManager::FindQuestIndex(const char *pId) const
{
	if(!pId)
		return -1;
	for(int i = 0; i < m_NumQuests; i++)
	{
		if(str_comp(m_aQuests[i].m_aId, pId) == 0)
			return i;
	}
	return -1;
}

bool CQuestManager::HasUnlock(int ClientID, const char *pKey) const
{
	if(!pKey || !pKey[0] || ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	for(int i = 0; i < m_aNumUnlocks[ClientID]; i++)
	{
		if(str_comp(m_aaaUnlocks[ClientID][i], pKey) == 0)
			return true;
	}
	return false;
}

bool CQuestManager::HasTravelUnlock(CPlayer *pPlayer, const char *pKey) const
{
	if(!pPlayer || !pKey || !pKey[0])
		return false;
	const int CID = pPlayer->GetCID();
	if(HasUnlock(CID, pKey))
		return true;
	return IsQuestCompleted(pPlayer, pKey);
}

bool CQuestManager::IsQuestCompleted(CPlayer *pPlayer, const char *pQuestId) const
{
	if(!pPlayer || !pQuestId)
		return false;
	const int Idx = FindQuestIndex(pQuestId);
	if(Idx < 0)
		return false;
	return m_aaState[pPlayer->GetCID()][Idx].m_Completed;
}

void CQuestManager::AddUnlock(CPlayer *pPlayer, const char *pKey)
{
	if(!pPlayer || !pKey || !pKey[0])
		return;
	const int CID = pPlayer->GetCID();
	if(HasUnlock(CID, pKey))
		return;
	if(m_aNumUnlocks[CID] >= MAX_PLAYER_UNLOCKS)
		return;
	str_copy(m_aaaUnlocks[CID][m_aNumUnlocks[CID]++], pKey, sizeof(m_aaaUnlocks[CID][0]));
}

void CQuestManager::ActivateQuest(CPlayer *pPlayer, const char *pQuestId)
{
	if(!pPlayer || !pQuestId)
		return;
	const int Idx = FindQuestIndex(pQuestId);
	if(Idx < 0)
		return;
	const int CID = pPlayer->GetCID();
	if(m_aaState[CID][Idx].m_Active || m_aaState[CID][Idx].m_Completed)
		return;
	m_aaState[CID][Idx].m_Active = true;
	m_aaState[CID][Idx].m_Step = 0;
	m_aaState[CID][Idx].m_SubProgress = 0;
	GS()->SendChatLocF(CID, "quest.accepted", u8"新任务：%s", GS()->Loc(CID, m_aQuests[Idx].m_aTitleKey, m_aQuests[Idx].m_aId));
}

void CQuestManager::GrantAutoQuests(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	const int CID = pPlayer->GetCID();
	for(int i = 0; i < m_NumQuests; i++)
	{
		if(!m_aQuests[i].m_AutoGrant)
			continue;
		if(m_aaState[CID][i].m_Active || m_aaState[CID][i].m_Completed)
			continue;
		m_aaState[CID][i].m_Active = true;
		m_aaState[CID][i].m_Step = 0;
	}
}

void CQuestManager::OnPlayerLogin(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->IsDummy())
		return;
	if(GS() && GS()->Accounts())
	{
		char aBuf[4096];
		if(GS()->Accounts()->GetQuestData(pPlayer->GetCID(), aBuf, sizeof(aBuf)) && aBuf[0])
			LoadPlayerData(pPlayer->GetCID(), aBuf);
	}
	GrantAutoQuests(pPlayer);
	TryCollectProgress(pPlayer);
}

void CQuestManager::OnTick()
{
	if(!GS())
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || pP->IsDummy() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;
		TryReachProgress(pP, pP->GetCharacter()->GetPos());
		TryCollectProgress(pP);
	}
}

void CQuestManager::OnCharacterSpawn(CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->IsDummy())
		return;
	GrantAutoQuests(pPlayer);
	TryCollectProgress(pPlayer);
}

void CQuestManager::RequestPersist(int ClientID)
{
	if(!GS() || !GS()->Accounts() || !GS()->Accounts()->IsEnabled())
		return;
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() < 0)
		return;
	char aBuf[4096];
	SavePlayerData(ClientID, aBuf, sizeof(aBuf));
	GS()->Accounts()->SetQuestData(ClientID, aBuf);
	GS()->Accounts()->RequestSaveQuestData(ClientID);
}

void CQuestManager::LoadPlayerData(int ClientID, const char *pJson)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pJson)
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pJson, "quest_save");
	if(!pRoot || pRoot->type != json_object)
		return;

	const json_value &Completed = (*pRoot)["completed"];
	if(Completed.type == json_array)
	{
		for(unsigned i = 0; i < Completed.u.array.length; i++)
		{
			if(Completed[(int)i].type != json_string)
				continue;
			const int Idx = FindQuestIndex(Completed[(int)i].u.string.ptr);
			if(Idx >= 0)
			{
				m_aaState[ClientID][Idx].m_Completed = true;
				m_aaState[ClientID][Idx].m_Active = false;
			}
		}
	}

	const json_value &Progress = (*pRoot)["progress"];
	if(Progress.type == json_object)
	{
		for(unsigned i = 0; i < Progress.u.object.length; i++)
		{
			const char *pName = Progress.u.object.values[i].name;
			const json_value *pVal = Progress.u.object.values[i].value;
			const int Idx = FindQuestIndex(pName);
			if(Idx < 0 || !pVal || pVal->type != json_integer)
				continue;
			m_aaState[ClientID][Idx].m_Active = true;
			m_aaState[ClientID][Idx].m_Step = (int)pVal->u.integer;
			m_aaState[ClientID][Idx].m_Completed = false;
		}
	}

	const json_value &Unlocks = (*pRoot)["unlocks"];
	if(Unlocks.type == json_array)
	{
		m_aNumUnlocks[ClientID] = 0;
		for(unsigned i = 0; i < Unlocks.u.array.length && m_aNumUnlocks[ClientID] < MAX_PLAYER_UNLOCKS; i++)
		{
			if(Unlocks[(int)i].type != json_string)
				continue;
			str_copy(m_aaaUnlocks[ClientID][m_aNumUnlocks[ClientID]++], Unlocks[(int)i].u.string.ptr, sizeof(m_aaaUnlocks[ClientID][0]));
		}
	}
}

void CQuestManager::SavePlayerData(int ClientID, char *pOut, int OutSize) const
{
	if(!pOut || OutSize <= 0)
		return;
	pOut[0] = 0;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	char aBody[3800];
	char aCompleted[1024];
	char aProgress[1024];
	char aUnlocks[1024];
	aCompleted[0] = 0;
	aProgress[0] = 0;
	aUnlocks[0] = 0;

	bool First = true;
	for(int i = 0; i < m_NumQuests; i++)
	{
		if(!m_aaState[ClientID][i].m_Completed)
			continue;
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s\"%s\"", First ? "" : ",", m_aQuests[i].m_aId);
		str_append(aCompleted, aPart, sizeof(aCompleted));
		First = false;
	}

	First = true;
	for(int i = 0; i < m_NumQuests; i++)
	{
		if(!m_aaState[ClientID][i].m_Active || m_aaState[ClientID][i].m_Completed)
			continue;
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s\"%s\":%d", First ? "" : ",", m_aQuests[i].m_aId, m_aaState[ClientID][i].m_Step);
		str_append(aProgress, aPart, sizeof(aProgress));
		First = false;
	}

	First = true;
	for(int i = 0; i < m_aNumUnlocks[ClientID]; i++)
	{
		char aPart[64];
		str_format(aPart, sizeof(aPart), "%s\"%s\"", First ? "" : ",", m_aaaUnlocks[ClientID][i]);
		str_append(aUnlocks, aPart, sizeof(aUnlocks));
		First = false;
	}

	str_format(aBody, sizeof(aBody), "{\"completed\":[%s],\"progress\":{%s},\"unlocks\":[%s]}", aCompleted, aProgress, aUnlocks);
	str_copy(pOut, aBody, OutSize);
}

bool CQuestManager::StepMatches(const SQuestStepDef &Step, CPlayer *pPlayer, vec2 Pos, const char *pNpcId, bool Talk, bool Kill, bool Collect) const
{
	if(!pPlayer || !Server())
		return false;
	const int World = Server()->GetClientWorldID(pPlayer->GetCID());

	if(str_comp(Step.m_aType, "talk_npc") == 0)
	{
		if(!Talk || !pNpcId || str_comp(Step.m_aNpc, pNpcId) != 0)
			return false;
		return true;
	}
	if(str_comp(Step.m_aType, "reach_zone") == 0)
	{
		if(Step.m_World >= 0 && Step.m_World != World)
			return false;
		return distance(Pos, vec2(Step.m_X, Step.m_Y)) <= Step.m_Radius;
	}
	if(str_comp(Step.m_aType, "kill_enemy") == 0)
	{
		(void)Pos;
		(void)pNpcId;
		return Kill;
	}
	if(str_comp(Step.m_aType, "collect_item") == 0)
	{
		(void)Pos;
		(void)pNpcId;
		if(!Collect)
			return false;
		return pPlayer->m_AccData.m_aItems[Step.m_ItemId].m_Num >= Step.m_Count;
	}
	return false;
}

void CQuestManager::AdvanceStep(CPlayer *pPlayer, int QuestIdx)
{
	if(!pPlayer || QuestIdx < 0 || QuestIdx >= m_NumQuests)
		return;
	const int CID = pPlayer->GetCID();
	SPlayerQuestState &St = m_aaState[CID][QuestIdx];
	if(!St.m_Active || St.m_Completed)
		return;

	const SQuestDef &Def = m_aQuests[QuestIdx];
	const SQuestStepDef &CurStep = Def.m_aSteps[St.m_Step];
	if(str_comp(CurStep.m_aType, "kill_enemy") == 0 && CurStep.m_Count > 1)
	{
		St.m_SubProgress++;
		if(St.m_SubProgress < CurStep.m_Count)
		{
			GS()->SendChatLocF(CID, "quest.kill_progress", u8"击杀进度：%d/%d", St.m_SubProgress, CurStep.m_Count);
			RequestPersist(CID);
			return;
		}
		St.m_SubProgress = 0;
	}

	St.m_Step++;
	if(St.m_Step >= Def.m_NumSteps)
		CompleteQuest(pPlayer, QuestIdx);
	else
	{
		GS()->SendChatLocF(CID, "quest.step", u8"任务进度更新：%s (%d/%d)",
			GS()->Loc(CID, Def.m_aTitleKey, Def.m_aId), St.m_Step, Def.m_NumSteps);
		RequestPersist(CID);
	}
}

void CQuestManager::CompleteQuest(CPlayer *pPlayer, int QuestIdx)
{
	if(!pPlayer || QuestIdx < 0 || QuestIdx >= m_NumQuests)
		return;
	const int CID = pPlayer->GetCID();
	SPlayerQuestState &St = m_aaState[CID][QuestIdx];
	const SQuestDef &Def = m_aQuests[QuestIdx];

	St.m_Completed = true;
	St.m_Active = false;

	for(int i = 0; i < Def.m_NumUnlocks; i++)
		AddUnlock(pPlayer, Def.m_aaUnlocks[i]);

	if(Def.m_RewardItem >= 0 && Def.m_RewardNum > 0)
		pPlayer->m_AccData.m_aItems[Def.m_RewardItem].m_Num += Def.m_RewardNum;

	GS()->SendChatLocF(CID, "quest.complete", u8"任务完成：%s", GS()->Loc(CID, Def.m_aTitleKey, Def.m_aId));

	if(Def.m_aNextQuest[0])
		ActivateQuest(pPlayer, Def.m_aNextQuest);

	RequestPersist(CID);

	if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
		GS()->Accounts()->RequestSaveItems(CID);
}

void CQuestManager::TryReachProgress(CPlayer *pPlayer, vec2 Pos)
{
	if(!pPlayer)
		return;
	const int CID = pPlayer->GetCID();
	for(int i = 0; i < m_NumQuests; i++)
	{
		SPlayerQuestState &St = m_aaState[CID][i];
		if(!St.m_Active || St.m_Completed || St.m_Step >= m_aQuests[i].m_NumSteps)
			continue;
		if(StepMatches(m_aQuests[i].m_aSteps[St.m_Step], pPlayer, Pos, nullptr, false, false, false))
			AdvanceStep(pPlayer, i);
	}
}

void CQuestManager::TryKillProgress(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	const int CID = pPlayer->GetCID();
	for(int i = 0; i < m_NumQuests; i++)
	{
		SPlayerQuestState &St = m_aaState[CID][i];
		if(!St.m_Active || St.m_Completed || St.m_Step >= m_aQuests[i].m_NumSteps)
			continue;
		const SQuestStepDef &Step = m_aQuests[i].m_aSteps[St.m_Step];
		if(str_comp(Step.m_aType, "kill_enemy") != 0)
			continue;
		if(StepMatches(Step, pPlayer, vec2(0, 0), nullptr, false, true, false))
			AdvanceStep(pPlayer, i);
	}
}

void CQuestManager::TryCollectProgress(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	const int CID = pPlayer->GetCID();
	for(int i = 0; i < m_NumQuests; i++)
	{
		SPlayerQuestState &St = m_aaState[CID][i];
		if(!St.m_Active || St.m_Completed || St.m_Step >= m_aQuests[i].m_NumSteps)
			continue;
		if(StepMatches(m_aQuests[i].m_aSteps[St.m_Step], pPlayer, vec2(0, 0), nullptr, false, false, true))
			AdvanceStep(pPlayer, i);
	}
}

bool CQuestManager::TryTalkNpc(CPlayer *pPlayer, const char *pNpcId)
{
	if(!pPlayer || !pNpcId)
		return false;

	char aDlgKey[64];
	str_format(aDlgKey, sizeof(aDlgKey), "npc.%s.talk", pNpcId);
	GS()->SendChatLoc(pPlayer->GetCID(), aDlgKey, pNpcId);

	const int CID = pPlayer->GetCID();
	bool Progressed = false;
	for(int i = 0; i < m_NumQuests; i++)
	{
		SPlayerQuestState &St = m_aaState[CID][i];
		if(!St.m_Active || St.m_Completed || St.m_Step >= m_aQuests[i].m_NumSteps)
			continue;
		if(StepMatches(m_aQuests[i].m_aSteps[St.m_Step], pPlayer, vec2(0, 0), pNpcId, true, false, false))
		{
			AdvanceStep(pPlayer, i);
			Progressed = true;
		}
	}
	return Progressed;
}

void CQuestManager::BuildQuestListPage(int ClientID)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return;

	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	pVote->SetVoteBuildClientID(ClientID);
	pVote->AddVote_PageHeader(GS()->Loc(ClientID, "quest.menu.title", u8"任务"));
	pVote->AddVote_Separator();

	for(int i = 0; i < m_NumQuests; i++)
	{
		const SQuestDef &Def = m_aQuests[i];
		const SPlayerQuestState &St = m_aaState[ClientID][i];
		char aLine[VOTE_DESC_LENGTH];
		const char *pTitle = GS()->Loc(ClientID, Def.m_aTitleKey, Def.m_aId);
		if(St.m_Completed)
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "quest.entry.done", u8"✓ %s"), pTitle);
		else if(St.m_Active)
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "quest.entry.active", u8"▹ %s (%d/%d)"), pTitle, St.m_Step, Def.m_NumSteps);
		else
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "quest.entry", u8"▹ %s"), pTitle);
		char aCmd[48];
		str_format(aCmd, sizeof(aCmd), "ccv_menuquestsel %d", i);
		pVote->AddVote(aLine, aCmd, ClientID);
	}
	pVote->AddVote_PageFooter();
}

void CQuestManager::BuildQuestDetailPage(int ClientID, int QuestIdx)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager() || QuestIdx < 0 || QuestIdx >= m_NumQuests)
		return;

	const SQuestDef &Def = m_aQuests[QuestIdx];
	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	char aDescKey[64];
	str_format(aDescKey, sizeof(aDescKey), "%s.desc", Def.m_aTitleKey);

	pVote->SetVoteBuildClientID(ClientID);
	pVote->AddVote_PageHeader(GS()->Loc(ClientID, Def.m_aTitleKey, Def.m_aId));
	pVote->AddVote_PageSubtitle(GS()->Loc(ClientID, aDescKey, Def.m_aId));
	pVote->AddVote_Separator();

	if(!m_aaState[ClientID][QuestIdx].m_Completed && m_aaState[ClientID][QuestIdx].m_Step < Def.m_NumSteps)
	{
		const SQuestStepDef &Step = Def.m_aSteps[m_aaState[ClientID][QuestIdx].m_Step];
		char aStepKey[64];
		str_format(aStepKey, sizeof(aStepKey), "quest.step.%s", Step.m_aType);
		pVote->AddVote_Section(GS()->Loc(ClientID, "quest.current_step", u8"当前步骤"));
		pVote->AddVote_TextLine(GS()->Loc(ClientID, aStepKey, Step.m_aType));
		if(Step.m_Count > 1)
			pVote->AddVote_ProgressLine(m_aaState[ClientID][QuestIdx].m_SubProgress, Step.m_Count);
	}

	pVote->AddVote_PageFooter();
}

static void ComChatTalk(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->QuestManager())
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || !pP->GetCharacter())
		return;

	const char *pNpcId = pResult->NumArguments() >= 1 ? pResult->GetString(0) : nullptr;
	if(!pNpcId)
	{
		if(pGame->Core()->NpcManager())
		{
			if(const SNpcDef *pNear = pGame->Core()->NpcManager()->FindNpcNear(pP, pP->GetCharacter()->GetPos()))
				pNpcId = pNear->m_aId;
		}
	}
	if(!pNpcId)
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "npc.no_one_near", u8"附近没有可交谈的对象。");
		return;
	}
	pGame->Core()->QuestManager()->TryTalkNpc(pP, pNpcId);
}

static void ComVoteQuestSel(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = static_cast<CCommandManager::SCommandContext *>(pUser);
	CGameContext *pGame = static_cast<CGameContext *>(pCtx->m_pContext);
	if(!pGame || !pGame->Core() || !pGame->Core()->VoteMenuManager())
		return;
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_QuestIdx = pResult->GetInteger(0);
	pV->m_Page = PAGE_QUEST_DETAIL;
	pV->m_LastPage = PAGE_QUESTS;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

void CQuestManager::RegisterChatCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("talk", "cmd.talk.help", "r", ComChatTalk, GS());
}

void CQuestManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("menuquestsel", "", "i", ComVoteQuestSel, GS());
}
