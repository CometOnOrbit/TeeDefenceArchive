/* (c) TeeDefenceArchive - 2026 */
#include <base/system.h>

#include <engine/shared/protocol.h>

#include <engine/shared/jsonparser.h>

#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include <generated/server_data.h>

#include "entities/character.h"

void CGameContext::AddVote(const char *pDesc, const char *pCmd, int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	while(pDesc && *pDesc == ' ')
		pDesc++;

	SPlayerVote::SVoteOptions Vote = {};
	str_copy(Vote.m_aDescription, pDesc, sizeof(Vote.m_aDescription));
	str_copy(Vote.m_aCommand, pCmd, sizeof(Vote.m_aCommand));
	GetPlayerVote(ClientID)->m_aVoteOptions.add(Vote);

	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = Vote.m_aDescription;
	Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::AddVote_TextLine(const char *pText)
{
	AddVote(pText, "ccv_null", m_VoteBuildClientID);
}

void CGameContext::AddVote_Goto(int Page, const char *pDesc)
{
	char aCmd[64];
	str_format(aCmd, sizeof(aCmd), "ccv_menugoto %d", Page);
	AddVote(pDesc, aCmd, m_VoteBuildClientID);
}

void CGameContext::SetVoteLastPage(int Page)
{
	if(m_VoteBuildClientID < 0 || m_VoteBuildClientID >= MAX_CLIENTS)
		return;
	GetPlayerVote(m_VoteBuildClientID)->m_LastPage = Page;
}

void CGameContext::AddVote_Space(int Num)
{
	if(m_VoteBuildClientID < 0 || m_VoteBuildClientID >= MAX_CLIENTS)
		return;
	for(int i = 0; i < Num; i++)
		AddVote(" ", "ccv_null", m_VoteBuildClientID);
}

void CGameContext::AddVote_Back()
{
	if(m_VoteBuildClientID < 0 || m_VoteBuildClientID >= MAX_CLIENTS || !m_apPlayers[m_VoteBuildClientID])
		return;
	AddVote_Goto(GetPlayerVote(m_VoteBuildClientID)->m_LastPage, u8"⏎ 返回");
}

void CGameContext::AddVote_ListInventory(int ItemType, const char *pCmdPrefix, bool Equip)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? m_apPlayers[m_VoteBuildClientID] : nullptr;
	if(!pP || !ItemHelper())
		return;

	bool Got = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(ItemHelper()->GetType(i) == ItemType && pP->m_AccData.m_aItems[i].m_Num > 0)
		{
			char aCmd[96];
			str_format(aCmd, sizeof(aCmd), "%s %d", pCmdPrefix, i);
			if(Equip && pP->m_AccData.m_Holding[ItemType] == i)
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"➳ %s x%d ✓", ItemHelper()->GetItemName(i), pP->m_AccData.m_aItems[i].m_Num);
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"➳ %s x%d", ItemHelper()->GetItemName(i), pP->m_AccData.m_aItems[i].m_Num);
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			Got = true;
		}
	}
	if(!Got)
		AddVote_TextLine(u8"（空）");
}

void CGameContext::AddVote_ListCraft(int ItemType)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? m_apPlayers[m_VoteBuildClientID] : nullptr;
	if(!pP || !ItemHelper())
		return;

	bool Got = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(ItemHelper()->GetType(i) == ItemType && ItemHelper()->HasFormula(i))
		{
			AddVote_Craft(i);
			Got = true;
		}
	}
	if(!Got)
		AddVote_TextLine(u8"（空）");
}

void CGameContext::AddVote_Craft(int ItemID)
{
	char aCmd[64];
	str_format(aCmd, sizeof(aCmd), "ccv_menucraft %d", ItemID);
	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), u8"➳ %s", ItemHelper()->GetItemName(ItemID));
	AddVote(aLine, aCmd, m_VoteBuildClientID);
}

void CGameContext::AddVote_ListFormula(int ItemID)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? m_apPlayers[m_VoteBuildClientID] : nullptr;
	if(!pP || !ItemHelper() || !ItemHelper()->CheckItemValid(ItemID))
		return;

	bool Got = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		const int Need = ItemHelper()->GetFormulaNeed(ItemID, i);
		if(Need > 0)
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"# %s %d/%d", ItemHelper()->GetItemName(i), pP->m_AccData.m_aItems[i].m_Num, Need);
			AddVote_TextLine(aLine);
			Got = true;
		}
	}
	if(!Got)
		AddVote_TextLine(u8"（空）");
}

