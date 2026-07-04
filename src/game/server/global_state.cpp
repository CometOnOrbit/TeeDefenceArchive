#include "global_state.h"
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/account.h>
#include <game/server/sql_query.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_wrapper.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/entities/character.h>
#include <engine/shared/config.h>
#include <game/commands.h>
#include <mysql.h>

// ─── Static Data Definitions ──────────────────────────────────────────

SGlobalGroup CGlobalState::ms_aGroups[GLOBAL_MAX_GROUPS];
int CGlobalState::ms_NumGroups = 0;
SGlobalTrade CGlobalState::ms_aTrades[GLOBAL_MAX_TRADES];
int CGlobalState::ms_NumTrades = 0;
std::map<int64, int> CGlobalState::ms_OnlineFriendsMap;

void CGlobalState::Init()
{
	for(int i = 0; i < GLOBAL_MAX_GROUPS; i++)
		ms_aGroups[i].Clear();
	for(int i = 0; i < GLOBAL_MAX_TRADES; i++)
		ms_aTrades[i].Clear();
	ms_NumGroups = 0;
	ms_NumTrades = 0;
}

// ══════════════════════════════════════════════════════════════════════
//  Cross-world Helpers
// ══════════════════════════════════════════════════════════════════════

// Find a player's CPlayer* across ALL worlds
// Uses IServer::GetClientWorldID + GameServer() for proper cross-world lookup
static CPlayer *FindPlayerCrossWorld(IServer *pServer, int ClientID)
{
	if(!pServer || ClientID < 0 || ClientID >= MAX_CLIENTS)
		return nullptr;
	int WID = pServer->GetClientWorldID(ClientID);
	if(WID < 0) return nullptr;
	IGameServer *pIGS = pServer->GameServer(WID);
	if(!pIGS) return nullptr;
	CGameContext *pWorld = (CGameContext *)pIGS;
	return pWorld->m_apPlayers[ClientID];
}

// Send a chat message to a player (works globally regardless of world)
static void SendChatToCross(IServer *pServer, int ClientID, const char *pText)
{
	if(!pServer || ClientID < 0) return;
	int WID = pServer->GetClientWorldID(ClientID);
	if(WID < 0) return;
	IGameServer *pIGS = pServer->GameServer(WID);
	if(!pIGS) return;
	((CGameContext *)pIGS)->SendChatTo(ClientID, pText);
}

// ══════════════════════════════════════════════════════════════════════
//  SGlobalGroup Helpers
// ══════════════════════════════════════════════════════════════════════

bool SGlobalGroup::IsMember(int64 AccountID) const
{
	for(const auto& AID : m_vMemberAccountIDs)
		if(AID == AccountID) return true;
	return false;
}

bool SGlobalGroup::IsLeader(int64 AccountID) const
{
	return m_LeaderAccountID == AccountID;
}

// ══════════════════════════════════════════════════════════════════════
//  Group Methods
// ══════════════════════════════════════════════════════════════════════

int CGlobalState::FindGroupByAccountID(int64 AccountID)
{
	for(int i = 0; i < ms_NumGroups; i++)
		if(ms_aGroups[i].m_ID >= 0 && ms_aGroups[i].IsMember(AccountID))
			return i;
	return -1;
}

int CGlobalState::GetPlayerGroupID(IServer *pServer, int ClientID)
{
	CPlayer *pPlayer = FindPlayerCrossWorld(pServer, ClientID);
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return -1;
	return FindGroupByAccountID(pPlayer->GetAccountId());
}

SGlobalGroup* CGlobalState::GetPlayerGroup(IServer *pServer, int ClientID)
{
	int Idx = GetPlayerGroupID(pServer, ClientID);
	if(Idx < 0) return nullptr;
	return &ms_aGroups[Idx];
}

bool CGlobalState::GroupCreate(CGameContext *pGS, int ClientID)
{
	if(!pGS) return false;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;
	int64 AID = pPlayer->GetAccountId();

	if(GetPlayerGroupID(pGS->Server(), ClientID) >= 0)
	{ pGS->SendChatTo(ClientID, "你已经在队伍中了。"); return false; }

	int Slot = -1;
	for(int i = 0; i < GLOBAL_MAX_GROUPS; i++)
	{
		if(ms_aGroups[i].m_ID < 0) { Slot = i; break; }
	}
	if(Slot < 0)
	{ pGS->SendChatTo(ClientID, "队伍系统繁忙。"); return false; }

	SGlobalGroup &G = ms_aGroups[Slot];
	G.Clear();
	G.m_ID = Slot + 1;
	G.m_LeaderAccountID = AID;
	G.m_vMemberAccountIDs.push_back(AID);
	G.m_CreatedAt = time(nullptr);
	G.m_TeamColor = (Slot % 7) + 1;

	if(Slot >= ms_NumGroups) ms_NumGroups = Slot + 1;

	pGS->SendChatTo(ClientID, "你创建了队伍。输入 /group_invite <名字> 邀请队友。");
	return true;
}

bool CGlobalState::GroupInvite(CGameContext *pGS, int LeaderCID, int TargetCID)
{
	if(!pGS) return false;
	CPlayer *pLeader = FindPlayerCrossWorld(pGS->Server(), LeaderCID);
	CPlayer *pTarget = FindPlayerCrossWorld(pGS->Server(), TargetCID);
	if(!pLeader || !pTarget) return false;
	if(LeaderCID == TargetCID)
	{ pGS->SendChatTo(LeaderCID, "不能邀请自己。"); return false; }

	int64 LeaderAID = pLeader->GetAccountId();
	int64 TargetAID = pTarget->GetAccountId();

	int GroupID = FindGroupByAccountID(LeaderAID);
	if(GroupID < 0)
		return GroupCreate(pGS, LeaderCID) ? GroupInvite(pGS, LeaderCID, TargetCID) : false;

	SGlobalGroup &G = ms_aGroups[GroupID];
	if(!G.IsLeader(LeaderAID))
	{ pGS->SendChatTo(LeaderCID, "只有队长可以邀请。"); return false; }
	if(G.IsFull())
	{ pGS->SendChatTo(LeaderCID, "队伍已满。"); return false; }
	if(G.IsMember(TargetAID))
	{ pGS->SendChatTo(LeaderCID, "已在队伍中。"); return false; }
	if(FindGroupByAccountID(TargetAID) >= 0)
	{ pGS->SendChatTo(LeaderCID, "该玩家已在其他队伍。"); return false; }

	pTarget->m_GroupInviteGroupID = G.m_ID;
	pTarget->m_GroupInviteExpire = time(nullptr) + GLOBAL_INVITE_TIMEOUT_SECS;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "你邀请了 %s 加入队伍。", pGS->Server()->ClientName(TargetCID));
	pGS->SendChatTo(LeaderCID, aBuf);
	str_format(aBuf, sizeof(aBuf), "%s 邀请你加入队伍。输入 /group_accept 接受。", pGS->Server()->ClientName(LeaderCID));
	pGS->SendChatTo(TargetCID, aBuf);
	return true;
}

