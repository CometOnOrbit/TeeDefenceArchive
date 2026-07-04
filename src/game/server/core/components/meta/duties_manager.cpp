#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/meta/duties_manager.h>
#include <game/server/core/components/meta/meta_manager.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entity_manager.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CDutiesManager::CDutiesManager()
{
	m_NumDefs = 0;
	mem_zero(m_aDefs, sizeof(m_aDefs));
}

void CDutiesManager::LoadDefs()
{
	m_NumDefs = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/duties.json", Storage());
	if(!pRoot)
	{
		dbg_msg("duty", "duties.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["duties"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumDefs < MAX_META_DUTIES; i++)
	{
		const json_value &D = Arr[(int)i];
		if(D.type != json_object)
			continue;
		SDutyDef &Def = m_aDefs[m_NumDefs++];
		mem_zero(&Def, sizeof(Def));
		if(D["id"].type == json_string)
			str_copy(Def.m_aId, D["id"].u.string.ptr, sizeof(Def.m_aId));
		if(D["title"].type == json_string)
			str_copy(Def.m_aTitle, D["title"].u.string.ptr, sizeof(Def.m_aTitle));
		if(D["desc"].type == json_string)
			str_copy(Def.m_aDesc, D["desc"].u.string.ptr, sizeof(Def.m_aDesc));
		if(D["type"].type == json_string)
			str_copy(Def.m_aType, D["type"].u.string.ptr, sizeof(Def.m_aType));
		if(D["count"].type == json_integer)
			Def.m_Count = (int)D["count"].u.integer;
		else
			Def.m_Count = 1;
		if(D["reward_item"].type == json_integer)
			Def.m_RewardItem = (int)D["reward_item"].u.integer;
		if(D["reward_num"].type == json_integer)
			Def.m_RewardNum = (int)D["reward_num"].u.integer;
		else
			Def.m_RewardNum = 1;
		if(D["tier"].type == json_integer)
			Def.m_Tier = (int)D["tier"].u.integer;
		else
			Def.m_Tier = 0;
	}
	dbg_msg("duty", "loaded %d duties", m_NumDefs);
}

void CDutiesManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadDefs();
	if(Core())
		Core()->Events().Register(this);
}

void CDutiesManager::OnShutdown()
{
	if(Core())
		Core()->Events().Unregister(this);
}

void CDutiesManager::RequestPersist(int ClientID)
{
	if(Core() && Core()->MetaManager())
		Core()->MetaManager()->Persist(ClientID);
}

void CDutiesManager::OnPlayerLogin(CPlayer *pPlayer)
{
	(void)pPlayer;
}

void CDutiesManager::OnClientReset(int ClientID)
{
	(void)ClientID;
}

static const char *DutyTitle(CGameContext *pGS, int ClientID, const SDutyDef &Def)
{
	char aKey[64];
	str_format(aKey, sizeof(aKey), "duty.%s.title", Def.m_aId);
	return pGS->Loc(ClientID, aKey, Def.m_aTitle);
}

bool CDutiesManager::TryClaim(CPlayer *pPlayer, int Idx)
{
	if(!pPlayer || !Core() || !Core()->MetaManager() || Idx < 0 || Idx >= m_NumDefs || Idx >= MAX_META_DUTIES)
		return false;
	const int CID = pPlayer->GetCID();
	SPlayerMetaData &Meta = Core()->MetaManager()->Get(CID);
	PlayerMeta_EnsureDay(&Meta);
	if(Meta.m_aDutyClaimed[Idx])
		return false;
	if(Meta.m_aDutyProgress[Idx] < m_aDefs[Idx].m_Count)
		return false;

	Meta.m_aDutyClaimed[Idx] = true;
	const SDutyDef &Def = m_aDefs[Idx];
	if(Def.m_RewardItem >= 0 && Def.m_RewardNum > 0 && GS()->ItemHelper() && GS()->ItemHelper()->CheckItemValid(Def.m_RewardItem))
	{
		vec2 Pos = pPlayer->m_ViewPos;
		if(CCharacter *pChr = pPlayer->GetCharacter())
			Pos = pChr->GetPos();
		if(Core()->EntityManager())
			Core()->EntityManager()->DropItem(Pos, CID, Def.m_RewardItem, Def.m_RewardNum);
	}
	char aBuf[128];
	GS()->LocFormat(aBuf, sizeof(aBuf), CID, "duty.reward", "日常奖励：%s", DutyTitle(GS(), CID, Def));
	GS()->SendChatTo(CID, aBuf);
	RequestPersist(CID);
	return true;
}

void CDutiesManager::BumpProgress(CPlayer *pPlayer, const char *pType, int Amount)
{
	if(!pPlayer || pPlayer->IsDummy() || !pType || Amount <= 0 || !Core() || !Core()->MetaManager())
		return;
	const int CID = pPlayer->GetCID();
	SPlayerMetaData &Meta = Core()->MetaManager()->Get(CID);
	PlayerMeta_EnsureDay(&Meta);
	bool Changed = false;
	for(int i = 0; i < m_NumDefs; i++)
	{
		if(Meta.m_aDutyClaimed[i])
			continue;
		if(str_comp(m_aDefs[i].m_aType, pType) != 0)
			continue;
		if(Meta.m_aDutyProgress[i] >= m_aDefs[i].m_Count)
			continue;
		Meta.m_aDutyProgress[i] = minimum(m_aDefs[i].m_Count, Meta.m_aDutyProgress[i] + Amount);
		Changed = true;
	}
	if(Changed)
		RequestPersist(CID);
}

void CDutiesManager::OnPlayerKill(CPlayer *pKiller, int ZombId)
{
	(void)ZombId;
	BumpProgress(pKiller, "kill", 1);
}

void CDutiesManager::OnPlayerCraft(CPlayer *pPlayer, int ItemId, int Amount)
{
	(void)ItemId;
	BumpProgress(pPlayer, "craft", maximum(1, Amount));
}

void CDutiesManager::OnWaveComplete(int Wave)
{
	(void)Wave;
	if(!GS())
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || pP->IsDummy() || pP->GetAccountId() < 0)
			continue;
		BumpProgress(pP, "wave", 1);
	}
}

