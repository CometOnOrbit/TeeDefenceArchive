#include "mmo_manager.h"

#include <game/server/data_center.h>
#include <game/server/gamecontext.h>
#include <game/server/mmo_exp.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/mmo_context.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_wrapper.h>
#include <game/server/global_state.h>
#include <game/server/core/components/economy/shop_data.h>
#include <game/server/core/components/npcs/npc_service.h>
#include <game/server/core/components/guilds/guild_manager.h>
#include <game/server/core/components/guilds/guild_data.h>
#include <game/server/core/components/guilds/guild_match_mode.h>
#include <game/server/core/components/guilds/guild_arena_maps.h>
#include <game/server/core/components/mmo/mmo_world_boss.h>
#include <game/server/account.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_query.h>
#include <game/server/entities/vehicle/aircraft.h>
#include <game/server/entities/vehicle/vehicle.h>
#include <game/server/entities/vehicle/vehicle_util.h>
#include <mysql.h>
#include <game/server/gameworld.h>
#include <game/server/interaction_sound.h>
#include <generated/server_data.h>
#include <game/commands.h>
#include <set>
#include <vector>

static const char *LocMMOItemName(CGameContext *pGS, int ClientID, const CMMOItemDescription *pDef)
{
	if(!pDef)
		return "?";
	return pGS->Loc(ClientID, pDef->GetNameKey(), pDef->GetName());
}

static bool IsEnchantableBagItem(const CItem &Item)
{
	if(!Item.IsValid())
		return false;
	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
	if(!pDesc)
		return false;
	return MMOItemTypeToWeapon(pDesc->GetType()) != -1;
}

static const char *VoteArgText(const char *pArgs, const char *pReason)
{
	if(pArgs && pArgs[0])
		return pArgs;
	if(pReason && pReason[0])
		return pReason;
	return nullptr;
}

static int VoteArgInt(const char *pArgs, const char *pReason, int Fallback = 0)
{
	if(pArgs && pArgs[0])
		return str_toint(pArgs);
	if(pReason && pReason[0])
		return str_toint(pReason);
	return Fallback;
}

static void TruncateUtf8(char *pBuf, int MaxBytes)
{
	if(MaxBytes <= 0)
	{
		pBuf[0] = 0;
		return;
	}
	if((int)str_length(pBuf) <= MaxBytes)
		return;
	pBuf[MaxBytes] = 0;
	while(MaxBytes > 0 && (pBuf[MaxBytes - 1] & 0xC0) == 0x80)
	{
		pBuf[MaxBytes - 1] = 0;
		MaxBytes--;
	}
}

static CVoteWrapper MMOPage(int ClientID, CGameContext *pGS, CVoteMenuManager *pVote, int LastPage, const char *pTitle)
{
	pVote->SetVoteLastPage(LastPage);
	pVote->SetVoteBuildClientID(ClientID);
	CVoteWrapper V(ClientID, pGS, pVote);
	V.GroupTitle(pTitle);
	return V;
}

static const char *LocVote(CGameContext *pGS, int ClientID, const char *pKey, const char *pFallback)
{
	return pGS->Loc(ClientID, pKey, pFallback);
}

static int CountMMOItemsByGroup(CPlayer *pP, ItemGroup Group, ItemType TypeFilter, bool AnyType)
{
	if(!pP)
		return 0;
	int Count = 0;
	for(const CItem &Item : pP->m_MMOInventory)
	{
		if(!Item.IsValid())
			continue;
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
		if(!pDef || pDef->GetGroup() != Group)
			continue;
		if(!AnyType && pDef->GetType() != TypeFilter)
			continue;
		Count++;
	}
	return Count;
}

// ─── Vote UI helpers ─────────────────────────────────────────────────

static int VoteTodayYYYYMMDD()
{
	const time_t Now = time(nullptr);
	const struct tm *pTm = localtime(&Now);
	if(!pTm)
		return 0;
	return (1900 + pTm->tm_year) * 10000 + (pTm->tm_mon + 1) * 100 + pTm->tm_mday;
}

static void VoteFormatDateYYYYMMDD(int Date, char *pBuf, int BufSize)
{
	if(!pBuf || BufSize <= 0)
		return;
	if(Date <= 0)
	{
		str_copy(pBuf, "—", BufSize);
		return;
	}
	str_format(pBuf, BufSize, "%04d-%02d-%02d", Date / 10000, (Date / 100) % 100, Date % 100);
}

static void VoteAppendWallet(CVoteWrapper &V, CPlayer *pP)
{
	if(!pP)
		return;
	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), "💰 金币 %d", pP->GetStat(AttributeIdentifier::Gold));
	V.Info(aLine);
}

static void VoteAppendLevelLine(CVoteWrapper &V, CPlayer *pP)
{
	if(!pP)
		return;
	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), "Lv.%d  经验 %d",
		pP->GetStat(AttributeIdentifier::Level),
		pP->GetStat(AttributeIdentifier::Experience));
	V.Info(aLine);
}

static void VoteAppendBossStatus(CVoteWrapper &V, CGameContext *pGS, int ClientID)
{
	if(!pGS || !pGS->Core())
		return;
	CWorldBossManager *pWB = pGS->Core()->GetWorldBossManager();
	if(!pWB)
	{
		V.Info("世界 Boss 系统未启用");
		return;
	}
	char aLine[VOTE_DESC_LENGTH];
	const char *pName = pWB->GetBossDisplayName(ClientID);
	if(pWB->IsBossAlive())
	{
		const int HpPct = pWB->GetBossMaxHP() > 0 ? pWB->GetBossHP() * 100 / pWB->GetBossMaxHP() : 0;
		str_format(aLine, sizeof(aLine), "🐉 %s 存活  HP %d/%d (%d%%)",
			pName, pWB->GetBossHP(), pWB->GetBossMaxHP(), HpPct);
	}
	else
	{
		const int Sec = pGS->Server() ? pWB->GetNextSpawnInTicks() / maximum(1, pGS->Server()->TickSpeed()) : 0;
		if(Sec > 0)
			str_format(aLine, sizeof(aLine), "🐉 %s 未出现  约 %d 秒后刷新", pName, Sec);
		else
			str_format(aLine, sizeof(aLine), "🐉 %s 即将降临…", pName);
	}
	V.Info(aLine);
}

static int VoteYesterdayYYYYMMDD()
{
	const time_t Now = time(nullptr) - 86400;
	const struct tm *pTm = localtime(&Now);
	if(!pTm)
		return 0;
	return (1900 + pTm->tm_year) * 10000 + (pTm->tm_mon + 1) * 100 + pTm->tm_mday;
}

static void VoteAppendGuildWarStatus(CVoteWrapper &V, CGameContext *pGS, int ClientID)
{
	CGuildManager *pMgr = pGS && pGS->Core() ? pGS->Core()->GuildManager() : nullptr;
	if(!pMgr)
		return;
	char aMode[16];
	char aMap[128];
	int Status = -1;
	if(!pMgr->GetActiveWarMatchInfo(ClientID, aMode, sizeof(aMode), aMap, sizeof(aMap), &Status))
	{
		V.Info("当前无进行中的公会战");
		return;
	}
	static const char *apStatus[] = {"待接受", "选模式中", "准备中", "进行中", "已结束", "已取消"};
	char aLine[VOTE_DESC_LENGTH];
	const char *pSt = (Status >= 0 && Status < 6) ? apStatus[Status] : "?";
	str_format(aLine, sizeof(aLine), "状态：%s", pSt);
	V.Info(aLine);
	if(aMode[0])
	{
		str_format(aLine, sizeof(aLine), "模式：%s", GuildWarModeDisplayName(aMode));
		V.Info(aLine);
	}
	if(aMap[0])
	{
		str_format(aLine, sizeof(aLine), "地图：%s", aMap);
		V.Info(aLine);
	}
}

static bool ItemMatchesInventoryFilter(const CItem &Item, CPlayer *pP)
{
	if(!pP || pP->m_InventoryFilterGroup < 0)
		return false;
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	if(!pDef)
		return false;
	if((int)pDef->GetGroup() != pP->m_InventoryFilterGroup)
		return false;
	if(pP->m_InventoryFilterType >= 0 && (int)pDef->GetType() != pP->m_InventoryFilterType)
		return false;
	return true;
}

static void AppendInventoryFilterTabs(CVoteWrapper &V, CGameContext *pGS, int ClientID, CPlayer *pP)
{
	static const ItemGroup s_aGroups[] = {
		ItemGroup::Usable, ItemGroup::Material, ItemGroup::Equipment,
		ItemGroup::Potion, ItemGroup::Quest, ItemGroup::Other,
	};
	static const char *s_apIcons[] = {"✌", "⚒", "⚰", "⚱", "☁", "☃"};

	V.GroupLine();
	V.GroupTitle(LocVote(pGS, ClientID, "mmo.inv.tabs", "背包分类"));

	char aCmd[48];
	char aLine[VOTE_DESC_LENGTH];
	for(size_t g = 0; g < sizeof(s_aGroups) / sizeof(s_aGroups[0]); g++)
	{
		const ItemGroup Group = s_aGroups[g];
		const int Count = CountMMOItemsByGroup(pP, Group, ItemType::Default, true);
		const bool Selected = pP->m_InventoryFilterGroup == (int)Group;
		str_format(aCmd, sizeof(aCmd), "ccv_inv_filter %d", (int)Group);
		str_format(aLine, sizeof(aLine), "%s %s (%d)%s",
			s_apIcons[g], ItemGroupToName(Group), Count, Selected ? " ✓" : "");
		V.Option(aCmd, aLine);
	}

	if(pP->m_InventoryFilterGroup < 0)
	{
		V.Info(LocVote(pGS, ClientID, "mmo.inv.pick_tab", "请选择上方分类标签"));
		return;
	}

	std::set<ItemType> vTypes;
	for(const CItem &Item : pP->m_MMOInventory)
	{
		if(!Item.IsValid())
			continue;
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
		if(!pDef || (int)pDef->GetGroup() != pP->m_InventoryFilterGroup)
			continue;
		vTypes.insert(pDef->GetType());
	}

	if(vTypes.size() > 1)
	{
		V.GroupLine();
		const bool AllSelected = pP->m_InventoryFilterType < 0;
		str_format(aCmd, sizeof(aCmd), "ccv_inv_filter %d", pP->m_InventoryFilterGroup);
		str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.inv.all_types", "全部 (%d)"), CountMMOItemsByGroup(pP, (ItemGroup)pP->m_InventoryFilterGroup, ItemType::Default, true));
		if(AllSelected)
			str_append(aLine, " ✓", sizeof(aLine));
		V.Option(aCmd, aLine);

		for(ItemType Type : vTypes)
		{
			const bool TypeSelected = pP->m_InventoryFilterType == (int)Type;
			const int TypeCount = CountMMOItemsByGroup(pP, (ItemGroup)pP->m_InventoryFilterGroup, Type, false);
			const char *pTypeName = Type == ItemType::Default ? ItemGroupToName((ItemGroup)pP->m_InventoryFilterGroup) : ItemTypeToName(Type);
			if(!pTypeName[0])
				pTypeName = LocVote(pGS, ClientID, "mmo.inv.misc", "杂项");
			str_format(aCmd, sizeof(aCmd), "ccv_inv_filter %d %d", pP->m_InventoryFilterGroup, (int)Type);
			str_format(aLine, sizeof(aLine), "%s (%d)%s", pTypeName, TypeCount, TypeSelected ? " ✓" : "");
			V.Option(aCmd, aLine);
		}
	}
}

CVoteMenuManager *CMMOManager::GetVoteMenu() const
{
	return Core() ? Core()->VoteMenuManager() : nullptr;
}

void CMMOManager::OpenVotePage(int ClientID, int Page, int LastPage)
{
	CVoteMenuManager *pVote = GetVoteMenu();
	if(!pVote)
		return;
	SPlayerVote *pV = pVote->GetPlayerVote(ClientID);
	if(LastPage >= 0)
		pV->m_LastPage = LastPage;
	else if(pV->m_Page != Page)
		pV->m_LastPage = pV->m_Page ? pV->m_Page : PAGE_MENU;
	pV->m_Page = Page;
	pVote->ClearVotes(ClientID);
}

bool CMMOManager::UnequipItemById(CPlayer *pPlayer, int ItemID)
{
	if(!pPlayer || ItemID <= 0)
		return false;

	bool Any = false;
	if(pPlayer->IsMMOWeaponEquipped(ItemID))
	{
		pPlayer->RemoveWeaponFromLoadouts(ItemID);
		Any = true;
	}

	for(const auto &Slot : pPlayer->m_EquippedSlots.getSlots())
	{
		if(Slot.second == ItemID)
		{
			pPlayer->m_EquippedSlots.unequipSlot(Slot.first);
			Any = true;
		}
	}

	if(!Any)
	{
		GS()->SendChatTo(pPlayer->GetCID(), "该物品未装备。");
		return false;
	}

	pPlayer->m_MMODirty = true;
	pPlayer->RecalcMMOStats();
	ApplyEquippedWeapon(pPlayer);
	SaveInventory(pPlayer);
	SavePlayerData(pPlayer);
	GS()->SendChatTo(pPlayer->GetCID(), "已卸下装备。");
	return true;
}