bool CGlobalState::GroupAccept(CGameContext *pGS, int ClientID)
{
	if(!pGS) return false;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;

	if(pPlayer->m_GroupInviteGroupID <= 0 || time(nullptr) > pPlayer->m_GroupInviteExpire)
	{ pGS->SendChatTo(ClientID, "没有有效的队伍邀请。"); return false; }

	if(GetPlayerGroupID(pGS->Server(), ClientID) >= 0)
	{ pGS->SendChatTo(ClientID, "你已经在队伍中了。"); return false; }

	for(int i = 0; i < ms_NumGroups; i++)
	{
		if(ms_aGroups[i].m_ID == pPlayer->m_GroupInviteGroupID && ms_aGroups[i].m_ID >= 0)
		{
			SGlobalGroup &G = ms_aGroups[i];
			if(G.IsFull())
			{ pGS->SendChatTo(ClientID, "队伍已满。"); return false; }

			int64 AID = pPlayer->GetAccountId();
			G.m_vMemberAccountIDs.push_back(AID);

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "%s 加入了队伍。", pGS->Server()->ClientName(ClientID));
			// Notify all group members across all worlds
			for(int m = 0; m < MAX_CLIENTS; m++)
			{
				CPlayer *pM = FindPlayerCrossWorld(pGS->Server(), m);
				if(pM && pM->GetAccountId() > 0 && G.IsMember(pM->GetAccountId()))
					SendChatToCross(pGS->Server(), m, aBuf);
			}
			pGS->SendChatTo(ClientID, "你加入了队伍。");

			pPlayer->m_GroupInviteGroupID = 0;
			SaveGroup(G, pGS);
			return true;
		}
	}
	pGS->SendChatTo(ClientID, "邀请已过期或队伍已解散。");
	pPlayer->m_GroupInviteGroupID = 0;
	return false;
}

bool CGlobalState::GroupLeave(CGameContext *pGS, int ClientID)
{
	if(!pGS) return false;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return false;

	int GroupIdx = GetPlayerGroupID(pGS->Server(), ClientID);
	if(GroupIdx < 0) return false;

	SGlobalGroup &G = ms_aGroups[GroupIdx];
	int64 AID = pPlayer->GetAccountId();
	bool WasLeader = G.IsLeader(AID);

	auto it = std::find(G.m_vMemberAccountIDs.begin(), G.m_vMemberAccountIDs.end(), AID);
	if(it != G.m_vMemberAccountIDs.end())
		G.m_vMemberAccountIDs.erase(it);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s 离开了队伍。", pGS->Server()->ClientName(ClientID));
	for(int m = 0; m < MAX_CLIENTS; m++)
	{
		CPlayer *pM = FindPlayerCrossWorld(pGS->Server(), m);
		if(pM && pM->GetAccountId() > 0 && G.IsMember(pM->GetAccountId()))
			SendChatToCross(pGS->Server(), m, aBuf);
	}

	if(G.m_vMemberAccountIDs.empty())
	{
		if(GroupIdx == ms_NumGroups - 1)
		{
			G.Clear();
			ms_NumGroups--;
		}
		else
		{
			G.Clear();
		}
	}
	else if(WasLeader)
	{
		G.m_LeaderAccountID = G.m_vMemberAccountIDs[0];
		str_format(aBuf, sizeof(aBuf), "队长已转移。");
		for(int m = 0; m < MAX_CLIENTS; m++)
		{
			CPlayer *pM = FindPlayerCrossWorld(pGS->Server(), m);
			if(pM && pM->GetAccountId() > 0 && G.IsMember(pM->GetAccountId()))
				SendChatToCross(pGS->Server(), m, aBuf);
		}
		SaveGroup(G, pGS);
	}
	return true;
}

bool CGlobalState::GroupKick(CGameContext *pGS, int LeaderCID, int TargetCID)
{
	if(!pGS) return false;
	CPlayer *pLeader = FindPlayerCrossWorld(pGS->Server(), LeaderCID);
	CPlayer *pTarget = FindPlayerCrossWorld(pGS->Server(), TargetCID);
	if(!pLeader || !pTarget) return false;

	int GroupIdx = GetPlayerGroupID(pGS->Server(), LeaderCID);
	if(GroupIdx < 0) { pGS->SendChatTo(LeaderCID, "你不在队伍中。"); return false; }

	SGlobalGroup &G = ms_aGroups[GroupIdx];
	if(!G.IsLeader(pLeader->GetAccountId()))
	{ pGS->SendChatTo(LeaderCID, "只有队长可以踢人。"); return false; }

	int64 TargetAID = pTarget->GetAccountId();
	if(!G.IsMember(TargetAID))
	{ pGS->SendChatTo(LeaderCID, "不在队伍中。"); return false; }

	auto it = std::find(G.m_vMemberAccountIDs.begin(), G.m_vMemberAccountIDs.end(), TargetAID);
	if(it != G.m_vMemberAccountIDs.end())
		G.m_vMemberAccountIDs.erase(it);

	char aBuf[128];
	pGS->SendChatTo(TargetCID, "你被踢出了队伍。");
	str_format(aBuf, sizeof(aBuf), "%s 被踢出了队伍。", pGS->Server()->ClientName(TargetCID));
	for(int m = 0; m < MAX_CLIENTS; m++)
	{
		CPlayer *pM = FindPlayerCrossWorld(pGS->Server(), m);
		if(pM && pM->GetAccountId() > 0 && G.IsMember(pM->GetAccountId()))
			SendChatToCross(pGS->Server(), m, aBuf);
	}

	if(G.m_vMemberAccountIDs.empty())
		G.Clear();
	return true;
}

