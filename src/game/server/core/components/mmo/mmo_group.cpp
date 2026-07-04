#include "mmo_manager.h"

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/global_state.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_wrapper.h>

// ══════════════════════════════════════════════════════════════════════
//  CMMOManager Group Methods — forward to CGlobalState
// ══════════════════════════════════════════════════════════════════════

int CMMOManager::FindGroupByAccountID(int64 AccountID)
{
	return CGlobalState::FindGroupByAccountID(AccountID);
}

int CMMOManager::GetPlayerGroupID(int ClientID)
{
	return CGlobalState::GetPlayerGroupID(GS()->Server(), ClientID);
}

SMMOGroup *CMMOManager::GetPlayerGroup(int ClientID)
{
	SGlobalGroup *pG = CGlobalState::GetPlayerGroup(GS()->Server(), ClientID);
	if(!pG)
		return nullptr;
	const int Idx = GetPlayerGroupID(ClientID);
	if(Idx < 0 || Idx >= MMO_MAX_GROUPS)
		return nullptr;
	SMMOGroup &Local = m_aGroups[Idx];
	Local.m_ID = pG->m_ID;
	Local.m_LeaderAccountID = pG->m_LeaderAccountID;
	Local.m_TeamColor = pG->m_TeamColor;
	Local.m_CreatedAt = pG->m_CreatedAt;
	Local.m_vMemberAccountIDs = pG->m_vMemberAccountIDs;
	return &Local;
}

bool CMMOManager::GroupCreate(int ClientID)
{
	return CGlobalState::GroupCreate(GS(), ClientID);
}

bool CMMOManager::GroupInvite(int LeaderCID, int TargetCID)
{
	return CGlobalState::GroupInvite(GS(), LeaderCID, TargetCID);
}

bool CMMOManager::GroupAccept(int ClientID)
{
	return CGlobalState::GroupAccept(GS(), ClientID);
}

bool CMMOManager::GroupLeave(int ClientID)
{
	return CGlobalState::GroupLeave(GS(), ClientID);
}

bool CMMOManager::GroupKick(int LeaderCID, int TargetCID)
{
	return CGlobalState::GroupKick(GS(), LeaderCID, TargetCID);
}

bool CMMOManager::GroupDisband(int LeaderCID)
{
	return CGlobalState::GroupDisband(GS(), LeaderCID);
}

bool CMMOManager::SaveGroup(SMMOGroup &Group)
{
	SGlobalGroup G;
	G.m_ID = Group.m_ID;
	G.m_LeaderAccountID = Group.m_LeaderAccountID;
	G.m_TeamColor = Group.m_TeamColor;
	G.m_CreatedAt = Group.m_CreatedAt;
	G.m_vMemberAccountIDs = Group.m_vMemberAccountIDs;
	return CGlobalState::SaveGroup(G, GS());
}

void CMMOManager::RemoveGroup(int GroupID)
{
	CGlobalState::RemoveGroup(GroupID);
}

void CMMOManager::GroupChat(int ClientID, const char *pMsg)
{
	CGlobalState::GroupChat(GS(), ClientID, pMsg);
}

void CMMOManager::GroupChatTo(int ClientID, const char *pMsg)
{
	if(pMsg && pMsg[0])
		GS()->SendChatTo(ClientID, pMsg);
}

static const char *FindOnlineName(CGameContext *pGS, int64 AccountID)
{
	if(!pGS)
		return "离线";
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		int WID = pGS->Server()->GetClientWorldID(i);
		if(WID < 0)
			continue;
		IGameServer *pIGS = pGS->Server()->GameServer(WID);
		if(!pIGS)
			continue;
		CPlayer *p = ((CGameContext *)pIGS)->m_apPlayers[i];
		if(p && p->GetAccountId() == AccountID)
			return pGS->Server()->ClientName(i);
	}
	return "离线";
}

