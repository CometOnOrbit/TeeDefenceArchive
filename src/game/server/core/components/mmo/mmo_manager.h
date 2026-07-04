#ifndef GAME_SERVER_CORE_COMPONENTS_MMO_MMO_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_MMO_MMO_MANAGER_H

#include "mmo_item.h"
#include <game/server/core/tworld_component.h>
#include <game/server/core/tools/path_finder.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <base/tl/array.h>
#include <engine/console.h>
#include <ctime>
#include <vector>
#include <string>
#include <unordered_map>

class CPlayer;
class CGameContext;
class CCommandManager;

// ─── Constants ───────────────────────────────────────────────────────

static constexpr int MMO_MAX_GROUPS = 32;
static constexpr int MMO_GROUP_MAX_MEMBERS = 8;
static constexpr int MMO_INVITE_TIMEOUT_SECS = 30;
static constexpr int MMO_MAX_TRADES = 8;
static constexpr int MMO_TRADE_TIMEOUT_SECS = 60;

// ─── Group System (AccountID-based, MRPG-style) ──────────────────────

struct SMMOGroup
{
	int m_ID = -1;
	int64 m_LeaderAccountID = 0;
	int m_TeamColor = 0;
	std::vector<int64> m_vMemberAccountIDs;
	time_t m_CreatedAt = 0;

	SMMOGroup() = default;
	bool IsMember(int64 AccountID) const;
	bool IsLeader(int64 AccountID) const;
	int GetOnlineCount(CGameContext* pGS) const;
	int GetMemberCount() const { return (int)m_vMemberAccountIDs.size(); }
	bool IsFull() const { return GetMemberCount() >= MMO_GROUP_MAX_MEMBERS; }
	void Clear() { m_ID = -1; m_LeaderAccountID = 0; m_TeamColor = 0; m_vMemberAccountIDs.clear(); m_CreatedAt = 0; }
};

// ─── Trade System ────────────────────────────────────────────────────

struct SMMOTradeSlot
{
	int m_ItemID = MMO_INVALID_ITEM_ID;
	int m_Count = 0;
	int m_SlotIndex = 0; // index in originator's inventory
};

struct SMMOTrade
{
	int m_PartyA_CID = -1;
	int m_PartyB_CID = -1;
	SMMOTradeSlot m_aOfferA[16];
	int m_NumOfferA = 0;
	int m_GoldA = 0;
	bool m_ConfirmedA = false;
	SMMOTradeSlot m_aOfferB[16];
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

// ─── Level / Exp Tables ──────────────────────────────────────────────

static inline int MMOExpForLevel(int Level)
{
	if(Level <= 0) return 100;
	return Level * 80 + 50; // MRPG-style progressive cost
}

// ─── World Spawn Entry ────────────────────────────────────────────

struct SWorldSpawnEntry
{
	int m_DefID = -1;
	vec2 m_Pos = vec2(0, 0);
	int m_RespawnDelay = 0;  // ticks between respawns
	int m_RespawnTick = 0;   // next respawn tick
	bool m_Active = true;
	int m_SpawnedCID = -1;   // -1 if not spawned
	int m_MinDistFromPlayer = 200; // minimum distance from players to spawn
	char m_ZoneName[64] = {}; // which zone this spawn belongs to
};

// ─── CMMOManager ─────────────────────────────────────────────────────

class CMMOManager : public TWorldComponent
{
public:
	// Lifecycle
	void OnPreInit() override;
	void OnConsoleInit() override;
	void OnShutdown() override;
	void LoadMobDefinitions();
	void OnInitWorld(const char *pWhereLocalWorld) override;

	// ─── MMO Bot (Mob) System ──────────────────────────────────────────
	int  SpawnMob(int DefID, vec2 Pos);
	void TickMMOBot(CPlayer *pPlayer);
	void ConMMOSpawn(const char *pMobName, CPlayer *pPlayer);
	void OnPlayerLogin(CPlayer *pPlayer) override;
	void OnClientReset(int ClientID) override;
	void OnTick() override;

