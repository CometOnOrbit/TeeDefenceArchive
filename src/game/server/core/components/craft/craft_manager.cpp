/* (c) TeeDefenceArchive - 2026 */
#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/commands.h>
#include <game/voting.h>
#include <game/server/account.h>
#include <game/server/core/components/craft/craft_manager.h>
#include <game/server/core/components/npcs/npc_service.h>
#include <game/server/gameworld.h>
#include <game/server/interaction_sound.h>
#include <generated/server_data.h>
#include <game/server/core/components/meta/durability_manager.h>
#include <game/server/entities/turret.h>
#include <game/server/turret_ammo.h>
#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/worldmodes/defence.h>
#include <engine/shared/world_detail.h>
#include <game/server/item_card_ops.h>
#include <game/server/item_system.h>
#include <game/server/player.h>
#include <game/server/turret_ammo.h>

#include <generated/server_data.h>

bool CCraftManager::TryCraftOneItem(int ClientID, int Item, char *pErr, int ErrSize)
{
	auto FailKey = [&](const char *pKey, const char *pDef) -> bool {
		if(pErr && ErrSize > 0)
			str_copy(pErr, GS()->Loc(ClientID, pKey, pDef), ErrSize);
		return false;
	};

	CAccountSystem *pAcc = GS()->Accounts();
	if(!pAcc || !pAcc->IsEnabled())
		return FailKey("craft.err.no_account", "未启用账号，无法合成。");

	CPlayer *pP = ClientID >= 0 && ClientID < MAX_CLIENTS ? GS()->m_apPlayers[ClientID] : nullptr;
	if(!pP || pP->GetAccountId() < 0)
		return FailKey("craft.err.no_login", "请先登录。");

	CItemHelper *pH = GS()->ItemHelper();
	if(!pH || !pH->CheckItemValid(Item))
		return FailKey("craft.err.unknown_item", "未知物品。");
	if(!pH->HasFormula(Item))
		return FailKey("craft.err.no_formula", "该物品无法合成。");

	const int StackMax = pH->GetMax(Item);
	if(StackMax > 0 && pP->m_AccData.m_aItems[Item].m_Num >= StackMax)
		return FailKey("craft.err.stack_max", "该物品已达持有上限。");

	for(int Round = 0; Round < 2; Round++)
	{
		for(int i = 0; i < NUM_ITEM; i++)
		{
			const int Need = pH->GetFormulaNeed(Item, i);
			if(!Need)
				continue;
			if(Round == 0)
			{
				if(pH->ItemExtraBlocksCraftConsume(pP->GetExtraForItem(i)))
				{
					if(pErr && ErrSize > 0)
						GS()->LocFormat(pErr, ErrSize, ClientID, "craft.err.material_has_card", "材料「%s」含有卡牌/零件，无法用于合成。", GS()->LocItemName(ClientID, i));
					return false;
				}
				if(Need > pP->m_AccData.m_aItems[i].m_Num)
					return FailKey("craft.err.no_materials", "材料不足。");
			}
			else
				pP->m_AccData.m_aItems[i].m_Num -= Need;
		}
	}

	pP->m_AccData.m_aItems[Item].m_Num++;
	pAcc->RequestSaveItems(ClientID);
	if(TWorldController *pCore = GS()->Core())
		pCore->Events().EmitPlayerCraft(pP, Item, 1);
	return true;
}

static void ComChatInv(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CAccountSystem *pAcc = pGame->Accounts();

	if(!pAcc->IsEnabled())
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "err.account.disabled", "未启用账号。");
		return;
	}

	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "err.login.required", "请先登录。");
		return;
	}

	CItemHelper *pH = pGame->ItemHelper();
	if(!pH)
		return;

	char aLine[256];
	int Lines = 0;
	for(int i = 0; i < NUM_ITEM && Lines < 12; i++)
	{
		const int N = pP->m_AccData.m_aItems[i].m_Num;
		if(N <= 0)
			continue;
		pGame->LocFormat(aLine, sizeof(aLine), pCtx->m_ClientID, "inv.line", "%d× %s (#%d)", N, pGame->LocItemName(pCtx->m_ClientID, i), i);
		pGame->SendChatTo(pCtx->m_ClientID, aLine);
		Lines++;
	}
	if(!Lines)
		pGame->SendChatLoc(pCtx->m_ClientID, "inv.empty", "（空）");
}