static int FindOnlineClientID(CGameContext *pGS, int64 AccountID)
{
	if(!pGS)
		return -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		int WID = pGS->Server()->GetClientWorldID(i);
		if(WID < 0)
			continue;
		IGameServer *pIGS = pGS->Server()->GameServer(WID);
		if(!pIGS)
			continue;
		CPlayer *p = ((CGameContext *)pIGS)->m_apPlayers[i];
		if(p && p->GetAccountId() == AccountID)
			return i;
	}
	return -1;
}

void CMMOManager::RefreshGroupVotePage(int ClientID)
{
	SPlayerVote *pV = GetVoteMenu() ? GetVoteMenu()->GetPlayerVote(ClientID) : nullptr;
	if(!pV || pV->m_Page != VOTE_PAGE_MMO_GROUP)
		return;
	pV->m_aExtraText[0] = 0;
	GetVoteMenu()->ClearVotes(ClientID);
}

// ══════════════════════════════════════════════════════════════════════
//  ShowGroupVotes — vote menu integration (MRPG-style)
// ══════════════════════════════════════════════════════════════════════

void CMMOManager::ShowGroupKickVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pP || !pVote)
		return;

	pVote->SetVoteLastPage(VOTE_PAGE_MMO_GROUP);
	pVote->SetVoteBuildClientID(ClientID);
	CVoteWrapper V(ClientID, GS(), pVote);

	const int GroupIdx = CGlobalState::GetPlayerGroupID(GS()->Server(), ClientID);
	if(GroupIdx < 0)
	{
		V.GroupTitle("踢出队员");
		V.Info("你不在队伍中");
		V.Footer();
		return;
	}

	SGlobalGroup &G = CGlobalState::GetGroupArray()[GroupIdx];
	if(!G.IsLeader(pP->GetAccountId()))
	{
		V.GroupTitle("踢出队员");
		V.Info("只有队长可以踢人");
		V.Footer();
		return;
	}

	V.GroupTitle("踢出队员");
	V.Info("选择要踢出的队员");

	char aBuf[128];
	for(const auto &AID : G.m_vMemberAccountIDs)
	{
		if(AID == G.m_LeaderAccountID)
			continue;
		const int TargetCID = FindOnlineClientID(GS(), AID);
		if(TargetCID < 0)
			continue;
		str_format(aBuf, sizeof(aBuf), "踢出 %s", FindOnlineName(GS(), AID));
		char aCmd[64];
		str_format(aCmd, sizeof(aCmd), "ccv_group_kick %d", TargetCID);
		V.Option(aCmd, aBuf);
	}

	V.Footer();
}

void CMMOManager::ShowGroupVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pP || !pVote)
		return;

	pVote->SetVoteLastPage(VOTE_PAGE_MMO_SOCIAL);
	pVote->SetVoteBuildClientID(ClientID);
	CVoteWrapper V(ClientID, GS(), pVote);

	int GroupIdx = CGlobalState::GetPlayerGroupID(GS()->Server(), ClientID);
	if(GroupIdx < 0)
	{
		V.GroupTitle("队伍");
		V.Info("创建或加入一支队伍");
		V.Option("ccv_group_create", "创建队伍");
		V.Footer();
		return;
	}

	SGlobalGroup &G = CGlobalState::GetGroupArray()[GroupIdx];
	V.GroupTitle("队伍");

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "队长: %s", FindOnlineName(GS(), G.m_LeaderAccountID));
	V.Info(aBuf);

	V.GroupLine();
	V.GroupTitle("成员");

	for(const auto &AID : G.m_vMemberAccountIDs)
	{
		str_format(aBuf, sizeof(aBuf), "%s %s", AID == G.m_LeaderAccountID ? "[队长]" : "·", FindOnlineName(GS(), AID));
		V.Info(aBuf);
	}

	V.GroupLine();
	V.GroupTitle("操作");

	if(G.IsLeader(pP->GetAccountId()))
	{
		V.Option("ccv_group_disband", "解散队伍");
		V.Option("ccv_group_kick_menu", "踢出队员");
	}
	V.Option("ccv_group_leave", "离开队伍");
	V.Footer();
}