bool CGlobalState::GroupDisband(CGameContext *pGS, int LeaderCID)
{
	if(!pGS) return false;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), LeaderCID);
	if(!pPlayer) return false;

	int GroupIdx = GetPlayerGroupID(pGS->Server(), LeaderCID);
	if(GroupIdx < 0) { pGS->SendChatTo(LeaderCID, "你不在队伍中。"); return false; }

	SGlobalGroup &G = ms_aGroups[GroupIdx];
	if(!G.IsLeader(pPlayer->GetAccountId()))
	{ pGS->SendChatTo(LeaderCID, "只有队长可以解散队伍。"); return false; }

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "队伍已被队长解散。");
	for(int m = 0; m < MAX_CLIENTS; m++)
	{
		CPlayer *pM = FindPlayerCrossWorld(pGS->Server(), m);
		if(pM && pM->GetAccountId() > 0 && G.IsMember(pM->GetAccountId()))
			SendChatToCross(pGS->Server(), m, aBuf);
	}

	RemoveGroup(GroupIdx);
	return true;
}

void CGlobalState::RemoveGroup(int GroupIdx)
{
	if(GroupIdx < 0 || GroupIdx >= ms_NumGroups) return;
	SGlobalGroup &G = ms_aGroups[GroupIdx];
	G.Clear();
	if(GroupIdx == ms_NumGroups - 1)
		ms_NumGroups--;
}

