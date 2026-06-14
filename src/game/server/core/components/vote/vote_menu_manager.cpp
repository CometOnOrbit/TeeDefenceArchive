/* (c) TeeDefenceArchive - 2026 */
#include <base/system.h>
#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>
#include <engine/shared/jsonparser.h>

#include <game/commands.h>
#include <game/voting.h>
#include <game/server/account.h>
#include <game/server/core/components/content/trait_manager.h>
#include <game/server/core/components/content/enemy_registry.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/meta/achievement_manager.h>
#include <game/server/core/components/meta/duties_manager.h>
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
		"itype.pickaxe", "itype.axe", "itype.sword", "itype.turret", "itype.material", "itype.card",
		"itype.helmet", "itype.chest", "itype.legs"};
	static const char *const s_apFallback[NUM_ITYPE] = {
		u8"镐", u8"斧", u8"剑", u8"炮塔", u8"材料", u8"卡牌",
		u8"头盔", u8"胸甲", u8"护腿"};
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

static void FormatProgressBar(int Current, int Max, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	if(Max <= 0)
		Max = 1;
	Current = maximum(0, minimum(Current, Max));
	const int Width = 10;
	const int Filled = (Current * Width + Max - 1) / Max;
	int Pos = 0;
	pOut[Pos++] = ' ';
	if(Pos < OutSize - 1)
		pOut[Pos++] = '[';
	for(int i = 0; i < Width && Pos < OutSize - 6; i++)
		pOut[Pos++] = (i < Filled) ? '#' : '.';
	if(Pos < OutSize - 1)
		pOut[Pos++] = ']';
	str_format(pOut + Pos, OutSize - Pos, " %d/%d", Current, Max);
}

static void AddVoteWrappedText(CVoteMenuManager *pVote, const char *pText)
{
	if(!pVote || !pText || !pText[0])
		return;

	const int MaxChunk = VOTE_DESC_LENGTH - 1;
	const char *p = pText;
	while(*p)
	{
		char aLine[VOTE_DESC_LENGTH];
		int Len = 0;
		while(p[Len] && Len < MaxChunk)
		{
			const unsigned char Byte = (unsigned char)p[Len];
			if(Len > 0 && Len + 1 < MaxChunk && Byte >= 0x80)
			{
				int CharBytes = 1;
				if((Byte & 0xE0) == 0xC0)
					CharBytes = 2;
				else if((Byte & 0xF0) == 0xE0)
					CharBytes = 3;
				else if((Byte & 0xF8) == 0xF0)
					CharBytes = 4;
				if(Len + CharBytes > MaxChunk)
					break;
			}
			Len++;
		}
		if(Len <= 0)
			break;
		str_copy(aLine, p, minimum((int)sizeof(aLine), Len + 1));
		pVote->AddVote_TextLine(aLine);
		p += Len;
	}
}

static void AddVoteItemDesc(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int ItemId)
{
	if(!pVote || !pCtx || !pP)
		return;
	const char *pDesc = pCtx->LocItemDesc(pP->GetCID(), ItemId);
	if(!pDesc || !pDesc[0])
		return;
	AddVoteWrappedText(pVote, pDesc);
}

static void AddVoteEmbeddedItems(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int HostId)
{
	if(!pVote || !pCtx || !pP || !pCtx->ItemHelper())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pP->m_AccData.m_aItems[HostId].m_aExtra, "vote_item_embedded");
	if(!pRoot || (*pRoot)["Extra"].type != json_object)
		return;

	const json_value &Extra = (*pRoot)["Extra"];
	const json_value *pArrays[2] = {&Extra["Cards"], &Extra["Parts"]};
	const char *apSlotKeys[2] = {"card.slot.cards", "card.slot.parts"};
	const char *apSlotFallback[2] = {u8"卡牌", u8"零件"};
	bool Any = false;

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
			char aLine[VOTE_DESC_LENGTH];
			const int PieceCap = pCtx->ItemHelper()->GetMaxCapacity(Id);
			str_format(aLine, sizeof(aLine), VL(pCtx, pP, "item.embedded", u8"  · %s ×%d (占%d) [%s]"),
				pCtx->LocItemName(pP->GetCID(), Id), Num, PieceCap,
				VL(pCtx, pP, apSlotKeys[a], apSlotFallback[a]));
			pVote->AddVote_TextLine(aLine);
			Any = true;
		}
	}
	if(Any)
		pVote->AddVote_Space();
}