bool CMMOManager::EquipWeapon(CPlayer *pPlayer, int ItemSlotIdx, int LoadoutSlot)
{
	if(!pPlayer || ItemSlotIdx < 0 || (size_t)ItemSlotIdx >= pPlayer->m_MMOInventory.size())
	{
		if(pPlayer)
			GS()->SendChatTo(pPlayer->GetCID(), "背包格无效。");
		return false;
	}

	const CItem &Item = pPlayer->m_MMOInventory[ItemSlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	if(!pDef || !pDef->IsEquipmentSlot())
	{
		GS()->SendChatTo(pPlayer->GetCID(), "该物品无法装备。");
		return false;
	}

	if(pPlayer->GetStat(AttributeIdentifier::Level) < pDef->GetLevelReq())
	{
		GS()->SendChatTo(pPlayer->GetCID(), "等级不足，无法装备。");
		return false;
	}

	const ItemType Type = pDef->GetType();
	const int ItemID = Item.GetID();

	if(CPlayer::IsMMOWeaponItemType(Type))
	{
		if(LoadoutSlot < 0 || LoadoutSlot >= CPlayer::MMO_WEAPON_LOADOUT_SIZE)
		{
			GS()->SendChatTo(pPlayer->GetCID(), "请选择栏位 1-4。");
			return false;
		}

		if(pPlayer->IsMMOWeaponEquipped(ItemID))
		{
			GS()->SendChatTo(pPlayer->GetCID(), "该武器已装备。");
			return false;
		}

		if(Type == ItemType::EquipHammer)
		{
			const int OldID = pPlayer->m_aMeleeLoadout[LoadoutSlot];
			if(OldID > 0 && OldID != ItemID)
				pPlayer->RemoveWeaponFromLoadouts(OldID);
			pPlayer->m_aMeleeLoadout[LoadoutSlot] = ItemID;
		}
		else
		{
			const int OldID = pPlayer->m_aRangedLoadout[LoadoutSlot];
			if(OldID > 0 && OldID != ItemID)
				pPlayer->RemoveWeaponFromLoadouts(OldID);
			pPlayer->m_aRangedLoadout[LoadoutSlot] = ItemID;
			pPlayer->m_EquippedSlots.equipSlot(Type, ItemID);
		}
	}
	else
	{
		pPlayer->m_EquippedSlots.equipSlot(Type, ItemID);
	}

	pPlayer->m_MMODirty = true;
	pPlayer->RecalcMMOStats();
	ApplyEquippedWeapon(pPlayer);
	SaveInventory(pPlayer);
	SavePlayerData(pPlayer);

	char aBuf[128];
	if(CPlayer::IsMMOWeaponItemType(Type))
		str_format(aBuf, sizeof(aBuf), "已装备到栏位 %d：%s", LoadoutSlot + 1, LocMMOItemName(GS(), pPlayer->GetCID(), pDef));
	else
		str_format(aBuf, sizeof(aBuf), "已装备：%s", LocMMOItemName(GS(), pPlayer->GetCID(), pDef));
	GS()->SendChatTo(pPlayer->GetCID(), aBuf);
	PlayInteractionSound(GS()->m_World, pPlayer, SOUND_SFX_ITEM_EQUIP);
	return true;
}

bool CMMOManager::UnequipWeapon(CPlayer *pPlayer)
{
	if(!pPlayer)
		return false;

	static const ItemType s_aOptional[] = {
		ItemType::EquipGun, ItemType::EquipShotgun, ItemType::EquipGrenade, ItemType::EquipLaser,
		ItemType::EquipHelmetTank, ItemType::EquipHelmetDPS, ItemType::EquipHelmetHealer,
		ItemType::EquipArmorTank, ItemType::EquipArmorDPS, ItemType::EquipArmorHealer,
		ItemType::EquipGloves, ItemType::EquipEidolon, ItemType::EquipPickaxe, ItemType::EquipRake, ItemType::EquipFishrod,
	};

	bool Any = false;
	for(ItemType Type : s_aOptional)
	{
		if(!pPlayer->m_EquippedSlots.isEquipped(Type))
			continue;

		const int ItemID = pPlayer->m_EquippedSlots.getSlot(Type);
		if(ItemID > 0 && pPlayer->IsMMOWeaponEquipped(ItemID))
			pPlayer->RemoveWeaponFromLoadouts(ItemID);
		else
			pPlayer->m_EquippedSlots.unequipSlot(Type);
		Any = true;
	}

	for(int i = 0; i < CPlayer::MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(pPlayer->m_aMeleeLoadout[i] > 0)
		{
			pPlayer->RemoveWeaponFromLoadouts(pPlayer->m_aMeleeLoadout[i]);
			Any = true;
		}
		if(pPlayer->m_aRangedLoadout[i] > 0)
		{
			pPlayer->RemoveWeaponFromLoadouts(pPlayer->m_aRangedLoadout[i]);
			Any = true;
		}
	}

	if(!Any)
	{
		GS()->SendChatTo(pPlayer->GetCID(), "没有可卸下的装备。");
		return false;
	}

	pPlayer->m_MMODirty = true;
	pPlayer->RecalcMMOStats();
	ApplyEquippedWeapon(pPlayer);
	SaveInventory(pPlayer);
	SavePlayerData(pPlayer);
	GS()->SendChatTo(pPlayer->GetCID(), "已卸下装备。");
	return true;
}

void CMMOManager::ApplyEquippedWeapon(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	CCharacter *pChr = pPlayer->GetCharacter();
	if(!pChr)
		return;

	for(int W = WEAPON_GUN; W <= WEAPON_LASER; W++)
		pChr->RemoveWeapon(W);

	pChr->GiveWeapon(WEAPON_HAMMER, -1);

	static const ItemType s_aRangedTypes[] = {
		ItemType::EquipGun, ItemType::EquipShotgun, ItemType::EquipGrenade, ItemType::EquipLaser,
	};
	const int MaxAmmo = pPlayer->UsesMMOFiniteAmmo() ? pPlayer->GetMMOMaxAmmo() : -1;
	for(int i = 0; i < CPlayer::MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		if(pPlayer->m_aRangedLoadout[i] <= 0)
			continue;
		const int WeaponID = MMOItemTypeToWeapon(s_aRangedTypes[i]);
		if(WeaponID >= 0)
			pChr->GiveWeapon(WeaponID, MaxAmmo);
	}

	if(MaxAmmo > 0)
		pChr->SyncMMOWeaponAmmo(MaxAmmo);

	int MeleeIdx = pPlayer->FirstNonEmptyMeleeLoadout();
	if(MeleeIdx >= 0 && pChr->TryActivateLoadoutIdx(CCharacter::WEAPONCAT_MELEE, MeleeIdx))
	{
		pChr->DoWeaponSwitch();
	}
	else
	{
		pChr->m_ActiveWeaponItemID = -1;
		pChr->m_ActiveCategory = CCharacter::WEAPONCAT_MELEE;
		pChr->SetWeapon(WEAPON_HAMMER);
	}

	if(!pPlayer->IsDummy())
		GS()->FlushBroadcastStats(pPlayer->GetCID());
}

bool CMMOManager::AssignWeaponLoadoutSlot(CPlayer *pPlayer, int ItemID, int LoadoutSlot)
{
	if(!pPlayer || ItemID <= 0 || LoadoutSlot < 0 || LoadoutSlot >= CPlayer::MMO_WEAPON_LOADOUT_SIZE)
		return false;

	const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
	if(!pDef || !CPlayer::IsMMOWeaponItemType(pDef->GetType()))
	{
		GS()->SendChatTo(pPlayer->GetCID(), "该物品不是武器。");
		return false;
	}

	if(!pPlayer->IsMMOWeaponEquipped(ItemID))
	{
		GS()->SendChatTo(pPlayer->GetCID(), "请先装备该武器。");
		return false;
	}

	const bool IsMelee = pDef->GetType() == ItemType::EquipHammer;
	int *pLoadout = IsMelee ? pPlayer->m_aMeleeLoadout : pPlayer->m_aRangedLoadout;

	const int OldIdx = IsMelee ? pPlayer->FindMeleeLoadoutIndex(ItemID) : pPlayer->FindRangedLoadoutIndex(ItemID);
	if(OldIdx == LoadoutSlot)
	{
		GS()->SendChatTo(pPlayer->GetCID(), "已在该栏位。");
		return false;
	}

	const int DisplacedID = pLoadout[LoadoutSlot];
	if(OldIdx >= 0)
		pLoadout[OldIdx] = -1;
	pLoadout[LoadoutSlot] = ItemID;

	// Swap if target slot was occupied
	if(DisplacedID > 0 && DisplacedID != ItemID && OldIdx >= 0)
		pLoadout[OldIdx] = DisplacedID;

	pPlayer->m_MMODirty = true;
	ApplyEquippedWeapon(pPlayer);
	SavePlayerData(pPlayer);

	char aBuf[96];
	str_format(aBuf, sizeof(aBuf), "已移动到%s栏位 %d。", IsMelee ? "近战" : "远程", LoadoutSlot + 1);
	GS()->SendChatTo(pPlayer->GetCID(), aBuf);
	return true;
}

void CMMOManager::ShowMMOInventory(int ClientID)
{
	OpenVotePage(ClientID, VOTE_PAGE_MMO_BACKPACK);
}

void CMMOManager::ShowMMOEquip(int ClientID)
{
	OpenVotePage(ClientID, VOTE_PAGE_MMO_EQUIP);
}

void CMMOManager::ShowMMOItemDetail(int ClientID, int ItemIdx)
{
	CVoteMenuManager *pVote = GetVoteMenu();
	if(!pVote)
		return;
	SPlayerVote *pV = pVote->GetPlayerVote(ClientID);
	pV->m_Select[SPlayerVote::ITEM] = ItemIdx;
	pV->m_LastPage = VOTE_PAGE_MMO_BACKPACK;
	pV->m_Page = VOTE_PAGE_MMO_ITEM;
	pVote->ClearVotes(ClientID);
}

static bool TryBindBlacksmithEnchant(CGameContext *pGS, CPlayer *pP, SPlayerVote *pVote, int ClientID)
{
	if(!pGS || !pP || !pVote)
		return false;
	if(!IsPlayerNearServiceNpc(pGS, pP, "blacksmith"))
	{
		NotifyNpcServiceDenied(pGS, ClientID);
		return false;
	}
	BindNpcService(pVote, "blacksmith");
	return true;
}

void CMMOManager::RegisterEconomyVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "checkin", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pMMO->ConCheckin(pG->Server()->ClientName(pCtx->m_ClientID), pP);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_ACTIVITIES, PAGE_MENU);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "ah_list", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_AUCTION_LIST, VOTE_PAGE_MMO_AUCTION);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "ah_sell", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_AUCTION_SELL, VOTE_PAGE_MMO_AUCTION);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "ah_sellpick", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int Slot = pR->GetInteger(0);
		if(!pP || Slot < 0 || (size_t)Slot >= pP->m_MMOInventory.size())
		{
			pG->SendChatTo(pCtx->m_ClientID, "背包格无效。");
			return;
		}
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		if(pSVote)
		{
			pSVote->m_Select[SPlayerVote::ITEM] = Slot;
			pSVote->m_Page = VOTE_PAGE_MMO_AUCTION_SELL_PRICE;
			pVote->ClearVotes(pCtx->m_ClientID);
		}
	}, pGame);

	VOTE_CMD(pManager, "ah_sellconfirm", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		const int Slot = pSVote ? pSVote->m_Select[SPlayerVote::ITEM] : -1;
		int Price = 0;
		if(pCtx->m_pArgs && pCtx->m_pArgs[0])
			Price = str_toint(pCtx->m_pArgs);
		pMMO->ConAuctionSell(pCtx->m_ClientID, Slot, Price);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_AUCTION, VOTE_PAGE_MMO_ECONOMY);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "ah_buy", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const int ListingID = pR->GetInteger(0);
		if(ListingID > 0)
			pMMO->ConAuctionBuy(pCtx->m_ClientID, ListingID);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_AUCTION_LIST, VOTE_PAGE_MMO_AUCTION);
	}, pGame);

	VOTE_CMD(pManager, "ah_cancelpick", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const int ListingID = pR->GetInteger(0);
		if(ListingID > 0)
			pMMO->ConAuctionCancel(pCtx->m_ClientID, ListingID);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_AUCTION_LIST, VOTE_PAGE_MMO_AUCTION);
	}, pGame);

	VOTE_CMD(pManager, "shopnpc", "s", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		const char *pNpc = pR->GetString(0);
		if(!pNpc || !pNpc[0])
			return;
		CPlayer *pBuyer = pG->m_apPlayers[pCtx->m_ClientID];
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		if(!pSVote || !pBuyer)
			return;
		if(!FindShopByNpcID(pNpc))
		{
			pG->SendChatTo(pCtx->m_ClientID, "未找到该商店。");
			return;
		}
		if(!IsPlayerNearServiceNpc(pG, pBuyer, pNpc))
		{
			NotifyNpcServiceDenied(pG, pCtx->m_ClientID);
			return;
		}
		BindNpcService(pSVote, pNpc);
		str_copy(pSVote->m_aExtraText, pNpc, sizeof(pSVote->m_aExtraText));
		pSVote->m_Page = VOTE_PAGE_MMO_SHOP_ITEMS;
		pVote->ClearVotes(pCtx->m_ClientID);
	}, pGame);

	VOTE_CMD(pManager, "shopbuy", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pBuyer = pG->m_apPlayers[pCtx->m_ClientID];
		const char *pArgs = pCtx->m_pArgs;
		char aNpc[32];
		int ItemID = 0;
		const char *pSpace = pArgs ? str_find(pArgs, " ") : nullptr;
		if(pSpace)
		{
			const int NpcLen = minimum((int)(pSpace - pArgs), (int)sizeof(aNpc) - 1);
			str_copy(aNpc, pArgs, NpcLen + 1);
			ItemID = str_toint(pSpace + 1);
		}
		else if(pArgs && pArgs[0])
		{
			CVoteMenuManager *pVote = pMMO->GetVoteMenu();
			SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
			if(!pSVote || !pSVote->m_aExtraText[0])
				return;
			str_copy(aNpc, pSVote->m_aExtraText, sizeof(aNpc));
			ItemID = str_toint(pArgs);
		}
		else
			return;
		if(!pBuyer || !aNpc[0] || !IsPlayerNearServiceNpc(pG, pBuyer, aNpc))
		{
			NotifyNpcServiceDenied(pG, pCtx->m_ClientID);
			return;
		}
		pMMO->ConShopBuy(pCtx->m_ClientID, aNpc, ItemID);
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		if(pVote)
		{
			SPlayerVote *pSVote = pVote->GetPlayerVote(pCtx->m_ClientID);
			if(pSVote && pSVote->m_aExtraText[0])
			{
				pSVote->m_Page = VOTE_PAGE_MMO_SHOP_ITEMS;
				pVote->ClearVotes(pCtx->m_ClientID);
			}
		}
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "enchant", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		if(!TryBindBlacksmithEnchant(pG, pP, pSVote, pCtx->m_ClientID))
			return;
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_ENCHANT_SELECT, VOTE_PAGE_MMO_ENCHANT);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "enchantpick", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		if(!EnsureNpcServiceAccess(pG, pP, pSVote))
		{
			NotifyNpcServiceDenied(pG, pCtx->m_ClientID);
			return;
		}
		pMMO->ConEnchant(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);

	VOTE_CMD(pManager, "recycle_pick", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		if(pSVote)
		{
			pSVote->m_Select[SPlayerVote::ITEM] = pR->GetInteger(0);
			pSVote->m_Page = VOTE_PAGE_MMO_RECYCLE_CONFIRM;
			pVote->ClearVotes(pCtx->m_ClientID);
		}
	}, pGame);

	VOTE_CMD(pManager, "recycle_confirm", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP)
			return;
		const int Slot = pR->GetInteger(0);
		int Qty = pR->NumArguments() >= 2 ? pR->GetInteger(1) : 0;
		const char *pReasonMsg = nullptr;
		int Gold = 0;
		if(pMMO->TrySellItem(pP, Slot, Qty, &Gold, &pReasonMsg))
		{
			char aBuf[160];
			str_format(aBuf, sizeof(aBuf), LocVote(pG, pCtx->m_ClientID, "mmo.recycle.ok", "回收成功，获得 %d 金币（今日 %d/%d）"),
				Gold, pP->m_DailySellGold, MMO_SELL_DAILY_GOLD_CAP);
			pG->SendChatTo(pCtx->m_ClientID, aBuf);
		}
		else if(pReasonMsg)
			pG->SendChatTo(pCtx->m_ClientID, pReasonMsg);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_RECYCLE, VOTE_PAGE_MMO_ECONOMY);
	}, pGame);
}

void CMMOManager::RegisterSocialVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "friend_list", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_FRIENDS_LIST, VOTE_PAGE_MMO_FRIENDS);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "friend_add", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG)
			return;
		const char *pName = pCtx->m_pArgs;
		if(pName && pName[0])
			CGlobalState::FriendRequestSend(pG, pCtx->m_ClientID, pName);
		else
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "friend_accept", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG)
			return;
		const char *pName = pCtx->m_pArgs;
		if(pName && pName[0])
			CGlobalState::FriendAccept(pG, pCtx->m_ClientID, pName);
		else
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "friend_decline", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG)
			return;
		const char *pName = pCtx->m_pArgs;
		if(pName && pName[0])
			CGlobalState::FriendDecline(pG, pCtx->m_ClientID, pName);
		else
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "friend_remove", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG)
			return;
		const char *pName = pCtx->m_pArgs;
		if(pName && pName[0])
			CGlobalState::FriendRemove(pG, pCtx->m_ClientID, pName);
		else
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
		(void)pR;
	}, pGame);
}