	// ─── Player Data (MRPG-style) ─────────────────────────────────────
	bool LoadPlayerData(CPlayer *pPlayer);
	bool SavePlayerData(CPlayer *pPlayer);
	bool LoadInventory(CPlayer *pPlayer);
	bool SaveInventory(CPlayer *pPlayer);

	// ─── Friend System ────────────────────────────────────────────────
	bool LoadFriends(CPlayer *pPlayer);
	bool SaveFriends(CPlayer *pPlayer);

	// ─── Daily Checkin ────────────────────────────────────────────────
	bool LoadCheckinData(CPlayer *pPlayer);
	bool SaveCheckinData(CPlayer *pPlayer);
	void ConCheckin(const char *pPlayerName, CPlayer *pPlayer);
	void RegisterCheckinCommands();
	void RegisterDebugCommands();

	// ─── Daily Recycle/Sell Limits ────────────────────────────────────
	bool LoadSellData(CPlayer *pPlayer);
	bool SaveSellData(CPlayer *pPlayer);
	void ResetDailySellIfNeeded(CPlayer *pPlayer);
	int CalcSellUnitPrice(const CMMOItemDescription *pDef) const;
	bool CanSellItem(CPlayer *pPlayer, int SlotIdx, const char **ppReason) const;
	bool TrySellItem(CPlayer *pPlayer, int SlotIdx, int Qty, int *pGoldOut, const char **ppReason);
	bool DropItemAtSlot(CPlayer *pPlayer, int SlotIdx, int Qty, const char **ppReason);
	bool SplitItemAtSlot(CPlayer *pPlayer, int SlotIdx, int SplitCount, const char **ppReason);

	// ─── Enchant System ────────────────────────────────────────────────
	void ConEnchant(int ClientID, int BagSlot);
	int GetEnchantCost(int CurrentEnchant);
	int GetEnchantSuccessRate(int CurrentEnchant);
	void RegisterEnchantCommands();

	// ─── Player Operations ────────────────────────────────────────────
	int  GetLevel(CPlayer *pPlayer);
	void AddExperience(CPlayer *pPlayer, int Amount);
	void AddGold(CPlayer *pPlayer, int Amount);
	bool SpendGold(CPlayer *pPlayer, int Amount);
	int  GetGold(CPlayer *pPlayer);

	// ─── Inventory Operations ─────────────────────────────────────────
	int  GetItemCount(CPlayer *pPlayer, int ItemID);
	bool GiveItem(CPlayer *pPlayer, int ItemID, int Count = 1, int Enchant = 0);
	bool GiveAllItems(CPlayer *pPlayer, int *pOutGiven = nullptr, int *pOutSkipped = nullptr);
	bool TakeItem(CPlayer *pPlayer, int ItemID, int Count = 1);
	bool HasItem(CPlayer *pPlayer, int ItemID, int Count = 1);

	// ─── Equipment System ────────────────────────────────────────────
	bool EquipWeapon(CPlayer *pPlayer, int ItemSlotIdx, int LoadoutSlot);
	bool UnequipWeapon(CPlayer *pPlayer);
	void ApplyEquippedWeapon(CPlayer *pPlayer);
	bool AssignWeaponLoadoutSlot(CPlayer *pPlayer, int ItemID, int LoadoutSlot);

	// ─── Mail System (MRPG-style) ──────────────────────────────────────
	int GetMailCount(int64 AccountID);
	int GetUnreadMailCount(int64 AccountID);
	void ShowMailboxVotes(int ClientID, class CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote);
	void ShowMailReadVotes(int ClientID, class CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote, int MailID);
	bool SendMail(const char *pSender, int64 TargetAID, const char *pTitle, const char *pDesc, const std::vector<std::pair<int,int>> &vItems);
	bool ClaimMailAttachments(CPlayer *pPlayer, int MailID);
	void DeleteMail(int MailID);
	void DeleteReadMails(int64 AccountID);

	// ─── Group System ─────────────────────────────────────────────────
	int  FindGroupByAccountID(int64 AccountID);
	int  GetPlayerGroupID(int ClientID);
	SMMOGroup* GetPlayerGroup(int ClientID);
	bool GroupCreate(int ClientID);
	bool GroupInvite(int LeaderCID, int TargetCID);
	bool GroupAccept(int ClientID);
	bool GroupLeave(int ClientID);
	bool GroupKick(int LeaderCID, int TargetCID);
	bool GroupDisband(int LeaderCID);
	bool SaveGroup(SMMOGroup &Group);
	void RemoveGroup(int GroupID);

