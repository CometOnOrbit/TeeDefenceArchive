/* (c) TeeDefenceArchive - 2026 */
#include <engine/console.h>
#include <engine/shared/config.h>

#include <game/voting.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "account.h"
#include "crafting.h"
#include "item_card_ops.h"
#include "item_system.h"

#include <generated/server_data.h>

#include "entities/character.h"

static bool IsDigitsOnly(const char *p)
{
	if(!p || !p[0])
		return false;
	for(const char *q = p; *q; q++)
		if(*q < '0' || *q > '9')
			return false;
	return true;
}

static int ResolveItemId(CItemHelper *pH, const char *pToken)
{
	if(!pH || !pToken || !pToken[0])
		return -1;
	int ByName = pH->FindItemByName(pToken);
	if(ByName >= 0)
		return ByName;
	if(IsDigitsOnly(pToken))
	{
		int Try = str_toint(pToken);
		if(pH->CheckItemValid(Try))
			return Try;
	}
	return -1;
}

bool TryCraftOneItem(CGameContext *pGame, int ClientID, int Item, char *pErr, int ErrSize)
{
	auto Fail = [&](const char *pMsg) -> bool {
		if(pErr && ErrSize > 0)
			str_copy(pErr, pMsg, ErrSize);
		return false;
	};

	CAccountSystem *pAcc = &pGame->m_Accounts;
	if(!pAcc->IsEnabled())
		return Fail("未启用账号，无法合成。");

	CPlayer *pP = ClientID >= 0 && ClientID < MAX_CLIENTS ? pGame->m_apPlayers[ClientID] : nullptr;
	if(!pP || pP->GetAccountId() < 0)
		return Fail("请先登录。");

	CItemHelper *pH = pGame->ItemHelper();
	if(!pH || !pH->CheckItemValid(Item))
		return Fail("未知物品。");
	if(!pH->HasFormula(Item))
		return Fail("该物品无法合成。");

	const int StackMax = pH->GetMax(Item);
	if(StackMax > 0 && pP->m_AccData.m_aItems[Item].m_Num >= StackMax)
		return Fail("该物品已达持有上限。");

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
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "材料「%s」含有卡牌/零件，无法用于合成。", pH->GetItemName(i));
					return Fail(aBuf);
				}
				if(Need > pP->m_AccData.m_aItems[i].m_Num)
					return Fail("材料不足。");
			}
			else
				pP->m_AccData.m_aItems[i].m_Num -= Need;
		}
	}

	pP->m_AccData.m_aItems[Item].m_Num++;
	pAcc->RequestSaveItems(ClientID);
	return true;
}

static void ComChatCraft(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	CItemHelper *pH = pGame->ItemHelper();

	if(!pH)
		return;

	const char *pTok = pResult->GetString(0);
	int Item = ResolveItemId(pH, pTok);
	if(Item < 0 || !pH->CheckItemValid(Item))
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "未知物品。");
		return;
	}

	char aErr[256];
	if(!TryCraftOneItem(pGame, pCtx->m_ClientID, Item, aErr, sizeof(aErr)))
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, aErr);
		if(CCharacter *pChr = pP ? pP->GetCharacter() : nullptr)
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_WEAPON_NOAMMO, CmaskOne(pCtx->m_ClientID));
		return;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "合成成功：%s。", pH->GetItemName(Item));
	pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, aBuf);
	if(CCharacter *pChr = pP ? pP->GetCharacter() : nullptr)
		pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
}

static void ComChatEquip(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CAccountSystem *pAcc = &pGame->m_Accounts;

	if(!pAcc->IsEnabled())
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "未启用账号。");
		return;
	}

	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "请先登录。");
		return;
	}

	CItemHelper *pH = pGame->ItemHelper();
	if(!pH)
		return;

	const int ItemId = pResult->GetInteger(0);
	if(!pH->CheckItemValid(ItemId) || ItemId <= 0)
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "无效物品。");
		return;
	}

	const int T = pH->GetType(ItemId);
	if(T != ITYPE_PICKAXE && T != ITYPE_AXE && T != ITYPE_SWORD)
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "只能装备镐、斧或剑。");
		return;
	}

	if(pP->m_AccData.m_aItems[ItemId].m_Num <= 0)
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "你没有该物品。");
		return;
	}

	pP->m_AccData.m_Holding[T] = ItemId;
	pAcc->RequestSaveAccount(pCtx->m_ClientID);

	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "已装备：%s。", pH->GetItemName(ItemId));
	pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, aBuf);
	if(CCharacter *pChr = pP->GetCharacter())
		pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_NINJA, CmaskOne(pCtx->m_ClientID));
}