void CMMOManager::RegisterLifestyleVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "fashion", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const char *pArgs = pCtx->m_pArgs;
		if(pArgs && str_comp_nocase(pArgs, "clear") == 0)
		{
			if(!pP || pP->m_FashionItemID == 0)
				pG->SendChatTo(pCtx->m_ClientID, "当前没有装备时装。");
			else
			{
				pP->m_FashionItemID = 0;
				pP->m_MMODirty = true;
				pG->SendChatTo(pCtx->m_ClientID, "已清除时装，恢复默认外观。");
			}
			return;
		}
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_FASHION_SELECT, VOTE_PAGE_MMO_LIFESTYLE);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "fashionpick", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(pP)
			pG->Core()->GetMMOManager()->EquipFashion(pP, pR->GetInteger(0));
	}, pGame);

	VOTE_CMD(pManager, "mount", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pMMO->ToggleVehicle(pCtx->m_ClientID);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_MOUNT, VOTE_PAGE_MMO_LIFESTYLE);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "vehicle", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pMMO->ToggleVehicle(pCtx->m_ClientID);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_MOUNT, VOTE_PAGE_MMO_LIFESTYLE);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "vehiclename", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const char *pNewName = pCtx->m_pArgs;
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		if(pP->m_VehicleType <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "🚁 你没有载具，无法改名。");
			return;
		}
		if(!pNewName || !pNewName[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 中填写新名字。");
			return;
		}
		str_copy(pP->m_aVehicleName, pNewName, sizeof(pP->m_aVehicleName));
		pMMO->SaveVehicleData(pP);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "🚁 载具已更名为: %s", pP->m_aVehicleName);
		pG->SendChatTo(pCtx->m_ClientID, aBuf);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_MOUNT, VOTE_PAGE_MMO_LIFESTYLE);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "vehicle_activate", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int Slot = pR->GetInteger(0);
		if(pP && Slot >= 0 && (size_t)Slot < pP->m_MMOInventory.size())
			pMMO->TryGrantVehicleFromItem(pP, pP->m_MMOInventory[Slot].GetID(), Slot);
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_MOUNT, VOTE_PAGE_MMO_LIFESTYLE);
	}, pGame);

	VOTE_CMD(pManager, "house_buy", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->VoteHouseBuy(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "house_tp", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->VoteHouseTp(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "marry", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->VoteMarry(pCtx->m_ClientID, pCtx->m_pArgs);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "marry_accept", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->VoteMarryAccept(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "divorce", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->VoteDivorce(pCtx->m_ClientID);
		(void)pR;
	}, pGame);
}

void CMMOManager::RegisterInventoryVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "use", "i", CMMOManager::ConUse, pGame);

	VOTE_CMD(pManager, "mmounequipid", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int ItemID = pR->GetInteger(0);
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		const int SlotIdx = pSVote ? pSVote->m_Select[SPlayerVote::ITEM] : -1;
		if(pMMO->UnequipItemById(pP, ItemID))
		{
			if(SlotIdx >= 0 && pP && (size_t)SlotIdx < pP->m_MMOInventory.size())
				pMMO->ShowMMOItemDetail(pCtx->m_ClientID, SlotIdx);
			else
				pMMO->ShowMMOEquip(pCtx->m_ClientID);
		}
	}, pGame);

	VOTE_CMD(pManager, "wloadslot", "ii", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int ItemID = pR->GetInteger(0);
		const int LoadoutSlot = pR->GetInteger(1);
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		const int SlotIdx = pSVote ? pSVote->m_Select[SPlayerVote::ITEM] : -1;
		if(pMMO->AssignWeaponLoadoutSlot(pP, ItemID, LoadoutSlot))
		{
			if(SlotIdx >= 0 && pP && (size_t)SlotIdx < pP->m_MMOInventory.size())
				pMMO->ShowMMOItemDetail(pCtx->m_ClientID, SlotIdx);
			else
				pMMO->ShowMMOEquip(pCtx->m_ClientID);
		}
	}, pGame);

	VOTE_CMD(pManager, "inv_filter", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP)
			return;
		const char *pArgs = pCtx->m_pArgs;
		const int Group = pArgs && pArgs[0] ? str_toint(pArgs) : -1;
		int Type = -1;
		if(pArgs)
		{
			const char *pSpace = str_find(pArgs, " ");
			if(pSpace)
				Type = str_toint(pSpace + 1);
		}
		pP->m_InventoryFilterGroup = Group;
		pP->m_InventoryFilterType = Type;
		pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_BACKPACK);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "mmodrop", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int Slot = pR->GetInteger(0);
		const char *pRest = pCtx->m_pArgs ? str_find(pCtx->m_pArgs, " ") : nullptr;
		int Qty = VoteArgInt(pRest ? pRest + 1 : nullptr, nullptr, 0);
		const char *pReasonMsg = nullptr;
		if(pMMO->DropItemAtSlot(pP, Slot, Qty, &pReasonMsg))
		{
			if(Slot >= 0)
				pMMO->ShowMMOItemDetail(pCtx->m_ClientID, Slot);
			else
				pMMO->ShowMMOInventory(pCtx->m_ClientID);
		}
		else if(pReasonMsg)
			pG->SendChatTo(pCtx->m_ClientID, pReasonMsg);
	}, pGame);

	VOTE_CMD(pManager, "mmosplit", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int Slot = pR->GetInteger(0);
		const char *pRest = pCtx->m_pArgs ? str_find(pCtx->m_pArgs, " ") : nullptr;
		int SplitCount = VoteArgInt(pRest ? pRest + 1 : nullptr, nullptr, 0);
		const char *pReasonMsg = nullptr;
		if(pMMO->SplitItemAtSlot(pP, Slot, SplitCount, &pReasonMsg))
			pMMO->ShowMMOItemDetail(pCtx->m_ClientID, Slot);
		else if(pReasonMsg)
			pG->SendChatTo(pCtx->m_ClientID, pReasonMsg);
	}, pGame);
}

void CMMOManager::RegisterGroupVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "group_create", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pMMO->GroupCreate(pCtx->m_ClientID);
		pMMO->RefreshGroupVotePage(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "group_leave", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pMMO->GroupLeave(pCtx->m_ClientID);
		pMMO->RefreshGroupVotePage(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "group_disband", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pMMO->GroupDisband(pCtx->m_ClientID);
		pMMO->RefreshGroupVotePage(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "group_kick_menu", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CVoteMenuManager *pVote = pG->Core()->GetMMOManager()->GetVoteMenu();
		if(!pVote)
			return;
		SPlayerVote *pSVote = pVote->GetPlayerVote(pCtx->m_ClientID);
		str_copy(pSVote->m_aExtraText, "kick", sizeof(pSVote->m_aExtraText));
		pSVote->m_Page = VOTE_PAGE_MMO_GROUP;
		pVote->ClearVotes(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "group_kick", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const int TargetCID = pR->GetInteger(0);
		if(TargetCID >= 0)
			pMMO->GroupKick(pCtx->m_ClientID, TargetCID);
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		if(pSVote)
			pSVote->m_aExtraText[0] = 0;
		pMMO->RefreshGroupVotePage(pCtx->m_ClientID);
	}, pGame);
}

void CMMOManager::RegisterMailVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "mail_read", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CVoteMenuManager *pVote = pG->Core()->GetMMOManager()->GetVoteMenu();
		if(!pVote)
			return;
		const int MailID = pR->GetInteger(0);
		SPlayerVote *pSVote = pVote->GetPlayerVote(pCtx->m_ClientID);
		pSVote->m_Select[SPlayerVote::ITEM] = MailID;
		pSVote->m_Page = VOTE_PAGE_MMO_MAIL_READ;
		pVote->ClearVotes(pCtx->m_ClientID);
	}, pGame);

	VOTE_CMD(pManager, "mail_claim", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int MailID = pR->GetInteger(0);
		if(MailID > 0)
			pMMO->ClaimMailAttachments(pP, MailID);
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		if(!pVote)
			return;
		SPlayerVote *pSVote = pVote->GetPlayerVote(pCtx->m_ClientID);
		pSVote->m_Select[SPlayerVote::ITEM] = MailID;
		pSVote->m_Page = VOTE_PAGE_MMO_MAILBOX;
		pVote->ClearVotes(pCtx->m_ClientID);
	}, pGame);

	VOTE_CMD(pManager, "mail_claimall", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		const int Claimed = pMMO->ClaimAllMailAttachments(pP);
		char aMsg[128];
		if(Claimed > 0)
			str_format(aMsg, sizeof(aMsg), "✅ 一键领取完成，共 %d 封邮件附件。", Claimed);
		else
			str_copy(aMsg, "没有可领取的邮件附件。", sizeof(aMsg));
		pG->SendChatTo(pCtx->m_ClientID, aMsg);
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		if(!pVote)
			return;
		SPlayerVote *pSVote = pVote->GetPlayerVote(pCtx->m_ClientID);
		pSVote->m_Page = VOTE_PAGE_MMO_MAILBOX;
		pVote->ClearVotes(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "mail_delete", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const int MailID = pR->GetInteger(0);
		if(MailID > 0)
			pMMO->DeleteMail(MailID);
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		if(!pVote)
			return;
		SPlayerVote *pSVote = pVote->GetPlayerVote(pCtx->m_ClientID);
		pSVote->m_Page = VOTE_PAGE_MMO_MAILBOX;
		pVote->ClearVotes(pCtx->m_ClientID);
	}, pGame);

	VOTE_CMD(pManager, "mail_delread", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(pP)
			pMMO->DeleteReadMails(pP->GetAccountId());
		CVoteMenuManager *pVote = pMMO->GetVoteMenu();
		if(!pVote)
			return;
		SPlayerVote *pSVote = pVote->GetPlayerVote(pCtx->m_ClientID);
		pSVote->m_Page = VOTE_PAGE_MMO_MAILBOX;
		pVote->ClearVotes(pCtx->m_ClientID);
		(void)pR;
	}, pGame);
}

void CMMOManager::RegisterActivityVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "rank", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pType = pCtx->m_pArgs;
		if(pType && str_comp_nocase(pType, "gold") == 0)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_RANKING_GOLD, VOTE_PAGE_MMO_RANKING);
		else
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_RANKING_LEVEL, VOTE_PAGE_MMO_RANKING);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "stats", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, PAGE_ATTRIBUTES);
		(void)pR;
	}, pGame);
}

bool CMMOManager::OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason)
{
	(void)pPlayer;
	(void)pCmd;
	(void)pArgs;
	(void)ReasonNumber;
	(void)pReason;
	return false;
}

// ─── Hub pages (navigation only) ─────────────────────────────────────

static void RenderMMOSocialHub(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, PAGE_MENU, "社交菜单");
	char aLine[VOTE_DESC_LENGTH];

	if(pP && pP->GetAccountId() > 0)
	{
		CMMOManager *pMMO = pGS->Core() ? pGS->Core()->GetMMOManager() : nullptr;
		const int Friends = (int)pP->m_aFriends.size();
		const int Pending = CGlobalState::CountIncomingFriendRequests(pGS, pP->GetAccountId());
		const int MailTotal = pMMO ? pMMO->GetMailCount(pP->GetAccountId()) : 0;
		const int MailUnread = pMMO ? pMMO->GetUnreadMailCount(pP->GetAccountId()) : 0;
		const int GroupIdx = CGlobalState::GetPlayerGroupID(pGS->Server(), ClientID);

		str_format(aLine, sizeof(aLine), "好友 %d 人%s", Friends,
			Pending > 0 ? "  ·  有新申请" : "");
		V.Info(aLine);
		str_format(aLine, sizeof(aLine), "邮箱 %d 封（%d 未读）", MailTotal, MailUnread);
		V.Info(aLine);
		if(GroupIdx >= 0)
			V.Info("已在队伍中");
		else
			V.Info("未加入队伍");
	}

	V.GoToPage(VOTE_PAGE_MMO_FRIENDS, "好友");
	V.GoToPage(VOTE_PAGE_MMO_GROUP, "队伍");
	V.GoToPage(VOTE_PAGE_MMO_MAILBOX, "邮箱");
	V.GoToPage(VOTE_PAGE_MMO_CHAT, "聊天与交易");
	V.Footer();
}

static void RenderMMOActivitiesHub(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, PAGE_MENU, "活动菜单");
	char aLine[VOTE_DESC_LENGTH];

	if(pP && pP->GetAccountId() > 0)
	{
		VoteAppendLevelLine(V, pP);
		const int Today = VoteTodayYYYYMMDD();
		if(pP->m_LastCheckinDate == Today)
		{
			str_format(aLine, sizeof(aLine), "今日已签到  连续 %d 天", pP->m_CheckinStreak);
			V.Info(aLine);
		}
		else
		{
			int StreakPreview = 1;
			if(pP->m_LastCheckinDate == VoteYesterdayYYYYMMDD())
				StreakPreview = pP->m_CheckinStreak + 1;
			str_format(aLine, sizeof(aLine), "今日未签到  预计 %d 金币（连续 %d 天）",
				StreakPreview * 10, StreakPreview);
			V.Info(aLine);
			if(StreakPreview % 7 == 0)
				V.Info("连续 7 天额外稀有奖励");
			V.Option("ccv_checkin", "立即签到");
		}
	}
	else
	{
		V.Option("ccv_checkin", "每日签到");
	}

	V.Info("请找任务大师（用锤子对话）查看与接取任务");
	V.GoToPage(VOTE_PAGE_MMO_RANKING, "排行榜");
	V.Footer();
}

static void RenderMMOEconomyHub(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, PAGE_MENU,
		LocVote(pGS, ClientID, "mmo.economy.title", "经济菜单"));
	if(pP)
		VoteAppendWallet(V, pP);
	V.Info(LocVote(pGS, ClientID, "mmo.economy.desc", "回收、拍卖、商店与装备强化"));
	V.GoToPage(VOTE_PAGE_MMO_SHOP, LocVote(pGS, ClientID, "mmo.economy.shop", "商店（附近商人）"));
	V.GoToPage(VOTE_PAGE_MMO_RECYCLE, LocVote(pGS, ClientID, "mmo.economy.recycle", "物品回收"));
	V.GoToPage(VOTE_PAGE_MMO_AUCTION, LocVote(pGS, ClientID, "mmo.economy.auction", "拍卖行"));
	if(pP && IsPlayerNearServiceNpc(pGS, pP, "blacksmith"))
	{
		SPlayerVote *pSVote = pVote->GetPlayerVote(ClientID);
		if(pSVote)
			BindNpcService(pSVote, "blacksmith");
		V.GoToPage(VOTE_PAGE_MMO_ENCHANT, LocVote(pGS, ClientID, "mmo.economy.enchant", "装备强化"));
	}
	else
		V.Info(LocVote(pGS, ClientID, "mmo.economy.enchant.npc", "装备强化请靠近铁匠"));
	V.Footer();
}

static void RenderMMOLifestyleHub(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, PAGE_MENU, "生活菜单");
	char aLine[VOTE_DESC_LENGTH];

	if(pP && pP->GetAccountId() > 0)
	{
		if(pP->m_VehicleType > 0)
		{
			str_format(aLine, sizeof(aLine), "载具：%s %s",
				pP->m_aVehicleName[0] ? pP->m_aVehicleName : "未命名",
				pP->m_pDeployedVehicle ? "（已部署）" : "（未部署）");
			V.Info(aLine);
		}
		else
			V.Info("载具：无");
		if(pP->m_FashionItemID > 0)
		{
			const CMMOItemDescription *pDef = CMMOItemDescription::Get(pP->m_FashionItemID);
			str_format(aLine, sizeof(aLine), "时装：%s", LocMMOItemName(pGS, ClientID, pDef));
			V.Info(aLine);
		}
		else
			V.Info("时装：未装备");
		str_format(aLine, sizeof(aLine), "房屋：%s Lv.%d",
			pP->m_HasHouse ? "已拥有" : "未购买", pP->m_HouseLevel);
		V.Info(aLine);
		if(pP->m_SpouseAccountID > 0)
		{
			char aSpouse[64];
			CMMOManager *pMMO = pGS->Core() ? pGS->Core()->GetMMOManager() : nullptr;
			if(pMMO && pMMO->LookupAccountName(pP->m_SpouseAccountID, aSpouse, sizeof(aSpouse)))
			{
				char aDate[16];
				VoteFormatDateYYYYMMDD(pP->m_MarriageDate, aDate, sizeof(aDate));
				str_format(aLine, sizeof(aLine), "婚姻：与 %s（%s）", aSpouse, aDate);
				V.Info(aLine);
			}
			else
				V.Info("婚姻：已婚");
		}
		else
			V.Info("婚姻：单身");
	}

	V.GoToPage(VOTE_PAGE_MMO_MOUNT, "载具");
	V.GoToPage(VOTE_PAGE_MMO_FASHION_SELECT, "时装");
	V.GoToPage(VOTE_PAGE_MMO_HOUSE, "房屋");
	V.GoToPage(VOTE_PAGE_MMO_MARRIAGE, "婚姻");
	V.Footer();
}

