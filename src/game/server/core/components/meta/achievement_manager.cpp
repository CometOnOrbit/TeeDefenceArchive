#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/meta/achievement_manager.h>
#include <game/server/core/components/meta/meta_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entity_manager.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CAchievementManager::CAchievementManager()
{
	m_NumDefs = 0;
	mem_zero(m_aDefs, sizeof(m_aDefs));
}

void CAchievementManager::LoadDefs()
{
	m_NumDefs = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/achievements.json", Storage());
	if(!pRoot)
	{
		dbg_msg("ach", "achievements.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["achievements"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumDefs < MAX_META_ACHIEVEMENTS; i++)
	{
		const json_value &A = Arr[(int)i];
		if(A.type != json_object)
			continue;
		SAchievementDef &Def = m_aDefs[m_NumDefs++];
		mem_zero(&Def, sizeof(Def));
		if(A["id"].type == json_string)
			str_copy(Def.m_aId, A["id"].u.string.ptr, sizeof(Def.m_aId));
		if(A["title"].type == json_string)
			str_copy(Def.m_aTitle, A["title"].u.string.ptr, sizeof(Def.m_aTitle));
		if(A["desc"].type == json_string)
			str_copy(Def.m_aDesc, A["desc"].u.string.ptr, sizeof(Def.m_aDesc));
		if(A["type"].type == json_string)
			str_copy(Def.m_aType, A["type"].u.string.ptr, sizeof(Def.m_aType));
		if(A["count"].type == json_integer)
			Def.m_Count = (int)A["count"].u.integer;
		else
			Def.m_Count = 1;
		if(A["reward_item"].type == json_integer)
			Def.m_RewardItem = (int)A["reward_item"].u.integer;
		if(A["reward_num"].type == json_integer)
			Def.m_RewardNum = (int)A["reward_num"].u.integer;
		else
			Def.m_RewardNum = 1;
	}
	dbg_msg("ach", "loaded %d achievements", m_NumDefs);
}

void CAchievementManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadDefs();
	if(Core())
		Core()->Events().Register(this);
}

void CAchievementManager::OnShutdown()
{
	if(Core())
		Core()->Events().Unregister(this);
}

void CAchievementManager::RequestPersist(int ClientID)
{
	if(Core() && Core()->MetaManager())
		Core()->MetaManager()->Persist(ClientID);
}

void CAchievementManager::OnPlayerLogin(CPlayer *pPlayer)
{
	(void)pPlayer;
}

void CAchievementManager::OnClientReset(int ClientID)
{
	(void)ClientID;
}

static const char *AchTitle(CGameContext *pGS, int ClientID, const SAchievementDef &Def)
{
	char aKey[64];
	str_format(aKey, sizeof(aKey), "achievement.%s.title", Def.m_aId);
	return pGS->Loc(ClientID, aKey, Def.m_aTitle);
}

void CAchievementManager::GrantReward(CPlayer *pPlayer, const SAchievementDef &Def)
{
	if(!pPlayer || !GS())
		return;
	if(Def.m_RewardItem >= 0 && Def.m_RewardNum > 0 && GS()->ItemHelper() && GS()->ItemHelper()->CheckItemValid(Def.m_RewardItem))
	{
		vec2 Pos = pPlayer->m_ViewPos;
		if(CCharacter *pChr = pPlayer->GetCharacter())
			Pos = pChr->GetPos();
		if(Core() && Core()->EntityManager())
			Core()->EntityManager()->DropItem(Pos, pPlayer->GetCID(), Def.m_RewardItem, Def.m_RewardNum);
	}
	char aBuf[128];
	GS()->LocFormat(aBuf, sizeof(aBuf), pPlayer->GetCID(), "achievement.unlocked", "成就解锁：%s", AchTitle(GS(), pPlayer->GetCID(), Def));
	GS()->SendChatTo(pPlayer->GetCID(), aBuf);
}

void CAchievementManager::TryComplete(CPlayer *pPlayer, int Idx)
{
	if(!pPlayer || !Core() || !Core()->MetaManager() || Idx < 0 || Idx >= m_NumDefs || Idx >= MAX_META_ACHIEVEMENTS)
		return;
	const int CID = pPlayer->GetCID();
	SPlayerMetaData &Meta = Core()->MetaManager()->Get(CID);
	if(Meta.m_aaAchievements[Idx])
		return;
	const SAchievementDef &Def = m_aDefs[Idx];
	if(Core()->MetaManager()->GetAchProgress(CID, Idx) < Def.m_Count)
		return;
	Meta.m_aaAchievements[Idx] = true;
	GrantReward(pPlayer, Def);
	RequestPersist(CID);
}

void CAchievementManager::BumpProgress(CPlayer *pPlayer, const char *pType, int Amount)
{
	if(!pPlayer || pPlayer->IsDummy() || !pType || Amount <= 0 || !Core() || !Core()->MetaManager())
		return;
	const int CID = pPlayer->GetCID();
	SPlayerMetaData &Meta = Core()->MetaManager()->Get(CID);
	PlayerMeta_EnsureDay(&Meta);
	for(int i = 0; i < m_NumDefs; i++)
	{
		if(Meta.m_aaAchievements[i])
			continue;
		if(str_comp(m_aDefs[i].m_aType, pType) != 0)
			continue;
		Core()->MetaManager()->SetAchProgress(CID, i, Core()->MetaManager()->GetAchProgress(CID, i) + Amount);
		TryComplete(pPlayer, i);
	}
	RequestPersist(CID);
}

void CAchievementManager::OnPlayerKill(CPlayer *pKiller, int ZombId)
{
	(void)ZombId;
	BumpProgress(pKiller, "kill", 1);
}

void CAchievementManager::OnPlayerCraft(CPlayer *pPlayer, int ItemId, int Amount)
{
	(void)ItemId;
	BumpProgress(pPlayer, "craft", maximum(1, Amount));
}

void CAchievementManager::OnPlayerMine(CPlayer *pPlayer, int MatId)
{
	(void)MatId;
	BumpProgress(pPlayer, "mine", 1);
}

void CAchievementManager::OnWaveComplete(int Wave)
{
	if(!GS() || !Core() || !Core()->MetaManager())
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || pP->IsDummy() || pP->GetAccountId() < 0)
			continue;
		SPlayerMetaData &Meta = Core()->MetaManager()->Get(i);
		for(int a = 0; a < m_NumDefs; a++)
		{
			if(str_comp(m_aDefs[a].m_aType, "wave") != 0)
				continue;
			if(Meta.m_aaAchievements[a])
				continue;
			if(Wave >= m_aDefs[a].m_Count)
			{
				Core()->MetaManager()->SetAchProgress(i, a, m_aDefs[a].m_Count);
				TryComplete(pP, a);
			}
		}
	}
}