static void ComChatInv(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CAccountSystem *pAcc = &pGame->m_Accounts;

	if(!pAcc->IsEnabled())
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "未启用账号。");
		return;
	}

	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "请先登录。");
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
		str_format(aLine, sizeof(aLine), "%d× %s (#%d)", N, pH->GetItemName(i), i);
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, aLine);
		Lines++;
	}
	if(!Lines)
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "背包为空。");
}

static void ComChatMenu(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame->Accounts()->IsEnabled())
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "未启用账号。");
		return;
	}
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
	{
		pGame->SendChat(-1, CHAT_ALL, pCtx->m_ClientID, "请先登录。");
		return;
	}
	pGame->GetPlayerVote(pCtx->m_ClientID)->m_Page = CGameContext::PAGE_MENU;
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteMenuGoto(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	pV->m_Page = pResult->GetInteger(0);
	pV->m_Confirm = false;
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSelectItem(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	const int Slot = pResult->GetInteger(0);
	if(Slot < 0 || Slot >= CGameContext::SPlayerVote::NUM_SELECT)
		return;
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	pV->m_Select[Slot] = pResult->GetInteger(1);
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteCraft(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	pV->m_Page = CGameContext::PAGE_CRAFT_SELECTED;
	pV->m_Select[CGameContext::SPlayerVote::ITEM] = pResult->GetInteger(0);
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteCheckItem(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	pV->m_Page = CGameContext::PAGE_CHECK_ITEM;
	pV->m_Select[CGameContext::SPlayerVote::ITEM] = pResult->GetInteger(0);
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteMake(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	const int Item = pV->m_Select[CGameContext::SPlayerVote::ITEM];
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	CItemHelper *pH = pGame->ItemHelper();
	char aErr[160];
	if(TryCraftOneItem(pGame, pCtx->m_ClientID, Item, aErr, sizeof(aErr)))
	{
		char aOk[VOTE_DESC_LENGTH];
		str_format(aOk, sizeof(aOk), "合成成功：%s。", pH ? pH->GetItemName(Item) : "?");
		str_copy(pV->m_aExtraText, aOk, sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP ? pP->GetCharacter() : nullptr)
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
	}
	else
	{
		str_copy(pV->m_aExtraText, aErr, sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP ? pP->GetCharacter() : nullptr)
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_WEAPON_NOAMMO, CmaskOne(pCtx->m_ClientID));
	}
	pGame->ClearVotes(pCtx->m_ClientID);
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
	CAccountSystem *pAcc = &pGame->m_Accounts;
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
	if(T != ITYPE_PICKAXE && T != ITYPE_AXE && T != ITYPE_SWORD && T != ITYPE_TURRET)
		return;
	if(pP->m_AccData.m_aItems[ItemId].m_Num <= 0)
		return;
	pP->m_AccData.m_Holding[T] = ItemId;
	pAcc->RequestSaveAccount(pCtx->m_ClientID);
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSetupTurret(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = &pGame->m_Accounts;
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
		return;
	if(pP->GetHolding(ITYPE_TURRET) <= 0)
		str_copy(pV->m_aExtraText, u8"请先在炮塔页装备一门炮塔。", sizeof(pV->m_aExtraText));
	else if(!pP->CreateTurret())
		str_copy(pV->m_aExtraText, u8"部署失败：需要存活角色。", sizeof(pV->m_aExtraText));
	else
	{
		str_copy(pV->m_aExtraText, u8"炮塔已部署在当前位置。", sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
	}
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVotePlaceCard(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = &pGame->m_Accounts;
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
		return;
	const int HostId = pResult->GetInteger(0);
	const char *pSlot = pResult->GetString(1);
	const int CardId = pResult->GetInteger(2);
	if(!VoteCardHostMatchesEquipped(pP, pGame->ItemHelper(), HostId))
	{
		str_copy(pV->m_aExtraText, u8"嵌入失败：请先将该物品装备到对应栏位。", sizeof(pV->m_aExtraText));
		pGame->ClearVotes(pCtx->m_ClientID);
		return;
	}
	if(ItemCardOps_Place(pGame, pP, HostId, pSlot, CardId))
	{
		str_copy(pV->m_aExtraText, u8"已嵌入。", sizeof(pV->m_aExtraText));
		pAcc->RequestSaveItems(pCtx->m_ClientID);
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
	}
	else
	{
		str_copy(pV->m_aExtraText, u8"嵌入失败（容量/叠加上限/材料不足）。", sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_WEAPON_NOAMMO, CmaskOne(pCtx->m_ClientID));
	}
	pGame->ClearVotes(pCtx->m_ClientID);
}

static void ComVoteSeparateCard(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	CGameContext::SPlayerVote *pV = pGame->GetPlayerVote(pCtx->m_ClientID);
	CAccountSystem *pAcc = &pGame->m_Accounts;
	if(!pAcc->IsEnabled() || !pP || pP->GetAccountId() < 0)
		return;
	const int HostId = pResult->GetInteger(0);
	const char *pSlot = pResult->GetString(1);
	const int CardId = pResult->GetInteger(2);
	if(!VoteCardHostMatchesEquipped(pP, pGame->ItemHelper(), HostId))
	{
		str_copy(pV->m_aExtraText, u8"拆卸失败：请先将该物品装备到对应栏位。", sizeof(pV->m_aExtraText));
		pGame->ClearVotes(pCtx->m_ClientID);
		return;
	}
	if(ItemCardOps_Separate(pGame, pP, HostId, pSlot, CardId))
	{
		str_copy(pV->m_aExtraText, u8"已卸下一个。", sizeof(pV->m_aExtraText));
		pAcc->RequestSaveItems(pCtx->m_ClientID);
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR, CmaskOne(pCtx->m_ClientID));
	}
	else
	{
		str_copy(pV->m_aExtraText, u8"拆卸失败。", sizeof(pV->m_aExtraText));
		if(CCharacter *pChr = pP->GetCharacter())
			pGame->m_World.CreateSound(pChr->GetPos(), SOUND_WEAPON_NOAMMO, CmaskOne(pCtx->m_ClientID));
	}
	pGame->ClearVotes(pCtx->m_ClientID);
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

void RegisterVoteMenuCommands(CCommandManager *pMgr, CGameContext *pGame)
{
	pMgr->AddCommand("menu", "打开投票菜单", "", ComChatMenu, pGame);
	pMgr->AddCommand("menugoto", "", "i", ComVoteMenuGoto, pGame);
	pMgr->AddCommand("menuselitem", "", "ii", ComVoteSelectItem, pGame);
	pMgr->AddCommand("menucraft", "", "i", ComVoteCraft, pGame);
	pMgr->AddCommand("menucheckitem", "", "i", ComVoteCheckItem, pGame);
	pMgr->AddCommand("menumake", "", "", ComVoteMake, pGame);
	pMgr->AddCommand("menuequip", "", "i", ComVoteEquip, pGame);
	pMgr->AddCommand("menusetupturret", "", "", ComVoteSetupTurret, pGame);
	pMgr->AddCommand("menuplace", "", "isi", ComVotePlaceCard, pGame);
	pMgr->AddCommand("menuseparate", "", "isi", ComVoteSeparateCard, pGame);
}

void RegisterCraftingChatCommands(CCommandManager *pMgr, CGameContext *pGame)
{
	pMgr->AddCommand("craft", "合成物品（名称或ID）", "s", ComChatCraft, pGame);
	pMgr->AddCommand("equip", "装备工具 i[itemid]", "i", ComChatEquip, pGame);
	pMgr->AddCommand("inv", "背包列表", "", ComChatInv, pGame);
}

void RegisterCraftingConsoleCommands(IConsole *pConsole, CGameContext *pGame)
{
	pConsole->Register("giveitem", "i[client] i[item] i[num]", CFGFLAG_SERVER, ConGiveItem, pGame, "Give item count to client (saves items)");
}