static void RenderMMOPvpHub(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, PAGE_MENU, "PvP 菜单");
	char aLine[VOTE_DESC_LENGTH];

	if(pP && pP->GetGuildID() >= 0)
	{
		CGuildManager *pGM = pGS->Core() ? pGS->Core()->GuildManager() : nullptr;
		SGuildData *pG = pGM ? pGM->GetPlayerGuild(ClientID) : nullptr;
		if(pG)
			str_format(aLine, sizeof(aLine), "公会：%s Lv.%d", pG->m_aName, pG->m_Level);
		else
			str_copy(aLine, "公会：已加入", sizeof(aLine));
		V.Info(aLine);
	}
	else
		V.Info("公会：未加入");

	VoteAppendBossStatus(V, pGS, ClientID);

	V.GoToPage(VOTE_PAGE_MMO_GUILD, "公会");
	V.GoToPage(VOTE_PAGE_MMO_ARENA, "竞技场");
	V.GoToPage(VOTE_PAGE_MMO_GUILD_WAR, "公会战");
	V.GoToPage(VOTE_PAGE_MMO_BOSS, "世界 Boss");
	V.Footer();
}

// ─── Feature pages ───────────────────────────────────────────────────

static void RenderMMOFriendsPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	const int Pending = (pP && pP->GetAccountId() > 0)
		? CGlobalState::CountIncomingFriendRequests(pGS, pP->GetAccountId()) : 0;

	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_SOCIAL, "好友");
	char aLine[VOTE_DESC_LENGTH];
	if(Pending > 0)
	{
		str_format(aLine, sizeof(aLine), "待处理好友申请：%d", Pending);
		V.Info(aLine);
	}
	V.GoToPage(VOTE_PAGE_MMO_FRIENDS_LIST, "我的好友");
	if(Pending > 0)
		V.GoToPage(VOTE_PAGE_MMO_FRIEND_REQUESTS, "好友申请");
	V.Info("添加好友：选下方选项，在 Reason 填写玩家名");
	V.Option("ccv_friend_add", "发送好友申请");
	V.Footer();
}

static void RenderMMORankingPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CMMOManager *pMMO = pGS->Core() ? pGS->Core()->GetMMOManager() : nullptr;
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_ACTIVITIES, "排行榜");
	if(pP && pP->GetAccountId() > 0)
	{
		char aLine[VOTE_DESC_LENGTH];
		const int Level = pP->GetStat(AttributeIdentifier::Level);
		const int Gold = pP->GetStat(AttributeIdentifier::Gold);
		str_format(aLine, sizeof(aLine), "你的等级 Lv.%d  金币 %d", Level, Gold);
		V.Info(aLine);
		if(pMMO)
		{
			const int Exp = pP->GetStat(AttributeIdentifier::Experience);
			const int LevelRank = pMMO->GetPlayerLevelRank(pP->GetAccountId(), Level, Exp);
			const int GoldRank = pMMO->GetPlayerGoldRank(pP->GetAccountId(), Gold);
			if(LevelRank > 0)
			{
				str_format(aLine, sizeof(aLine), "等级名次：第 %d 名", LevelRank);
				V.Info(aLine);
			}
			if(GoldRank > 0)
			{
				str_format(aLine, sizeof(aLine), "财富名次：第 %d 名", GoldRank);
				V.Info(aLine);
			}
		}
	}
	V.GoToPage(VOTE_PAGE_MMO_RANKING_LEVEL, "等级排行榜 Top 20");
	V.GoToPage(VOTE_PAGE_MMO_RANKING_GOLD, "财富排行榜 Top 20");
	V.Footer();
}

static void RenderMMOAuctionPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_ECONOMY, "拍卖行");
	if(pP)
		VoteAppendWallet(V, pP);
	V.Info("浏览挂单或上架背包物品");
	V.GoToPage(VOTE_PAGE_MMO_AUCTION_LIST, "浏览拍卖行");
	V.GoToPage(VOTE_PAGE_MMO_AUCTION_SELL, "上架物品");
	V.Footer();
}

static void RenderMMOShopPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_ECONOMY, "商店");
	if(pP)
		VoteAppendWallet(V, pP);
	V.Info(LocVote(pGS, ClientID, "mmo.shop.desc", "冒险者装备 / 杂货 / 治疗补给"));
	V.GoToPage(VOTE_PAGE_MMO_SHOP_LIST, LocVote(pGS, ClientID, "mmo.shop.pick", "选择商店"));
	V.Footer();
}

static void RenderMMOEnchantPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_ECONOMY, "装备强化");
	if(pP)
		VoteAppendWallet(V, pP);
	V.Info("选择背包中的武器进行强化");
	V.GoToPage(VOTE_PAGE_MMO_ENCHANT_SELECT, "选择要强化的武器");
	V.Footer();
}

static void RenderMMOGuildPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CGuildManager *pGuildMgr = pGS->Core() ? pGS->Core()->GuildManager() : nullptr;
	const bool InGuild = pP && pP->GetGuildID() >= 0;

	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_PVP, "公会");

	if(!InGuild)
	{
		CGuildManager *pGuildMgrPending = pGuildMgr;
		if(pGuildMgrPending && pGuildMgrPending->HasPendingInvite(ClientID))
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), "收到公会邀请（来自 %s）", pGuildMgrPending->GetPendingInviterName(ClientID));
			V.Info(aLine);
			V.Option("ccv_guild_accept", "接受邀请");
			V.Option("ccv_guild_decline", "拒绝邀请");
		}
		V.Info("创建公会需 500 金币");
		V.Info("创建：选下方选项，在 Reason 填写「名称」或「名称 标签」");
		V.Option("ccv_guild_create", "创建公会");
		V.GoToPage(VOTE_PAGE_MMO_GUILD_BROWSE, "浏览公会并申请加入");
	}
	else
	{
		SGuildData *pG = pGuildMgr ? pGuildMgr->GetPlayerGuild(ClientID) : nullptr;
		if(pG)
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), "「%s」 Lv.%d  成员 %d/%d",
				pG->m_aName, pG->m_Level, pG->MemberCount(), pG->m_MaxMembers);
			V.Info(aLine);
		}
		V.Option("ccv_guild_info", "公会信息（聊天）");
		V.GoToPage(VOTE_PAGE_MMO_GUILD_MEMBERS, "查看成员");
		if(pG)
		{
			EGuildRank Rank = pG->GetRank(ClientID);
			if(Rank == GUILDRANK_LEADER || Rank == GUILDRANK_CO_LEADER || Rank == GUILDRANK_OFFICER)
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), "待审批申请：%d", pG->m_JoinRequests.size());
				V.Info(aLine);
				V.Option("ccv_guild_requests", "审批加入申请");
				V.Info("邀请成员：选下方选项，Reason 填玩家名");
				V.Option("ccv_guild_invite", "邀请成员");
				V.Info("捐赠金币：选下方选项，Reason 填金额");
				V.Option("ccv_guild_donate", "捐赠金币");
				V.Info("设置公告：选下方选项，Reason 填公告内容");
				V.Option("ccv_guild_motd", "设置公告");
			}
			if(Rank == GUILDRANK_LEADER)
				V.Option("ccv_guild_disband", "解散公会");
		}
		V.Option("ccv_guild_leave", "退出公会");
	}

	V.Footer();
}

static void RenderMMOGuildWarPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_PVP, "公会战");
	VoteAppendGuildWarStatus(V, pGS, ClientID);
	V.Info("流程：约战 → 接受 → 选模式/地图 → 加入 → 开始");
	V.GoToPage(VOTE_PAGE_MMO_GUILD_WAR_MODE, "选择比赛模式（被挑战方）");
	V.GoToPage(VOTE_PAGE_MMO_GUILD_WAR_MAP, "选择比赛地图");
	V.Option("ccv_guild_war_challenge", "发起约战（Reason 填公会名）");
	V.Option("ccv_guild_war_accept", "接受约战");
	V.Option("ccv_guild_war_status", "查看比赛状态");
	V.Option("ccv_guild_war_join", "加入队伍");
	V.Option("ccv_guild_war_leave", "离开队伍");
	V.Option("ccv_guild_war_start", "开始比赛（会长/副会长）");
	V.Option("ccv_guild_war_cancel", "取消比赛");
	V.Footer();
}

static void RenderMMOGuildWarModePage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_GUILD_WAR, "选择模式");
	V.Info("被挑战方会长/副会长在「选模式中」阶段可选");

	for(int i = 0; i < NUM_GUILDWAR_MODES; i++)
	{
		if(i == GUILDWAR_MODE_IDM)
			continue; // legacy alias, hide from menu
		const SGuildWarModeDef *pDef = GetGuildWarModeDef((EGuildWarMode)i);
		if(!pDef)
			continue;
		char aCmd[VOTE_CMD_LENGTH];
		char aLine[VOTE_DESC_LENGTH];
		str_format(aCmd, sizeof(aCmd), "ccv_guild_war_setmode %s", pDef->m_pId);
		str_format(aLine, sizeof(aLine), "%s — %s（目标 %d 分）", pDef->m_pDisplayName, pDef->m_pDescription, pDef->m_DefaultTargetScore);
		V.Option(aCmd, aLine);
	}

	V.GoToPage(VOTE_PAGE_MMO_GUILD_WAR_MAP, "选择比赛地图");
	V.Footer();
}

static void RenderMMOGuildWarMapPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_GUILD_WAR, "选择地图");
	char aMode[16];
	char aSelectedMap[128];
	int Status = -1;

	CGuildManager *pGuildMgr = pGS && pGS->Core() ? pGS->Core()->GuildManager() : nullptr;
	if(!pGuildMgr || !pGuildMgr->GetActiveWarMatchInfo(ClientID, aMode, sizeof(aMode), aSelectedMap, sizeof(aSelectedMap), &Status))
	{
		V.Info("你不在进行中的公会战中");
		V.Footer();
		return;
	}

	if(Status < 2 || aMode[0] == '\0')
	{
		V.Info("请先完成模式选择（被挑战方）");
		V.GoToPage(VOTE_PAGE_MMO_GUILD_WAR_MODE, "选择比赛模式");
		V.Footer();
		return;
	}

	if(Status >= 3)
	{
		V.Info("比赛已开始，无法更换地图");
		V.Footer();
		return;
	}

	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), "模式：%s", GuildWarModeDisplayName(aMode));
	V.Info(aLine);
	if(aSelectedMap[0])
	{
		str_format(aLine, sizeof(aLine), "当前：%s", aSelectedMap);
		V.Info(aLine);
	}

	std::vector<std::string> Maps;
	ListArenaMapsForMode(pGS->Storage(), aMode, Maps);
	if(Maps.empty())
	{
		V.Info("没有可用地图");
		str_format(aLine, sizeof(aLine), "模式 %s：需 maps/vanilla（TDM）/ maps/fng（FNG）/ ctf 前缀（CTF）", GuildWarModeDisplayName(aMode));
		V.Info(aLine);
		V.Info("若已放地图仍为空，请从含 maps/ 的目录启动服务器");
		V.Footer();
		return;
	}

	char aCmd[VOTE_CMD_LENGTH];
	const int MaxShow = 20;
	for(int i = 0; i < (int)Maps.size() && i < MaxShow; i++)
	{
		str_format(aCmd, sizeof(aCmd), "ccv_guild_war_setmap %s", Maps[i].c_str());
		V.Option(aCmd, Maps[i].c_str());
	}
	if((int)Maps.size() > MaxShow)
		V.Info("仅显示前 20 张地图");

	V.Footer();
}

static void RenderMMOBossPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_PVP, "世界 Boss");
	VoteAppendBossStatus(V, pGS, ClientID);
	V.Info("参与击杀可获得金币与稀有掉落");
	V.Option("ccv_boss", "刷新详细状态");
	CWorldBossManager *pWB = pGS->Core() ? pGS->Core()->GetWorldBossManager() : nullptr;
	if(pWB && pWB->IsBossAlive())
		V.Option("ccv_boss_tp", "传送到 Boss 附近");
	V.Footer();
}

static void RenderMMOVehiclePage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_LIFESTYLE, "载具");
	char aLine[VOTE_DESC_LENGTH];
	if(!pP || pP->m_VehicleType <= 0)
	{
		V.Info("你还没有载具");
		V.Info("使用载具物品（史莱姆/幼龙飞行器）可激活");
		V.Footer();
		return;
	}
	str_format(aLine, sizeof(aLine), "名字：%s", pP->m_aVehicleName[0] ? pP->m_aVehicleName : "未命名");
	V.Info(aLine);
	const bool Deployed = pP->m_pDeployedVehicle != nullptr;
	const bool Riding = pP->GetCharacter() && pP->GetCharacter()->m_OnVehicle;
	str_format(aLine, sizeof(aLine), "类型：%s  状态：%s",
		pP->m_VehicleType == 1 ? "飞行器" : "未知",
		Riding ? "驾驶中" : (Deployed ? "已部署" : "未部署"));
	V.Info(aLine);
	V.Option("ccv_vehicle", Deployed ? "收回载具" : "部署载具");
	V.Info("改名：选下方，Reason 填新名字");
	V.Option("ccv_vehiclename", "给载具改名");
	V.Footer();
}

static void RenderMMOMountPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	RenderMMOVehiclePage(ClientID, pVote, pGS);
}

static void RenderMMOHousePage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_LIFESTYLE, "房屋");
	char aLine[VOTE_DESC_LENGTH];
	if(pP && pP->m_HasHouse)
	{
		str_format(aLine, sizeof(aLine), "已拥有房屋  Lv.%d", pP->m_HouseLevel);
		V.Info(aLine);
		V.Option("ccv_house_tp", "传送回家");
	}
	else
	{
		V.Info("尚未购买房屋");
		V.Option("ccv_house_buy", "购买房屋");
	}
	V.Footer();
}

static void RenderMMOMarriagePage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	CPlayer *pP = pGS->m_apPlayers[ClientID];
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_LIFESTYLE, "婚姻");
	char aLine[VOTE_DESC_LENGTH];
	CMMOManager *pMMO = pGS->Core() ? pGS->Core()->GetMMOManager() : nullptr;

	if(pP && pP->m_SpouseAccountID > 0)
	{
		char aSpouse[64];
		char aDate[16];
		VoteFormatDateYYYYMMDD(pP->m_MarriageDate, aDate, sizeof(aDate));
		if(pMMO && pMMO->LookupAccountName(pP->m_SpouseAccountID, aSpouse, sizeof(aSpouse)))
			str_format(aLine, sizeof(aLine), "配偶：%s（%s）", aSpouse, aDate);
		else
			str_format(aLine, sizeof(aLine), "已婚（%s）", aDate);
		V.Info(aLine);
		V.Option("ccv_divorce", "申请离婚");
	}
	else
	{
		V.Info("当前单身");
		char aProposer[MAX_NAME_LENGTH];
		if(pMMO && pMMO->GetIncomingMarriageProposerName(ClientID, aProposer, sizeof(aProposer)))
		{
			str_format(aLine, sizeof(aLine), "💍 %s 向你求婚", aProposer);
			V.Info(aLine);
			V.Option("ccv_marry_accept", "接受求婚");
		}
		V.Info("求婚：选下方，Reason 填对方玩家名");
		V.Option("ccv_marry", "向玩家求婚");
	}
	V.Footer();
}

