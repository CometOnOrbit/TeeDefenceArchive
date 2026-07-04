#ifndef GAME_SERVER_GLOBAL_STATE_H
#define GAME_SERVER_GLOBAL_STATE_H

#include <base/system.h>
#include <ctime>
#include <vector>
#include <map>
#include <unordered_map>

class CGameContext;
class CPlayer;
class CCommandManager;
class IServer;

// ─── Constants ───────────────────────────────────────────────────────

static constexpr int GLOBAL_MAX_GROUPS = 32;
static constexpr int GLOBAL_GROUP_MAX_MEMBERS = 8;
static constexpr int GLOBAL_INVITE_TIMEOUT_SECS = 30;
static constexpr int GLOBAL_MAX_TRADES = 8;
static constexpr int GLOBAL_TRADE_TIMEOUT_SECS = 60;
static constexpr int GLOBAL_TRADE_SLOTS = 16;

// ─── Group Data ──────────────────────────────────────────────────────

struct SGlobalGroup
{
	int m_ID = -1;
	int64 m_LeaderAccountID = 0;
	int m_TeamColor = 0;
	std::vector<int64> m_vMemberAccountIDs;
	time_t m_CreatedAt = 0;
	
	bool IsMember(int64 AccountID) const;
	bool IsLeader(int64 AccountID) const;
	int GetMemberCount() const { return (int)m_vMemberAccountIDs.size(); }
	bool IsFull() const { return GetMemberCount() >= GLOBAL_GROUP_MAX_MEMBERS; }
	void Clear() { m_ID = -1; m_LeaderAccountID = 0; m_TeamColor = 0; m_vMemberAccountIDs.clear(); m_CreatedAt = 0; }
};

// ─── Trade Data ──────────────────────────────────────────────────────

struct SGlobalTradeSlot
{
	int m_ItemID = 0;
	int m_Count = 0;
	int m_SlotIndex = 0;
};

struct SGlobalTrade
{
	int m_PartyA_CID = -1;
	int m_PartyB_CID = -1;
	SGlobalTradeSlot m_aOfferA[GLOBAL_TRADE_SLOTS];
	int m_NumOfferA = 0;
	int m_GoldA = 0;
	bool m_ConfirmedA = false;
	SGlobalTradeSlot m_aOfferB[GLOBAL_TRADE_SLOTS];
	int m_NumOfferB = 0;
	int m_GoldB = 0;
	bool m_ConfirmedB = false;
	time_t m_ExpiresAt = 0;
	bool m_Active = false;

	void Clear()
	{
		m_PartyA_CID = m_PartyB_CID = -1;
		m_NumOfferA = m_NumOfferB = 0;
		m_GoldA = m_GoldB = 0;
		m_ConfirmedA = m_ConfirmedB = false;
		m_ExpiresAt = 0;
		m_Active = false;
	}
};

// ─── CGlobalState ────────────────────────────────────────────────────
// Cross-world runtime state. All worlds share this.
// Methods take CGameContext* for player lookup across all worlds.

class CGlobalState
{
public:
	static void Init();

	// ─── Group ─────────────────────────────────────────────────────
	static int  FindGroupByAccountID(int64 AccountID);
	static int  GetPlayerGroupID(IServer *pServer, int ClientID);
	static SGlobalGroup* GetPlayerGroup(IServer *pServer, int ClientID);
	static bool GroupCreate(CGameContext *pGS, int ClientID);
	static bool GroupInvite(CGameContext *pGS, int LeaderCID, int TargetCID);
	static bool GroupAccept(CGameContext *pGS, int ClientID);
	static bool GroupLeave(CGameContext *pGS, int ClientID);
	static bool GroupKick(CGameContext *pGS, int LeaderCID, int TargetCID);
	static bool GroupDisband(CGameContext *pGS, int LeaderCID);
	static bool SaveGroup(SGlobalGroup &Group, CGameContext *pGS);
	static void RemoveGroup(int GroupIdx);
	static void GroupChat(CGameContext *pGS, int ClientID, const char *pMsg);
	static int  FindClientByName(CGameContext *pGS, const char *pName);

	// ─── Trade ─────────────────────────────────────────────────────
	static bool TradeRequest(CGameContext *pGS, int FromCID, int ToCID);
	static bool TradeAccept(CGameContext *pGS, int ClientID);
	static bool TradeDecline(CGameContext *pGS, int ClientID);
	static bool TradeAddItem(CGameContext *pGS, int ClientID, int ItemSlotIndex, int Count);
	static bool TradeAddGold(CGameContext *pGS, int ClientID, int Amount);
	static bool TradeConfirm(CGameContext *pGS, int ClientID);
	static bool TradeCancel(CGameContext *pGS, int ClientID);

	// ─── Friend System ─────────────────────────────────────────────
	// Global online tracking: AccountID → ClientID
	static std::map<int64, int> ms_OnlineFriendsMap;

	static void NotifyFriendsOnline(CGameContext *pGS, CPlayer *pPlayer);
	static void NotifyFriendsOffline(CGameContext *pGS, CPlayer *pPlayer);
	static bool FriendAdd(CGameContext *pGS, int ClientID, const char *pFriendName);
	static bool FriendRemove(CGameContext *pGS, int ClientID, const char *pFriendName);
	static void FriendList(CGameContext *pGS, int ClientID);

	// ─── Whisper / World Chat ─────────────────────────────────────
	static void Whisper(CGameContext *pGS, int ClientID, const char *pTarget, const char *pMsg);
	static void WorldChat(CGameContext *pGS, int ClientID, const char *pMsg);

	// ─── Command Registration (registered in ALL worlds) ─────────
	static void RegisterGlobalGroupCommands(CGameContext *pGS, CCommandManager *pManager);
	static void RegisterGlobalTradeCommands(CGameContext *pGS, CCommandManager *pManager);
	static void RegisterGlobalFriendCommands(CGameContext *pGS, CCommandManager *pManager);

	// ─── Data Access (for iterating in vote menus, etc.) ───────
	static SGlobalGroup* GetGroupArray() { return ms_aGroups; }
	static SGlobalTrade* GetTradeArray() { return ms_aTrades; }
	static int GetGroupCount() { return ms_NumGroups; }
	static int GetTradeCount() { return ms_NumTrades; }

private:
	static SGlobalGroup ms_aGroups[GLOBAL_MAX_GROUPS];
	static int ms_NumGroups;
	static SGlobalTrade ms_aTrades[GLOBAL_MAX_TRADES];
	static int ms_NumTrades;
};

#endif // GAME_SERVER_GLOBAL_STATE_H