static void ComVoteMenuGoto(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	const int NewPage = pResult->GetInteger(0);

	if(NewPage == PAGE_MENU)
	{
		ClearNpcService(pV);
	}
	else if(IsRemoteShopBrowserPage(NewPage))
	{
		// 宽松：商店浏览页不要求 NPC 绑定，列表页再按距离过滤商人
	}
	else if(IsNpcServicePage(NewPage))
	{
		if(NewPage == VOTE_PAGE_MMO_ENCHANT || NewPage == VOTE_PAGE_MMO_ENCHANT_SELECT)
		{
			if(!IsPlayerNearServiceNpc(pGame, pP, "blacksmith"))
			{
				NotifyNpcServiceDenied(pGame, pCtx->m_ClientID);
				return;
			}
			BindNpcService(pV, "blacksmith");
		}
		else if(!pV->m_aServiceNpcId[0] || !IsPlayerNearServiceNpc(pGame, pP, pV->m_aServiceNpcId))
		{
			NotifyNpcServiceDenied(pGame, pCtx->m_ClientID);
			return;
		}
	}

	pV->m_Page = NewPage;
	pV->m_Confirm = false;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSelectItem(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	const int Slot = pResult->GetInteger(0);
	if(Slot < 0 || Slot >= SPlayerVote::NUM_SELECT)
		return;
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_Select[Slot] = pResult->GetInteger(1);
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteCraft(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_Page = PAGE_CRAFT_SELECTED;
	pV->m_Select[SPlayerVote::ITEM] = pResult->GetInteger(0);
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteCheckItem(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	pV->m_Page = PAGE_CHECK_ITEM;
	pV->m_Select[SPlayerVote::ITEM] = pResult->GetInteger(0);
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteMake(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	const int Item = pV->m_Select[SPlayerVote::ITEM];
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	CItemHelper *pH = pGame->ItemHelper();
	char aErr[160];
	if(pGame->Core() && pGame->Core()->CraftManager() && pGame->Core()->CraftManager()->TryCraftOneItem(pCtx->m_ClientID, Item, aErr, sizeof(aErr)))
	{
		pGame->LocFormat(pV->m_aExtraText, sizeof(pV->m_aExtraText), pCtx->m_ClientID, "vote.craft.ok", "合成成功：%s。",
			pH ? pGame->LocItemName(pCtx->m_ClientID, Item) : pGame->Loc(pCtx->m_ClientID, "common.unknown", "?"));
		if(CCharacter *pChr = pP ? pP->GetCharacter() : nullptr)
			PlayInteractionSound(pGame->m_World, pP, SOUND_SFX_CRAFT);
	}
	else
	{
		str_copy(pV->m_aExtraText, aErr, sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP ? pP->GetCharacter() : nullptr)
			PlayInteractionSound(pGame->m_World, pP, SOUND_SFX_TICK);
	}
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static bool VoteCardHostMatchesEquipped(CPlayer *pP, CItemHelper *pH, int HostId)
{
	if(!pP || !pH || HostId <= 0 || !pH->CheckItemValid(HostId))
		return false;
	const int T = pH->GetType(HostId);
	switch(T)
	{
	case ITYPE_PICKAXE:
	case ITYPE_AXE:
	case ITYPE_SWORD:
	case ITYPE_TURRET:
		return pP->GetHolding(T) == HostId;
	default:
		return false;
	}
}

static void ComVoteEquip(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CAccountSystem *pAcc = pGame->Accounts();
	if(!pAcc->IsEnabled())
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
		return;
	CItemHelper *pH = pGame->ItemHelper();
	if(!pH)
		return;
	const int ItemId = pResult->GetInteger(0);
	if(!pH->CheckItemValid(ItemId) || ItemId <= 0)
		return;
	const int T = pH->GetType(ItemId);
	if(T != ITYPE_PICKAXE && T != ITYPE_AXE && T != ITYPE_SWORD && T != ITYPE_TURRET
		&& T != ITYPE_HELMET && T != ITYPE_CHEST && T != ITYPE_LEGS)
		return;
	if(pP->m_AccData.m_aItems[ItemId].m_Num <= 0)
		return;
	pP->m_AccData.m_Holding[T] = ItemId;
	if(TWorldController *pCore = pGame->Core())
	{
		CDurabilityManager::EnsureDurability(pP->m_AccData.m_aItems[ItemId].m_aExtra, sizeof(pP->m_AccData.m_aItems[ItemId].m_aExtra));
		pCore->Events().EmitPlayerEquip(pP, ItemId);
	}
	pAcc->RequestSaveAccount(pCtx->m_ClientID);
	PlayInteractionSound(pGame->m_World, pP, SOUND_SFX_ITEM_EQUIP);
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSetupTurret(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = pGame->Accounts();
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
		return;
	if(pP->GetHolding(ITYPE_TURRET) <= 0)
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.need_equip", "请先在炮塔页装备一门炮塔。"), sizeof(pV->m_aExtraText));
	else if(pP->IsTurretPlacing())
	{
		pP->CancelTurretPlace();
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "turret.place.cancelled", "已取消部署。"), sizeof(pV->m_aExtraText));
	}
	else if(!pP->BeginTurretPlace())
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.deploy_fail", "部署失败：需要存活角色。"), sizeof(pV->m_aExtraText));
	else
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "turret.place.hint", "瞄准位置，左键部署；再次打开菜单可取消。");
		pV->m_aExtraText[0] = 0;
	}
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteTurretPlaceCancel(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	if(pP)
		pP->CancelTurretPlace();
	pV->m_Page = PAGE_TURRET;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteRecallTurret(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = pGame->Accounts();
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
	{
		if(pV)
			str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "err.account.disabled", "未启用账号。"), sizeof(pV->m_aExtraText));
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
		return;
	}

	pP->SyncDeployedTurretRef();
	if(!pP->HasDeployedTurret())
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.recall.none", "场上没有已部署的炮塔。"), sizeof(pV->m_aExtraText));
	else if(!pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.recall.fail", "收回失败：需要存活角色。"), sizeof(pV->m_aExtraText));
	else if(!pP->RecallTurret())
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.recall.too_far", "距离太远，请靠近炮塔后再收回。"), sizeof(pV->m_aExtraText));
	else
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "vote.turret.recall.ok", "炮塔已收回。");
		pGame->m_World.CreateSound(pP->GetCharacter()->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
		pV->m_aExtraText[0] = 0;
	}

	pV->m_Page = PAGE_TURRET;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteRepairTurret(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = pGame->Accounts();
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
		return;

	CTurret *pT = pP->GetDeployedTurret();
	if(!pT || !pT->IsBroken())
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.repair.not_broken", "炮塔未损坏，无需修复。"), sizeof(pV->m_aExtraText));
	else if(!pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.repair.fail", "修复失败：需要存活角色。"), sizeof(pV->m_aExtraText));
	else if(distance(pP->GetCharacter()->GetPos(), pT->GetPos()) > 520.0f)
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.repair.too_far", "距离太远，请靠近炮塔后再修复。"), sizeof(pV->m_aExtraText));
	else if(!TurretRepair_CanAfford(pGame, pP, pT->GetItemDefId()))
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.repair.no_materials", "修复材料不足。"), sizeof(pV->m_aExtraText));
	else if(!pP->RepairDeployedTurret())
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.turret.repair.fail", "修复失败：需要存活角色。"), sizeof(pV->m_aExtraText));
	else
	{
		pAcc->RequestSaveAccount(pCtx->m_ClientID);
		pGame->SendChatLoc(pCtx->m_ClientID, "vote.turret.repair.ok", "炮塔已修复。");
		pGame->m_World.CreateSound(pP->GetCharacter()->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
		pV->m_aExtraText[0] = 0;
	}

	pV->m_Page = PAGE_TURRET;
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVotePlaceCard(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = pGame->Accounts();
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
		return;
	const int HostId = pResult->GetInteger(0);
	const char *pSlot = pResult->GetString(1);
	const int CardId = pResult->GetInteger(2);
	if(!VoteCardHostMatchesEquipped(pP, pGame->ItemHelper(), HostId))
	{
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.card.need_equip", "嵌入失败：请先将该物品装备到对应栏位。"), sizeof(pV->m_aExtraText));
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
		return;
	}
	if(ItemCardOps_Place(pGame, pP, HostId, pSlot, CardId))
	{
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.card.place_ok", "已嵌入。"), sizeof(pV->m_aExtraText));
		pAcc->RequestSaveItems(pCtx->m_ClientID);
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
	}
	else
	{
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.card.place_fail", "嵌入失败（容量/叠加上限/材料不足）。"), sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_WEAPON_NOAMMO, CmaskOne(pCtx->m_ClientID));
	}
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSeparateCard(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = pGame->Accounts();
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
		return;
	const int HostId = pResult->GetInteger(0);
	const char *pSlot = pResult->GetString(1);
	const int CardId = pResult->GetInteger(2);
	if(!VoteCardHostMatchesEquipped(pP, pGame->ItemHelper(), HostId))
	{
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.card.separate_need_equip", "拆卸失败：请先将该物品装备到对应栏位。"), sizeof(pV->m_aExtraText));
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
		return;
	}
	if(ItemCardOps_Separate(pGame, pP, HostId, pSlot, CardId))
	{
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.card.separate_ok", "已卸下一个。"), sizeof(pV->m_aExtraText));
		pAcc->RequestSaveItems(pCtx->m_ClientID);
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
	}
	else
	{
		str_copy(pV->m_aExtraText, pGame->Loc(pCtx->m_ClientID, "vote.card.separate_fail", "拆卸失败。"), sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_WEAPON_NOAMMO, CmaskOne(pCtx->m_ClientID));
	}
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

static void ConGiveItem(IConsole::IResult *pResult, void *pUser)
{
	CGameContext *pGame = (CGameContext *)pUser;
	const int CID = pResult->GetInteger(0);
	const int Item = pResult->GetInteger(1);
	const int Num = pResult->GetInteger(2);

	if(CID < 0 || CID >= MAX_CLIENTS || !pGame->m_apPlayers[CID])
		return;

	CItemHelper *pH = pGame->ItemHelper();
	if(!pH || !pH->CheckItemValid(Item) || Num == 0)
		return;

	pGame->m_apPlayers[CID]->m_AccData.m_aItems[Item].m_Num += Num;
	pGame->Accounts()->RequestSaveItems(CID);
}

static void ComVoteBumpAmmo(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	const int CID = pCtx->m_ClientID;
	if(CID < 0 || !pGame->m_apPlayers[CID])
		return;
	const int Mat = pResult->GetInteger(0);
	const int Delta = pResult->GetInteger(1);
	CPlayer *pP = pGame->m_apPlayers[CID];
	pP->SetTurretAmmoMatPct(Mat, pP->GetTurretAmmoMix().m_aPct[Mat] + Delta);
	pGame->Core()->VoteMenuManager()->ClearVotes(CID);
	pGame->Core()->VoteMenuManager()->GetPlayerVote(CID)->m_Page = PAGE_TURRET_AMMO;
	pGame->Core()->VoteMenuManager()->InitVotes(CID);
}

static void ComVoteTravel(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	const int CID = pCtx->m_ClientID;
	if(CID < 0 || !pGame->Core() || !pGame->Core()->WorldManager())
		return;

	const int DestWorld = pResult->GetInteger(0);
	const int CurWorld = pGame->Server()->GetClientWorldID(CID);
	const CWorldDetail *pCur = pGame->Server()->GetWorldDetail(CurWorld);
	const CWorldDetail *pDest = pGame->Server()->GetWorldDetail(DestWorld);
	const bool MiniGameTravel = pCur && pDest
		&& ((pCur->GetType() == WorldType::RPG && pDest->GetType() == WorldType::Defence)
			|| (pCur->GetType() == WorldType::Defence && pDest->GetType() == WorldType::RPG));
	pGame->Core()->WorldManager()->ExecuteWithSpawn(CID, DestWorld, nullptr, MiniGameTravel);
}

static void ComVoteSetDifficulty(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	const int CID = pCtx->m_ClientID;
	if(CID < 0 || !pGame->m_pController)
		return;
	auto *pCtrl = dynamic_cast<CGameControllerDefence *>(pGame->m_pController);
	if(!pCtrl)
		return;
	const int Diff = pResult->GetInteger(0);
	if(!pCtrl->TdSetDifficulty(Diff))
	{
		pGame->SendChatLoc(CID, "difficulty.locked", "第 1 波已开始，无法更改难度。");
		return;
	}
	static const char *const s_apKeys[NUM_TD_DIFF] = {"difficulty.easy", "difficulty.normal", "difficulty.hard"};
	static const char *const s_apFallback[NUM_TD_DIFF] = {"简单", "普通", "困难"};
	const int D = clamp(Diff, 0, 2);
	pGame->SendChatLocF(CID, "difficulty.changed", "难度已设为：%s", pGame->Loc(CID, s_apKeys[D], s_apFallback[D]));
	if(pGame->Core() && pGame->Core()->VoteMenuManager())
	{
		pGame->Core()->VoteMenuManager()->GetPlayerVote(CID)->m_Page = PAGE_DIFFICULTY;
		pGame->Core()->VoteMenuManager()->ClearVotes(CID);
	}
}

static void ComVoteResetAmmo(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	const int CID = pCtx->m_ClientID;
	if(CID < 0 || !pGame->m_apPlayers[CID])
		return;
	CPlayer *pP = pGame->m_apPlayers[CID];
	TurretAmmo_DefaultMix(&pP->GetTurretAmmoMix());
	TurretAmmo_ClearDebt(pP);
	pGame->Core()->VoteMenuManager()->ClearVotes(CID);
	pGame->Core()->VoteMenuManager()->GetPlayerVote(CID)->m_Page = PAGE_TURRET_AMMO;
	pGame->Core()->VoteMenuManager()->InitVotes(CID);
}

void CCraftManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	CGameContext *pGame = GS();
	pMgr->AddVoteCommand("menugoto", "", "i", ComVoteMenuGoto, pGame);
	pMgr->AddVoteCommand("menuselitem", "", "ii", ComVoteSelectItem, pGame);
	pMgr->AddVoteCommand("menucraft", "", "i", ComVoteCraft, pGame);
	pMgr->AddVoteCommand("menucheckitem", "", "i", ComVoteCheckItem, pGame);
	pMgr->AddVoteCommand("menumake", "", "", ComVoteMake, pGame);
	pMgr->AddVoteCommand("menuequip", "", "i", ComVoteEquip, pGame);
	pMgr->AddVoteCommand("menusetupturret", "", "", ComVoteSetupTurret, pGame);
	pMgr->AddVoteCommand("menuturretcancel", "", "", ComVoteTurretPlaceCancel, pGame);
	pMgr->AddVoteCommand("menurecallturret", "", "", ComVoteRecallTurret, pGame);
	pMgr->AddVoteCommand("menurepairturret", "", "", ComVoteRepairTurret, pGame);
	pMgr->AddVoteCommand("menuplace", "", "isi", ComVotePlaceCard, pGame);
	pMgr->AddVoteCommand("menuseparate", "", "isi", ComVoteSeparateCard, pGame);
	pMgr->AddVoteCommand("menubumpammo", "", "ii", ComVoteBumpAmmo, pGame);
	pMgr->AddVoteCommand("menuresetammo", "", "", ComVoteResetAmmo, pGame);
	pMgr->AddVoteCommand("menutravel", "", "i", ComVoteTravel, pGame);
	pMgr->AddVoteCommand("menudifficulty", "", "i", ComVoteSetDifficulty, pGame);
}

void CCraftManager::RegisterChatCommands(CCommandManager *pMgr)
{
	CGameContext *pGame = GS();
	// craft/equip 仅通过投票菜单 ccv_menucraft / ccv_menuequip
	pMgr->AddCommand("inv", "cmd.inv.help", "", ComChatInv, pGame);
}

void CCraftManager::OnConsoleInit()
{
	if(!GS())
		return;
	Console()->Register("giveitem", "i[client] i[item] i[num]", CFGFLAG_SERVER, ConGiveItem, GS(), "Give item count to client (saves items)");
}