static void RenderMMOChatPage(int ClientID, CVoteMenuManager *pVote, CGameContext *pGS)
{
	(void)pGS;
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_SOCIAL, "聊天与交易");
	V.Info("私聊：/w <玩家名> <消息>");
	V.Info("世界：/world <消息>  或  /wc <消息>");
	V.Info("队伍：/group <消息>（队内频道）");
	V.Info("交易：/trade <玩家名> 发起交易");
	V.Info("好友：投票菜单 → 社交 → 好友");
	V.Footer();
}

bool CMMOManager::OnVoteMenuPage(int ClientID, int Page)
{
	CVoteMenuManager *pVote = GetVoteMenu();
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(ClientID) : nullptr;
	if(!pVote || !pP || !pSVote)
		return false;

	switch(Page)
	{
	case PAGE_ATTRIBUTES:
		ShowAttributeVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_BACKPACK:
		RenderMMOInventoryVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_ITEM:
		RenderMMOItemVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_EQUIP:
		RenderMMOEquipVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_GROUP:
		if(pSVote->m_aExtraText[0] && str_comp(pSVote->m_aExtraText, "kick") == 0)
			ShowGroupKickVotes(ClientID, pVote, pP, pSVote);
		else
			ShowGroupVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_MAILBOX:
		ShowMailboxVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_MAIL_READ:
		ShowMailReadVotes(ClientID, pVote, pP, pSVote, pSVote->m_Select[SPlayerVote::ITEM]);
		return true;
	case VOTE_PAGE_MMO_SOCIAL:
		RenderMMOSocialHub(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_ACTIVITIES:
		RenderMMOActivitiesHub(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_ECONOMY:
		RenderMMOEconomyHub(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_LIFESTYLE:
		RenderMMOLifestyleHub(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_PVP:
		RenderMMOPvpHub(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_FRIENDS:
		RenderMMOFriendsPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_RANKING:
		RenderMMORankingPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_AUCTION:
		RenderMMOAuctionPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_SHOP:
		RenderMMOShopPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_ENCHANT:
		RenderMMOEnchantPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_GUILD:
		RenderMMOGuildPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_GUILD_WAR:
		RenderMMOGuildWarPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_GUILD_WAR_MODE:
		RenderMMOGuildWarModePage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_GUILD_WAR_MAP:
		RenderMMOGuildWarMapPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_BOSS:
		RenderMMOBossPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_MOUNT:
		RenderMMOMountPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_HOUSE:
		RenderMMOHousePage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_MARRIAGE:
		RenderMMOMarriagePage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_CHAT:
		RenderMMOChatPage(ClientID, pVote, GS());
		return true;
	case VOTE_PAGE_MMO_AUCTION_LIST:
		RenderAuctionListVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_AUCTION_SELL:
		RenderAuctionSellVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_AUCTION_SELL_PRICE:
		RenderAuctionSellPriceVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_SHOP_LIST:
		RenderShopListVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_SHOP_ITEMS:
		RenderShopItemsVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_ENCHANT_SELECT:
		RenderEnchantSelectVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_FASHION_SELECT:
		RenderFashionSelectVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_FRIENDS_LIST:
		RenderFriendsListVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_FRIEND_REQUESTS:
		RenderFriendRequestsVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_RANKING_LEVEL:
		RenderRankingLevelVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_RANKING_GOLD:
		RenderRankingGoldVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_GUILD_BROWSE:
		RenderGuildBrowseVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_GUILD_DETAIL:
		RenderGuildDetailVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_GUILD_MEMBERS:
		RenderGuildMembersVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_GUILD_REQUESTS:
		RenderGuildRequestsVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_RECYCLE:
		RenderRecycleVotes(ClientID, pVote, pP, pSVote);
		return true;
	case VOTE_PAGE_MMO_RECYCLE_CONFIRM:
		RenderRecycleConfirmVotes(ClientID, pVote, pP, pSVote);
		return true;
	default:
		return false;
	}
}

void CMMOManager::RenderMMOInventoryVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CGameContext *pGS = GS();
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, PAGE_MENU, LocVote(pGS, ClientID, "mmo.inv.title", "背包"));
	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.inv.summary", "金币: %d  格数: %zu"),
		pP->GetStat(AttributeIdentifier::Gold), pP->m_MMOInventory.size());
	V.Info(aLine);

	if(pP->m_MMOInventory.IsEmpty())
	{
		V.Info(LocVote(pGS, ClientID, "mmo.inv.empty", "背包是空的"));
		V.Footer();
		return;
	}

	AppendInventoryFilterTabs(V, pGS, ClientID, pP);

	if(pP->m_InventoryFilterGroup < 0)
	{
		V.Footer();
		return;
	}

	V.GroupLine();
	V.GroupTitle(LocVote(pGS, ClientID, "mmo.inv.list", "物品列表"));

	int Shown = 0;
	for(size_t i = 0; i < pP->m_MMOInventory.size() && Shown < 20; i++)
	{
		const CItem &Item = pP->m_MMOInventory[i];
		if(!Item.IsValid() || !ItemMatchesInventoryFilter(Item, pP))
			continue;

		const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
		if(!pDef)
			continue;

		char aName[48];
		str_copy(aName, LocMMOItemName(pGS, ClientID, pDef), sizeof(aName));
		TruncateUtf8(aName, 22);

		char aCmd[32];
		str_format(aCmd, sizeof(aCmd), "ccv_mmoitem %d", (int)i);
		const bool Equipped = pP->IsMMOWeaponEquipped(Item.GetID()) || pP->m_EquippedSlots.isEquippedItem(Item.GetID());
		str_format(aLine, sizeof(aLine), "%s[%zu] %s x%d",
			Equipped ? "✔ " : "", i, aName, Item.GetValue());
		if(Item.GetEnchant() > 0)
		{
			char aEnch[16];
			str_format(aEnch, sizeof(aEnch), " +%d", Item.GetEnchant());
			str_append(aLine, aEnch, sizeof(aLine));
		}
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		V.Option(aCmd, aLine);
		Shown++;
	}

	if(Shown == 0)
		V.Info(LocVote(pGS, ClientID, "mmo.inv.filter_empty", "该分类下没有物品"));
	else if((int)pP->m_MMOInventory.size() > Shown)
		V.Info(LocVote(pGS, ClientID, "mmo.inv.truncated", "仅显示前 20 格"));

	V.GroupLine();
	V.Option("ccv_mmoequippage", LocVote(pGS, ClientID, "mmo.inv.open_equip", "打开装备栏"));
	V.Footer();
}

void CMMOManager::RenderMMOItemVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	if(!pVote || !pP || !pSVote)
		return;

	CGameContext *pGS = GS();
	const int SlotIdx = pSVote->m_Select[SPlayerVote::ITEM];
	if(SlotIdx < 0 || (size_t)SlotIdx >= pP->m_MMOInventory.size())
	{
		MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_BACKPACK, LocVote(pGS, ClientID, "mmo.item.title", "物品"))
			.Info(LocVote(pGS, ClientID, "mmo.item.missing", "物品不存在"))
			.Footer();
		return;
	}

	const CItem &Item = pP->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	if(!pDef)
	{
		MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_BACKPACK, LocVote(pGS, ClientID, "mmo.item.title", "物品"))
			.Info(LocVote(pGS, ClientID, "mmo.item.unknown", "未知物品"))
			.Footer();
		return;
	}

	char aTitle[48];
	const bool Equipped = pP->IsMMOWeaponEquipped(Item.GetID()) || pP->m_EquippedSlots.isEquippedItem(Item.GetID());
	str_format(aTitle, sizeof(aTitle), "%s%s x%d",
		Equipped ? "✔ " : "", LocMMOItemName(pGS, ClientID, pDef), Item.GetValue());
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_BACKPACK, aTitle);

	char aLine[VOTE_DESC_LENGTH];
	if(Item.GetEnchant() > 0)
	{
		str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.item.enchant", "强化: +%d"), Item.GetEnchant());
		V.Info(aLine);
	}
	const int AtkBonus = pDef->GetWeaponAttackBonus(Item.GetEnchant());
	const int DefBonus = pDef->GetAttributeValue(AttributeIdentifier::Defense, Item.GetEnchant());
	if(AtkBonus > 0 || DefBonus > 0 || pDef->m_Attack > 0 || pDef->m_Defense > 0)
	{
		if(AtkBonus > 0 || DefBonus > 0)
			str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.item.stats", "攻击 +%d  防御 +%d"), AtkBonus, DefBonus);
		else
			str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.item.stats", "攻击 %d  防御 %d"), pDef->m_Attack, pDef->m_Defense);
		V.Info(aLine);
	}
	const int EngineWeapon = MMOItemTypeToWeapon(pDef->GetType());
	if(EngineWeapon >= 0)
	{
		const int WpnDmgBonus = pDef->GetEngineWeaponDamageBonus(EngineWeapon, Item.GetEnchant());
		if(WpnDmgBonus > 0)
		{
			static const char *s_apWeaponDmgLabel[] = {"锤伤", "枪伤", "霰弹伤", "榴弹伤", "步枪伤"};
			const char *pLabel = EngineWeapon >= 0 && EngineWeapon < (int)(sizeof(s_apWeaponDmgLabel) / sizeof(s_apWeaponDmgLabel[0]))
				? s_apWeaponDmgLabel[EngineWeapon] : "专精伤";
			str_format(aLine, sizeof(aLine), "%s +%d", pLabel, WpnDmgBonus);
			V.Info(aLine);
		}
	}
	const int AttackSpd = pDef->GetAttackSpeedPercent(Item.GetEnchant());
	if(AttackSpd != 100)
	{
		str_format(aLine, sizeof(aLine), "攻速 %d%%", AttackSpd);
		V.Info(aLine);
	}
	const int AmmoBonus = pDef->GetAttributeValue(AttributeIdentifier::Ammo, Item.GetEnchant());
	if(AmmoBonus > 0)
	{
		str_format(aLine, sizeof(aLine), "弹药 +%d", AmmoBonus);
		V.Info(aLine);
	}
	const int AmmoRegen = pDef->GetAttributeValue(AttributeIdentifier::AmmoRegen, Item.GetEnchant());
	if(AmmoRegen > 0 && AmmoRegen != 100)
	{
		str_format(aLine, sizeof(aLine), "回弹 %d%%", AmmoRegen);
		V.Info(aLine);
	}
	if(pDef->HasWeaponProfile())
	{
		const SMMOWeaponProfile &WP = pDef->GetWeaponProfile();
		char aTraits[256];
		aTraits[0] = '\0';
		auto AppendTrait = [&](const char *pText)
		{
			if(aTraits[0])
				str_append(aTraits, "  ", sizeof(aTraits));
			str_append(aTraits, pText, sizeof(aTraits));
		};
		char aPart[64];
		if(WP.m_DamageMulPercent != 100)
		{
			str_format(aPart, sizeof(aPart), "伤害 %d%%", WP.m_DamageMulPercent);
			AppendTrait(aPart);
		}
		if(WP.m_ProjSpeedPercent != 100)
		{
			str_format(aPart, sizeof(aPart), "弹速 %d%%", WP.m_ProjSpeedPercent);
			AppendTrait(aPart);
		}
		if(WP.m_ProjRangePercent != 100)
		{
			str_format(aPart, sizeof(aPart), "射程 %d%%", WP.m_ProjRangePercent);
			AppendTrait(aPart);
		}
		if(WP.m_SpreadDegrees > 0)
		{
			str_format(aPart, sizeof(aPart), "散布 %d°", WP.m_SpreadDegrees);
			AppendTrait(aPart);
		}
		if(WP.m_ReloadPercent != 100)
		{
			str_format(aPart, sizeof(aPart), "冷却 %d%%", WP.m_ReloadPercent);
			AppendTrait(aPart);
		}
		if(WP.m_ForcePercent != 100)
		{
			str_format(aPart, sizeof(aPart), "击退 %d%%", WP.m_ForcePercent);
			AppendTrait(aPart);
		}
		if(WP.m_CritChance > 0)
		{
			str_format(aPart, sizeof(aPart), "暴击 %d%%", WP.m_CritChance);
			AppendTrait(aPart);
		}
		if(WP.m_LifestealPercent > 0)
		{
			str_format(aPart, sizeof(aPart), "吸血 %d%%", WP.m_LifestealPercent);
			AppendTrait(aPart);
		}
		if(WP.m_Multishot > 0)
		{
			str_format(aPart, sizeof(aPart), "连发 +%d", WP.m_Multishot);
			AppendTrait(aPart);
		}
		if(WP.m_FanShots >= 2)
		{
			str_format(aPart, sizeof(aPart), "扇形 %d发 ±%.2frad", WP.m_FanShots, WP.m_FanSpreadRad / 100.f);
			AppendTrait(aPart);
		}
		if(WP.m_Pierce > 0)
		{
			str_format(aPart, sizeof(aPart), "穿透 %d", WP.m_Pierce);
			AppendTrait(aPart);
		}
		if(WP.m_ShotgunPelletsAdd != 0)
		{
			str_format(aPart, sizeof(aPart), "弹丸 %+d", WP.m_ShotgunPelletsAdd);
			AppendTrait(aPart);
		}
		if(WP.m_ShotgunPelletCount > 0)
		{
			str_format(aPart, sizeof(aPart), "霰弹 ×%d", WP.m_ShotgunPelletCount);
			AppendTrait(aPart);
		}
		if(WP.m_LaserReachPercent != 100)
		{
			str_format(aPart, sizeof(aPart), "激光射程 %d%%", WP.m_LaserReachPercent);
			AppendTrait(aPart);
		}
		if(WP.m_RecoilPercent != 100)
		{
			str_format(aPart, sizeof(aPart), "后坐 %d%%", WP.m_RecoilPercent);
			AppendTrait(aPart);
		}
		if(WP.m_Explosive)
			AppendTrait("爆炸弹");
		if(WP.m_Pulse)
			AppendTrait("脉冲");
		if(WP.m_FireStyle != EMMOFireStyle::Default)
		{
			const char *pStyleLabel = FireStyleLabelZh(WP.m_FireStyle);
			if(pStyleLabel)
				AppendTrait(pStyleLabel);
		}
		if(aTraits[0])
		{
			str_format(aLine, sizeof(aLine), "武器特性: %s", aTraits);
			V.Info(aLine);
		}
	}
	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.item.level_req", "等级需求: %d"), pDef->GetLevelReq());
	V.Info(aLine);

	const char *pDesc = pGS->Loc(ClientID, pDef->GetDescriptionKey(), pDef->GetDescription());
	if(pDesc && pDesc[0])
		V.Info(pDesc);

	if(pDef->GetGroup() == ItemGroup::Quest)
	{
		V.Info(LocVote(pGS, ClientID, "mmo.item.quest_hint", "任务物品，无法装备或丢弃。"));
		V.Footer();
		return;
	}

	V.GroupLine();
	V.GroupTitle(LocVote(pGS, ClientID, "mmo.item.actions", "操作"));

	char aCmd[48];
	if(pDef->IsEquipmentSlot())
	{
		if(Equipped)
		{
			str_format(aCmd, sizeof(aCmd), "ccv_mmounequipid %d", Item.GetID());
			V.Option(aCmd, LocVote(pGS, ClientID, "mmo.item.unequip", "卸下"));

			if(CPlayer::IsMMOWeaponItemType(pDef->GetType()))
			{
				const bool IsMelee = pDef->GetType() == ItemType::EquipHammer;
				const int CurIdx = IsMelee ? pP->FindMeleeLoadoutIndex(Item.GetID()) : pP->FindRangedLoadoutIndex(Item.GetID());
				V.GroupLine();
				V.GroupTitle(IsMelee ? "调整近战栏位" : "调整远程栏位");
				for(int s = 0; s < CPlayer::MMO_WEAPON_LOADOUT_SIZE; s++)
				{
					if(s == CurIdx)
						continue;
					str_format(aCmd, sizeof(aCmd), "ccv_wloadslot %d %d", Item.GetID(), s);
					str_format(aLine, sizeof(aLine), "移动到栏位 %d", s + 1);
					V.Option(aCmd, aLine);
				}
			}
		}
		else if(CPlayer::IsMMOWeaponItemType(pDef->GetType()))
		{
			const bool IsMelee = pDef->GetType() == ItemType::EquipHammer;
			V.GroupLine();
			str_format(aLine, sizeof(aLine), "选择%s栏位 (1-4)", IsMelee ? "近战" : "远程");
			V.Info(aLine);
			for(int s = 0; s < CPlayer::MMO_WEAPON_LOADOUT_SIZE; s++)
			{
				const int *pLoadout = IsMelee ? pP->m_aMeleeLoadout : pP->m_aRangedLoadout;
				const int Occupied = pLoadout[s];
				str_format(aCmd, sizeof(aCmd), "ccv_mmoequip %d %d", SlotIdx, s);
				if(Occupied > 0)
				{
					const CMMOItemDescription *pOcc = CMMOItemDescription::Get(Occupied);
					str_format(aLine, sizeof(aLine), "栏位 %d (替换 %s)", s + 1,
						LocMMOItemName(pGS, ClientID, pOcc));
				}
				else
					str_format(aLine, sizeof(aLine), "栏位 %d (空)", s + 1);
				TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
				V.Option(aCmd, aLine);
			}
		}
		else
		{
			str_format(aCmd, sizeof(aCmd), "ccv_mmoequip %d %d", SlotIdx, 0);
			V.Option(aCmd, LocVote(pGS, ClientID, "mmo.item.equip", "装备"));
		}
	}

	if(pDef->GetType() == ItemType::UseSingle || pDef->GetType() == ItemType::UseMultiple)
	{
		str_format(aCmd, sizeof(aCmd), "ccv_use %d", SlotIdx);
		V.Option(aCmd, LocVote(pGS, ClientID, "mmo.item.use", "使用"));
	}

	if((Item.GetID() == 194 || Item.GetID() == 195) && pP->m_VehicleType <= 0)
	{
		str_format(aCmd, sizeof(aCmd), "ccv_vehicle_activate %d", SlotIdx);
		V.Option(aCmd, "激活载具");
	}

	if(pDef->GetGroup() == ItemGroup::Potion)
	{
		str_format(aCmd, sizeof(aCmd), "ccv_mmoequip %d", SlotIdx);
		V.Option(aCmd, Equipped
			? LocVote(pGS, ClientID, "mmo.item.potion_auto_off", "关闭自动使用")
			: LocVote(pGS, ClientID, "mmo.item.potion_auto_on", "开启自动使用"));
	}

	if(pDef->IsStackable() && Item.GetValue() > 1)
	{
		V.Info(LocVote(pGS, ClientID, "mmo.item.split_hint", "拆分：选下方选项，在 Reason 填写数量"));
		str_format(aCmd, sizeof(aCmd), "ccv_mmosplit %d", SlotIdx);
		V.Option(aCmd, LocVote(pGS, ClientID, "mmo.item.split", "拆分堆叠"));
	}

	if(pDef->CanDrop() && pDef->GetType() != ItemType::EquipTitle && !Equipped)
	{
		V.Info(LocVote(pGS, ClientID, "mmo.item.drop_hint", "丢弃：选下方选项，Reason 可填数量（默认全部）"));
		str_format(aCmd, sizeof(aCmd), "ccv_mmodrop %d", SlotIdx);
		V.Option(aCmd, LocVote(pGS, ClientID, "mmo.item.drop", "丢弃"));
	}

	if(pDef->CanTrade() && CalcSellUnitPrice(pDef) > 0 && !Equipped)
	{
		str_format(aCmd, sizeof(aCmd), "ccv_ah_sellpick %d", SlotIdx);
		V.Option(aCmd, LocVote(pGS, ClientID, "mmo.item.auction", "上架拍卖行"));
	}

	if(IsEnchantableBagItem(Item))
	{
		str_format(aCmd, sizeof(aCmd), "ccv_enchantpick %d", SlotIdx);
		V.Option(aCmd, LocVote(pGS, ClientID, "mmo.item.enchant_action", "强化"));
	}

	V.Footer();
}