bool CGlobalState::SaveGroup(SGlobalGroup &Group, CGameContext *pGS)
{
	if(!pGS) return false;
	CSqlConnectionPool *pPool = pGS->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aMembers[512] = {0};
	int Len = 0;
	for(size_t i = 0; i < Group.m_vMemberAccountIDs.size(); i++)
	{
		int Rem = (int)sizeof(aMembers) - Len - 1;
		if(Rem <= 0) break;
		if(Len > 0)
		{
			str_format(aMembers + Len, Rem, ",");
			Len++;
			Rem--;
		}
		str_format(aMembers + Len, Rem, "%lld", (long long)Group.m_vMemberAccountIDs[i]);
		Len = str_length(aMembers);
	}

	char aQuery[1024];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_groups` (`GroupID`, `LeaderUID`, `Members`, `TeamColor`) "
		"VALUES (%d, %lld, '%s', %d) "
		"ON DUPLICATE KEY UPDATE `LeaderUID`=%lld, `Members`='%s', `TeamColor`=%d",
		Group.m_ID, (long long)Group.m_LeaderAccountID, aMembers, Group.m_TeamColor,
		(long long)Group.m_LeaderAccountID, aMembers, Group.m_TeamColor);
	SqlExecQuery(pSql, pGS->Config(), aQuery);
	pPool->Release(pRaw);
	return true;
}

void CGlobalState::GroupChat(CGameContext *pGS, int ClientID, const char *pMsg)
{
	if(!pGS) return;
	int GroupIdx = GetPlayerGroupID(pGS->Server(), ClientID);
	if(GroupIdx < 0)
	{ pGS->SendChatTo(ClientID, "你不在队伍中。"); return; }

	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer) return;

	const SGlobalGroup &G = ms_aGroups[GroupIdx];
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "[队伍] %s: %s", pGS->Server()->ClientName(ClientID), pMsg);
	for(int m = 0; m < MAX_CLIENTS; m++)
	{
		CPlayer *pM = FindPlayerCrossWorld(pGS->Server(), m);
		if(pM && pM->GetAccountId() > 0 && G.IsMember(pM->GetAccountId()))
			SendChatToCross(pGS->Server(), m, aBuf);
	}
}

int CGlobalState::FindClientByName(CGameContext *pGS, const char *pName)
{
	if(!pGS) return -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *p = FindPlayerCrossWorld(pGS->Server(), i);
		if(!p || p->IsDummy() || p->GetTeam() == TEAM_SPECTATORS) continue;
		if(str_comp_nocase(pGS->Server()->ClientName(i), pName) == 0) return i;
	}
	return -1;
}

// ══════════════════════════════════════════════════════════════════════
//  Trade Methods
// ══════════════════════════════════════════════════════════════════════

bool CGlobalState::TradeRequest(CGameContext *pGS, int FromCID, int ToCID)
{
	if(!pGS) return false;
	if(FromCID == ToCID)
	{ pGS->SendChatTo(FromCID, "不能和自己交易。"); return false; }
	CPlayer *pTo = FindPlayerCrossWorld(pGS->Server(), ToCID);
	if(!pTo || pTo->GetTeam() == TEAM_SPECTATORS)
	{ pGS->SendChatTo(FromCID, "该玩家不在游戏中。"); return false; }

	for(int i = 0; i < ms_NumTrades; i++)
	{
		if(!ms_aTrades[i].m_Active) continue;
		if(ms_aTrades[i].m_PartyA_CID == FromCID || ms_aTrades[i].m_PartyB_CID == FromCID)
		{ pGS->SendChatTo(FromCID, "你已经在交易中。"); return false; }
		if(ms_aTrades[i].m_PartyA_CID == ToCID || ms_aTrades[i].m_PartyB_CID == ToCID)
		{ pGS->SendChatTo(FromCID, "该玩家正在交易。"); return false; }
	}

	int Slot = -1;
	for(int i = 0; i < GLOBAL_MAX_TRADES; i++)
	{
		if(!ms_aTrades[i].m_Active) { Slot = i; break; }
	}
	if(Slot < 0)
	{ pGS->SendChatTo(FromCID, "交易系统繁忙。"); return false; }

	SGlobalTrade &T = ms_aTrades[Slot];
	T.Clear();
	T.m_PartyA_CID = FromCID;
	T.m_PartyB_CID = ToCID;
	T.m_ExpiresAt = time(nullptr) + GLOBAL_TRADE_TIMEOUT_SECS;
	T.m_Active = true;
	if(Slot >= ms_NumTrades) ms_NumTrades = Slot + 1;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "你向 %s 发起了交易请求。", pGS->Server()->ClientName(ToCID));
	pGS->SendChatTo(FromCID, aBuf);
	str_format(aBuf, sizeof(aBuf), "%s 想和你交易。输入 /trade_accept 接受。", pGS->Server()->ClientName(FromCID));
	pGS->SendChatTo(ToCID, aBuf);
	return true;
}

bool CGlobalState::TradeAccept(CGameContext *pGS, int ClientID)
{
	if(!pGS) return false;
	for(int i = 0; i < ms_NumTrades; i++)
	{
		auto &T = ms_aTrades[i];
		if(!T.m_Active || T.m_PartyB_CID != ClientID) continue;

		pGS->SendChatTo(T.m_PartyA_CID, "对方接受了交易邀请。");
		pGS->SendChatTo(T.m_PartyB_CID, "你接受了交易邀请。");

		const char *pHelp =
			"─ 交易 ─\n"
			"/trade_additem <背包格> <数量> 放入物品\n"
			"/trade_addgold <金额> 放入金币\n"
			"/trade_confirm 确认\n"
			"/trade_cancel 取消";
		pGS->SendChatTo(T.m_PartyA_CID, pHelp);
		pGS->SendChatTo(T.m_PartyB_CID, pHelp);
		return true;
	}
	pGS->SendChatTo(ClientID, "没有待处理的交易邀请。");
	return false;
}

bool CGlobalState::TradeDecline(CGameContext *pGS, int ClientID)
{
	if(!pGS) return false;
	for(int i = 0; i < ms_NumTrades; i++)
	{
		auto &T = ms_aTrades[i];
		if(!T.m_Active || (T.m_PartyA_CID != ClientID && T.m_PartyB_CID != ClientID)) continue;

		int Other = (T.m_PartyA_CID == ClientID) ? T.m_PartyB_CID : T.m_PartyA_CID;
		if(Other >= 0)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "%s 取消了交易。", pGS->Server()->ClientName(ClientID));
			SendChatToCross(pGS->Server(), Other, aBuf);
		}
		pGS->SendChatTo(ClientID, "你取消了交易。");
		T.Clear();
		return true;
	}
	pGS->SendChatTo(ClientID, "没有进行的交易。");
	return false;
}

bool CGlobalState::TradeAddItem(CGameContext *pGS, int ClientID, int ItemSlotIndex, int Count)
{
	if(!pGS) return false;
	int TradeIdx = -1;
	for(int i = 0; i < ms_NumTrades; i++)
	{
		if(ms_aTrades[i].m_Active && (ms_aTrades[i].m_PartyA_CID == ClientID || ms_aTrades[i].m_PartyB_CID == ClientID))
		{ TradeIdx = i; break; }
	}
	if(TradeIdx < 0)
	{ pGS->SendChatTo(ClientID, "你不在交易中。"); return false; }

	SGlobalTrade &T = ms_aTrades[TradeIdx];
	if(T.m_ConfirmedA || T.m_ConfirmedB)
	{ pGS->SendChatTo(ClientID, "交易已锁定，请取消后重新添加。"); return false; }

	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer) return false;

	if(ItemSlotIndex < 0 || ItemSlotIndex >= (int)pPlayer->m_MMOInventory.size())
	{ pGS->SendChatTo(ClientID, "背包格不存在。"); return false; }

	CItem *pItem = nullptr;
	if((size_t)ItemSlotIndex < pPlayer->m_MMOInventory.size())
		pItem = &pPlayer->m_MMOInventory[ItemSlotIndex];
	if(!pItem || !pItem->IsValid())
	{ pGS->SendChatTo(ClientID, "该格子没有物品。"); return false; }
	if(Count <= 0 || Count > pItem->m_Count)
	{ pGS->SendChatTo(ClientID, "数量无效。"); return false; }

	bool IsA = (T.m_PartyA_CID == ClientID);
	auto &pOffer = IsA ? T.m_aOfferA : T.m_aOfferB;
	int &pNum = IsA ? T.m_NumOfferA : T.m_NumOfferB;

	if(pNum >= GLOBAL_TRADE_SLOTS)
	{ pGS->SendChatTo(ClientID, "交易物品已满。"); return false; }

	for(int i = 0; i < pNum; i++)
	{
		if(pOffer[i].m_SlotIndex == ItemSlotIndex)
		{
			pOffer[i].m_Count = Count;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "已更新物品数量为 %d。", Count);
			pGS->SendChatTo(ClientID, aBuf);
			return true;
		}
	}

	pOffer[pNum].m_ItemID = pItem->m_ItemID;
	pOffer[pNum].m_Count = Count;
	pOffer[pNum].m_SlotIndex = ItemSlotIndex;
	pNum++;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "已放入物品 x%d (格#%d) 到交易中。", Count, ItemSlotIndex);
	pGS->SendChatTo(ClientID, aBuf);
	return true;
}

bool CGlobalState::TradeAddGold(CGameContext *pGS, int ClientID, int Amount)
{
	if(!pGS) return false;
	int TradeIdx = -1;
	for(int i = 0; i < ms_NumTrades; i++)
	{
		if(ms_aTrades[i].m_Active && (ms_aTrades[i].m_PartyA_CID == ClientID || ms_aTrades[i].m_PartyB_CID == ClientID))
		{ TradeIdx = i; break; }
	}
	if(TradeIdx < 0)
	{ pGS->SendChatTo(ClientID, "你不在交易中。"); return false; }

	SGlobalTrade &T = ms_aTrades[TradeIdx];
	if(T.m_ConfirmedA || T.m_ConfirmedB)
	{ pGS->SendChatTo(ClientID, "交易已锁定，请取消后重新设置。"); return false; }

	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer) return false;

	if(Amount < 0 || Amount > pPlayer->GetStat(AttributeIdentifier::Gold))
	{ pGS->SendChatTo(ClientID, "金币数量无效。"); return false; }

	if(T.m_PartyA_CID == ClientID) T.m_GoldA = Amount;
	else T.m_GoldB = Amount;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "已放入 %d 金币到交易中。", Amount);
	pGS->SendChatTo(ClientID, aBuf);
	return true;
}

bool CGlobalState::TradeConfirm(CGameContext *pGS, int ClientID)
{
	if(!pGS) return false;
	int TradeIdx = -1;
	for(int i = 0; i < ms_NumTrades; i++)
	{
		if(ms_aTrades[i].m_Active && (ms_aTrades[i].m_PartyA_CID == ClientID || ms_aTrades[i].m_PartyB_CID == ClientID))
		{ TradeIdx = i; break; }
	}
	if(TradeIdx < 0)
	{ pGS->SendChatTo(ClientID, "你不在交易中。"); return false; }

	SGlobalTrade &T = ms_aTrades[TradeIdx];
	bool IsA = (T.m_PartyA_CID == ClientID);

	if(IsA) T.m_ConfirmedA = true;
	else T.m_ConfirmedB = true;

	pGS->SendChatTo(ClientID, "你已确认交易。等待对方确认...");
	int Other = IsA ? T.m_PartyB_CID : T.m_PartyA_CID;
	if(Other >= 0) pGS->SendChatTo(Other, "对方已确认交易。");

	if(T.m_ConfirmedA && T.m_ConfirmedB)
	{
		CPlayer *pA = FindPlayerCrossWorld(pGS->Server(), T.m_PartyA_CID);
		CPlayer *pB = FindPlayerCrossWorld(pGS->Server(), T.m_PartyB_CID);
		if(!pA || !pB)
		{ T.Clear(); return true; }

		if(pA->GetStat(AttributeIdentifier::Gold) < T.m_GoldA || pB->GetStat(AttributeIdentifier::Gold) < T.m_GoldB)
		{
			if(pA) pGS->SendChatTo(T.m_PartyA_CID, "金币不足，交易取消。");
			if(pB) pGS->SendChatTo(T.m_PartyB_CID, "金币不足，交易取消。");
			T.Clear(); return true;
		}

		for(int i = 0; i < T.m_NumOfferA; i++)
		{
			if(!pA->m_MMOInventory.RemoveByID(T.m_aOfferA[i].m_ItemID, T.m_aOfferA[i].m_Count))
			{
				pGS->SendChatTo(T.m_PartyA_CID, "物品不足，交易取消。");
				if(pB) pGS->SendChatTo(T.m_PartyB_CID, "对方物品不足，交易取消。");
				T.Clear(); return true;
			}
		}
		pA->SetStat(AttributeIdentifier::Gold, pA->GetStat(AttributeIdentifier::Gold) - T.m_GoldA);

		for(int i = 0; i < T.m_NumOfferB; i++)
		{
			if(!pB->m_MMOInventory.RemoveByID(T.m_aOfferB[i].m_ItemID, T.m_aOfferB[i].m_Count))
			{
				pGS->SendChatTo(T.m_PartyB_CID, "物品不足，交易取消。");
				if(pA) pGS->SendChatTo(T.m_PartyA_CID, "对方物品不足，交易取消。");
				T.Clear(); return true;
			}
		}
		pB->SetStat(AttributeIdentifier::Gold, pB->GetStat(AttributeIdentifier::Gold) - T.m_GoldB);

		for(int i = 0; i < T.m_NumOfferA; i++)
			pB->m_MMOInventory.Add(T.m_aOfferA[i].m_ItemID, T.m_aOfferA[i].m_Count);
		pB->SetStat(AttributeIdentifier::Gold, pB->GetStat(AttributeIdentifier::Gold) + T.m_GoldA);

		for(int i = 0; i < T.m_NumOfferB; i++)
			pA->m_MMOInventory.Add(T.m_aOfferB[i].m_ItemID, T.m_aOfferB[i].m_Count);
		pA->SetStat(AttributeIdentifier::Gold, pA->GetStat(AttributeIdentifier::Gold) + T.m_GoldB);

		pA->m_MMODirty = true;
		pB->m_MMODirty = true;

		if(pA) pGS->SendChatTo(T.m_PartyA_CID, "✅ 交易完成！");
		if(pB) pGS->SendChatTo(T.m_PartyB_CID, "✅ 交易完成！");

		// Save player data via their respective world's MMO manager
		{
			IServer *pSrv = pGS->Server();
			int WIDA = pSrv->GetClientWorldID(T.m_PartyA_CID);
			int WIDB = pSrv->GetClientWorldID(T.m_PartyB_CID);
			if(WIDA >= 0)
			{
				CGameContext *pAWorld = (CGameContext *)pSrv->GameServer(WIDA);
				if(pAWorld && pAWorld->Core() && pAWorld->Core()->GetMMOManager())
				{
					pAWorld->Core()->GetMMOManager()->SavePlayerData(pA);
					pAWorld->Core()->GetMMOManager()->SaveInventory(pA);
				}
			}
			if(WIDB >= 0)
			{
				CGameContext *pBWorld = (CGameContext *)pSrv->GameServer(WIDB);
				if(pBWorld && pBWorld->Core() && pBWorld->Core()->GetMMOManager())
				{
					pBWorld->Core()->GetMMOManager()->SavePlayerData(pB);
					pBWorld->Core()->GetMMOManager()->SaveInventory(pB);
				}
			}
		}
		T.Clear();
	}
	return true;
}

bool CGlobalState::TradeCancel(CGameContext *pGS, int ClientID)
{
	return TradeDecline(pGS, ClientID);
}

// ══════════════════════════════════════════════════════════════════════
//  Command Registration — registered from gamecontroller.cpp
//  (Available in ALL worlds, not just RPG)
// ══════════════════════════════════════════════════════════════════════

// ─── Group Command Callbacks ────────────────────────────────────────

static void ConGroupList(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;

	int GID = CGlobalState::GetPlayerGroupID(pGame->Server(), pCtx->m_ClientID);
	if(GID < 0) { pGame->SendChatTo(pCtx->m_ClientID, "你不在任何队伍中。"); return; }

	const SGlobalGroup &G = CGlobalState::GetGroupArray()[GID];
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══ 队伍 (%zu/%d) ═══", G.m_vMemberAccountIDs.size(), GLOBAL_GROUP_MAX_MEMBERS);
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	for(const auto &AID : G.m_vMemberAccountIDs)
	{
		const char *pName = "离线";
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *p = FindPlayerCrossWorld(pGame->Server(), i);
			if(p && p->GetAccountId() == AID) { pName = pGame->Server()->ClientName(i); break; }
		}
		str_format(aBuf, sizeof(aBuf), "%s %s (ID:%lld)",
			AID == G.m_LeaderAccountID ? "👑" : "  ", pName, (long long)AID);
		pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	}
	(void)pResult;
}

static void ConGroupCreate(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::GroupCreate(pGame, pCtx->m_ClientID);
	(void)pResult;
}

static void ConGroupInvite(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;

	int CID = pCtx->m_ClientID;
	int Target = CGlobalState::FindClientByName(pGame, pResult->GetString(0));
	if(Target < 0) { pGame->SendChatTo(CID, "未找到该玩家。"); return; }
	CGlobalState::GroupInvite(pGame, CID, Target);
	(void)pResult;
}

static void ConGroupAccept(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::GroupAccept(pGame, pCtx->m_ClientID);
	(void)pResult;
}

static void ConGroupLeave(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::GroupLeave(pGame, pCtx->m_ClientID);
	(void)pResult;
}

static void ConGroupKick(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	int CID = pCtx->m_ClientID;
	int Target = CGlobalState::FindClientByName(pGame, pResult->GetString(0));
	if(Target < 0) { pGame->SendChatTo(CID, "未找到该玩家。"); return; }
	CGlobalState::GroupKick(pGame, CID, Target);
	(void)pResult;
}

static void ConGroupChat(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::GroupChat(pGame, pCtx->m_ClientID, pResult->GetString(0));
}

// ─── Trade Command Callbacks ─────────────────────────────────────────

static void ConTradeStart(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	int CID = pCtx->m_ClientID;
	int Target = CGlobalState::FindClientByName(pGame, pResult->GetString(0));
	if(Target < 0) { pGame->SendChatTo(CID, "未找到该玩家。"); return; }
	CGlobalState::TradeRequest(pGame, CID, Target);
	(void)pResult;
}

static void ConTradeAccept(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::TradeAccept(pGame, pCtx->m_ClientID);
	(void)pResult;
}

static void ConTradeDecline(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::TradeDecline(pGame, pCtx->m_ClientID);
	(void)pResult;
}

static void ConTradeAddItem(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::TradeAddItem(pGame, pCtx->m_ClientID, pResult->GetInteger(0), pResult->GetInteger(1));
}

static void ConTradeAddGold(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::TradeAddGold(pGame, pCtx->m_ClientID, pResult->GetInteger(0));
}

static void ConTradeConfirm(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::TradeConfirm(pGame, pCtx->m_ClientID);
	(void)pResult;
}

static void ConTradeCancel(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::TradeCancel(pGame, pCtx->m_ClientID);
	(void)pResult;
}

// ══════════════════════════════════════════════════════════════════════
//  Cross-world Helpers (for friend notifications etc.)
// ══════════════════════════════════════════════════════════════════════

static CGameContext *GetWorldGC(IServer *pServer, int WorldID)
{
	if(!pServer || WorldID < 0) return nullptr;
	IGameServer *pIGS = pServer->GameServer(WorldID);
	return (CGameContext *)pIGS;
}
void CGlobalState::RegisterGlobalGroupCommands(CGameContext *pGS, CCommandManager *pManager)
{
	if(!pGS || !pManager) return;

	pManager->AddCommand("group", "查看队伍状态", "", ConGroupList, pGS);
	pManager->AddCommand("group_create", "创建队伍", "", ConGroupCreate, pGS);
	pManager->AddCommand("group_invite", "邀请玩家 <名字>", "s", ConGroupInvite, pGS);
	pManager->AddCommand("group_accept", "接受队伍邀请", "", ConGroupAccept, pGS);
	pManager->AddCommand("group_leave", "离开队伍", "", ConGroupLeave, pGS);
	pManager->AddCommand("group_kick", "踢出队员 <名字>", "s", ConGroupKick, pGS);
	pManager->AddCommand("group_chat", "队伍发言 <消息>", "r", ConGroupChat, pGS);
	pManager->AddCommand("g", "队伍发言快捷 <消息>", "r", ConGroupChat, pGS);
}

void CGlobalState::RegisterGlobalTradeCommands(CGameContext *pGS, CCommandManager *pManager)
{
	if(!pGS || !pManager) return;

	pManager->AddCommand("trade", "发起交易 <玩家名>", "s", ConTradeStart, pGS);
	pManager->AddCommand("trade_accept", "接受交易", "", ConTradeAccept, pGS);
	pManager->AddCommand("trade_decline", "拒绝交易", "", ConTradeDecline, pGS);
	pManager->AddCommand("trade_additem", "放入物品 <背包格> <数量>", "ii", ConTradeAddItem, pGS);
	pManager->AddCommand("trade_addgold", "放入金币 <金额>", "i", ConTradeAddGold, pGS);
	pManager->AddCommand("trade_confirm", "确认交易", "", ConTradeConfirm, pGS);
	pManager->AddCommand("trade_cancel", "取消交易", "", ConTradeCancel, pGS);
}

// ══════════════════════════════════════════════════════════════════════
//  Friend System
// ══════════════════════════════════════════════════════════════════════

void CGlobalState::NotifyFriendsOnline(CGameContext *pGS, CPlayer *pPlayer)
{
	if(!pGS || !pPlayer) return;
	int64 AID = pPlayer->GetAccountId();
	int CID = pPlayer->GetCID();
	if(AID <= 0) return;

	// Register online first so other online friends' list includes us
	ms_OnlineFriendsMap[AID] = CID;

	// Notify all online friends that this player came online
	// We iterate over this player's friend list and notify any that are online
	for(size_t fi = 0; fi < pPlayer->m_aFriends.size(); fi++)
	{
		int64 FriendAID = pPlayer->m_aFriends[fi];
		auto it = ms_OnlineFriendsMap.find(FriendAID);
		if(it == ms_OnlineFriendsMap.end()) continue;

		int FriendCID = it->second;
		if(FriendCID == CID) continue;

		CPlayer *pFriend = FindPlayerCrossWorld(pGS->Server(), FriendCID);
		if(!pFriend || pFriend->GetAccountId() != FriendAID) continue;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[系统] 你的好友 %s 上线了", pGS->Server()->ClientName(CID));
		SendChatToCross(pGS->Server(), FriendCID, aBuf);
	}
}

void CGlobalState::NotifyFriendsOffline(CGameContext *pGS, CPlayer *pPlayer)
{
	if(!pGS || !pPlayer) return;
	int64 AID = pPlayer->GetAccountId();
	int CID = pPlayer->GetCID();
	if(AID <= 0) return;

	// Unregister online first so friends don't see stale entry
	ms_OnlineFriendsMap.erase(AID);

	// Notify all online friends that this player went offline
	for(size_t fi = 0; fi < pPlayer->m_aFriends.size(); fi++)
	{
		int64 FriendAID = pPlayer->m_aFriends[fi];
		auto it = ms_OnlineFriendsMap.find(FriendAID);
		if(it == ms_OnlineFriendsMap.end()) continue;

		int FriendCID = it->second;
		CPlayer *pFriend = FindPlayerCrossWorld(pGS->Server(), FriendCID);
		if(!pFriend || pFriend->GetAccountId() != FriendAID) continue;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[系统] 你的好友 %s 下线了", pGS->Server()->ClientName(CID));
		SendChatToCross(pGS->Server(), FriendCID, aBuf);
	}
}

bool CGlobalState::FriendAdd(CGameContext *pGS, int ClientID, const char *pFriendName)
{
	if(!pGS) return false;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{ pGS->SendChatTo(ClientID, "请先登录。"); return false; }

	// Resolve friend's account ID
	int TargetCID = FindClientByName(pGS, pFriendName);
	if(TargetCID < 0)
	{ pGS->SendChatTo(ClientID, "未找到该玩家。"); return false; }

	CPlayer *pTarget = FindPlayerCrossWorld(pGS->Server(), TargetCID);
	if(!pTarget || pTarget->GetAccountId() <= 0)
	{ pGS->SendChatTo(ClientID, "该玩家未登录。"); return false; }

	if(ClientID == TargetCID)
	{ pGS->SendChatTo(ClientID, "不能添加自己为好友。"); return false; }

	int64 FriendAID = pTarget->GetAccountId();
	int64 MyAID = pPlayer->GetAccountId();

	// Check max friends
	if((int)pPlayer->m_aFriends.size() >= 100)
	{ pGS->SendChatTo(ClientID, "好友数量已达上限 (100)。"); return false; }

	// Check if already friends
	for(size_t i = 0; i < pPlayer->m_aFriends.size(); i++)
	{
		if(pPlayer->m_aFriends[i] == FriendAID)
		{ pGS->SendChatTo(ClientID, "该玩家已经是你的好友。"); return false; }
	}

	// Add friend
	pPlayer->m_aFriends.push_back(FriendAID);
	pPlayer->m_aFriendsDirty = true;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s 已添加为好友。", pGS->Server()->ClientName(TargetCID));
	pGS->SendChatTo(ClientID, aBuf);

	// Notify target if online (bidirectional friendship)
	// Also add ourselves to their friend list for mutual tracking
	bool TargetAlreadyHasUs = false;
	for(size_t i = 0; i < pTarget->m_aFriends.size(); i++)
	{
		if(pTarget->m_aFriends[i] == MyAID)
		{ TargetAlreadyHasUs = true; break; }
	}
	if(!TargetAlreadyHasUs && (int)pTarget->m_aFriends.size() < 100)
	{
		pTarget->m_aFriends.push_back(MyAID);
		pTarget->m_aFriendsDirty = true;
	}

	str_format(aBuf, sizeof(aBuf), "%s 添加你为好友。", pGS->Server()->ClientName(ClientID));
	SendChatToCross(pGS->Server(), TargetCID, aBuf);

	// Save immediately
	{
		CGameContext *pTargetGS = (CGameContext *)pGS->Server()->GameServer(pGS->Server()->GetClientWorldID(ClientID));
		if(pTargetGS && pTargetGS->Core() && pTargetGS->Core()->GetMMOManager())
		{
			pTargetGS->Core()->GetMMOManager()->SaveFriends(pPlayer);
			if(!TargetAlreadyHasUs)
				pTargetGS->Core()->GetMMOManager()->SaveFriends(pTarget);
		}
	}
	return true;
}

bool CGlobalState::FriendRemove(CGameContext *pGS, int ClientID, const char *pFriendName)
{
	if(!pGS) return false;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{ pGS->SendChatTo(ClientID, "请先登录。"); return false; }

	// Resolve friend's account ID
	int TargetCID = FindClientByName(pGS, pFriendName);
	if(TargetCID < 0)
	{ pGS->SendChatTo(ClientID, "未找到该玩家。"); return false; }

	CPlayer *pTarget = FindPlayerCrossWorld(pGS->Server(), TargetCID);
	if(!pTarget || pTarget->GetAccountId() <= 0)
	{ pGS->SendChatTo(ClientID, "该玩家未登录。"); return false; }

	int64 FriendAID = pTarget->GetAccountId();

	// Find and remove from our list
	bool Found = false;
	for(auto it = pPlayer->m_aFriends.begin(); it != pPlayer->m_aFriends.end(); ++it)
	{
		if(*it == FriendAID)
		{
			pPlayer->m_aFriends.erase(it);
			pPlayer->m_aFriendsDirty = true;
			Found = true;
			break;
		}
	}

	if(!Found)
	{ pGS->SendChatTo(ClientID, "该玩家不是你的好友。"); return false; }

	// Also remove from target's list (bidirectional)
	for(auto it = pTarget->m_aFriends.begin(); it != pTarget->m_aFriends.end(); ++it)
	{
		if(*it == pPlayer->GetAccountId())
		{
			pTarget->m_aFriends.erase(it);
			pTarget->m_aFriendsDirty = true;
			break;
		}
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s 已从好友列表中移除。", pGS->Server()->ClientName(TargetCID));
	pGS->SendChatTo(ClientID, aBuf);

	str_format(aBuf, sizeof(aBuf), "%s 已将你从好友列表中移除。", pGS->Server()->ClientName(ClientID));
	SendChatToCross(pGS->Server(), TargetCID, aBuf);

	// Save immediately
	{
		CGameContext *pTargetGS = (CGameContext *)pGS->Server()->GameServer(pGS->Server()->GetClientWorldID(ClientID));
		if(pTargetGS && pTargetGS->Core() && pTargetGS->Core()->GetMMOManager())
		{
			pTargetGS->Core()->GetMMOManager()->SaveFriends(pPlayer);
			pTargetGS->Core()->GetMMOManager()->SaveFriends(pTarget);
		}
	}
	return true;
}

void CGlobalState::FriendList(CGameContext *pGS, int ClientID)
{
	if(!pGS) return;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{ pGS->SendChatTo(ClientID, "请先登录。"); return; }

	if(pPlayer->m_aFriends.empty())
	{ pGS->SendChatTo(ClientID, "好友列表为空。使用 /friend add <名字> 添加好友。"); return; }

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══ 好友列表 (%d/100) ═══", (int)pPlayer->m_aFriends.size());
	pGS->SendChatTo(ClientID, aBuf);

	for(size_t i = 0; i < pPlayer->m_aFriends.size(); i++)
	{
		int64 FriendAID = pPlayer->m_aFriends[i];
		const char *pStatus = "离线";
		const char *pName = "未知";

		// Check if online
		auto it = ms_OnlineFriendsMap.find(FriendAID);
		if(it != ms_OnlineFriendsMap.end())
		{
			CPlayer *pF = FindPlayerCrossWorld(pGS->Server(), it->second);
			if(pF && pF->GetAccountId() == FriendAID)
			{
				pName = pGS->Server()->ClientName(it->second);
				pStatus = "在线";
			}
		}

		if(pStatus[0] == '在' && str_comp(pName, "未知") != 0)
			str_format(aBuf, sizeof(aBuf), "%d. %s [%s]", (int)i+1, pName, pStatus);
		else
			str_format(aBuf, sizeof(aBuf), "%d. [%s] (ID:%lld)", (int)i+1, pStatus, (long long)FriendAID);
		pGS->SendChatTo(ClientID, aBuf);
	}
}

// ══════════════════════════════════════════════════════════════════════
//  Whisper / World Chat
// ══════════════════════════════════════════════════════════════════════

void CGlobalState::Whisper(CGameContext *pGS, int ClientID, const char *pTarget, const char *pMsg)
{
	if(!pGS || !pTarget || !pMsg) return;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer) return;

	int TargetCID = FindClientByName(pGS, pTarget);
	if(TargetCID < 0)
	{ pGS->SendChatTo(ClientID, "未找到该玩家。"); return; }

	if(TargetCID == ClientID)
	{ pGS->SendChatTo(ClientID, "不能对自己私聊。"); return; }

	// SendChat handles CHAT_WHISPER by sending to both sender and recipient
	// Server() routes packets globally by client ID, so one call suffices
	pGS->SendChat(ClientID, CHAT_WHISPER, TargetCID, pMsg);
}

void CGlobalState::WorldChat(CGameContext *pGS, int ClientID, const char *pMsg)
{
	if(!pGS || !pMsg) return;
	CPlayer *pPlayer = FindPlayerCrossWorld(pGS->Server(), ClientID);
	if(!pPlayer) return;
	if(pPlayer->IsDummy()) return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "[世界] %s: %s", pGS->Server()->ClientName(ClientID), pMsg);

	// Broadcast to ALL clients across ALL worlds
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!pGS->Server()->ClientIngame(i))
			continue;
		int WID = pGS->Server()->GetClientWorldID(i);
		if(WID < 0) continue;
		CGameContext *pWorld = (CGameContext *)pGS->Server()->GameServer(WID);
		if(!pWorld) continue;
		pWorld->SendChatTo(i, aBuf);
	}
}

// ══════════════════════════════════════════════════════════════════════
//  Friend Command Callbacks
// ══════════════════════════════════════════════════════════════════════

static void ConFriendAdd(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::FriendAdd(pGame, pCtx->m_ClientID, pResult->GetString(0));
	(void)pResult;
}

static void ConFriendRemove(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::FriendRemove(pGame, pCtx->m_ClientID, pResult->GetString(0));
	(void)pResult;
}

static void ConFriendList(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::FriendList(pGame, pCtx->m_ClientID);
	(void)pResult;
}

static void ConWhisper(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::Whisper(pGame, pCtx->m_ClientID, pResult->GetString(0), pResult->GetString(1));
	(void)pResult;
}

static void ConWorldChat(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CGlobalState::WorldChat(pGame, pCtx->m_ClientID, pResult->GetString(0));
	(void)pResult;
}

void CGlobalState::RegisterGlobalFriendCommands(CGameContext *pGS, CCommandManager *pManager)
{
	if(!pGS || !pManager) return;

	// Friend commands
	pManager->AddCommand("friend_add", "添加好友 <玩家名>", "s", ConFriendAdd, pGS);
	pManager->AddCommand("friend_remove", "删除好友 <玩家名>", "s", ConFriendRemove, pGS);
	pManager->AddCommand("friend_list", "查看好友列表", "", ConFriendList, pGS);

	// Alias: /friend <subcommand> style
	pManager->AddCommand("friend", "好友系统 - 输入 /friend add/remove/list", "", ConFriendList, pGS);

	// Whisper commands
	pManager->AddCommand("w", "私聊 <玩家名> <消息>", "sr", ConWhisper, pGS);
	pManager->AddCommand("whisper", "私聊 <玩家名> <消息>", "sr", ConWhisper, pGS);

	// World chat commands
	pManager->AddCommand("world", "世界喊话 <消息>", "r", ConWorldChat, pGS);
	pManager->AddCommand("wc", "世界喊话快捷 <消息>", "r", ConWorldChat, pGS);
}
