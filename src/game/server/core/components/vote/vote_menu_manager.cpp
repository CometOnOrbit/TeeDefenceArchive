/* (c) TeeDefenceArchive - 2026 */
#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/voting.h>
#include <game/server/account.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/skills/skill_manager.h>
#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/item_system.h>
#include <game/server/entities/turret.h>
#include <game/server/player.h>
#include <game/server/turret_ammo.h>

#include <generated/server_data.h>

static const char *VL(CGameContext *pCtx, CPlayer *pP, const char *pKey, const char *pDefault)
{
	if(pCtx)
		return pCtx->Loc(pP ? pP->GetCID() : -1, pKey, pDefault);
	return pDefault;
}

static const char *ItemTypeLoc(CGameContext *pCtx, CPlayer *pP, int ItemType)
{
	static const char *const s_apKeys[NUM_ITYPE] = {
		"itype.pickaxe", "itype.axe", "itype.sword", "itype.turret", "itype.material", "itype.card"};
	static const char *const s_apFallback[NUM_ITYPE] = {
		u8"镐", u8"斧", u8"剑", u8"炮塔", u8"材料", u8"卡牌"};
	if(ItemType < 0 || ItemType >= NUM_ITYPE)
		return VL(pCtx, pP, "common.unknown", "?");
	return VL(pCtx, pP, s_apKeys[ItemType], s_apFallback[ItemType]);
}

static const char *TurretMatLoc(CGameContext *pCtx, CPlayer *pP, int Mat)
{
	static const char *const s_apKeys[NUM_TURRET_AMMO_MATS] = {
		"turret.mat.log", "turret.mat.coal", "turret.mat.copper", "turret.mat.iron",
		"turret.mat.gold", "turret.mat.diamond", "turret.mat.energy", "turret.mat.zombieheart"};
	static const char *const s_apFallback[NUM_TURRET_AMMO_MATS] = {
		u8"木材", u8"煤炭", u8"铜", u8"铁", u8"金", u8"钻石", u8"能量", u8"僵尸之心"};
	if(Mat < 0 || Mat >= NUM_TURRET_AMMO_MATS)
		return VL(pCtx, pP, "common.unknown", "?");
	return VL(pCtx, pP, s_apKeys[Mat], s_apFallback[Mat]);
}

static const char *TurretMatShort(CGameContext *pCtx, CPlayer *pP, int Mat)
{
	static const char *const s_apKeys[NUM_TURRET_AMMO_MATS] = {
		"turret.mat.log.short", "turret.mat.coal.short", "turret.mat.copper.short", "turret.mat.iron.short",
		"turret.mat.gold.short", "turret.mat.diamond.short", "turret.mat.energy.short", "turret.mat.zombieheart.short"};
	static const char *const s_apFallback[NUM_TURRET_AMMO_MATS] = {
		u8"木", u8"煤", u8"铜", u8"铁", u8"金", u8"钻", u8"能", u8"心"};
	if(Mat < 0 || Mat >= NUM_TURRET_AMMO_MATS)
		return VL(pCtx, pP, "common.unknown", "?");
	return VL(pCtx, pP, s_apKeys[Mat], s_apFallback[Mat]);
}

static void TurretAmmoSummaryLines(CGameContext *pCtx, CPlayer *pP, const STurretAmmoMix &Mix, char *pLine0, int Len0, char *pLine1, int Len1)
{
	pLine0[0] = pLine1[0] = 0;
	for(int m = 0; m < NUM_TURRET_AMMO_MATS; m++)
	{
		char aChunk[16];
		str_format(aChunk, sizeof(aChunk), "%s%d%% ", TurretMatShort(pCtx, pP, m), Mix.m_aPct[m]);
		if(m < NUM_TURRET_AMMO_MATS / 2)
			str_append(pLine0, aChunk, Len0);
		else
			str_append(pLine1, aChunk, Len1);
	}
}

SPlayerVote *CVoteMenuManager::GetPlayerVote(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return nullptr;
	return &m_aPlayerVotes[ClientID];
}