void CMMOManager::RenderMMOEquipVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	pP->RecalcMMOStats();
	char aLine[VOTE_DESC_LENGTH];
	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, PAGE_MENU, "装备栏");
	str_format(aLine, sizeof(aLine), "攻击 %d  防御 %d  HP %d",
		maximum(1, pP->m_MMOAttack), maximum(1, pP->m_MMODefense), pP->GetBaseMaxHealth());
	V.Info(aLine);

	static const ItemType s_aShowTypes[] = {
		ItemType::EquipHelmetTank, ItemType::EquipHelmetDPS, ItemType::EquipHelmetHealer,
		ItemType::EquipArmorTank, ItemType::EquipArmorDPS, ItemType::EquipArmorHealer,
		ItemType::EquipGloves, ItemType::EquipEidolon,
	};

	V.GroupLine();
	V.GroupTitle("近战武器 · 键1 + 滚轮");
	for(int i = 0; i < CPlayer::MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		const int ItemID = pP->m_aMeleeLoadout[i];
		if(ItemID > 0)
		{
			const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
			str_format(aLine, sizeof(aLine), "栏位 %d: %s", i + 1, LocMMOItemName(GS(), ClientID, pDef));
		}
		else
			str_format(aLine, sizeof(aLine), "栏位 %d: (空)", i + 1);
		if(ItemID > 0)
		{
			char aCmd[32];
			str_format(aCmd, sizeof(aCmd), "ccv_mmounequipid %d", ItemID);
			V.Option(aCmd, aLine);
		}
		else
			V.Info(aLine);
	}

	V.GroupLine();
	V.GroupTitle("远程武器 · 键2 + 滚轮");
	for(int i = 0; i < CPlayer::MMO_WEAPON_LOADOUT_SIZE; i++)
	{
		const int ItemID = pP->m_aRangedLoadout[i];
		if(ItemID > 0)
		{
			const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
			str_format(aLine, sizeof(aLine), "栏位 %d: %s", i + 1, LocMMOItemName(GS(), ClientID, pDef));
		}
		else
			str_format(aLine, sizeof(aLine), "栏位 %d: (空)", i + 1);
		if(ItemID > 0)
		{
			char aCmd[32];
			str_format(aCmd, sizeof(aCmd), "ccv_mmounequipid %d", ItemID);
			V.Option(aCmd, aLine);
		}
		else
			V.Info(aLine);
	}

	bool Any = false;
	V.GroupLine();
	V.GroupTitle("其他装备");

	for(ItemType Type : s_aShowTypes)
	{
		const int ItemID = pP->m_EquippedSlots.getSlot(Type);
		if(ItemID < 0)
			continue;
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
		str_format(aLine, sizeof(aLine), "%s: %s", ItemTypeToName(Type), LocMMOItemName(GS(), ClientID, pDef));
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		char aCmd[32];
		str_format(aCmd, sizeof(aCmd), "ccv_mmounequipid %d", ItemID);
		V.Option(aCmd, aLine);
		Any = true;
	}

	if(!Any)
		V.Info("未装备任何物品");

	V.GroupLine();
	V.Option("ccv_mmo", "打开背包");
	V.Footer();
}

void CMMOManager::ShowAttributeVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	pP->RecalcMMOStats();
	char aLine[VOTE_DESC_LENGTH];
	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, PAGE_MENU, "角色属性");

	const int Level = pP->GetStat(AttributeIdentifier::Level);
	str_format(aLine, sizeof(aLine), "等级 %d  经验 %d / %d",
		Level, pP->GetStat(AttributeIdentifier::Experience), ExpForLevel(Level));
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), "技能点 %d  金币 %d",
		pP->GetStat(AttributeIdentifier::SkillPoints), pP->GetStat(AttributeIdentifier::Gold));
	V.Info(aLine);

	const int STR = pP->GetStat(AttributeIdentifier::STR);
	const int DEX = pP->GetStat(AttributeIdentifier::DEX);
	const int CON = pP->GetStat(AttributeIdentifier::CON);
	const int INT = pP->GetStat(AttributeIdentifier::INT);
	const int WIS = pP->GetStat(AttributeIdentifier::WIS);
	const int CHA = pP->GetStat(AttributeIdentifier::CHA);
	V.GroupLine();
	V.GroupTitle("六维属性");
	str_format(aLine, sizeof(aLine), "STR %d  DEX %d  CON %d", STR, DEX, CON);
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), "INT %d  WIS %d  CHA %d", INT, WIS, CHA);
	V.Info(aLine);

	V.GroupLine();
	V.GroupTitle("战斗效能");
	str_format(aLine, sizeof(aLine), "近战 +%d  远程 +%d  防御 %d",
		pP->GetEffectiveMeleeAttack(), pP->GetEffectiveRangedAttack(), pP->GetEffectiveDefense());
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), "MMO 攻 %d  防 %d  HP %d",
		maximum(1, pP->m_MMOAttack), maximum(1, pP->m_MMODefense), pP->GetBaseMaxHealth());
	V.Info(aLine);

	if(pP->GetStat(AttributeIdentifier::SkillPoints) > 0)
	{
		V.GroupLine();
		V.GroupTitle("分配属性 (+2)");
		V.Option("ccv_addstatvote str", "力量 STR");
		V.Option("ccv_addstatvote dex", "敏捷 DEX");
		V.Option("ccv_addstatvote con", "体质 CON");
		V.Option("ccv_addstatvote int", "智力 INT");
		V.Option("ccv_addstatvote wis", "智慧 WIS");
		V.Option("ccv_addstatvote cha", "魅力 CHA");
	}
	else
	{
		V.Info("无可用技能点");
	}

	V.Footer();
}

void CMMOManager::RenderAuctionListVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_AUCTION, "拍卖行列表");
	char aLine[VOTE_DESC_LENGTH];
	VoteAppendWallet(V, pP);
	V.Info("点击购买；[我的] 可取消自己的挂单");

	CSqlConnectionPool *pPool = GS()->Accounts() ? GS()->Accounts()->GetSqlPool() : nullptr;
	if(!pPool || !pPool->IsInitialized() || pP->GetAccountId() <= 0)
	{
		V.Info("请先登录或数据库不可用");
		V.Footer();
		return;
	}

	void *pRaw = pPool->Acquire();
	if(!pRaw)
	{
		V.Info("数据库连接失败");
		V.Footer();
		return;
	}
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `ListingID`, `SellerUserID`, `ItemID`, `ItemCount`, `ItemEnchant`, `Price` "
		"FROM `tw_auction_listings` ORDER BY `Price` ASC LIMIT 20");
	if(!SqlExecQuery(pSql, GS()->Config(), aQuery))
	{
		pPool->Release(pRaw);
		V.Info("查询拍卖行失败");
		V.Footer();
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes || mysql_num_rows(pRes) == 0)
	{
		if(pRes) mysql_free_result(pRes);
		pPool->Release(pRaw);
		V.Info("拍卖行暂无物品");
		V.Footer();
		return;
	}

	const int64 MyAID = pP->GetAccountId();
	int Count = 0;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		const int ListingID = Row[0] ? atoi(Row[0]) : 0;
		const int64 SellerAID = Row[1] ? (int64)atoll(Row[1]) : 0;
		const int ItemID = Row[2] ? atoi(Row[2]) : 0;
		const int ItemCount = Row[3] ? atoi(Row[3]) : 0;
		const int ItemEnchant = Row[4] ? atoi(Row[4]) : 0;
		const int Price = Row[5] ? atoi(Row[5]) : 0;

		const CMMOItemDescription *pDesc = CMMOItemDescription::Get(ItemID);
		const char *pItemName = pDesc ? LocMMOItemName(GS(), ClientID, pDesc) : "?";

		char aCmd[48];
		if(SellerAID == MyAID)
		{
			str_format(aCmd, sizeof(aCmd), "ccv_ah_cancelpick %d", ListingID);
			str_format(aLine, sizeof(aLine), "[我的] %s x%d +%d %dg", pItemName, ItemCount, ItemEnchant, Price);
		}
		else
		{
			str_format(aCmd, sizeof(aCmd), "ccv_ah_buy %d", ListingID);
			str_format(aLine, sizeof(aLine), "#%d %s x%d +%d %dg", ListingID, pItemName, ItemCount, ItemEnchant, Price);
		}
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		V.Option(aCmd, aLine);
		Count++;
	}

	mysql_free_result(pRes);
	pPool->Release(pRaw);

	if(Count >= 20)
		V.Info("仅显示前 20 条");

	V.Footer();
}

void CMMOManager::RenderAuctionSellVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_AUCTION, "上架物品");
	char aLine[VOTE_DESC_LENGTH];
	VoteAppendWallet(V, pP);
	V.Info("选择物品后在下一页填写价格");

	if(pP->m_MMOInventory.IsEmpty())
	{
		V.Info("背包是空的");
		V.Footer();
		return;
	}

	int Shown = 0;
	for(size_t i = 0; i < pP->m_MMOInventory.size() && Shown < 20; i++)
	{
		const CItem &Item = pP->m_MMOInventory[i];
		if(!Item.IsValid())
			continue;
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
		if(!pDef)
			continue;

		char aName[48];
		str_copy(aName, LocMMOItemName(GS(), ClientID, pDef), sizeof(aName));
		TruncateUtf8(aName, 22);

		char aCmd[32];
		str_format(aCmd, sizeof(aCmd), "ccv_ah_sellpick %d", (int)i);
		str_format(aLine, sizeof(aLine), "[%zu] %s x%d", i, aName, Item.GetValue());
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		V.Option(aCmd, aLine);
		Shown++;
	}

	if((int)pP->m_MMOInventory.size() > Shown)
		V.Info("仅显示前 20 格");

	V.Footer();
}

void CMMOManager::RenderAuctionSellPriceVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	if(!pVote || !pP || !pSVote)
		return;

	const int SlotIdx = pSVote->m_Select[SPlayerVote::ITEM];
	if(SlotIdx < 0 || (size_t)SlotIdx >= pP->m_MMOInventory.size())
	{
		MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_AUCTION_SELL, "上架")
			.Info("物品不存在")
			.Footer();
		return;
	}

	const CItem &Item = pP->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	char aTitle[48];
	if(pDef)
		str_format(aTitle, sizeof(aTitle), "上架 %s", LocMMOItemName(GS(), ClientID, pDef));
	else
		str_copy(aTitle, "上架物品", sizeof(aTitle));

	MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_AUCTION_SELL, aTitle)
		.Info("在 Reason 填写价格")
		.Option("ccv_ah_sellconfirm", "确认上架")
		.Footer();
}