	// ─── Group Vote Menu (MRPG-style) ─────────────────────────────────
	void ShowGroupVotes(int ClientID, class CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote);
	void ShowGroupKickVotes(int ClientID, class CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote);
	void RefreshGroupVotePage(int ClientID);

	// ─── Group Chat ───────────────────────────────────────────────────
	void GroupChat(int ClientID, const char *pMsg);
	void GroupChatTo(int ClientID, const char *pMsg);

	// ─── Trading System ───────────────────────────────────────────────
	bool TradeRequest(int FromCID, int ToCID);
	bool TradeAccept(int ClientID);
	bool TradeDecline(int ClientID);
	bool TradeAddItem(int ClientID, int ItemSlotIndex, int Count);
	bool TradeAddGold(int ClientID, int Amount);
	bool TradeConfirm(int ClientID);
	bool TradeCancel(int ClientID);

	// ─── Zone System ────────────────────────────────────────────────
	void LoadZoneDefs();
	void SpawnZoneMobs(int WorldID);
	std::unordered_map<int, std::vector<SMMOZoneDef>> m_WorldZones; // indexed by world

	// ─── Quest Mob Spawning ──────────────────────────────────────────
	int SpawnQuestMob(int DefID, vec2 Pos, int QuestID, int StepPos, int TargetClientID);
	void DespawnQuestMobs(int QuestID, int ClientID);

	// ─── World Mob Spawning ──────────────────────────────────────────
	void LoadWorldSpawns(int WorldID);
	void TickWorldSpawns();
	void LoadNPCBots();
	void SpawnNPCBots();
	void RegisterWorldSpawn(int WorldID, int DefID, vec2 Pos, int RespawnDelay = 0);
	bool LoadWorldSpawnsFromConfig(int WorldID);