void CGameContext::CountItemNum(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID] || !ItemHelper())
		return;

	int ItemCount[NUM_ITYPE] = {0};
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(m_apPlayers[ClientID]->m_AccData.m_aItems[i].m_Num > 0)
			ItemCount[ItemHelper()->GetType(i)]++;
	}
	for(int t = 0; t < NUM_ITYPE; t++)
		m_apPlayers[ClientID]->m_AccData.m_ItemCount[t] = ItemCount[t];
}

namespace
{
static int AddVotesSeparateFromHostExtra(CGameContext *pGame, int VoteClient, int HostId, const json_value &Extra)
{
	if(Extra.type != json_object || !pGame->ItemHelper())
		return 0;

	int Added = 0;
	const json_value *pArrays[2] = {&Extra["Cards"], &Extra["Parts"]};
	const char *apSlots[2] = {"Cards", "Parts"};
	for(int a = 0; a < 2; a++)
	{
		const json_value &Arr = *pArrays[a];
		if(Arr.type != json_array)
			continue;
		for(unsigned u = 0; u < Arr.u.array.length; u++)
		{
			const json_value &El = Arr[(int)u];
			if(El.type != json_object || El["id"].type != json_integer || El["num"].type != json_integer)
				continue;
			const int Id = (int)El["id"].u.integer;
			const int Num = (int)El["num"].u.integer;
			if(Num <= 0 || !pGame->ItemHelper()->CheckItemValid(Id))
				continue;
			char aCmd[112];
			str_format(aCmd, sizeof(aCmd), "ccv_menuseparate %d %s %d", HostId, apSlots[a], Id);
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"－ %s ×%d", pGame->ItemHelper()->GetItemName(Id), Num);
			pGame->AddVote(aLine, aCmd, VoteClient);
			Added++;
		}
	}
	return Added;
}

/** Embed/separate votes for cards & parts on an equipped host (turret or tool). */
static void AddVotesHostCardManage(CGameContext *pGame, int VoteClient, CPlayer *pP, int HostId)
{
	CItemHelper *pIH = pGame ? pGame->ItemHelper() : nullptr;
	if(!pGame || !pP || !pIH || HostId <= 0 || pP->m_AccData.m_aItems[HostId].m_Num <= 0)
		return;

	const int HostIType = pIH->GetType(HostId);
	pGame->AddVote_TextLine(u8"─ 嵌入卡牌/零件（消耗背包×1）");
	bool AnyEmbed = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(pP->m_AccData.m_aItems[i].m_Num <= 0)
			continue;
		if(!pIH->IsPlaceableOnItemType(i, HostIType))
			continue;
		const char *pSlot = (i >= ITEM_PART_FIRST) ? "Parts" : "Cards";
		char aCmd[112];
		str_format(aCmd, sizeof(aCmd), "ccv_menuplace %d %s %d", HostId, pSlot, i);
		char aLine[VOTE_DESC_LENGTH];
		str_format(aLine, sizeof(aLine), u8"＋ %s → %s", pIH->GetItemName(i), pSlot);
		pGame->AddVote(aLine, aCmd, VoteClient);
		AnyEmbed = true;
	}
	if(!AnyEmbed)
		pGame->AddVote_TextLine(u8"（背包中暂无可嵌入物品）");

	pGame->AddVote_Space();
	pGame->AddVote_TextLine(u8"─ 拆卸（每次卸 1 个）");
	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pP->m_AccData.m_aItems[HostId].m_aExtra, "vote_host_ex");
	const json_value *pEx = nullptr;
	if(pRoot && (*pRoot)["Extra"].type == json_object)
		pEx = &(*pRoot)["Extra"];
	if(pEx)
	{
		if(AddVotesSeparateFromHostExtra(pGame, VoteClient, HostId, *pEx) == 0)
			pGame->AddVote_TextLine(u8"（无嵌入）");
	}
	else
		pGame->AddVote_TextLine(u8"（无嵌入）");
}
} // namespace