void CMMOManager::RenderRecycleVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CGameContext *pGS = GS();
	ResetDailySellIfNeeded(pP);
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_ECONOMY, LocVote(pGS, ClientID, "mmo.recycle.title", "物品回收"));
	char aLine[VOTE_DESC_LENGTH];
	VoteAppendWallet(V, pP);

	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.recycle.rate", "回收价约为原价 25%%，另扣 %d%% 手续费"),
		MMO_SELL_TAX_PERCENT);
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.recycle.daily", "今日已回收 %d/%d 金币，%d/%d 次"),
		pP->m_DailySellGold, MMO_SELL_DAILY_GOLD_CAP, pP->m_DailySellCount, MMO_SELL_DAILY_COUNT_CAP);
	V.Info(aLine);

	if(pP->m_MMOInventory.IsEmpty())
	{
		V.Info(LocVote(pGS, ClientID, "mmo.inv.empty", "背包是空的"));
		V.Footer();
		return;
	}

	V.GroupLine();
	V.GroupTitle(LocVote(pGS, ClientID, "mmo.recycle.list", "可回收物品"));

	int Shown = 0;
	for(size_t i = 0; i < pP->m_MMOInventory.size() && Shown < 20; i++)
	{
		const char *pReason = nullptr;
		if(!CanSellItem(pP, (int)i, &pReason))
			continue;

		const CItem &Item = pP->m_MMOInventory[i];
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
		if(!pDef)
			continue;

		const int Unit = CalcSellUnitPrice(pDef);
		const int NetUnit = Unit - (Unit * MMO_SELL_TAX_PERCENT / 100);
		char aName[48];
		str_copy(aName, LocMMOItemName(pGS, ClientID, pDef), sizeof(aName));
		TruncateUtf8(aName, 18);

		char aCmd[32];
		str_format(aCmd, sizeof(aCmd), "ccv_recycle_pick %d", (int)i);
		str_format(aLine, sizeof(aLine), "[%zu] %s x%d  ~%dg/个", i, aName, Item.GetValue(), maximum(1, NetUnit));
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		V.Option(aCmd, aLine);
		Shown++;
	}

	if(Shown == 0)
		V.Info(LocVote(pGS, ClientID, "mmo.recycle.none", "没有可回收的物品"));
	else if((int)pP->m_MMOInventory.size() > Shown)
		V.Info(LocVote(pGS, ClientID, "mmo.inv.truncated", "仅显示前 20 格"));

	V.Footer();
}

void CMMOManager::RenderRecycleConfirmVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	if(!pVote || !pP || !pSVote)
		return;

	CGameContext *pGS = GS();
	const int SlotIdx = pSVote->m_Select[SPlayerVote::ITEM];
	if(SlotIdx < 0 || (size_t)SlotIdx >= pP->m_MMOInventory.size())
	{
		MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_RECYCLE, LocVote(pGS, ClientID, "mmo.recycle.title", "物品回收"))
			.Info(LocVote(pGS, ClientID, "mmo.item.missing", "物品不存在"))
			.Footer();
		return;
	}

	const CItem &Item = pP->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	const char *pReason = nullptr;
	if(!CanSellItem(pP, SlotIdx, &pReason))
	{
		MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_RECYCLE, LocVote(pGS, ClientID, "mmo.recycle.title", "物品回收"))
			.Info(pReason ? pReason : LocVote(pGS, ClientID, "mmo.recycle.blocked", "无法回收"))
			.Footer();
		return;
	}

	char aTitle[48];
	str_format(aTitle, sizeof(aTitle), LocVote(pGS, ClientID, "mmo.recycle.confirm_title", "回收 %s"),
		LocMMOItemName(pGS, ClientID, pDef));
	CVoteWrapper V = MMOPage(ClientID, pGS, pVote, VOTE_PAGE_MMO_RECYCLE, aTitle);

	char aLine[VOTE_DESC_LENGTH];
	const int Unit = CalcSellUnitPrice(pDef);
	const int NetUnit = maximum(1, Unit - (Unit * MMO_SELL_TAX_PERCENT / 100));
	const int NetTotal = NetUnit * Item.GetValue();
	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.recycle.qty", "数量: %d"), Item.GetValue());
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.recycle.unit", "单价约 %d 金币（税后）"), NetUnit);
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.recycle.total", "预计获得 %d 金币"), NetTotal);
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), LocVote(pGS, ClientID, "mmo.recycle.daily", "今日已回收 %d/%d 金币，%d/%d 次"),
		pP->m_DailySellGold, MMO_SELL_DAILY_GOLD_CAP, pP->m_DailySellCount, MMO_SELL_DAILY_COUNT_CAP);
	V.Info(aLine);

	V.GroupLine();
	V.Info(LocVote(pGS, ClientID, "mmo.recycle.qty_hint", "Reason 可填回收数量（默认全部）"));
	char aCmd[48];
	str_format(aCmd, sizeof(aCmd), "ccv_recycle_confirm %d", SlotIdx);
	V.Option(aCmd, LocVote(pGS, ClientID, "mmo.recycle.confirm", "确认回收"));
	V.Footer();
}

void CMMOManager::RenderShopListVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote)
		return;

	static const char *s_apShops[] = {"quest_master", "shopkeeper", "healer", "blacksmith", "tea_house"};
	static const char *s_apShopNames[] = {"冒险者装备", "杂货店", "治疗补给", "铁匠材料", "茶馆特供"};
	static const char *s_apShopDesc[] = {"武器与防具", "消耗品与材料", "药水与恢复", "矿石与强化", "食物与装饰"};

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_SHOP, "附近商人");
	if(pP)
		VoteAppendWallet(V, pP);
	char aCmd[48];
	char aLine[VOTE_DESC_LENGTH];
	int Shown = 0;
	for(int i = 0; i < (int)(sizeof(s_apShops) / sizeof(s_apShops[0])); i++)
	{
		if(!FindShopByNpcID(s_apShops[i]))
			continue;
		if(pP && !IsPlayerNearServiceNpc(GS(), pP, s_apShops[i]))
			continue;
		str_format(aCmd, sizeof(aCmd), "ccv_shopnpc %s", s_apShops[i]);
		str_format(aLine, sizeof(aLine), "%s — %s", s_apShopNames[i], s_apShopDesc[i]);
		V.Option(aCmd, aLine);
		Shown++;
	}
	if(Shown == 0)
		V.Info("附近没有可交易的商人，请走到商人旁边。");
	V.Footer();
}

void CMMOManager::RenderShopItemsVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	if(!pVote || !pP || !pSVote || !pSVote->m_aExtraText[0])
		return;

	const SShopEntry *pShop = FindShopByNpcID(pSVote->m_aExtraText);
	char aTitle[48];
	str_format(aTitle, sizeof(aTitle), "%s", pShop ? pShop->m_pName : pSVote->m_aExtraText);
	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_SHOP_LIST, aTitle);
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[48];

	if(!pShop)
	{
		V.Info("未找到该商店");
		V.Footer();
		return;
	}

	str_format(aLine, sizeof(aLine), "金币: %d", pP->GetStat(AttributeIdentifier::Gold));
	V.Info(aLine);

	for(int i = 0; i < pShop->m_NumItems; i++)
	{
		const SShopItem &It = pShop->m_Items[i];
		str_format(aCmd, sizeof(aCmd), "ccv_shopbuy %s %d", pShop->m_pNpcID, It.m_ItemID);
		str_format(aLine, sizeof(aLine), "#%d %s - %d 金币", It.m_ItemID, It.m_pName, It.m_Price);
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		V.Option(aCmd, aLine);
	}

	V.Footer();
}

void CMMOManager::RenderEnchantSelectVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_ENCHANT, "装备强化");
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[32];

	VoteAppendWallet(V, pP);
	V.Info("仅可强化背包中的武器");

	bool Any = false;
	for(size_t i = 0; i < pP->m_MMOInventory.size() && i < 20; i++)
	{
		const CItem &Item = pP->m_MMOInventory[i];
		if(!IsEnchantableBagItem(Item))
			continue;

		const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
		char aName[48];
		str_copy(aName, LocMMOItemName(GS(), ClientID, pDef), sizeof(aName));
		TruncateUtf8(aName, 20);

		str_format(aCmd, sizeof(aCmd), "ccv_enchantpick %d", (int)i);
		str_format(aLine, sizeof(aLine), "[%zu] %s +%d", i, aName, Item.GetEnchant());
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		V.Option(aCmd, aLine);
		Any = true;
	}

	if(!Any)
		V.Info("背包中没有可强化的武器");

	V.Footer();
}

void CMMOManager::RenderFashionSelectVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_LIFESTYLE, "时装");
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[32];

	if(pP->m_FashionItemID > 0)
	{
		const CMMOItemDescription *pCur = CMMOItemDescription::Get(pP->m_FashionItemID);
		str_format(aLine, sizeof(aLine), "当前: %s", LocMMOItemName(GS(), ClientID, pCur));
		V.Info(aLine);
	}

	if(pP->m_MMOInventory.IsEmpty())
	{
		V.Info("背包是空的");
		V.Option("ccv_fashion clear", "清除时装");
		V.Footer();
		return;
	}

	int Shown = 0;
	for(size_t i = 0; i < pP->m_MMOInventory.size() && Shown < 20; i++)
	{
		const CItem &Item = pP->m_MMOInventory[i];
		if(!Item.IsValid())
			continue;
		const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
		if(!pDef)
			continue;

		char aName[48];
		str_copy(aName, LocMMOItemName(GS(), ClientID, pDef), sizeof(aName));
		TruncateUtf8(aName, 22);

		str_format(aCmd, sizeof(aCmd), "ccv_fashionpick %d", (int)i);
		str_format(aLine, sizeof(aLine), "[%zu] %s", i, aName);
		TruncateUtf8(aLine, VOTE_DESC_LENGTH - 4);
		V.Option(aCmd, aLine);
		Shown++;
	}

	V.Option("ccv_fashion clear", "清除时装");
	V.Footer();
}

void CMMOManager::RenderFriendsListVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_FRIENDS, "我的好友");
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[VOTE_CMD_LENGTH];

	std::vector<SFriendListEntry> Friends;
	if(!LoadFriendListDetails(pP, Friends) || Friends.empty())
	{
		V.Info("好友列表为空");
		V.Info("返回上级可发送好友申请");
		V.Footer();
		return;
	}

	str_format(aLine, sizeof(aLine), "共 %d 位好友", (int)Friends.size());
	V.Info(aLine);

	const int MaxShow = 15;
	for(int i = 0; i < (int)Friends.size() && i < MaxShow; i++)
	{
		const SFriendListEntry &F = Friends[i];
		char aStatus[64];
		if(F.m_Online)
			str_copy(aStatus, "在线", sizeof(aStatus));
		else if(F.m_LastOnlineAt > 0)
		{
			const time_t Now = time(nullptr);
			const int Diff = (int)(Now - F.m_LastOnlineAt);
			if(Diff < 60)
				str_copy(aStatus, "离线 · 刚刚", sizeof(aStatus));
			else if(Diff < 3600)
				str_format(aStatus, sizeof(aStatus), "离线 · %d 分钟前", Diff / 60);
			else if(Diff < 86400)
				str_format(aStatus, sizeof(aStatus), "离线 · %d 小时前", Diff / 3600);
			else
				str_format(aStatus, sizeof(aStatus), "离线 · %d 天前", Diff / 86400);
		}
		else
			str_copy(aStatus, "离线 · 未知", sizeof(aStatus));

		str_format(aLine, sizeof(aLine), "%s  [%s]", F.m_aName, aStatus);
		str_format(aCmd, sizeof(aCmd), "ccv_friend_remove %s", F.m_aName);
		V.Option(aCmd, aLine);
	}

	if((int)Friends.size() > MaxShow)
		V.Info("仅显示前 15 位，其余请用 /friend_list");

	V.Footer();
}

void CMMOManager::RenderFriendRequestsVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP || pP->GetAccountId() <= 0)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_FRIENDS, "好友申请");
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[VOTE_CMD_LENGTH];

	std::vector<SFriendRequestEntry> Requests;
	if(!LoadIncomingFriendRequests(pP->GetAccountId(), Requests) || Requests.empty())
	{
		V.Info("暂无待处理的好友申请");
		V.Footer();
		return;
	}

	str_format(aLine, sizeof(aLine), "共 %d 条申请", (int)Requests.size());
	V.Info(aLine);

	for(size_t i = 0; i < Requests.size() && i < 15; i++)
	{
		const SFriendRequestEntry &R = Requests[i];
		str_format(aLine, sizeof(aLine), "接受：%s", R.m_aName);
		str_format(aCmd, sizeof(aCmd), "ccv_friend_accept %s", R.m_aName);
		V.Option(aCmd, aLine);
		str_format(aLine, sizeof(aLine), "拒绝：%s", R.m_aName);
		str_format(aCmd, sizeof(aCmd), "ccv_friend_decline %s", R.m_aName);
		V.Option(aCmd, aLine);
	}

	V.Footer();
}

void CMMOManager::RenderRankingLevelVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_RANKING, "等级排行榜");
	char aLine[VOTE_DESC_LENGTH];

	if(pP && pP->GetAccountId() > 0)
	{
		const int Level = pP->GetStat(AttributeIdentifier::Level);
		const int Exp = pP->GetStat(AttributeIdentifier::Experience);
		const int MyRank = GetPlayerLevelRank(pP->GetAccountId(), Level, Exp);
		if(MyRank > 0)
		{
			str_format(aLine, sizeof(aLine), "你的名次：第 %d 名（Lv.%d  经验 %d）", MyRank, Level, Exp);
			V.Info(aLine);
			V.GroupLine();
		}
	}

	CSqlConnectionPool *pPool = GS()->Accounts() ? GS()->Accounts()->GetSqlPool() : nullptr;
	if(!pPool || !pPool->IsInitialized())
	{
		V.Info("数据库连接失败");
		V.Footer();
		return;
	}

	void *pRaw = pPool->Acquire();
	if(!pRaw)
	{
		V.Info("数据库连接失败");
		V.Footer();
		return;
	}
	MYSQL *pSql = (MYSQL *)pRaw;

	const char *pQuery =
		"SELECT a.`Username`, p.`Level`, p.`Experience` FROM `tw_mmo_players` p "
		"JOIN `tw_Accounts` a ON p.`UserID` = a.`UserID` "
		"ORDER BY p.`Level` DESC, p.`Experience` DESC LIMIT 20";
	if(!SqlExecQuery(pSql, GS()->Config(), pQuery))
	{
		pPool->Release(pRaw);
		V.Info("查询排行榜失败");
		V.Footer();
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		V.Info("排行榜暂无数据");
		V.Footer();
		return;
	}

	int Rank = 0;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		Rank++;
		const char *pName = Row[0] ? Row[0] : "?";
		const int Level = Row[1] ? atoi(Row[1]) : 0;
		const int Exp = Row[2] ? atoi(Row[2]) : 0;
		str_format(aLine, sizeof(aLine), "%d. %s — Lv.%d  经验 %d", Rank, pName, Level, Exp);
		V.Info(aLine);
	}

	if(Rank == 0)
		V.Info("暂无数据");

	mysql_free_result(pRes);
	pPool->Release(pRaw);
	V.Footer();
}