void CVoteMenuManager::AddVote(const char *pDesc, const char *pCmd, int ClientID)
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
	GS()->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
}

void CVoteMenuManager::AddVote_TextLine(const char *pText)
{
	AddVote(pText, "ccv_null", m_VoteBuildClientID);
}

void CVoteMenuManager::AddVote_Goto(int Page, const char *pDesc)
{
	char aCmd[64];
	str_format(aCmd, sizeof(aCmd), "ccv_menugoto %d", Page);
	AddVote(pDesc, aCmd, m_VoteBuildClientID);
}

void CVoteMenuManager::SetVoteLastPage(int Page)
{
	if(m_VoteBuildClientID < 0 || m_VoteBuildClientID >= MAX_CLIENTS)
		return;
	GetPlayerVote(m_VoteBuildClientID)->m_LastPage = Page;
}

void CVoteMenuManager::AddVote_Space(int Num)
{
	if(m_VoteBuildClientID < 0 || m_VoteBuildClientID >= MAX_CLIENTS)
		return;
	for(int i = 0; i < Num; i++)
		AddVote(" ", "ccv_null", m_VoteBuildClientID);
}

void CVoteMenuManager::AddVote_Back()
{
	if(m_VoteBuildClientID < 0 || m_VoteBuildClientID >= MAX_CLIENTS || !GS()->m_apPlayers[m_VoteBuildClientID])
		return;
	CPlayer *pP = GS()->m_apPlayers[m_VoteBuildClientID];
	AddVote_Goto(GetPlayerVote(m_VoteBuildClientID)->m_LastPage, VL(GS(), pP, "menu.back", u8"⏎ 返回"));
}

void CVoteMenuManager::AddVote_ListInventory(int ItemType, const char *pCmdPrefix, bool Equip)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? GS()->m_apPlayers[m_VoteBuildClientID] : nullptr;
	if(!pP || !GS()->ItemHelper())
		return;

	bool Got = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(GS()->ItemHelper()->GetType(i) == ItemType && pP->m_AccData.m_aItems[i].m_Num > 0)
		{
			char aCmd[96];
			str_format(aCmd, sizeof(aCmd), "%s %d", pCmdPrefix, i);
			char aLine[VOTE_DESC_LENGTH];
			if(Equip && pP->m_AccData.m_Holding[ItemType] == i)
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.item.equipped", u8"➳ %s x%d ✓"),
					GS()->LocItemName(m_VoteBuildClientID, i), pP->m_AccData.m_aItems[i].m_Num);
			else
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.item", u8"➳ %s x%d"),
					GS()->LocItemName(m_VoteBuildClientID, i), pP->m_AccData.m_aItems[i].m_Num);
			AddVote(aLine, aCmd, m_VoteBuildClientID);
			Got = true;
		}
	}
	if(!Got)
		AddVote_TextLine(VL(GS(), pP, "inv.empty", u8"（空）"));
}

void CVoteMenuManager::AddVote_ListCraft(int ItemType)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? GS()->m_apPlayers[m_VoteBuildClientID] : nullptr;
	if(!pP || !GS()->ItemHelper())
		return;

	bool Got = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(GS()->ItemHelper()->GetType(i) == ItemType && GS()->ItemHelper()->HasFormula(i))
		{
			AddVote_Craft(i);
			Got = true;
		}
	}
	if(!Got)
	{
		CPlayer *pP = m_VoteBuildClientID >= 0 ? GS()->m_apPlayers[m_VoteBuildClientID] : nullptr;
		AddVote_TextLine(VL(GS(), pP, "inv.empty", u8"（空）"));
	}
}

void CVoteMenuManager::AddVote_Craft(int ItemID)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? GS()->m_apPlayers[m_VoteBuildClientID] : nullptr;
	char aCmd[64];
	str_format(aCmd, sizeof(aCmd), "ccv_menucraft %d", ItemID);
	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.item", u8"➳ %s"), GS()->LocItemName(m_VoteBuildClientID, ItemID));
	AddVote(aLine, aCmd, m_VoteBuildClientID);
}