static void AddVotePlaceableTypes(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int ItemId)
{
	if(!pVote || !pCtx || !pP || !pCtx->ItemHelper())
		return;
	CItemHelper *pH = pCtx->ItemHelper();
	const int T = pH->GetType(ItemId);
	if(T != ITYPE_CARD && !pH->IsPartItem(ItemId))
		return;

	char aLine[VOTE_DESC_LENGTH];
	char aBuf[64];
	aLine[0] = 0;
	bool Any = false;
	for(int t = 0; t < NUM_ITYPE; t++)
	{
		if(!pH->IsPlaceableOnItemType(ItemId, t))
			continue;
		if(t == ITYPE_MATERIAL || t == ITYPE_CARD)
			continue;
		const char *pName = ItemTypeLoc(pCtx, pP, t);
		char aPiece[40];
		str_format(aPiece, sizeof(aPiece), "%s%s", Any ? ", " : "", pName);
		str_append(aLine, aPiece, sizeof(aLine));
		Any = true;
	}
	if(!Any)
		return;
	str_format(aBuf, sizeof(aBuf), VL(pCtx, pP, "item.placeable", u8"可安放: %s"), aLine);
	pVote->AddVote_TextLine(aBuf);
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
	AddVote_Goto(GetPlayerVote(m_VoteBuildClientID)->m_LastPage, VL(GS(), pP, "menu.back", u8"« 返回"));
}