void CGameContext::InitVotes(int ClientID)
{
	CPlayer *pP = (ClientID >= 0 && ClientID < MAX_CLIENTS) ? m_apPlayers[ClientID] : nullptr;
	if(!pP || !ItemHelper())
		return;

	SetVoteBuildClientID(ClientID);

	SPlayerVote *pVote = GetPlayerVote(ClientID);
	const int Page = pVote->m_Page;
	SAccSyncData &Data = pP->m_AccData;

	const char *pItemLists[NUM_ITYPE] = {u8"镐", u8"斧", u8"剑", u8"炮塔", u8"材料", u8"卡牌"};

	if(pVote->m_aExtraText[0])
	{
		AddVote_TextLine(pVote->m_aExtraText);
		AddVote_TextLine("---");
	}

	switch(Page)
	{
	case PAGE_MENU:
	{
		SetVoteLastPage(Page);
		AddVote_TextLine(u8"☪ 玩家菜单");
		AddVote_TextLine("---");
		AddVote_Goto(PAGE_INVENTORY, u8"☞ 背包");
		AddVote_Goto(PAGE_CRAFT, u8"☞ 合成");
		AddVote_Goto(PAGE_EQUIPMENT, u8"☞ 装备");
		AddVote_Goto(PAGE_TURRET, u8"☞ 炮塔");
		AddVote_Space();
	}
	break;

	case PAGE_INVENTORY:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(u8"☪ 背包");
		AddVote_Space();
		CountItemNum(ClientID);
		for(int i = 0; i < NUM_ITYPE; i++)
		{
			if(pVote->m_Select[SPlayerVote::ITEMLIST] != i)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::ITEMLIST, i);
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"▹ %s (%d)", pItemLists[i], Data.m_ItemCount[i]);
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"▾ %s (%d)", pItemLists[i], Data.m_ItemCount[i]);
				AddVote_TextLine(aLine);
			}
		}
		AddVote_Space();
		AddVote_Back();
		AddVote_TextLine("---------------------");
		AddVote_ListInventory(pVote->m_Select[SPlayerVote::ITEMLIST], "ccv_menucheckitem");
	}
	break;

	case PAGE_CHECK_ITEM:
	{
		const int SelectItem = pVote->m_Select[SPlayerVote::ITEM];
		if(!ItemHelper()->CheckItemValid(SelectItem))
		{
			pVote->m_Page = PAGE_INVENTORY;
			SetVoteBuildClientID(-1);
			InitVotes(ClientID);
			return;
		}
		SetVoteLastPage(PAGE_INVENTORY);
		AddVote_TextLine(u8"☪ 物品信息");
		AddVote_Space();
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"物品: %s", ItemHelper()->GetItemName(SelectItem));
			AddVote_TextLine(aLine);
		}
		{
			const int T = ItemHelper()->GetType(SelectItem);
			if(T == ITYPE_CARD)
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"需要容量: %d", ItemHelper()->GetMaxCapacity(SelectItem));
				AddVote_TextLine(aLine);
			}
			else if(T != ITYPE_MATERIAL)
			{
				const int Cap = ItemHelper()->GetCapacityFromExtra(pP->GetExtraForItem(SelectItem));
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"容量: %d/%d", Cap, ItemHelper()->GetMaxCapacity(SelectItem));
				AddVote_TextLine(aLine);
			}
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"持有: %d", Data.m_aItems[SelectItem].m_Num);
			AddVote_TextLine(aLine);
		}
		AddVote_Space();
		{
			const int T = ItemHelper()->GetType(SelectItem);
			if(T == ITYPE_PICKAXE || T == ITYPE_AXE || T == ITYPE_SWORD || T == ITYPE_TURRET)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuequip %d", SelectItem);
				AddVote(u8"☝ 装备", aCmd, m_VoteBuildClientID);
			}
		}
		{
			const int TT = ItemHelper()->GetType(SelectItem);
			if((TT == ITYPE_PICKAXE || TT == ITYPE_AXE || TT == ITYPE_SWORD) && SelectItem == Data.m_Holding[TT] && Data.m_aItems[SelectItem].m_Num > 0)
			{
				AddVote_Space();
				AddVotesHostCardManage(this, ClientID, pP, SelectItem);
			}
		}
		AddVote_Space();
		AddVote_Back();
	}
	break;

	case PAGE_CRAFT:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(u8"☪ 合成");
		AddVote_Space();
		for(int i = 0; i < NUM_ITYPE; i++)
		{
			if(pVote->m_Select[SPlayerVote::ITEMLIST] != i)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::ITEMLIST, i);
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"▹ %s", pItemLists[i]);
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"▾ %s", pItemLists[i]);
				AddVote_TextLine(aLine);
			}
		}
		AddVote_Space();
		AddVote_Back();
		AddVote_TextLine("---------------------");
		AddVote_ListCraft(pVote->m_Select[SPlayerVote::ITEMLIST]);
	}
	break;

	case PAGE_CRAFT_SELECTED:
	{
		const int Sel = pVote->m_Select[SPlayerVote::ITEM];
		if(!ItemHelper()->CheckItemValid(Sel) || !ItemHelper()->HasFormula(Sel))
		{
			pVote->m_Page = PAGE_CRAFT;
			SetVoteBuildClientID(-1);
			InitVotes(ClientID);
			return;
		}
		SetVoteLastPage(PAGE_CRAFT);
		AddVote_TextLine(u8"☪ 合成");
		AddVote_Space();
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"物品: %s", ItemHelper()->GetItemName(Sel));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"持有: %d", Data.m_aItems[Sel].m_Num);
			AddVote_TextLine(aLine);
		}
		AddVote_TextLine(u8"配方:");
		AddVote_TextLine("---");
		AddVote_ListFormula(Sel);
		AddVote_TextLine("===");
		AddVote(u8"- 合成 !", "ccv_menumake", m_VoteBuildClientID);
		AddVote_Space(2);
		AddVote_Back();
	}
	break;

	case PAGE_EQUIPMENT:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(u8"☪ 装备栏");
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"剑: %s", ItemHelper()->GetItemName(Data.m_Holding[ITYPE_SWORD], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"斧: %s", ItemHelper()->GetItemName(Data.m_Holding[ITYPE_AXE], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"镐: %s", ItemHelper()->GetItemName(Data.m_Holding[ITYPE_PICKAXE], false));
			AddVote_TextLine(aLine);
		}
		AddVote_Space();
		CountItemNum(ClientID);
		for(int i = 0; i < NUM_ITYPE; i++)
		{
			if(i != ITYPE_PICKAXE && i != ITYPE_AXE && i != ITYPE_SWORD)
				continue;
			if(pVote->m_Select[SPlayerVote::EQUIPMENT] != i)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::EQUIPMENT, i);
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"▹ %s (%d)", pItemLists[i], Data.m_ItemCount[i]);
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), u8"▾ %s (%d)", pItemLists[i], Data.m_ItemCount[i]);
				AddVote_TextLine(aLine);
			}
		}
		AddVote_Space();
		AddVote_Back();
		AddVote_TextLine("---------------------");
		AddVote_ListInventory(pVote->m_Select[SPlayerVote::EQUIPMENT], "ccv_menucheckitem", true);
	}
	break;

	case PAGE_TURRET:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(u8"☪ 炮塔");
		const int TurretID = Data.m_Holding[ITYPE_TURRET];
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"当前装备: %s", ItemHelper()->GetItemName(TurretID, false));
			AddVote_TextLine(aLine);
		}
		AddVote_Space();
		AddVote(u8"⚙ 部署炮塔（当前位置）", "ccv_menusetupturret", ClientID);
		AddVote_Space();

		if(TurretID > 0 && Data.m_aItems[TurretID].m_Num > 0)
			AddVotesHostCardManage(this, ClientID, pP, TurretID);
		else
			AddVote_TextLine(u8"请先在背包中装备一门炮塔。");

		AddVote_Space();
		AddVote_ListInventory(ITYPE_TURRET, "ccv_menuequip", true);
		AddVote_Space();
		AddVote_Back();
	}
	break;

	default:
		pVote->m_Page = PAGE_MENU;
		SetVoteBuildClientID(-1);
		InitVotes(ClientID);
		return;
	}

	SetVoteBuildClientID(-1);
}