void CVoteMenuManager::AddVote_ListFormula(int ItemID)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? GS()->m_apPlayers[m_VoteBuildClientID] : nullptr;
	if(!pP || !GS()->ItemHelper() || !GS()->ItemHelper()->CheckItemValid(ItemID))
		return;

	bool Got = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		const int Need = GS()->ItemHelper()->GetFormulaNeed(ItemID, i);
		if(Need > 0)
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.formula", u8"# %s %d/%d"),
				GS()->LocItemName(m_VoteBuildClientID, i), pP->m_AccData.m_aItems[i].m_Num, Need);
			AddVote_TextLine(aLine);
			Got = true;
		}
	}
	if(!Got)
		AddVote_TextLine(VL(GS(), pP, "inv.empty", u8"（空）"));
}

void CVoteMenuManager::CountItemNum(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GS()->m_apPlayers[ClientID] || !GS()->ItemHelper())
		return;

	int ItemCount[NUM_ITYPE] = {0};
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(GS()->m_apPlayers[ClientID]->m_AccData.m_aItems[i].m_Num > 0)
			ItemCount[GS()->ItemHelper()->GetType(i)]++;
	}
	for(int t = 0; t < NUM_ITYPE; t++)
		GS()->m_apPlayers[ClientID]->m_AccData.m_ItemCount[t] = ItemCount[t];
}

namespace
{
static int AddVotesSeparateFromHostExtra(CGameContext *pCtx, CVoteMenuManager *pVote, CPlayer *pP, int VoteClient, int HostId, const json_value &Extra)
{
	if(Extra.type != json_object || !pCtx || !pCtx->ItemHelper())
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
			if(Num <= 0 || !pCtx->ItemHelper()->CheckItemValid(Id))
				continue;
			char aCmd[112];
			str_format(aCmd, sizeof(aCmd), "ccv_menuseparate %d %s %d", HostId, apSlots[a], Id);
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(pCtx, pP, "card.detach", u8"－ %s ×%d"), pCtx->LocItemName(pP->GetCID(), Id), Num);
			pVote->AddVote(aLine, aCmd, VoteClient);
			Added++;
		}
	}
	return Added;
}

/** Embed/separate votes for cards & parts on an equipped host (turret or tool). */
static void AddVotesHostCardManage(CGameContext *pCtx, CVoteMenuManager *pVote, int VoteClient, CPlayer *pP, int HostId)
{
	CItemHelper *pIH = pCtx ? pCtx->ItemHelper() : nullptr;
	if(!pCtx || !pVote || !pP || !pIH || HostId <= 0 || pP->m_AccData.m_aItems[HostId].m_Num <= 0)
		return;

	const int HostIType = pIH->GetType(HostId);
	pVote->AddVote_TextLine(VL(pCtx, pP, "card.embed_header", u8"─ 嵌入卡牌/零件（消耗背包×1）"));
	bool AnyEmbed = false;
	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(pP->m_AccData.m_aItems[i].m_Num <= 0)
			continue;
		if(!pIH->IsPlaceableOnItemType(i, HostIType))
			continue;
		const char *pSlotCmd = (i >= ITEM_PART_BARREL) ? "Parts" : "Cards";
		const char *pSlot = VL(pCtx, pP, (i >= ITEM_PART_BARREL) ? "card.slot.parts" : "card.slot.cards",
			(i >= ITEM_PART_BARREL) ? "Parts" : "Cards");
		char aCmd[112];
		str_format(aCmd, sizeof(aCmd), "ccv_menuplace %d %s %d", HostId, pSlotCmd, i);
		char aLine[VOTE_DESC_LENGTH];
		str_format(aLine, sizeof(aLine), VL(pCtx, pP, "card.embed", u8"＋ %s → %s"), pCtx->LocItemName(pP->GetCID(), i), pSlot);
		pVote->AddVote(aLine, aCmd, VoteClient);
		AnyEmbed = true;
	}
	if(!AnyEmbed)
		pVote->AddVote_TextLine(VL(pCtx, pP, "card.no_embeddable", u8"（背包中暂无可嵌入物品）"));

	pVote->AddVote_Space();
	pVote->AddVote_TextLine(VL(pCtx, pP, "card.detach_header", u8"─ 拆卸（每次卸 1 个）"));
	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pP->m_AccData.m_aItems[HostId].m_aExtra, "vote_host_ex");
	const json_value *pEx = nullptr;
	if(pRoot && (*pRoot)["Extra"].type == json_object)
		pEx = &(*pRoot)["Extra"];
	if(pEx)
	{
		if(AddVotesSeparateFromHostExtra(pCtx, pVote, pP, VoteClient, HostId, *pEx) == 0)
			pVote->AddVote_TextLine(VL(pCtx, pP, "card.no_embedded", u8"（无嵌入）"));
	}
	else
		pVote->AddVote_TextLine(VL(pCtx, pP, "card.no_embedded", u8"（无嵌入）"));
}
} // namespace