	// ─── Vote Menu Integration ────────────────────────────────────────
	bool OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason) override;
	bool OnVoteMenuPage(int ClientID, int Page) override;
	void ShowMMOInventory(int ClientID);
	void ShowMMOItemDetail(int ClientID, int ItemIdx);
	// Called from vote_menu_manager.cpp's InitVotes switch (backward compat)
	void RenderMMOInventoryVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pVote);
	void RenderMMOItemVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pVote);
	void RenderMMOEquipVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pVote);
	void ShowAttributeVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void ShowMMOEquip(int ClientID);
	void RenderAuctionListVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderAuctionSellVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderAuctionSellPriceVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderShopListVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderShopItemsVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderEnchantSelectVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderFashionSelectVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderFriendsListVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderRankingLevelVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderRankingGoldVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderGuildBrowseVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderGuildDetailVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderGuildMembersVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderGuildRequestsVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderRecycleVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void RenderRecycleConfirmVotes(int ClientID, class CVoteMenuManager *pV, class CPlayer *pP, class SPlayerVote *pSVote);
	void ConShopBuy(int ClientID, const char *pNpcID, int ItemID);
	void ToggleMount(int ClientID);
	void TogglePet(int ClientID);
	bool EquipFashion(CPlayer *pPlayer, int BagSlot);

	CVoteMenuManager *GetVoteMenu() const;
	void OpenVotePage(int ClientID, int Page, int LastPage = -1);
	bool UnequipItemById(CPlayer *pPlayer, int ItemID);

	// ─── Command Registration ─────────────────────────────────────────
	void RegisterPartyCommands(CCommandManager *pManager);
	void RegisterMMOCommands(CCommandManager *pManager);
	void RegisterMMOVoteCommands(CCommandManager *pManager);
	void RegisterRankingCommands();

	// ─── Mount System ───────────────────────────────────────────────────
	void RegisterMountCommands();

	// ─── Auto Pathfinding Commands ────────────────────────────────────
	void RegisterAutoPathCommands();

	// ─── Fashion Commands ─────────────────────────────────────────
	void RegisterFashionCommands();

	// ─── Pet System ──────────────────────────────────────────────────
	bool LoadPetData(CPlayer *pPlayer);
	bool SavePetData(CPlayer *pPlayer);
	void RegisterPetCommands();

	// ─── Housing System ────────────────────────────────────────────────
	bool LoadHouseData(CPlayer *pPlayer);
	bool SaveHouseData(CPlayer *pPlayer);
	void RegisterHouseCommands();

	// ─── Marriage System ──────────────────────────────────────────────
	bool LoadMarriageData(CPlayer *pPlayer);
	bool SaveMarriageData(CPlayer *pPlayer);
	void RegisterMarriageCommands();

	// ─── Auction System ────────────────────────────────────────────────
	void ConAuctionList(int ClientID);
	void ConAuctionSell(int ClientID, int BagSlot, int Price);
	void ConAuctionBuy(int ClientID, int ListingID);
	void ConAuctionCancel(int ClientID, int ListingID);
	void RegisterAuctionCommands();

	// ─── Ranking System ────────────────────────────────────────────────
	void ConRanking(int ClientID, const char *pType);

	// Callbacks (static, for command registration)
	static void ConGroupList(IConsole::IResult *pResult, void *pUser);
	static void ConGroupCreate(IConsole::IResult *pResult, void *pUser);
	static void ConGroupInvite(IConsole::IResult *pResult, void *pUser);
	static void ConGroupAccept(IConsole::IResult *pResult, void *pUser);
	static void ConGroupLeave(IConsole::IResult *pResult, void *pUser);
	static void ConGroupKick(IConsole::IResult *pResult, void *pUser);
	static void ConGroupChat(IConsole::IResult *pResult, void *pUser);
	static void ConTradeStart(IConsole::IResult *pResult, void *pUser);
	static void ConTradeAccept(IConsole::IResult *pResult, void *pUser);
	static void ConTradeDecline(IConsole::IResult *pResult, void *pUser);
	static void ConTradeAddItem(IConsole::IResult *pResult, void *pUser);
	static void ConTradeAddGold(IConsole::IResult *pResult, void *pUser);
	static void ConTradeConfirm(IConsole::IResult *pResult, void *pUser);
	static void ConTradeCancel(IConsole::IResult *pResult, void *pUser);
	static void ConMMO(IConsole::IResult *pResult, void *pUser);
	static void ConMMOEquip(IConsole::IResult *pResult, void *pUser);
	static void ConMMOUnequip(IConsole::IResult *pResult, void *pUser);
	static void ConStats(IConsole::IResult *pResult, void *pUser);
	static void ConAddStat(IConsole::IResult *pResult, void *pUser);
	static void ConAddStatVote(IConsole::IResult *pResult, void *pUser);
	static void ConMMOSell(IConsole::IResult *pResult, void *pUser);
	static void ConMMOSkills(IConsole::IResult *pResult, void *pUser);
	static void ConMMOLearn(IConsole::IResult *pResult, void *pUser);
	static void ConShop(IConsole::IResult *pResult, void *pUser);
	static void ConUse(IConsole::IResult *pResult, void *pUser);
	static void ConBuy(IConsole::IResult *pResult, void *pUser);
	static void ConSell(IConsole::IResult *pResult, void *pUser);
	static void ConItemSlot(IConsole::IResult *pResult, void *pUser);
	static void ConStory(IConsole::IResult *pResult, void *pUser);

	// Emoticon → item quick-slot use (Phase 3)
	void UseItemByEmoticon(CPlayer *pPlayer, int EmoticonId);

private:
	// Helper: find a player by name
	int FindClientByName(const char *pName);

	// State
public: // TODO: make private with accessors
	// Groups
	SMMOGroup m_aGroups[MMO_MAX_GROUPS];
	int m_NumGroups = 0;

	// Pathfinding (shared by all MMO bots)
	CPathFinder *m_pPathFinder = nullptr;

	// Trades
	SMMOTrade m_aTrades[MMO_MAX_TRADES];
	int m_NumTrades = 0;

	// Active invitations (by parties, not global — tracked within each trade)

	// World spawn entries (WorldID → vector of spawn points)
	std::unordered_map<int, std::vector<SWorldSpawnEntry>> m_WorldSpawns;

};


#endif