void CDutiesManager::OnPlayerMine(CPlayer *pPlayer, int MatId)
{
	(void)MatId;
	BumpProgress(pPlayer, "mine", 1);
}

void CDutiesManager::BuildDutiesPage(int ClientID)
{
	if(!Core() || !Core()->VoteMenuManager() || !Core()->MetaManager())
		return;
	SPlayerMetaData &Meta = Core()->MetaManager()->Get(ClientID);
	PlayerMeta_EnsureDay(&Meta);
	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	pVote->SetVoteBuildClientID(ClientID);
	pVote->AddVote_PageHeader(GS()->Loc(ClientID, "duty.title", "日常任务"));
	if(Meta.m_aDay[0])
		pVote->AddVote_PageSubtitle(Meta.m_aDay);
	if(Core()->MiniEventsManager())
	{
		const char *pEv = Core()->MiniEventsManager()->ActiveTitle();
		if(pEv && pEv[0])
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "duty.mini_event", "★ 限时活动：%s"), pEv);
			pVote->AddVote_TextLine(aLine);
		}
	}
	pVote->AddVote_Separator();
	int Done = 0;
	for(int i = 0; i < m_NumDefs; i++)
		if(Meta.m_aDutyClaimed[i])
			Done++;
	{
		char aSum[VOTE_DESC_LENGTH];
		str_format(aSum, sizeof(aSum), GS()->Loc(ClientID, "duty.progress", "今日进度 %d/%d"), Done, m_NumDefs);
		pVote->AddVote_PageSubtitle(aSum);
	}
	pVote->AddVote_Separator();

	static const char *const s_apTierKeys[4] = {
		"duty.tier.basic", "duty.tier.medium", "duty.tier.advanced", "duty.tier.expert"
	};
	static const char *const s_apTierFallback[4] = {
		"基础", "中等", "高级", "进阶"
	};

	for(int tier = 0; tier < 4; tier++)
	{
		bool AnyInTier = false;
		for(int i = 0; i < m_NumDefs; i++)
		{
			if(m_aDefs[i].m_Tier == tier)
			{
				AnyInTier = true;
				break;
			}
		}
		if(!AnyInTier)
			continue;

		pVote->AddVote_Section(GS()->Loc(ClientID, s_apTierKeys[tier], s_apTierFallback[tier]));
		for(int i = 0; i < m_NumDefs; i++)
		{
			if(m_aDefs[i].m_Tier != tier)
				continue;

			char aLine[VOTE_DESC_LENGTH];
			char aCmd[VOTE_CMD_LENGTH];
			const SDutyDef &Def = m_aDefs[i];
			const char *pTitle = DutyTitle(GS(), ClientID, Def);
			if(Meta.m_aDutyClaimed[i])
				str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "duty.entry.done", "✓ %s"), pTitle);
			else if(Meta.m_aDutyProgress[i] >= Def.m_Count)
				str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "duty.entry.claim", "★ 领取 · %s"), pTitle);
			else
				str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "duty.entry", "▹ %s"), pTitle);

			if(!Meta.m_aDutyClaimed[i] && Meta.m_aDutyProgress[i] >= Def.m_Count)
			{
				str_format(aCmd, sizeof(aCmd), "ccv_dutyclaim %d", i);
				pVote->AddVote(aLine, aCmd, ClientID);
			}
			else
				pVote->AddVote_TextLine(aLine);

			if(!Meta.m_aDutyClaimed[i])
				pVote->AddVote_ProgressLine(Meta.m_aDutyProgress[i], Def.m_Count);
		}
	}
	pVote->AddVote_PageFooter();
}

static void ComVoteDutyPage(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(pGame->Core() && pGame->Core()->VoteMenuManager())
	{
		SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
		pV->m_Page = PAGE_DUTIES;
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
	}
}

static void ComVoteDutyClaim(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pGame->Core() || !pGame->Core()->DutiesManager() || !pP)
		return;
	pGame->Core()->DutiesManager()->TryClaim(pP, pResult->GetInteger(0));
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

void CDutiesManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("menuduties", "", "", ComVoteDutyPage, GS());
	pMgr->AddCommand("dutyclaim", "", "i", ComVoteDutyClaim, GS());
}

bool CDutiesManager::OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason)
{
	(void)pPlayer;
	(void)pCmd;
	(void)pArgs;
	(void)ReasonNumber;
	(void)pReason;
	return false;
}