void CVoteMenuManager::InitVotes(int ClientID)
{
	CPlayer *pP = (ClientID >= 0 && ClientID < MAX_CLIENTS) ? GS()->m_apPlayers[ClientID] : nullptr;
	if(!pP || !GS()->ItemHelper())
		return;

	SetVoteBuildClientID(ClientID);

	SPlayerVote *pVote = GetPlayerVote(ClientID);
	const int Page = pVote->m_Page;
	SAccSyncData &Data = pP->m_AccData;

	if(pVote->m_aExtraText[0])
	{
		AddVote_TextLine(pVote->m_aExtraText);
		AddVote_TextLine(VL(GS(), pP, "menu.sep.short", "---"));
	}

	switch(Page)
	{
	case PAGE_MENU:
	{
		SetVoteLastPage(Page);
		AddVote_TextLine(VL(GS(), pP, "menu.title", u8"☪ 玩家菜单"));
		AddVote_TextLine(VL(GS(), pP, "menu.sep.short", "---"));
		AddVote_Goto(PAGE_INVENTORY, VL(GS(), pP, "menu.goto.inventory", u8"☞ 背包"));
		AddVote_Goto(PAGE_CRAFT, VL(GS(), pP, "menu.goto.craft", u8"☞ 合成"));
		AddVote_Goto(PAGE_EQUIPMENT, VL(GS(), pP, "menu.goto.equipment", u8"☞ 装备"));
		AddVote_Goto(PAGE_TURRET, VL(GS(), pP, "menu.goto.turret", u8"☞ 炮塔"));
		if(!GS()->Config() || GS()->Config()->m_SvFreeWorldTravel)
			AddVote_Goto(PAGE_WORLDS, VL(GS(), pP, "menu.goto.worlds", u8"☞ 世界/地图"));
		AddVote_Goto(PAGE_QUESTS, VL(GS(), pP, "menu.goto.quests", u8"☞ 任务"));
		AddVote_Goto(PAGE_COMMUNITY, VL(GS(), pP, "menu.goto.community", u8"☞ 社区与赞助"));
		AddVote_Goto(PAGE_SKILLS, VL(GS(), pP, "menu.goto.skills", u8"☞ 技能"));
		AddVote_Goto(PAGE_TRAITS, VL(GS(), pP, "menu.goto.traits", u8"☞ 特质"));
		//if(GS()->m_pController && static_cast<CGameController *>(GS()->m_pController)->TdCanChangeDifficulty()) // for now
		//	AddVote_Goto(PAGE_DIFFICULTY, VL(GS(), pP, "menu.goto.difficulty", u8"☞ 难度选择"));
		AddVote_Space();
	}
	break;

	case PAGE_DIFFICULTY:
	{
		CGameController *pCtrl = GS()->m_pController ? static_cast<CGameController *>(GS()->m_pController) : nullptr;
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(VL(GS(), pP, "difficulty.title", u8"☪ 难度选择"));
		AddVote_TextLine(VL(GS(), pP, "menu.sep.short", "---"));
		if(pCtrl)
		{
			char aLine[128];
			static const char *const s_apKeys[NUM_TD_DIFF] = {"difficulty.easy", "difficulty.normal", "difficulty.hard"};
			static const char *const s_apFallback[NUM_TD_DIFF] = {u8"简单", u8"普通", u8"困难"};
			for(int d = 0; d < NUM_TD_DIFF; d++)
			{
				char aCmd[48];
				str_format(aCmd, sizeof(aCmd), "ccv_menudifficulty %d", d);
				const char *pLabel = VL(GS(), pP, s_apKeys[d], s_apFallback[d]);
				if(pCtrl->GetTdDifficulty() == d)
					str_format(aLine, sizeof(aLine), VL(GS(), pP, "difficulty.selected", u8"✓ %s"), pLabel);
				else
					str_copy(aLine, pLabel, sizeof(aLine));
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
		}
		else
			AddVote_TextLine(VL(GS(), pP, "difficulty.unavailable", u8"当前无法更改难度。"));
		AddVote_TextLine(VL(GS(), pP, "difficulty.hint", u8"仅第 1 波开始前可更改"));
		AddVote_TextLine(VL(GS(), pP, "menu.sep.long", "---------------------"));
	}
	break;

	case PAGE_COMMUNITY:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(VL(GS(), pP, "community.title", u8"☪ 社区与赞助"));
		AddVote_TextLine(VL(GS(), pP, "menu.sep.short", "---"));
		{
			char aLine[128];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "community.qq_group", u8"交流 QQ 群：%d"), GS()->TdQQGroup());
			AddVote_TextLine(aLine);
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "community.sponsor", u8"赞助 QQ：%d"), GS()->TdQQSponsor());
			AddVote_TextLine(aLine);
		}
		AddVote_TextLine(VL(GS(), pP, "community.thanks", u8"感谢支持 — 用于服务器与模式开发"));
		AddVote_TextLine(VL(GS(), pP, "menu.sep.long", "---------------------"));
		AddVote_Back();
	}
	break;

	case PAGE_SKILLS:
	{
		SetVoteLastPage(PAGE_MENU);
		if(Core() && Core()->SkillManager())
			Core()->SkillManager()->BuildSkillsListPage(ClientID);
	}
	break;

	case PAGE_SKILL_SELECT:
	{
		SetVoteLastPage(pVote->m_LastPage >= 0 ? pVote->m_LastPage : PAGE_SKILLS);
		if(Core() && Core()->SkillManager())
			Core()->SkillManager()->BuildSkillDetailPage(ClientID, pVote->m_SkillId);
	}
	break;

	case PAGE_TRAITS:
	{
		SetVoteLastPage(PAGE_MENU);
		if(Core() && Core()->TraitManager())
			Core()->TraitManager()->BuildTraitVotePage(ClientID);
	}
	break;

	case PAGE_QUESTS:
	{
		SetVoteLastPage(PAGE_MENU);
		if(Core() && Core()->QuestManager())
			Core()->QuestManager()->BuildQuestListPage(ClientID);
	}
	break;

	case PAGE_QUEST_DETAIL:
	{
		SetVoteLastPage(pVote->m_LastPage >= 0 ? pVote->m_LastPage : PAGE_QUESTS);
		if(Core() && Core()->QuestManager())
			Core()->QuestManager()->BuildQuestDetailPage(ClientID, pVote->m_QuestIdx);
	}
	break;

	case PAGE_INVENTORY:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(VL(GS(), pP, "inv.title", u8"☪ 背包"));
		AddVote_Space();
		CountItemNum(ClientID);
		for(int i = 0; i < NUM_ITYPE; i++)
		{
			if(pVote->m_Select[SPlayerVote::ITEMLIST] != i)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::ITEMLIST, i);
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.category", u8"▹ %s (%d)"),
					ItemTypeLoc(GS(), pP, i), Data.m_ItemCount[i]);
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.category.open", u8"▾ %s (%d)"),
					ItemTypeLoc(GS(), pP, i), Data.m_ItemCount[i]);
				AddVote_TextLine(aLine);
			}
		}
		AddVote_Space();
		AddVote_Back();
		AddVote_TextLine(VL(GS(), pP, "menu.sep.long", "---------------------"));
		AddVote_ListInventory(pVote->m_Select[SPlayerVote::ITEMLIST], "ccv_menucheckitem");
	}
	break;

	case PAGE_CHECK_ITEM:
	{
		const int SelectItem = pVote->m_Select[SPlayerVote::ITEM];
		if(!GS()->ItemHelper()->CheckItemValid(SelectItem))
		{
			pVote->m_Page = PAGE_INVENTORY;
			SetVoteBuildClientID(-1);
			InitVotes(ClientID);
			return;
		}
		SetVoteLastPage(PAGE_INVENTORY);
		AddVote_TextLine(VL(GS(), pP, "item.info", u8"☪ 物品信息"));
		AddVote_Space();
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.name", u8"物品: %s"), GS()->LocItemName(ClientID, SelectItem));
			AddVote_TextLine(aLine);
		}
		{
			const int T = GS()->ItemHelper()->GetType(SelectItem);
			if(T == ITYPE_CARD)
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.capacity_need", u8"需要容量: %d"), GS()->ItemHelper()->GetMaxCapacity(SelectItem));
				AddVote_TextLine(aLine);
			}
			else if(T != ITYPE_MATERIAL)
			{
				const int Cap = GS()->ItemHelper()->GetCapacityFromExtra(pP->GetExtraForItem(SelectItem));
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.capacity", u8"容量: %d/%d"), Cap, GS()->ItemHelper()->GetMaxCapacity(SelectItem));
				AddVote_TextLine(aLine);
			}
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.owned", u8"持有: %d"), Data.m_aItems[SelectItem].m_Num);
			AddVote_TextLine(aLine);
		}
		AddVote_Space();
		{
			const int T = GS()->ItemHelper()->GetType(SelectItem);
			if(T == ITYPE_PICKAXE || T == ITYPE_AXE || T == ITYPE_SWORD || T == ITYPE_TURRET)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuequip %d", SelectItem);
				AddVote(VL(GS(), pP, "item.equip", u8"☝ 装备"), aCmd, m_VoteBuildClientID);
			}
		}
		{
			const int TT = GS()->ItemHelper()->GetType(SelectItem);
			if((TT == ITYPE_PICKAXE || TT == ITYPE_AXE || TT == ITYPE_SWORD) && SelectItem == Data.m_Holding[TT] && Data.m_aItems[SelectItem].m_Num > 0)
			{
				AddVote_Space();
				AddVotesHostCardManage(GS(), this, ClientID, pP, SelectItem);
			}
		}
		AddVote_Space();
		AddVote_Back();
	}
	break;

	case PAGE_CRAFT:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(VL(GS(), pP, "craft.title", u8"☪ 合成"));
		AddVote_Space();
		for(int i = 0; i < NUM_ITYPE; i++)
		{
			if(pVote->m_Select[SPlayerVote::ITEMLIST] != i)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::ITEMLIST, i);
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.category", u8"▹ %s"), ItemTypeLoc(GS(), pP, i));
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.category.open", u8"▾ %s"), ItemTypeLoc(GS(), pP, i));
				AddVote_TextLine(aLine);
			}
		}
		AddVote_Space();
		AddVote_Back();
		AddVote_TextLine(VL(GS(), pP, "menu.sep.long", "---------------------"));
		AddVote_ListCraft(pVote->m_Select[SPlayerVote::ITEMLIST]);
	}
	break;

	case PAGE_CRAFT_SELECTED:
	{
		const int Sel = pVote->m_Select[SPlayerVote::ITEM];
		if(!GS()->ItemHelper()->CheckItemValid(Sel) || !GS()->ItemHelper()->HasFormula(Sel))
		{
			pVote->m_Page = PAGE_CRAFT;
			SetVoteBuildClientID(-1);
			InitVotes(ClientID);
			return;
		}
		SetVoteLastPage(PAGE_CRAFT);
		AddVote_TextLine(VL(GS(), pP, "craft.title", u8"☪ 合成"));
		AddVote_Space();
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.name", u8"物品: %s"), GS()->LocItemName(ClientID, Sel));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.owned", u8"持有: %d"), Data.m_aItems[Sel].m_Num);
			AddVote_TextLine(aLine);
		}
		AddVote_TextLine(VL(GS(), pP, "craft.recipe", u8"配方:"));
		AddVote_TextLine(VL(GS(), pP, "menu.sep.short", "---"));
		AddVote_ListFormula(Sel);
		AddVote_TextLine(VL(GS(), pP, "menu.sep.mid", "==="));
		AddVote(VL(GS(), pP, "craft.make", u8"- 合成 !"), "ccv_menumake", m_VoteBuildClientID);
		AddVote_Space(2);
		AddVote_Back();
	}
	break;

	case PAGE_EQUIPMENT:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(VL(GS(), pP, "equip.title", u8"☪ 装备栏"));
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.sword", u8"剑: %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_SWORD], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.axe", u8"斧: %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_AXE], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.pickaxe", u8"镐: %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_PICKAXE], false));
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
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.category", u8"▹ %s (%d)"),
					ItemTypeLoc(GS(), pP, i), Data.m_ItemCount[i]);
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.category.open", u8"▾ %s (%d)"),
					ItemTypeLoc(GS(), pP, i), Data.m_ItemCount[i]);
				AddVote_TextLine(aLine);
			}
		}
		AddVote_Space();
		AddVote_Back();
		AddVote_TextLine(VL(GS(), pP, "menu.sep.long", "---------------------"));
		AddVote_ListInventory(pVote->m_Select[SPlayerVote::EQUIPMENT], "ccv_menucheckitem", true);
	}
	break;

	case PAGE_TURRET:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(VL(GS(), pP, "turret.title", u8"☪ 炮塔"));
		const int TurretID = Data.m_Holding[ITYPE_TURRET];
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "turret.equipped", u8"当前装备: %s"), GS()->LocItemName(ClientID, TurretID, false));
			AddVote_TextLine(aLine);
		}
		AddVote_Space();
		if(pP->IsTurretPlacing())
		{
			AddVote_TextLine(VL(GS(), pP, "turret.place.aiming", u8"◎ 瞄准中 — 左键部署"));
			AddVote(VL(GS(), pP, "turret.place.cancel", u8"✗ 取消部署"), "ccv_menuturretcancel", ClientID);
			AddVote_Space();
		}
		if(pP->HasDeployedTurret())
		{
			CTurret *pT = pP->GetDeployedTurret();
			if(pT)
			{
				char aHp[VOTE_DESC_LENGTH];
				if(pT->IsBroken())
					str_copy(aHp, VL(GS(), pP, "turret.health.broken", u8"炮塔状态: 已损坏"), sizeof(aHp));
				else
					str_format(aHp, sizeof(aHp), VL(GS(), pP, "turret.health", u8"炮塔生命: %d / %d"), pT->GetHealth(), pT->GetMaxHealth());
				AddVote_TextLine(aHp);
			}
			AddVote(VL(GS(), pP, "turret.recall", u8"↩ 收回炮塔"), "ccv_menurecallturret", ClientID);
			if(pT && pT->IsBroken())
			{
				AddVote(VL(GS(), pP, "turret.repair", u8"🔧 修复炮塔"), "ccv_menurepairturret", ClientID);
				CItemHelper *pIH = GS()->ItemHelper();
				if(pIH && pT)
				{
					bool AnyCost = false;
					for(int i = 0; i < NUM_ITEM; i++)
					{
						const int Need = TurretRepair_MaterialCost(pIH, pT->GetItemDefId(), i);
						if(Need <= 0)
							continue;
						char aLine[VOTE_DESC_LENGTH];
						str_format(aLine, sizeof(aLine), VL(GS(), pP, "turret.repair.cost", u8"  · %s × %d"), GS()->LocItemName(ClientID, i), Need);
						AddVote_TextLine(aLine);
						AnyCost = true;
					}
					if(!AnyCost)
						AddVote_TextLine(VL(GS(), pP, "turret.repair.no_cost", u8"  （无材料修复需求）"));
				}
			}
			AddVote_Space();
		}
		AddVote(VL(GS(), pP, "turret.deploy", u8"⚙ 部署炮塔（瞄准放置）"), "ccv_menusetupturret", ClientID);
		AddVote_Goto(PAGE_TURRET_AMMO, VL(GS(), pP, "turret.ammo_mix", u8"☞ 子弹材料配比"));
		AddVote_Space();

		if(TurretID > 0 && Data.m_aItems[TurretID].m_Num > 0)
			AddVotesHostCardManage(GS(), this, ClientID, pP, TurretID);
		else
			AddVote_TextLine(VL(GS(), pP, "turret.need_equip", u8"请先在背包中装备一门炮塔。"));

		AddVote_Space();
		AddVote_ListInventory(ITYPE_TURRET, "ccv_menuequip", true);
		AddVote_Space();
		AddVote_Back();
	}
	break;

	case PAGE_TURRET_AMMO:
	{
		SetVoteLastPage(PAGE_TURRET);
		const STurretAmmoMix &Mix = pP->GetTurretAmmoMix();
		char aSum0[VOTE_DESC_LENGTH];
		char aSum1[VOTE_DESC_LENGTH];
		AddVote_TextLine(VL(GS(), pP, "turret.ammo.title", u8"☪ 子弹配比 (合计100%)"));
		TurretAmmoSummaryLines(GS(), pP, Mix, aSum0, sizeof(aSum0), aSum1, sizeof(aSum1));
		AddVote_TextLine(aSum0);
		if(aSum1[0])
			AddVote_TextLine(aSum1);
		for(int m = 0; m < NUM_TURRET_AMMO_MATS; m++)
		{
			char aCmd[64];
			char aLbl[VOTE_DESC_LENGTH];
			const char *pMat = TurretMatLoc(GS(), pP, m);
			str_format(aCmd, sizeof(aCmd), "ccv_menubumpammo %d 5", m);
			str_format(aLbl, sizeof(aLbl), VL(GS(), pP, "turret.ammo.bump", u8"+5%% %s (%d%%)"), pMat, Mix.m_aPct[m]);
			AddVote(aLbl, aCmd, ClientID);
		}
		AddVote(VL(GS(), pP, "turret.ammo.reset", u8"↺ 重置为纯木材"), "ccv_menuresetammo", ClientID);
		AddVote_Back();
	}
	break;

	case PAGE_WORLDS:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_TextLine(VL(GS(), pP, "worlds.title", u8"☪ 世界传送"));
		Core()->WorldManager()->AddVotes(ClientID);
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

void CVoteMenuManager::ClearVotes(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	GetPlayerVote(ClientID)->m_aVoteOptions.clear();

	CNetMsg_Sv_VoteClearOptions ClearMsg;
	GS()->Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

	InitVotes(ClientID);

	// make some noise
	GS()->m_World.CreateSound(GS()->m_apPlayers[ClientID]->m_ViewPos, SOUND_WEAPON_NOAMMO, CmaskOne(ClientID));
}

bool CVoteMenuManager::TryHandleVoteMenuOption(int ClientID, const char *pDescription)
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

void CVoteMenuManager::ProcessVoteMenuCommand(int ClientID, const char *pCmdLine)
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

	GS()->CommandManager()->OnCommand(aName, pArgs, ClientID);
}

static void ComChatMenu(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame->Accounts() || !pGame->Accounts()->IsEnabled())
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "err.account.disabled", u8"未启用账号。");
		return;
	}
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "err.login.required", u8"请先登录。");
		return;
	}
	if(pGame->Core() && pGame->Core()->VoteMenuManager())
	{
		pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID)->m_Page = PAGE_MENU;
		pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
	}
}

void CVoteMenuManager::RegisterChatCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("menu", "cmd.menu.help", "", ComChatMenu, GS());
}