void CAchievementManager::BuildAchievementsPage(int ClientID)
{
	if(!Core() || !Core()->VoteMenuManager() || !Core()->MetaManager())
		return;
	SPlayerMetaData &Meta = Core()->MetaManager()->Get(ClientID);
	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	pVote->SetVoteBuildClientID(ClientID);
	pVote->AddVote_PageHeader(GS()->Loc(ClientID, "achievement.title", "成就"));
	int Done = 0;
	for(int i = 0; i < m_NumDefs; i++)
		if(Meta.m_aaAchievements[i])
			Done++;
	{
		char aSum[VOTE_DESC_LENGTH];
		str_format(aSum, sizeof(aSum), GS()->Loc(ClientID, "achievement.progress", "已解锁 %d/%d"), Done, m_NumDefs);
		pVote->AddVote_PageSubtitle(aSum);
	}
	pVote->AddVote_Separator();
	for(int i = 0; i < m_NumDefs; i++)
	{
		char aLine[VOTE_DESC_LENGTH];
		const bool Complete = Meta.m_aaAchievements[i];
		const char *pTitle = AchTitle(GS(), ClientID, m_aDefs[i]);
		if(Complete)
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "achievement.entry.done", "✓ %s"), pTitle);
		else
			str_format(aLine, sizeof(aLine), GS()->Loc(ClientID, "achievement.entry", "▹ %s"), pTitle);
		pVote->AddVote_TextLine(aLine);
		if(!Complete)
			pVote->AddVote_ProgressLine(Core()->MetaManager()->GetAchProgress(ClientID, i), m_aDefs[i].m_Count);
	}
	pVote->AddVote_PageFooter();
}

static void ComVoteAchPage(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(pGame->Core() && pGame->Core()->AchievementManager())
	{
		SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
		pV->m_Page = PAGE_ACHIEVEMENTS;
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
	}
}

void CAchievementManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("menuachievements", "", "", ComVoteAchPage, GS());
}

bool CAchievementManager::OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason)
{
	(void)pPlayer;
	(void)pCmd;
	(void)pArgs;
	(void)ReasonNumber;
	(void)pReason;
	return false;
}