void CMMOManager::RenderRankingGoldVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote)
		return;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_RANKING, "财富排行榜");
	char aLine[VOTE_DESC_LENGTH];

	if(pP && pP->GetAccountId() > 0)
	{
		const int Gold = pP->GetStat(AttributeIdentifier::Gold);
		const int MyRank = GetPlayerGoldRank(pP->GetAccountId(), Gold);
		if(MyRank > 0)
		{
			str_format(aLine, sizeof(aLine), "你的名次：第 %d 名（%d 金币）", MyRank, Gold);
			V.Info(aLine);
			V.GroupLine();
		}
	}

	CSqlConnectionPool *pPool = GS()->Accounts() ? GS()->Accounts()->GetSqlPool() : nullptr;
	if(!pPool || !pPool->IsInitialized())
	{
		V.Info("数据库连接失败");
		V.Footer();
		return;
	}

	void *pRaw = pPool->Acquire();
	if(!pRaw)
	{
		V.Info("数据库连接失败");
		V.Footer();
		return;
	}
	MYSQL *pSql = (MYSQL *)pRaw;

	const char *pQuery =
		"SELECT a.`Username`, p.`Gold` FROM `tw_mmo_players` p "
		"JOIN `tw_Accounts` a ON p.`UserID` = a.`UserID` "
		"ORDER BY p.`Gold` DESC LIMIT 20";
	if(!SqlExecQuery(pSql, GS()->Config(), pQuery))
	{
		pPool->Release(pRaw);
		V.Info("查询排行榜失败");
		V.Footer();
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		V.Info("排行榜暂无数据");
		V.Footer();
		return;
	}

	int Rank = 0;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		Rank++;
		const char *pName = Row[0] ? Row[0] : "?";
		const int Gold = Row[1] ? atoi(Row[1]) : 0;
		str_format(aLine, sizeof(aLine), "%d. %s — %d 金币", Rank, pName, Gold);
		V.Info(aLine);
	}

	if(Rank == 0)
		V.Info("暂无数据");

	mysql_free_result(pRes);
	pPool->Release(pRaw);
	V.Footer();
}

void CMMOManager::RenderGuildBrowseVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CGuildManager *pGuildMgr = Core() ? Core()->GuildManager() : nullptr;
	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_GUILD, "浏览公会");
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[VOTE_CMD_LENGTH];

	if(pP->GetGuildID() >= 0)
	{
		V.Info("你已在公会中，无法申请加入其他公会");
		V.Footer();
		return;
	}

	if(!pGuildMgr || pGuildMgr->GetNumGuilds() == 0)
	{
		V.Info("暂无公会");
		V.Footer();
		return;
	}

	str_format(aLine, sizeof(aLine), "共 %d 个公会", pGuildMgr->GetNumGuilds());
	V.Info(aLine);

	for(int i = 0; i < pGuildMgr->GetNumGuilds() && i < 20; i++)
	{
		const SGuildData *pG = pGuildMgr->GetGuildByIndex(i);
		if(!pG)
			continue;
		str_format(aLine, sizeof(aLine), "%s [%s] Lv.%d  %d/%d",
			pG->m_aName, pG->m_aLeaderName, pG->m_Level, pG->MemberCount(), pG->m_MaxMembers);
		str_format(aCmd, sizeof(aCmd), "ccv_guild_detail %d", pG->m_ID);
		V.Option(aCmd, aLine);
	}

	if(pGuildMgr->GetNumGuilds() > 20)
		V.Info("仅显示前 20 个公会");

	V.Footer();
}

void CMMOManager::RenderGuildDetailVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	if(!pVote || !pP || !pSVote)
		return;

	CGuildManager *pGuildMgr = Core() ? Core()->GuildManager() : nullptr;
	const int GuildID = pSVote->m_Select[SPlayerVote::ITEMLIST];
	SGuildData *pG = pGuildMgr ? pGuildMgr->GetGuild(GuildID) : nullptr;

	char aTitle[VOTE_DESC_LENGTH];
	if(pG)
		str_format(aTitle, sizeof(aTitle), "公会：%s", pG->m_aName);
	else
		str_copy(aTitle, "公会详情", sizeof(aTitle));

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_GUILD_BROWSE, aTitle);
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[VOTE_CMD_LENGTH];

	if(!pG)
	{
		V.Info("找不到该公会");
		V.Footer();
		return;
	}

	str_format(aLine, sizeof(aLine), "会长：%s", pG->m_aLeaderName);
	V.Info(aLine);
	if(pG->m_aTag[0])
	{
		str_format(aLine, sizeof(aLine), "标签：%s", pG->m_aTag);
		V.Info(aLine);
	}
	str_format(aLine, sizeof(aLine), "等级：%d  银行：%d 金币", pG->m_Level, pG->m_Gold);
	V.Info(aLine);
	str_format(aLine, sizeof(aLine), "成员：%d/%d", pG->MemberCount(), pG->m_MaxMembers);
	V.Info(aLine);
	if(pG->m_aMotd[0])
	{
		str_format(aLine, sizeof(aLine), "公告：%s", pG->m_aMotd);
		V.Info(aLine);
	}

	if(pP->GetGuildID() < 0)
	{
		const int AccountID = pP->GetAccountId();
		if(pGuildMgr && pGuildMgr->HasJoinRequest(GuildID, AccountID))
			V.Info("你已提交加入申请，请等待审批");
		else if(pG->MemberCount() >= pG->m_MaxMembers)
			V.Info("该公会成员已满");
		else
		{
			str_format(aCmd, sizeof(aCmd), "ccv_guild_apply %d", GuildID);
			V.Option(aCmd, "申请加入");
		}
	}

	V.Footer();
}

void CMMOManager::RenderGuildMembersVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CGuildManager *pGuildMgr = Core() ? Core()->GuildManager() : nullptr;
	SGuildData *pG = pGuildMgr ? pGuildMgr->GetPlayerGuild(ClientID) : nullptr;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_GUILD, "公会成员");
	char aLine[VOTE_DESC_LENGTH];

	if(!pG)
	{
		V.Info("你不在公会中");
		V.Footer();
		return;
	}

	str_format(aLine, sizeof(aLine), "%s — %d/%d 成员", pG->m_aName, pG->MemberCount(), pG->m_MaxMembers);
	V.Info(aLine);

	static const char *s_apRankNames[] = {"成员", "管理员", "副会长", "会长"};
	for(int i = 0; i < pG->m_Members.size() && i < 25; i++)
	{
		const SGuildMember &M = pG->m_Members[i];
		const char *pRankName = "成员";
		if(M.m_Rank >= GUILDRANK_MEMBER && M.m_Rank < GUILDRANK_NUM_RANKS)
			pRankName = s_apRankNames[M.m_Rank];

		const char *pStatus = "离线";
		CPlayer *pMember = GS()->m_apPlayers[M.m_ClientID];
		if(pMember && pMember->GetCharacter())
			pStatus = "在线";

		str_format(aLine, sizeof(aLine), "%s [%s] %s", M.m_aName, pRankName, pStatus);
		V.Info(aLine);
	}

	if(pG->m_Members.size() > 25)
		V.Info("仅显示前 25 位成员");

	V.Footer();
}

void CMMOManager::RenderGuildRequestsVotes(int ClientID, CVoteMenuManager *pVote, CPlayer *pP, SPlayerVote *pSVote)
{
	(void)pSVote;
	if(!pVote || !pP)
		return;

	CGuildManager *pGuildMgr = Core() ? Core()->GuildManager() : nullptr;
	SGuildData *pG = pGuildMgr ? pGuildMgr->GetPlayerGuild(ClientID) : nullptr;

	CVoteWrapper V = MMOPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_GUILD, "加入申请");
	char aLine[VOTE_DESC_LENGTH];
	char aCmd[VOTE_CMD_LENGTH];

	if(!pG)
	{
		V.Info("你不在公会中");
		V.Footer();
		return;
	}

	EGuildRank Rank = pG->GetRank(ClientID);
	if(Rank != GUILDRANK_LEADER && Rank != GUILDRANK_CO_LEADER && Rank != GUILDRANK_OFFICER)
	{
		V.Info("你没有权限审批申请");
		V.Footer();
		return;
	}

	if(pG->m_JoinRequests.size() == 0)
	{
		V.Info("暂无待处理的加入申请");
		V.Footer();
		return;
	}

	for(int i = 0; i < pG->m_JoinRequests.size() && i < 15; i++)
	{
		const SGuildJoinRequest &R = pG->m_JoinRequests[i];
		str_format(aLine, sizeof(aLine), "%s", R.m_aName);
		V.Info(aLine);
		str_format(aCmd, sizeof(aCmd), "ccv_guild_req_accept %d", R.m_AccountID);
		V.Option(aCmd, "批准");
		str_format(aCmd, sizeof(aCmd), "ccv_guild_req_deny %d", R.m_AccountID);
		V.Option(aCmd, "拒绝");
	}

	if(pG->m_JoinRequests.size() > 15)
		V.Info("仅显示前 15 条申请");

	V.Footer();
}

void CMMOManager::ConShopBuy(int ClientID, const char *pNpcID, int ItemID)
{
	CGameContext *pGame = GS();
	CPlayer *pP = pGame->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChatTo(ClientID, "请先登录。");
		return;
	}

	if(!pNpcID || !pNpcID[0] || !IsPlayerNearServiceNpc(pGame, pP, pNpcID))
	{
		NotifyNpcServiceDenied(pGame, ClientID);
		return;
	}

	const SShopEntry *pShop = FindShopByNpcID(pNpcID);
	if(!pShop)
	{
		pGame->SendChatTo(ClientID, "未找到该商店。");
		return;
	}

	const SShopItem *pShopItem = nullptr;
	for(int i = 0; i < pShop->m_NumItems; i++)
	{
		if(pShop->m_Items[i].m_ItemID == ItemID)
		{
			pShopItem = &pShop->m_Items[i];
			break;
		}
	}
	if(!pShopItem)
	{
		pGame->SendChatTo(ClientID, "该物品不在此商店出售。");
		return;
	}

	if(pP->GetStat(AttributeIdentifier::Gold) < pShopItem->m_Price)
	{
		pGame->SendChatLocF(ClientID, "buy.nogold",
			"金币不足！需要 %d，你只有 %d。", pShopItem->m_Price, pP->GetStat(AttributeIdentifier::Gold));
		return;
	}

	if(!pP->m_MMOInventory.Add(ItemID, 1))
	{
		pGame->SendChatTo(ClientID, "背包已满！");
		return;
	}

	pP->SetStat(AttributeIdentifier::Gold, pP->GetStat(AttributeIdentifier::Gold) - pShopItem->m_Price);
	pP->m_MMODirty = true;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "购买了 %s，花费 %d 金币！(剩余: %d)",
		pShopItem->m_pName, pShopItem->m_Price, pP->GetStat(AttributeIdentifier::Gold));
	pGame->SendChatTo(ClientID, aBuf);
	if(CPlayer *pBuyer = pGame->m_apPlayers[ClientID])
		PlayInteractionSound(pGame->m_World, pBuyer, SOUND_SFX_TRADE);
}

void CMMOManager::DeployVehicle(int ClientID)
{
	CGameContext *pGame = GS();
	CPlayer *pP = pGame->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChatTo(ClientID, "请先登录。");
		return;
	}
	if(pP->m_VehicleType <= 0)
	{
		pGame->SendChatTo(ClientID, "🚁 你没有载具。");
		return;
	}
	if(pP->m_pDeployedVehicle)
	{
		pGame->SendChatTo(ClientID, "🚁 载具已部署。使用收回载具。");
		return;
	}
	CCharacter *pChr = pP->GetCharacter();
	if(!pChr)
	{
		pGame->SendChatTo(ClientID, "🚁 你没有角色。");
		return;
	}

	vec2 SpawnPos = pChr->GetPos() + vec2(48.f, -16.f);
	if(VehicleSpotBlocked(&pGame->m_World, SpawnPos))
	{
		pGame->SendChatTo(ClientID, "🚁 此处无法部署载具。");
		return;
	}

	CVehicle *pVehicle = nullptr;
	if(pP->m_VehicleType == 1)
		pVehicle = new CAircraft(&pGame->m_World, SpawnPos, pP->GetTeam());
	else
	{
		pGame->SendChatTo(ClientID, "🚁 未知载具类型。");
		return;
	}

	pVehicle->BoardDriver(pChr);
	pP->m_pDeployedVehicle = pVehicle;
	pGame->SendChatTo(ClientID, "🚁 载具已部署！方向键移动，钩索上下，跳跃下车。");
}

void CMMOManager::RecallVehicle(int ClientID)
{
	CGameContext *pGame = GS();
	CPlayer *pP = pGame->m_apPlayers[ClientID];
	if(!pP)
		return;

	if(CCharacter *pChr = pP->GetCharacter())
	{
		if(pChr->m_OnVehicle)
		{
			if(CVehicle *pVehicle = VehicleFindByOccupant(&pGame->m_World, ClientID))
				pVehicle->HandleOccupantDismount(ClientID);
		}
	}

	if(pP->m_pDeployedVehicle)
	{
		pP->m_pDeployedVehicle->Reset();
		pP->m_pDeployedVehicle = nullptr;
		pGame->SendChatTo(ClientID, "🚁 载具已收回。");
	}
	else
		pGame->SendChatTo(ClientID, "🚁 没有已部署的载具。");
}

void CMMOManager::ToggleVehicle(int ClientID)
{
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP)
		return;
	if(pP->m_pDeployedVehicle)
		RecallVehicle(ClientID);
	else
		DeployVehicle(ClientID);
}

bool CMMOManager::TryGrantVehicleFromItem(CPlayer *pPlayer, int ItemID, int BagSlot)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
		return false;

	int VehicleType = 0;
	if(ItemID == 194 || ItemID == 195)
		VehicleType = 1;
	if(VehicleType <= 0)
		return false;

	const int ClientID = pPlayer->GetCID();
	if(pPlayer->m_VehicleType > 0)
	{
		GS()->SendChatTo(ClientID, "🚁 你已拥有载具。");
		return false;
	}

	if(BagSlot >= 0)
	{
		if((size_t)BagSlot >= pPlayer->m_MMOInventory.size() ||
			pPlayer->m_MMOInventory[BagSlot].GetID() != ItemID)
		{
			GS()->SendChatTo(ClientID, "背包格无效。");
			return false;
		}
		if(!pPlayer->m_MMOInventory.RemoveAt(BagSlot, 1))
		{
			GS()->SendChatTo(ClientID, "激活失败。");
			return false;
		}
		pPlayer->m_MMODirty = true;
		SaveInventory(pPlayer);
	}

	pPlayer->m_VehicleType = VehicleType;
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(ItemID);
	if(pDef && !pPlayer->m_aVehicleName[0])
		str_copy(pPlayer->m_aVehicleName, GS()->Loc(ClientID, pDef->GetNameKey(), pDef->GetName()), sizeof(pPlayer->m_aVehicleName));
	SaveVehicleData(pPlayer);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "🚁 已获得载具：%s", pPlayer->m_aVehicleName[0] ? pPlayer->m_aVehicleName : "飞行器");
	GS()->SendChatTo(ClientID, aBuf);
	PlayInteractionSound(GS()->m_World, pPlayer, SOUND_SFX_ITEM_EQUIP);
	return true;
}

bool CMMOManager::EquipFashion(CPlayer *pPlayer, int BagSlot)
{
	if(!pPlayer)
		return false;

	const int ClientID = pPlayer->GetCID();
	if(BagSlot < 0 || (size_t)BagSlot >= pPlayer->m_MMOInventory.size())
	{
		GS()->SendChatTo(ClientID, "背包格无效！");
		return false;
	}

	const CItem &Item = pPlayer->m_MMOInventory[BagSlot];
	if(!Item.IsValid())
	{
		GS()->SendChatTo(ClientID, "该背包格没有物品！");
		return false;
	}

	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
	if(!pDesc)
	{
		GS()->SendChatTo(ClientID, "物品数据不存在！");
		return false;
	}

	pPlayer->m_FashionItemID = Item.GetID();
	pPlayer->m_MMODirty = true;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "✨ 已装备时装: %s！", LocMMOItemName(GS(), ClientID, pDesc));
	GS()->SendChatTo(ClientID, aBuf);
	return true;
}
