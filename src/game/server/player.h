/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

#include <base/system.h>
#include <base/vmath.h>

#include <generated/protocol.h>

#include <memory>
#include <map>
#include <vector>

#include "alloc.h"
#include "core/attribute_types.h"
#include "item_system.h"
#include "turret_ammo.h"
#include "core/components/inventory/equipped_slots.h"
#include "core/components/mmo/mmo_item.h"
#include "core/components/accounts/profession_data.h"
#include "core/tools/motd_menu.h"
class CTurret;
class CTurretPreview;

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

enum
{
	ZOMB_NAV_PATH_CAP = 384,
};

enum EZombType
{
	ZOMB_NONE = 0,
	ZOMB_ZABY = 1,
	ZOMB_ZOOMER,
	ZOMB_ZOOKER,
	ZOMB_ZAMER,
	ZOMB_ZUNNER,
	ZOMB_ZASTER,
	ZOMB_ZOTTER,
	ZOMB_ZENADE,
	ZOMB_FLOMBIE,
	ZOMB_ZINJA,
	ZOMB_ZELE,
	ZOMB_ZINVIS,
	ZOMB_ZEATER,
	ZOMB_ZSHIELD,
	ZOMB_ZHEALER,
	ZOMB_ZSPLITTER,
	ZOMB_SPIDER_BOSS,
	NUM_ZOMB_TYPES,
};

enum
{
	NUM_ZOMB_SUB = 3,
};

struct CTeeInfos
{
	char m_aaSkinPartNames[NUM_SKINPARTS][MAX_SKIN_ARRAY_SIZE];
	int m_aUseCustomColors[NUM_SKINPARTS];
	int m_aSkinPartColors[NUM_SKINPARTS];
};