void CGameContext::ClearVotes(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	GetPlayerVote(ClientID)->m_aVoteOptions.clear();

	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

	InitVotes(ClientID);
}

bool CGameContext::TryHandleVoteMenuOption(int ClientID, const char *pDescription)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pDescription)
		return false;

	SPlayerVote *pV = GetPlayerVote(ClientID);
	for(int i = 0; i < pV->m_aVoteOptions.size(); i++)
	{
		if(str_comp_nocase(pDescription, pV->m_aVoteOptions[i].m_aDescription) != 0)
			continue;

		const char *pCmd = pV->m_aVoteOptions[i].m_aCommand;
		if(!pCmd[0])
			return true;

		if(str_comp(pCmd, "ccv_null") == 0)
			return true;

		if(str_length(pCmd) >= 4 && str_comp_nocase_num(pCmd, "ccv_", 4) == 0)
		{
			ProcessVoteMenuCommand(ClientID, pCmd);
			return true;
		}

		return true;
	}

	return false;
}

void CGameContext::ProcessVoteMenuCommand(int ClientID, const char *pCmdLine)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pCmdLine || str_length(pCmdLine) < 5)
		return;
	if(str_comp_nocase_num(pCmdLine, "ccv_", 4) != 0)
		return;

	const char *pRest = pCmdLine + 4;
	while(*pRest == ' ')
		pRest++;

	char aName[32];
	int k = 0;
	while(pRest[k] && pRest[k] != ' ' && k < (int)sizeof(aName) - 1)
	{
		aName[k] = pRest[k];
		k++;
	}
	aName[k] = 0;
	const char *pArgs = str_skip_whitespaces_const(pRest + k);

	CommandManager()->OnCommand(aName, pArgs, ClientID);
}