void CVoteMenuManager::AddVote_PageHeader(const char *pTitle)
{
	if(!pTitle || !pTitle[0])
		return;
	char aBuf[VOTE_DESC_LENGTH];
	str_format(aBuf, sizeof(aBuf), u8"══ %s ══", pTitle);
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_PageSubtitle(const char *pText)
{
	if(!pText || !pText[0])
		return;
	char aBuf[VOTE_DESC_LENGTH];
	str_format(aBuf, sizeof(aBuf), u8"◇ %s", pText);
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_Separator()
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? GS()->m_apPlayers[m_VoteBuildClientID] : nullptr;
	AddVote_TextLine(VL(GS(), pP, "menu.sep.short", u8"────────────────"));
}

void CVoteMenuManager::AddVote_Section(const char *pLabel)
{
	if(!pLabel || !pLabel[0])
		return;
	char aBuf[VOTE_DESC_LENGTH];
	str_format(aBuf, sizeof(aBuf), u8"▸ %s", pLabel);
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_ProgressLine(int Current, int Max)
{
	char aBuf[VOTE_DESC_LENGTH];
	FormatProgressBar(Current, Max, aBuf, sizeof(aBuf));
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_PageFooter()
{
	AddVote_Space();
	AddVote_Back();
}

void CVoteMenuManager::AddVote_EmptyHint(const char *pText)
{
	if(!pText || !pText[0])
		return;
	char aBuf[VOTE_DESC_LENGTH];
	str_format(aBuf, sizeof(aBuf), u8"  · %s", pText);
	AddVote_TextLine(aBuf);
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
		const char *pSlotCmd = pIH->IsPartItem(i) ? "Parts" : "Cards";
		const char *pSlot = VL(pCtx, pP, pIH->IsPartItem(i) ? "card.slot.parts" : "card.slot.cards",
			pIH->IsPartItem(i) ? "Parts" : "Cards");
		char aCmd[112];
		str_format(aCmd, sizeof(aCmd), "ccv_menuplace %d %s %d", HostId, pSlotCmd, i);
		char aLine[VOTE_DESC_LENGTH];
		const int NeedCap = pIH->GetMaxCapacity(i);
		str_format(aLine, sizeof(aLine), VL(pCtx, pP, "card.embed_cap", u8"＋ %s (占%d) → %s"),
			pCtx->LocItemName(pP->GetCID(), i), NeedCap, pSlot);
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
		AddVote_PageHeader(VL(GS(), pP, "menu.title", u8"玩家菜单"));
		if(pP && pP->GetAccountId() >= 0 && pP->m_AccData.m_aUsername[0])
			AddVote_PageSubtitle(pP->m_AccData.m_aUsername);
		AddVote_Separator();
		if(pP && pP->GetAccountId() >= 0)
		{
			AddVote_Section(VL(GS(), pP, "menu.section.current_equip", u8"当前装备"));
			static const int s_aShowSlots[] = {
				ITYPE_PICKAXE, ITYPE_AXE, ITYPE_SWORD, ITYPE_TURRET,
				ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS,
			};
			static const char *const s_apSlotKeys[] = {
				"menu.slot.pickaxe", "menu.slot.axe", "menu.slot.sword", "menu.slot.turret",
				"menu.slot.helmet", "menu.slot.chest", "menu.slot.legs",
			};
			static const char *const s_apSlotFallback[] = {
				u8"  镐", u8"  斧", u8"  剑", u8"  炮塔",
				u8"  头盔", u8"  胸甲", u8"  护腿",
			};
			for(int s = 0; s < 7; s++)
			{
				char aLine[VOTE_DESC_LENGTH];
				const int ItemId = pP->m_AccData.m_Holding[s_aShowSlots[s]];
				const char *pItemName = GS()->LocItemName(ClientID, ItemId, false);
				str_format(aLine, sizeof(aLine), "%s · %s",
					VL(GS(), pP, s_apSlotKeys[s], s_apSlotFallback[s]),
					pItemName);
				AddVote_TextLine(aLine);
			}
			AddVote_Separator();
		}
		AddVote_Section(VL(GS(), pP, "menu.section.gear", u8"物品与装备"));
		AddVote_Goto(PAGE_INVENTORY, VL(GS(), pP, "menu.goto.inventory", u8"  ☞ 背包"));
		AddVote_Goto(PAGE_CRAFT, VL(GS(), pP, "menu.goto.craft", u8"  ☞ 合成"));
		AddVote_Goto(PAGE_EQUIPMENT, VL(GS(), pP, "menu.goto.equipment", u8"  ☞ 装备"));
		AddVote_Goto(PAGE_TURRET, VL(GS(), pP, "menu.goto.turret", u8"  ☞ 炮塔"));
		AddVote_Space();
		AddVote_Section(VL(GS(), pP, "menu.section.growth", u8"成长与目标"));
		AddVote_Goto(PAGE_QUESTS, VL(GS(), pP, "menu.goto.quests", u8"  ☞ 任务"));
		AddVote_Goto(PAGE_DUTIES, VL(GS(), pP, "menu.goto.duties", u8"  ☞ 日常"));
		AddVote_Goto(PAGE_ACHIEVEMENTS, VL(GS(), pP, "menu.goto.achievements", u8"  ☞ 成就"));
		AddVote_Space();
		AddVote_Section(VL(GS(), pP, "menu.section.power", u8"角色能力"));
		AddVote_Goto(PAGE_SKILLS, VL(GS(), pP, "menu.goto.skills", u8"  ☞ 技能"));
		AddVote_Goto(PAGE_TRAITS, VL(GS(), pP, "menu.goto.traits", u8"  ☞ 特质"));
		AddVote_Space();
		AddVote_Section(VL(GS(), pP, "menu.section.other", u8"其他"));
		if(!GS()->Config() || GS()->Config()->m_SvFreeWorldTravel)
			AddVote_Goto(PAGE_WORLDS, VL(GS(), pP, "menu.goto.worlds", u8"  ☞ 世界传送"));
		AddVote_Goto(PAGE_COMMUNITY, VL(GS(), pP, "menu.goto.community", u8"  ☞ 社区与赞助"));
		AddVote_Space();
		AddVote_Goto(PAGE_COMPENDIUM, VL(GS(), pP, "menu.goto.compendium", u8"  ☞ 图鉴"));
	}
	break;

	case PAGE_DIFFICULTY:
	{
		CGameController *pCtrl = GS()->m_pController ? static_cast<CGameController *>(GS()->m_pController) : nullptr;
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "difficulty.title", u8"难度选择"));
		AddVote_Separator();
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
			AddVote_EmptyHint(VL(GS(), pP, "difficulty.unavailable", u8"当前无法更改难度"));
		AddVote_EmptyHint(VL(GS(), pP, "difficulty.hint", u8"仅第 1 波开始前可更改"));
		AddVote_PageFooter();
	}
	break;

	case PAGE_COMMUNITY:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "community.title", u8"社区与赞助"));
		AddVote_Separator();
		{
			char aLine[128];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "community.qq_group", u8"  QQ 群：%d"), GS()->TdQQGroup());
			AddVote_TextLine(aLine);
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "community.sponsor", u8"  赞助 QQ：%d"), GS()->TdQQSponsor());
			AddVote_TextLine(aLine);
		}
		AddVote_EmptyHint(VL(GS(), pP, "community.thanks", u8"感谢支持服务器与模式开发"));
		AddVote_PageFooter();
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

	case PAGE_DUTIES:
	{
		SetVoteLastPage(PAGE_MENU);
		if(Core() && Core()->DutiesManager())
			Core()->DutiesManager()->BuildDutiesPage(ClientID);
	}
	break;

	case PAGE_ACHIEVEMENTS:
	{
		SetVoteLastPage(PAGE_MENU);
		if(Core() && Core()->AchievementManager())
			Core()->AchievementManager()->BuildAchievementsPage(ClientID);
	}
	break;

	case PAGE_INVENTORY:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "inv.title", u8"背包"));
		AddVote_Separator();
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
		AddVote_Separator();
		AddVote_ListInventory(pVote->m_Select[SPlayerVote::ITEMLIST], "ccv_menucheckitem");
		AddVote_PageFooter();
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
		AddVote_PageHeader(VL(GS(), pP, "item.info", u8"物品详情"));
		AddVote_Separator();
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.name", u8"◇ %s"), GS()->LocItemName(ClientID, SelectItem));
			AddVote_TextLine(aLine);
		}
		AddVoteItemDesc(this, GS(), pP, SelectItem);
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
				AddVoteEmbeddedItems(this, GS(), pP, SelectItem);
			}
		}
		AddVotePlaceableTypes(this, GS(), pP, SelectItem);
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.owned", u8"持有: %d"), Data.m_aItems[SelectItem].m_Num);
			AddVote_TextLine(aLine);
		}
		AddVote_Space();
		{
			const int T = GS()->ItemHelper()->GetType(SelectItem);
			if(T == ITYPE_PICKAXE || T == ITYPE_AXE || T == ITYPE_SWORD || T == ITYPE_TURRET
			|| T == ITYPE_HELMET || T == ITYPE_CHEST || T == ITYPE_LEGS)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuequip %d", SelectItem);
				AddVote(VL(GS(), pP, "item.equip", u8"☝ 装备"), aCmd, m_VoteBuildClientID);
			}
		}
		{
			const int TT = GS()->ItemHelper()->GetType(SelectItem);
			if((TT == ITYPE_PICKAXE || TT == ITYPE_AXE || TT == ITYPE_SWORD
			|| TT == ITYPE_HELMET || TT == ITYPE_CHEST || TT == ITYPE_LEGS) && SelectItem == Data.m_Holding[TT] && Data.m_aItems[SelectItem].m_Num > 0)
			{
				AddVote_Space();
				AddVotesHostCardManage(GS(), this, ClientID, pP, SelectItem);
			}
		}
		AddVote_PageFooter();
	}
	break;

	case PAGE_CRAFT:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "craft.title", u8"合成"));
		AddVote_Separator();
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
		AddVote_Separator();
		AddVote_ListCraft(pVote->m_Select[SPlayerVote::ITEMLIST]);
		AddVote_PageFooter();
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
		AddVote_PageHeader(VL(GS(), pP, "craft.title", u8"合成"));
		AddVote_Separator();
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.name", u8"◇ %s"), GS()->LocItemName(ClientID, Sel));
			AddVote_TextLine(aLine);
		}
		AddVoteItemDesc(this, GS(), pP, Sel);
		{
			const int T = GS()->ItemHelper()->GetType(Sel);
			if(T == ITYPE_CARD || GS()->ItemHelper()->IsPartItem(Sel))
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.capacity_need", u8"需要容量: %d"), GS()->ItemHelper()->GetMaxCapacity(Sel));
				AddVote_TextLine(aLine);
			}
			else if(T != ITYPE_MATERIAL)
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.capacity", u8"容量上限: %d"), GS()->ItemHelper()->GetMaxCapacity(Sel));
				AddVote_TextLine(aLine);
			}
		}
		AddVotePlaceableTypes(this, GS(), pP, Sel);
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.owned", u8"持有：%d"), Data.m_aItems[Sel].m_Num);
			AddVote_TextLine(aLine);
		}
		AddVote_Section(VL(GS(), pP, "craft.recipe", u8"所需材料"));
		AddVote_ListFormula(Sel);
		AddVote_Separator();
		AddVote(VL(GS(), pP, "craft.make", u8"★ 开始合成"), "ccv_menumake", m_VoteBuildClientID);
		AddVote_PageFooter();
	}
	break;

	case PAGE_EQUIPMENT:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "equip.title", u8"装备栏"));
		AddVote_Separator();
		AddVote_Section(VL(GS(), pP, "equip.section.weapon", u8"武器与工具"));
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.sword", u8"  剑 · %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_SWORD], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.axe", u8"  斧 · %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_AXE], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.pickaxe", u8"  镐 · %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_PICKAXE], false));
			AddVote_TextLine(aLine);
		}
		AddVote_Section(VL(GS(), pP, "equip.section.armor", u8"盔甲"));
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.helmet", u8"  头盔 · %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_HELMET], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.chest", u8"  胸甲 · %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_CHEST], false));
			AddVote_TextLine(aLine);
		}
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "equip.legs", u8"  护腿 · %s"), GS()->LocItemName(ClientID, Data.m_Holding[ITYPE_LEGS], false));
			AddVote_TextLine(aLine);
		}
		AddVote_Separator();
		AddVote_Section(VL(GS(), pP, "equip.section.change", u8"更换装备"));
		CountItemNum(ClientID);
		for(int i = 0; i < NUM_ITYPE; i++)
		{
			if(i != ITYPE_PICKAXE && i != ITYPE_AXE && i != ITYPE_SWORD
				&& i != ITYPE_HELMET && i != ITYPE_CHEST && i != ITYPE_LEGS)
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
		AddVote_Separator();
		AddVote_ListInventory(pVote->m_Select[SPlayerVote::EQUIPMENT], "ccv_menucheckitem", true);
		AddVote_PageFooter();
	}
	break;

	case PAGE_TURRET:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "turret.title", u8"炮塔"));
		const int TurretID = Data.m_Holding[ITYPE_TURRET];
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "turret.equipped", u8"◇ 装备：%s"), GS()->LocItemName(ClientID, TurretID, false));
			AddVote_TextLine(aLine);
		}
		AddVote_Separator();
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
			AddVote_EmptyHint(VL(GS(), pP, "turret.need_equip", u8"请先在背包中装备炮塔"));
		AddVote_Separator();
		AddVote_Section(VL(GS(), pP, "turret.section.equip", u8"装备炮塔"));
		AddVote_ListInventory(ITYPE_TURRET, "ccv_menuequip", true);
		AddVote_PageFooter();
	}
	break;

	case PAGE_TURRET_AMMO:
	{
		SetVoteLastPage(PAGE_TURRET);
		const STurretAmmoMix &Mix = pP->GetTurretAmmoMix();
		char aSum0[VOTE_DESC_LENGTH];
		char aSum1[VOTE_DESC_LENGTH];
		AddVote_PageHeader(VL(GS(), pP, "turret.ammo.title", u8"子弹配比"));
		AddVote_EmptyHint(VL(GS(), pP, "turret.ammo.hint", u8"合计须为 100%"));
		AddVote_Separator();
		TurretAmmoSummaryLines(GS(), pP, Mix, aSum0, sizeof(aSum0), aSum1, sizeof(aSum1));
		AddVote_TextLine(aSum0);
		if(aSum1[0])
			AddVote_TextLine(aSum1);
		AddVote_Separator();
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
		AddVote_PageFooter();
	}
	break;

	case PAGE_WORLDS:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "worlds.title", u8"世界传送"));
		AddVote_Separator();
		Core()->WorldManager()->AddVotes(ClientID);
		AddVote_PageFooter();
	}
	break;

	case PAGE_COMPENDIUM:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "compendium.title", u8"图鉴"));
		AddVote_Separator();
		AddVote_PageSubtitle(VL(GS(), pP, "compendium.subtitle", u8"查阅游戏资料"));
		AddVote_Space();

		AddVote_Section(VL(GS(), pP, "compendium.section.zombie", u8"僵尸种类"));
		AddVote_Goto(PAGE_COMPENDIUM_ZOMBIE, VL(GS(), pP, "compendium.goto.zombie", u8"  ☞ 查看全部僵尸"));
		{
			const int Num = Core() && Core()->EnemyRegistry() ? Core()->EnemyRegistry()->NumEnemies() : 0;
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "compendium.count.zombie", u8"  （共 %d 种）"), Num);
			AddVote_TextLine(aLine);
		}
		AddVote_Space();

		AddVote_Section(VL(GS(), pP, "compendium.section.item", u8"物品效果"));
		AddVote_Goto(PAGE_COMPENDIUM_ITEM, VL(GS(), pP, "compendium.goto.item", u8"  ☞ 浏览全部物品"));
		AddVote_Space();

		AddVote_Section(VL(GS(), pP, "compendium.section.turret_ammo", u8"炮塔子弹配比"));
		AddVote_Goto(PAGE_COMPENDIUM_TURRET_AMMO, VL(GS(), pP, "compendium.goto.turret_ammo", u8"  ☞ 材料效果说明"));
		AddVote_PageFooter();
	}
	break;

	case PAGE_COMPENDIUM_ZOMBIE:
	{
		SetVoteLastPage(PAGE_COMPENDIUM);
		AddVote_PageHeader(VL(GS(), pP, "compendium.zombie.title", u8"僵尸种类"));
		AddVote_Separator();
		if(Core() && Core()->EnemyRegistry())
		{
			CEnemyRegistry *pER = Core()->EnemyRegistry();
			const int Num = pER->NumEnemies();
			if(Num <= 0)
				AddVote_EmptyHint(VL(GS(), pP, "compendium.zombie.empty", u8"暂无数据"));
			else
			{
				for(int i = 0; i < Num; i++)
				{
					const SEnemyDef *pDef = pER->GetEnemy(i);
					if(!pDef)
						continue;

					char aZombKey[48];
					str_format(aZombKey, sizeof(aZombKey), "enemy.%s", pDef->m_aId);
					const char *pZombName = VL(GS(), pP, aZombKey, pDef->m_aId);
					const int NameLen = str_length(pZombName);

					// Line 1: name + wave
					{
						char aLine[VOTE_DESC_LENGTH];
						if(pDef->m_WaveMin > 0)
						{
							char aWave[24];
							str_format(aWave, sizeof(aWave), VL(GS(), pP, "compendium.zombie.wave", u8" 第%d波起"), pDef->m_WaveMin);
							str_format(aLine, sizeof(aLine), u8"▹ %s%s", pZombName, aWave);
						}
						else
							str_format(aLine, sizeof(aLine), u8"▹ %s", pZombName);
						AddVote_TextLine(aLine);
					}

					// Line 2: HP mul
					if(pDef->m_HpMul > 0.01f && pDef->m_HpMul != 1.f)
					{
						char aHp[VOTE_DESC_LENGTH];
						str_format(aHp, sizeof(aHp), VL(GS(), pP, "compendium.zombie.hp_mul", u8"  血量×%.1f"), pDef->m_HpMul);
						AddVote_TextLine(aHp);
					}

					// Line 3: tags (one per line if many)
					if(pDef->m_NumTags > 0)
					{
						for(int t = 0; t < pDef->m_NumTags; t++)
						{
							char aTagKey[48];
							str_format(aTagKey, sizeof(aTagKey), "compendium.tag.%s", pDef->m_aaTagNames[t]);
							const char *pTagDesc = VL(GS(), pP, aTagKey, pDef->m_aaTagNames[t]);
							char aTag[VOTE_DESC_LENGTH];
							str_format(aTag, sizeof(aTag), VL(GS(), pP, "compendium.zombie.tag_line", u8"  ·%s"), pTagDesc);
							AddVote_TextLine(aTag);
						}
					}

					// Line 4+: loot, one per line
					for(int l = 0; l < pDef->m_NumLoot; l++)
					{
						const SEnemyLootEntry &Entry = pDef->m_aLoot[l];
						const char *pMatName = GS()->LocItemName(ClientID, Entry.m_ItemId);
						char aLoot[VOTE_DESC_LENGTH];
						if(Entry.m_MinNum == Entry.m_MaxNum)
							str_format(aLoot, sizeof(aLoot), u8"  掉落 %s ×%d", pMatName, Entry.m_MinNum);
						else
							str_format(aLoot, sizeof(aLoot), u8"  掉落 %s ×%d~%d", pMatName, Entry.m_MinNum, Entry.m_MaxNum);
						AddVote_TextLine(aLoot);
					}

					if(pDef->m_NumLoot + pDef->m_NumTags > 0)
						AddVote_Space();
				}
			}
		}
		else
			AddVote_EmptyHint(VL(GS(), pP, "compendium.zombie.unavailable", u8"僵尸数据尚未加载"));
		AddVote_PageFooter();
	}
	break;

	case PAGE_COMPENDIUM_ITEM:
	{
		SetVoteLastPage(PAGE_COMPENDIUM);
		AddVote_PageHeader(VL(GS(), pP, "compendium.item.title", u8"物品效果"));
		AddVote_Separator();

		CItemHelper *pH = GS()->ItemHelper();
		if(!pH)
			AddVote_EmptyHint(VL(GS(), pP, "compendium.item.unavailable", u8"物品系统尚未加载"));
		else
		{
			for(int t = 0; t < NUM_ITYPE; t++)
			{
				if(pVote->m_Select[SPlayerVote::ITEMLIST] != t)
				{
					char aCmd[64];
					str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::ITEMLIST, t);
					char aLine[VOTE_DESC_LENGTH];
					str_format(aLine, sizeof(aLine), VL(GS(), pP, "compendium.item.category", u8"▹ %s"), ItemTypeLoc(GS(), pP, t));
					AddVote(aLine, aCmd, m_VoteBuildClientID);
				}
				else
				{
					char aLine[VOTE_DESC_LENGTH];
					str_format(aLine, sizeof(aLine), VL(GS(), pP, "compendium.item.category.open", u8"▾ %s"), ItemTypeLoc(GS(), pP, t));
					AddVote_TextLine(aLine);

					const int Cat = pVote->m_Select[SPlayerVote::ITEMLIST];
					for(int i = 0; i < NUM_ITEM; i++)
					{
						if(pH->GetType(i) != Cat)
							continue;
						if(!pH->HasItemDefinition(i))
							continue;

						const char *pName = GS()->LocItemName(ClientID, i);
						const char *pDesc = GS()->LocItemDesc(ClientID, i);
						const int PrefixLen = 4; // "  · "
						const int SepLen = pDesc && pDesc[0] ? 3 : 0; // " — "
						const int NameLen = str_length(pName);
						const int DescAvail = VOTE_DESC_LENGTH - 1 - PrefixLen - NameLen - SepLen;

						char aLine2[VOTE_DESC_LENGTH];
						if(pDesc && pDesc[0] && DescAvail > 2)
						{
							char aShortDesc[VOTE_DESC_LENGTH];
							str_copy(aShortDesc, pDesc, minimum((int)sizeof(aShortDesc), DescAvail + 1));
							if(str_length(aShortDesc) < str_length(pDesc))
							{
								// Truncated — replace last 3 chars with "..."
								const int Dst = str_length(aShortDesc);
								if(Dst >= 4)
								{
									aShortDesc[Dst - 1] = '.';
									aShortDesc[Dst - 2] = '.';
									aShortDesc[Dst - 3] = '.';
								}
							}
							str_format(aLine2, sizeof(aLine2), u8"  · %s — %s", pName, aShortDesc);
						}
						else
							str_format(aLine2, sizeof(aLine2), u8"  · %s", pName);
						AddVote_TextLine(aLine2);
					}
				}
			}
		}
		AddVote_PageFooter();
	}
	break;

	case PAGE_COMPENDIUM_TURRET_AMMO:
	{
		SetVoteLastPage(PAGE_COMPENDIUM);
		AddVote_PageHeader(VL(GS(), pP, "compendium.turret_ammo.title", u8"子弹材料效果"));
		AddVote_Separator();
		AddVoteWrappedText(this, u8"炮塔使用材料作子弹，不同材料提供不同效果，配比可在炮塔页面调整。");

		static const struct { int m_Mat; const char *m_pName; const char *m_pEffect; } s_aAmmo[] = {
			{TURRET_AMMO_LOG,      u8"木材", u8"基础材料，无特殊加成"},
			{TURRET_AMMO_COAL,     u8"煤炭", u8"≥20% 子弹爆炸；主导→定向爆裂弹"},
			{TURRET_AMMO_COPPER,   u8"铜",   u8"≥20% 连锁闪电；主导→电弧弹"},
			{TURRET_AMMO_IRON,     u8"铁",   u8"≥15% 伤害提升（每25% +1倍）"},
			{TURRET_AMMO_GOLD,     u8"金",   u8"≥10% 伤害提升（每20% +1倍）"},
			{TURRET_AMMO_DIAMOND,  u8"钻石", u8"≥10% 伤害提升（每10% +1倍）"},
			{TURRET_AMMO_ENEGRY,   u8"能量", u8"≥25% 聚变效果；配煤爆裂更大"},
			{TURRET_AMMO_ZOMBIEHEART, u8"僵尸之心", u8"无战斗效果，珍贵通货"},
		};

		for(int i = 0; i < 8; i++)
		{
			const char *pMat = TurretMatLoc(GS(), pP, s_aAmmo[i].m_Mat);
			const char *pEffect = s_aAmmo[i].m_pEffect;

			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), u8"▹ %s", pMat);
			AddVote_TextLine(aLine);

			AddVoteWrappedText(this, pEffect);
			AddVote_Space();
		}

		AddVoteWrappedText(this, u8"伤害加成可叠加。卡牌特效（爆炸/聚变/电子链）也可与材料效果叠加。");
		AddVote_PageFooter();
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

	if(Core() && Core()->DispatchPlayerVoteCommand(ClientID, aName, pArgs))
		return;
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