// player object
class CPlayer
{
	friend class CCharacterBotAI;
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientID, bool Dummy, bool AsSpec = false);
	~CPlayer();

	void Init(int CID);

	void TryRespawn();
	void SpawnAt(vec2 Pos);
	void Respawn();
	void ForbidRespawn();
	bool IsEliminated() const { return m_RespawnDisabled; }
	void SetTeam(int Team, bool DoChatMsg = true);
	int GetTeam() const { return m_Team; }
	int GetCID() const { return m_ClientID; }
	bool IsDummy() const { return m_Dummy; }
	void SetDummy(bool Val) { m_Dummy = Val; }

	int64 GetAccountId() const { return m_AccountId; }
	void SetAccountId(int64 Id) { m_AccountId = Id; }
	void ResetAccData();
	void ClearAccount();

	void InitZombie(int Zomb);
	void InitQuestNpc(int DefIdx);
	bool IsQuestNpc() const { return m_QuestNpcDefIdx >= 0; }
	int GetQuestNpcDefIdx() const { return m_QuestNpcDefIdx; }
	float GetActiveDistance() const;
	bool IsSnappingInactiveForClient(int ClientID) const;
	bool IsVisibleForClient(int ClientID) const;
	int GetZomb() const { return m_Zomb; }
	int GetZombSub(int i) const { return (i >= 0 && i < NUM_ZOMB_SUB) ? m_aZombSub[i] : ZOMB_NONE; }
	void SetZombSub(int i, int Type);
	bool HasZombType(int Type) const;
	bool IsZombVisible() const { return m_ZombVisible; }
	void SetZombVisible(bool Visible) { m_ZombVisible = Visible; }
	bool PressTab() const;

	const STurretAmmoMix &GetTurretAmmoMix() const { return m_TurretAmmoMix; }
	STurretAmmoMix &GetTurretAmmoMix() { return m_TurretAmmoMix; }
	void SetTurretAmmoMatPct(int MatSlot, int Pct);

	const char *GetLanguage() const
	{
		if(m_aLanguage[0])
			return m_aLanguage;
		if(m_AccData.m_aLanguage[0])
			return m_AccData.m_aLanguage;
		return "zh-cn";
	}
	void SetLanguage(const char *pLang);

	int GetHolding(int ItemType) const { return m_AccData.m_Holding[ItemType]; }
	const char *GetExtra(int ItemType) const;
	const char *GetExtraForItem(int ItemID) const;

	SAccSyncData m_AccData;
	int m_aTurretAmmoDebt[NUM_TURRET_AMMO_MATS];

	void Tick();
	void PostTick();
	void Snap(int SnappingClient);
	void SnapPlayerInfoOnly(int SnappingClient, class CGameContext *pSnappingCtx);

	bool PendingChangeWorld();
	void ChangeWorld(int WorldID, vec2 *pPos = nullptr);
	int GetCurrentWorldID() const;

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect();

	// MRPG-style: F3/F4 vote result dispatch (dialog advance, etc.)
	bool ParseVoteOptionResult(int Vote);

	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();
	bool CreateTurret(vec2 Pos = vec2(0.0f, 0.0f));
	void DestroyTurret();
	CTurret *SyncDeployedTurretRef();
	bool HasDeployedTurret() { return SyncDeployedTurretRef() != nullptr; }
	CTurret *GetDeployedTurret() { return SyncDeployedTurretRef(); }
	bool RecallTurret();
	bool RepairDeployedTurret();
	bool BeginTurretPlace();
	void CancelTurretPlace();
	void UpdateTurretPlaceFromAim();
	bool IsTurretPlacing() const { return m_TurretPlacing; }
	bool IsTurretPlaceValid() const;
	vec2 GetTurretPlacePos() const { return m_TurretPlacePos; }
	bool ConfirmTurretPlace();

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// Previous input snapshot for key event detection (MRPG-style)
	CNetObj_PlayerInput *m_pLastInput;
	bool m_LastInputInit;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int GetSpectatorID() const { return m_SpectatorID; }
	bool SetSpectatorID(int SpecMode, int SpectatorID);
	bool m_DeadSpecMode;
	bool DeadCanFollow(CPlayer *pPlayer) const;
	void UpdateDeadSpecMode();

	bool m_IsReadyToEnter;
	bool m_IsReadyToPlay;

	bool m_RespawnDisabled;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCallTick;
	int m_LastVoteTryTick;
	int m_LastChatTeamTick;
	int m_LastSetTeamTick;
	int m_LastSetSpectatorModeTick;
	int m_LastChangeInfoTick;
	int m_LastEmoteTick;
	int m_LastKillTick;
	int m_LastReadyChangeTick;
	int m_LastDialogTick;

	// player skin
	CTeeInfos m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	int m_LastActionTick;
	int m_TeamChangeTick;
	int m_NextLoginHintTick;

	int m_InactivityTickCounter;

	int64 m_AccountId;
	int m_Zomb;
	int m_QuestNpcDefIdx;
	int m_aZombSub[NUM_ZOMB_SUB];
	bool m_ZombVisible;
	bool m_ZamerDetonating;
	int m_ZombAiLowSpeedTicks;
	int m_ZombAiJumpCooldown;
	int m_ZombAiLastMoveDir;
	int m_ZombAiHookCooldown;
	int m_ZombAiHumanScanTick;
	vec2 m_ZombAiCachedHumanPos;
	float m_ZombAiCachedHumanDist;
	bool m_ZombAiCachedHasHuman;
	int m_ZombAiCachedHumanCid;
	vec2 m_ZombAiPathGoal;
	bool m_ZombAiMcJumpTried;
	CNetObj_PlayerInput m_ZombAiLastInp;

	short m_aZombNavTx[ZOMB_NAV_PATH_CAP];
	short m_aZombNavTy[ZOMB_NAV_PATH_CAP];
	int m_ZombNavLen;
	int m_ZombNavIndex;
	int m_ZombNavNextRebuildTick;
	short m_ZombNavCachedGoalTX;
	short m_ZombNavCachedGoalTY;

	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

public:
	bool IsGuest() const { return m_IsGuest; }
	void SetGuest(bool Guest) { m_IsGuest = Guest; }

	// MMO mode data
	int m_MMOLevel;
	int m_MMOExp;
	int m_MMOGold;
	int m_MMOSkillPoints;
	int m_MMOReputation; // 声望/位阶 — 用于解锁首都等区域
	int m_MMOAttack;
	int m_MMODefense;
	bool m_MMODirty;
	CMMOInventory m_MMOInventory; // MRPG-style deque<CItem>
	EquippedSlots m_EquippedSlots;

	static constexpr int MMO_WEAPON_LOADOUT_SIZE = 4;
	int m_aMeleeLoadout[MMO_WEAPON_LOADOUT_SIZE];   // EquipHammer items (-1 = empty)
	int m_aRangedLoadout[MMO_WEAPON_LOADOUT_SIZE];  // 0=Gun, 1=Shotgun, 2=Grenade, 3=Laser
	int m_aWeaponBar[MMO_WEAPON_LOADOUT_SIZE];      // hotbar slots 1-4 (-1 = empty)

	void InitWeaponLoadouts();
	bool IsMMOWeaponEquipped(int ItemID) const;
	int FindMeleeLoadoutIndex(int ItemID) const;
	int FindRangedLoadoutIndex(int ItemID) const;
	int FindWeaponBarIndex(int ItemID) const;
	int FirstEmptyMeleeLoadout() const;
	int FirstEmptyWeaponBarSlot() const;
	int FirstNonEmptyMeleeLoadout() const;
	int FirstNonEmptyRangedLoadout() const;
	void RemoveWeaponFromLoadouts(int ItemID);
	void AutoFillWeaponBar(int ItemID);
	void MigrateWeaponLoadoutFromEquippedSlots();
	void EnsureWeaponBarFromLoadouts();
	static int RangedLoadoutIndexForItemType(ItemType Type);
	static bool IsMMOWeaponItemType(ItemType Type);

	// MMO skill tree
	struct SMMOSkillState
	{
		int m_SkillID;
		int m_Level;
	};
	SMMOSkillState m_aMMOSkills[8];
	int m_NumMMOSkills;

	void ApplyMMOSkillBonuses();
	int GetMMOSkillLevel(int SkillID) const;

	// Level & attribute methods
	void AddMMOExperience(int Amount);
	void RecalcMMOStats();

	// MRPG-style broadcast HUD: builds stats panel (HP/Level/Gold/Weapon/Skills)
	void FormatBroadcastBasicStats(char *pBuffer, int Size, const char *pAppendStr = nullptr);

	// Level-based stat scaling (MRPG) — profession-aware
	int GetMaxHealth() const
	{
		return GetBaseMaxHealth();
	}

	// MMO bot data (non-null = this player slot is an MMO mob)
	struct SMMOBotData *m_pMMOBotData = nullptr;
	struct SMMOQuestMobInfo *m_pQuestMobInfo = nullptr;

	// Profession
	EProfession m_Profession{PROF_NONE};

	// MRPG-style MOTD menu data (NPC dialog via MOTD)
	CMotdPlayerData m_MotdData;
	std::unique_ptr<MotdMenu> m_pMotdMenu;

	// Skill slots 3/4/5: skill IDs bound to keyboard slots (-1 = empty)
	int m_aSkillSlots[3];

	// Item quick slots: inventory slot indices bound to emoticon keys 0-3
	int m_aItemQuickSlots[4];

	// MRPG-style stat map
	std::map<AttributeIdentifier, int> m_aStats;

	// Helper methods
	int GetStat(AttributeIdentifier ID) const;
	void SetStat(AttributeIdentifier ID, int Value);
	void SyncStatsFromFields(); // Transitional: sync m_aStats from old fields

	// ── Story Flag System ─────────────────────────────────────
	// Generic key-value story flags, persisted as story_data string in DB.
	// Used via dialog FLAG/SET_FLAG conditions and chat commands.
	std::map<std::string, int> m_StoryFlags;

	// Convenience: get/set a story flag (0 = false/unset)
	int GetStoryFlag(const char *pKey) const;
	void SetStoryFlag(const char *pKey, int Value);
	bool HasStoryFlag(const char *pKey) const { return GetStoryFlag(pKey) != 0; }

	// TRPG六维 → 有效战斗属性
	int GetEffectiveMeleeAttack() const;   // STR * 2
	int GetEffectiveRangedAttack() const;  // DEX * 2
	int GetEffectiveDefense() const;       // CON * 2
	int GetMaxMana() const;                // 50 + INT * 5 + Level * 3

	int GetMMOItemEnchant(int ItemID) const;
	int GetMMOEquippedAttributeSum(AttributeIdentifier ID) const;
	int GetMMOMaxAmmo() const;
	int GetMMOAmmoRegenPercent() const;
	bool UsesMMOFiniteAmmo() const;

	// Guild
	int m_GuildID{-1};
	int m_MatchTeam{0};       // 0=uninvolved, -1=challenger, 1=challenged (in match)
	int m_OriginalWorld{0};   // world index before match teleport

	int GetGuildID() const { return m_GuildID; }

	EProfession GetProfession() const { return m_Profession; }
	void SetProfession(EProfession Prof) { m_Profession = Prof; }
	int GetBaseMaxHealth() const;
	float GetBaseAttack() const;
	float GetBaseDefense() const;

	// Defence → RPG reward bridge
	int m_DefencePendingExp = 0;

	// Group invite tracking (MRPG-style)
	int m_GroupInviteGroupID = 0;
	time_t m_GroupInviteExpire = 0;

	// Friend list (Tier 1 social)
	std::vector<int64> m_aFriends;
	bool m_aFriendsDirty = false;

	// Daily checkin
	int m_CheckinStreak = 0;
	int m_LastCheckinDate = 0;  // YYYYMMDD format

	// MMO inventory filters (MRPG-style tabs)
	int m_InventoryFilterGroup = -1; // (int)ItemGroup, -1 = none selected
	int m_InventoryFilterType = -1;  // (int)ItemType, -1 = all types in group

	// Daily item recycle/sell limits (anti-inflation)
	int m_LastSellDate = 0;     // YYYYMMDD
	int m_DailySellGold = 0;     // gold earned from NPC recycle today
	int m_DailySellCount = 0;    // recycle transactions today

		// ── Pet System ────────────────────────────────────────────
	int m_PetID = 0;              // 0 = 无宠物
	char m_aPetName[32] = {0};    // 宠物名字
	int m_PetLevel = 1;           // 宠物等级
	class CPet *m_pPet = nullptr; // 当前召唤的宠物实体

	// ── Auto Pathfinding ───────────────────────────────────────
	bool m_AutoMoving = false;
	int m_AutoTargetX = 0;
	int m_AutoTargetY = 0;
	int m_FollowTargetCID = -1;

	// ── Fashion / Appearance System ─────────────────────────
	int m_FashionItemID = 0;        // 0 = 无时装

	// ── Mount System ────────────────────────────────────────
	bool m_IsMounted = false;      // 是否骑乘
	int m_MountSpeedBonus = 50;     // 移速加成百分比
	int m_MountSkin = 0;            // 坐骑外观ID (0=默认)

	// ── Housing System ───────────────────────────────────────
	bool m_HasHouse = false;
	int m_HouseLevel = 1;

	// ── Marriage System ──────────────────────────────────────
	int64 m_SpouseAccountID = 0;  // 配偶的 AccountID，0=单身
	int m_MarriageDate = 0;       // 结婚日期 YYYYMMDD

	// ── World Boss System ────────────────────────────────────────
	bool m_IsWorldBoss = false;  // 该玩家是世界 Boss（由 CWorldBossManager 控制）

private:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	bool m_IsGuest;

	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;
	bool m_Dummy;

	CTurret *m_pTurret;
	CTurretPreview *m_pTurretPreview;
	bool m_TurretPlacing;
	bool m_TurretPlaceLastFire;
	int m_TurretPlaceFailMsgTick;
	vec2 m_TurretPlacePos;
	STurretAmmoMix m_TurretAmmoMix;
	char m_aLanguage[16];

	// used for spectator mode
	int m_SpecMode;
	int m_SpectatorID;
	bool m_ActiveSpecSwitch;

	int m_PendingChangeWorldID;
	bool m_HasPendingChangeWorldPos;
	vec2 m_PendingChangeWorldPos;
};

#endif
