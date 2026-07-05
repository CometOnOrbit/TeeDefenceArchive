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
#include <game/server/core/components/npcs/npc_service.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/mmo_exp.h>
#include <game/server/core/attribute_types.h>
#include <game/server/gamecontroller.h>
#include <game/server/worldmodes/defence.h>
#include <game/server/item_system.h>
#include <game/server/entities/turret.h>
#include <game/server/player.h>
#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/turret_ammo.h>

#include <generated/server_data.h>
#include <game/server/interaction_sound.h>

const char *CVoteMenuManager::VL(CGameContext *pCtx, CPlayer *pP, const char *pKey, const char *pDefault)
{
	if(pCtx)
		return pCtx->Loc(pP ? pP->GetCID() : -1, pKey, pDefault);
	return pDefault;
}

const char *CVoteMenuManager::ItemTypeLoc(CGameContext *pCtx, CPlayer *pP, int ItemType)
{
	static const char *const s_apKeys[NUM_ITYPE] = {
		"itype.pickaxe", "itype.axe", "itype.sword", "itype.turret", "itype.material", "itype.card",
		"itype.helmet", "itype.chest", "itype.legs"};
	static const char *const s_apFallback[NUM_ITYPE] = {
		"镐", "斧", "剑", "炮塔", "材料", "卡牌",
		"头盔", "胸甲", "护腿"};
	if(ItemType < 0 || ItemType >= NUM_ITYPE)
		return VL(pCtx, pP, "common.unknown", "?");
	return VL(pCtx, pP, s_apKeys[ItemType], s_apFallback[ItemType]);
}

const char *CVoteMenuManager::TurretMatLoc(CGameContext *pCtx, CPlayer *pP, int Mat)
{
	static const char *const s_apKeys[NUM_TURRET_AMMO_MATS] = {
		"turret.mat.log", "turret.mat.coal", "turret.mat.copper", "turret.mat.iron",
		"turret.mat.gold", "turret.mat.diamond", "turret.mat.energy", "turret.mat.zombieheart"};
	static const char *const s_apFallback[NUM_TURRET_AMMO_MATS] = {
		"木材", "煤炭", "铜", "铁", "金", "钻石", "能量", "僵尸之心"};
	if(Mat < 0 || Mat >= NUM_TURRET_AMMO_MATS)
		return VL(pCtx, pP, "common.unknown", "?");
	return VL(pCtx, pP, s_apKeys[Mat], s_apFallback[Mat]);
}

const char *CVoteMenuManager::TurretMatShort(CGameContext *pCtx, CPlayer *pP, int Mat)
{
	static const char *const s_apKeys[NUM_TURRET_AMMO_MATS] = {
		"turret.mat.log.short", "turret.mat.coal.short", "turret.mat.copper.short", "turret.mat.iron.short",
		"turret.mat.gold.short", "turret.mat.diamond.short", "turret.mat.energy.short", "turret.mat.zombieheart.short"};
	static const char *const s_apFallback[NUM_TURRET_AMMO_MATS] = {
		"木", "煤", "铜", "铁", "金", "钻", "能", "心"};
	if(Mat < 0 || Mat >= NUM_TURRET_AMMO_MATS)
		return VL(pCtx, pP, "common.unknown", "?");
	return VL(pCtx, pP, s_apKeys[Mat], s_apFallback[Mat]);
}

void CVoteMenuManager::TurretAmmoSummaryLines(CGameContext *pCtx, CPlayer *pP, const STurretAmmoMix &Mix, char *pLine0, int Len0, char *pLine1, int Len1)
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

void CVoteMenuManager::FormatProgressBar(int Current, int Max, char *pOut, int OutSize)
{
	if(!pOut || OutSize <= 0)
		return;
	if(Max <= 0)
		Max = 1;
	Current = maximum(0, minimum(Current, Max));
	const int Width = 10;
	const int Filled = (Current * Width + Max - 1) / Max;
	int Pos = 0;
	// ┃
	pOut[Pos++] = 0xE2; pOut[Pos++] = 0x94; pOut[Pos++] = 0x83;
	for(int i = 0; i < Width && Pos < OutSize - 6; i++)
	{
		if(i < Filled)
			{ pOut[Pos++] = 0xE2; pOut[Pos++] = 0x96; pOut[Pos++] = 0x88; } // █
		else
			{ pOut[Pos++] = 0xE2; pOut[Pos++] = 0x96; pOut[Pos++] = 0x91; } // ░
	}
	// ┃
	pOut[Pos++] = 0xE2; pOut[Pos++] = 0x94; pOut[Pos++] = 0x83;
	if(Pos < OutSize - 10)
		str_format(pOut + Pos, OutSize - Pos, " %d/%d", Current, Max);
	else if(Pos < OutSize)
		pOut[Pos] = 0;
}

void CVoteMenuManager::AddVoteWrappedText(CVoteMenuManager *pVote, const char *pText)
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

void CVoteMenuManager::AddVoteItemDesc(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int ItemId)
{
	if(!pVote || !pCtx || !pP)
		return;
	const char *pDesc = pCtx->LocItemDesc(pP->GetCID(), ItemId);
	if(!pDesc || !pDesc[0])
		return;
	AddVoteWrappedText(pVote, pDesc);
}

void CVoteMenuManager::AddVoteEmbeddedItems(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int HostId)
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
	const char *apSlotFallback[2] = {"卡牌", "零件"};
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
			str_format(aLine, sizeof(aLine), VL(pCtx, pP, "item.embedded", "  · %s ×%d (占%d) [%s]"),
				pCtx->LocItemName(pP->GetCID(), Id), Num, PieceCap,
				VL(pCtx, pP, apSlotKeys[a], apSlotFallback[a]));
			pVote->AddVote_TextLine(aLine);
			Any = true;
		}
	}
	if(Any)
		pVote->AddVote_Space();
}

void CVoteMenuManager::AddVotePlaceableTypes(CVoteMenuManager *pVote, CGameContext *pCtx, CPlayer *pP, int ItemId)
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
	str_format(aBuf, sizeof(aBuf), VL(pCtx, pP, "item.placeable", "可安放: %s"), aLine);
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

void CVoteMenuManager::AddVote_Info(const char *pText)
{
	AddVote_TextLine(pText);
}

void CVoteMenuManager::AddVote_Option(const char *pDesc, const char *pCmd)
{
	if(m_VoteBuildClientID < 0 || m_VoteBuildClientID >= MAX_CLIENTS || !pDesc || !pCmd)
		return;

	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	// ▾ (U+257E) — MRPG selectable marker
	aBuf[Pos++] = 0xE2;
	aBuf[Pos++] = 0x97;
	aBuf[Pos++] = 0xBE;
	aBuf[Pos++] = ' ';
	for(const char *p = pDesc; *p && Pos < (int)sizeof(aBuf) - 1; p++)
		aBuf[Pos++] = *p;
	aBuf[Pos] = 0;
	AddVote(aBuf, pCmd, m_VoteBuildClientID);
}

void CVoteMenuManager::AddVote_GroupTitle(const char *pTitle)
{
	if(!pTitle || !pTitle[0])
		return;

	// MRPG aligned title: ▾──── ├ Title ├ ────▾
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	const int TitleLen = str_length(pTitle);
	const int Pad = maximum(0, ((int)VOTE_DESC_LENGTH - TitleLen - 12) / 2);

	auto appendChar = [&](char c) {
		if(Pos < (int)sizeof(aBuf) - 1)
			aBuf[Pos++] = c;
	};
	auto appendUtf8 = [&](const char *pSeq, int Len) {
		for(int i = 0; i < Len && Pos < (int)sizeof(aBuf) - 1; i++)
			aBuf[Pos++] = pSeq[i];
	};

	appendUtf8("\xE2\x97\xBE", 3); // ▾
	for(int i = 0; i < Pad / 4; i++)
		appendUtf8("\xE2\x94\x80", 3); // ─
	appendUtf8("\xE2\x94\x9C", 3); // ├
	appendChar(' ');
	for(const char *p = pTitle; *p && Pos < (int)sizeof(aBuf) - 4; p++)
		appendChar(*p);
	appendChar(' ');
	appendUtf8("\xE2\x94\x9C", 3); // ├
	for(int i = 0; i < Pad / 4; i++)
		appendUtf8("\xE2\x94\x80", 3);
	appendUtf8("\xE2\x97\xBE", 3);
	aBuf[Pos] = 0;
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_GroupLine()
{
	// ▾───────────────────├
	AddVote_TextLine("\xE2\x97\xBE\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x9C");
}

void CVoteMenuManager::AddVote_Footer()
{
	AddVote_GroupLine();
	AddVote_Back();
}

void CVoteMenuManager::AddVote_Goto(int Page, const char *pDesc)
{
	char aCmd[64];
	str_format(aCmd, sizeof(aCmd), "ccv_menugoto %d", Page);
	AddVote_Option(pDesc, aCmd);
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
	char aBuf[VOTE_DESC_LENGTH];
	str_format(aBuf, sizeof(aBuf), "%s %s", "\u21A9", VL(GS(), pP, "menu.back", "返回"));
	char aCmd[64];
	str_format(aCmd, sizeof(aCmd), "ccv_menugoto %d", GetPlayerVote(m_VoteBuildClientID)->m_LastPage);
	AddVote_Option(aBuf, aCmd);
}

void CVoteMenuManager::AddVote_PageHeader(const char *pTitle)
{
	if(!pTitle || !pTitle[0])
		return;
	// ╔══════ Title ══════╗
	char aBuf[VOTE_DESC_LENGTH];
	int Remaining = VOTE_DESC_LENGTH - 1;
	int Pos = 0;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x95; aBuf[Pos++] = 0x94; Remaining -= 3; // ╔
	const int TitleLen = str_length(pTitle);
	const int PadNeeded = maximum(0, (VOTE_DESC_LENGTH - TitleLen - 6) / 2);
	for(int i = 0; i < PadNeeded && Remaining > 3; i++)
	{ aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x95; aBuf[Pos++] = 0x90; Remaining -= 3; } // ═
	aBuf[Pos++] = 0x20; Remaining--;
	for(const char *p = pTitle; *p && Remaining > 0; p++)
	{ aBuf[Pos++] = *p; Remaining--; }
	aBuf[Pos++] = 0x20; Remaining--;
	for(int i = 0; i < PadNeeded && Remaining > 3; i++)
	{ aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x95; aBuf[Pos++] = 0x90; Remaining -= 3; } // ═
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x95; aBuf[Pos++] = 0x97; // ╗
	aBuf[Pos] = 0;
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_PageSubtitle(const char *pText)
{
	if(!pText || !pText[0])
		return;
	// ──── subtitle ────
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x80; // ─
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x80; // ─
	aBuf[Pos++] = 0x20;
	for(const char *p = pText; *p && Pos < (int)VOTE_DESC_LENGTH - 6; p++)
		aBuf[Pos++] = *p;
	aBuf[Pos++] = 0x20;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x80; // ─
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x80; // ─
	aBuf[Pos] = 0;
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_Separator()
{
	// ════════════════════════
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	int MaxChars = (VOTE_DESC_LENGTH - 1) / 3;
	for(int i = 0; i < MaxChars; i++)
	{ aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x95; aBuf[Pos++] = 0x90; } // ═
	aBuf[Pos] = 0;
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_Section(const char *pLabel)
{
	if(!pLabel || !pLabel[0])
		return;
	// ┏━ label ━━━━━━━━━┓
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x8F; // ┏
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x81; // ━
	aBuf[Pos++] = 0x20;
	const int LabelLen = str_length(pLabel);
	for(const char *p = pLabel; *p; p++)
		aBuf[Pos++] = *p;
	aBuf[Pos++] = 0x20;
	// fill remaining with ━
	while(Pos <= (int)VOTE_DESC_LENGTH - 7)
	{ aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x81; } // ━
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x93; // ┓
	aBuf[Pos] = 0;
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_ProgressLine(int Current, int Max)
{
	char aBuf[VOTE_DESC_LENGTH];
	FormatProgressBar(Current, Max, aBuf, sizeof(aBuf));
	AddVote_TextLine(aBuf);
}

void CVoteMenuManager::AddVote_BulletItem(const char *pDesc, const char *pCmd)
{
	if(!pDesc || !pDesc[0])
		return;
	// ┊ desc
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x8A; // ┊
	aBuf[Pos++] = 0x20;
	for(const char *p = pDesc; *p && Pos < (int)VOTE_DESC_LENGTH - 1; p++)
		aBuf[Pos++] = *p;
	aBuf[Pos] = 0;
	AddVote(aBuf, pCmd, m_VoteBuildClientID);
}

void CVoteMenuManager::AddVote_ItemEntry(const char *pDesc, const char *pCmd)
{
	if(!pDesc || !pDesc[0])
		return;
	// ▪ desc
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x96; aBuf[Pos++] = 0xAA; // ▪ (U+25AA)
	aBuf[Pos++] = 0x20;
	for(const char *p = pDesc; *p && Pos < (int)VOTE_DESC_LENGTH - 1; p++)
		aBuf[Pos++] = *p;
	aBuf[Pos] = 0;
	AddVote(aBuf, pCmd, m_VoteBuildClientID);
}

void CVoteMenuManager::AddVote_PageFooter()
{
	// ┗━━━━━━━━━━━━━━━━━━━┛
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x97; // ┗
	int MaxChars = (VOTE_DESC_LENGTH - 6) / 3;
	for(int i = 0; i < MaxChars; i++)
	{ aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x81; } // ━
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x9B; // ┛
	aBuf[Pos] = 0;
	AddVote_TextLine(aBuf);
	AddVote_Back();
}

void CVoteMenuManager::AddVote_EmptyHint(const char *pText)
{
	if(!pText || !pText[0])
		return;
	// │ · text
	char aBuf[VOTE_DESC_LENGTH];
	int Pos = 0;
	aBuf[Pos++] = 0xE2; aBuf[Pos++] = 0x94; aBuf[Pos++] = 0x82; // │
	aBuf[Pos++] = 0x20;
	// · (U+00B7)
	aBuf[Pos++] = 0xC2; aBuf[Pos++] = 0xB7;
	aBuf[Pos++] = 0x20;
	for(const char *p = pText; *p && Pos < (int)VOTE_DESC_LENGTH - 1; p++)
		aBuf[Pos++] = *p;
	aBuf[Pos] = 0;
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
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.item.equipped", "➳ %s x%d ✓"),
					GS()->LocItemName(m_VoteBuildClientID, i), pP->m_AccData.m_aItems[i].m_Num);
			else
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "inv.item", "➳ %s x%d"),
					GS()->LocItemName(m_VoteBuildClientID, i), pP->m_AccData.m_aItems[i].m_Num);
			AddVote(aLine, aCmd, m_VoteBuildClientID);
			Got = true;
		}
	}
	if(!Got)
		AddVote_TextLine(VL(GS(), pP, "inv.empty", "（空）"));
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
		AddVote_TextLine(VL(GS(), pP, "inv.empty", "（空）"));
	}
}

void CVoteMenuManager::AddVote_Craft(int ItemID)
{
	CPlayer *pP = m_VoteBuildClientID >= 0 ? GS()->m_apPlayers[m_VoteBuildClientID] : nullptr;
	char aCmd[64];
	str_format(aCmd, sizeof(aCmd), "ccv_menucraft %d", ItemID);
	char aLine[VOTE_DESC_LENGTH];
	str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.item", "➳ %s"), GS()->LocItemName(m_VoteBuildClientID, ItemID));
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
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.formula", "# %s %d/%d"),
				GS()->LocItemName(m_VoteBuildClientID, i), pP->m_AccData.m_aItems[i].m_Num, Need);
			AddVote_TextLine(aLine);
			Got = true;
		}
	}
	if(!Got)
		AddVote_TextLine(VL(GS(), pP, "inv.empty", "（空）"));
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

int CVoteMenuManager::AddVotesSeparateFromHostExtra(CGameContext *pCtx, CVoteMenuManager *pVote, CPlayer *pP, int VoteClient, int HostId, const json_value &Extra)
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
			str_format(aLine, sizeof(aLine), VL(pCtx, pP, "card.detach", "－ %s ×%d"), pCtx->LocItemName(pP->GetCID(), Id), Num);
			pVote->AddVote(aLine, aCmd, VoteClient);
			Added++;
		}
	}
	return Added;
}

/** Embed/separate votes for cards & parts on an equipped host (turret or tool). */
void CVoteMenuManager::AddVotesHostCardManage(CGameContext *pCtx, CVoteMenuManager *pVote, int VoteClient, CPlayer *pP, int HostId)
{
	CItemHelper *pIH = pCtx ? pCtx->ItemHelper() : nullptr;
	if(!pCtx || !pVote || !pP || !pIH || HostId <= 0 || pP->m_AccData.m_aItems[HostId].m_Num <= 0)
		return;

	const int HostIType = pIH->GetType(HostId);
	pVote->AddVote_TextLine(VL(pCtx, pP, "card.embed_header", "─ 嵌入卡牌/零件（消耗背包×1）"));
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
		str_format(aLine, sizeof(aLine), VL(pCtx, pP, "card.embed_cap", "＋ %s (占%d) → %s"),
			pCtx->LocItemName(pP->GetCID(), i), NeedCap, pSlot);
		pVote->AddVote(aLine, aCmd, VoteClient);
		AnyEmbed = true;
	}
	if(!AnyEmbed)
		pVote->AddVote_TextLine(VL(pCtx, pP, "card.no_embeddable", "（背包中暂无可嵌入物品）"));

	pVote->AddVote_Space();
	pVote->AddVote_TextLine(VL(pCtx, pP, "card.detach_header", "─ 拆卸（每次卸 1 个）"));
	CJsonParser Parser;
	json_value *pRoot = Parser.ParseString(pP->m_AccData.m_aItems[HostId].m_aExtra, "vote_host_ex");
	const json_value *pEx = nullptr;
	if(pRoot && (*pRoot)["Extra"].type == json_object)
		pEx = &(*pRoot)["Extra"];
	if(pEx)
	{
		if(CVoteMenuManager::AddVotesSeparateFromHostExtra(pCtx, pVote, pP, VoteClient, HostId, *pEx) == 0)
			pVote->AddVote_TextLine(VL(pCtx, pP, "card.no_embedded", "（无嵌入）"));
	}
	else
		pVote->AddVote_TextLine(VL(pCtx, pP, "card.no_embedded", "（无嵌入）"));
}

bool CVoteMenuManager::BuildMenuPage(int ClientID, int Page)
{
	if(!Core())
		return false;

	for(int i = 0; i < Core()->ComponentCount(); i++)
	{
		TWorldComponent *pComp = Core()->GetComponent(i);
		if(pComp && pComp->OnVoteMenuPage(ClientID, Page))
			return true;
	}
	return false;
}

void CVoteMenuManager::InitVotes(int ClientID)
{
	CPlayer *pP = (ClientID >= 0 && ClientID < MAX_CLIENTS) ? GS()->m_apPlayers[ClientID] : nullptr;
	if(!pP)
		return;
	const bool IsRpg = GS()->IsWorldType(WorldType::RPG);
	if(!IsRpg && !GS()->ItemHelper())
		return;

	SetVoteBuildClientID(ClientID);

	SPlayerVote *pVote = GetPlayerVote(ClientID);
	int Page = pVote->m_Page;
	SAccSyncData &Data = pP->m_AccData;

	if(!EnsureNpcServiceAccess(GS(), pP, pVote))
	{
		if(IsNpcServicePage(Page) || IsRemoteShopBrowserPage(Page))
			NotifyNpcServiceDenied(GS(), ClientID);
		ClearNpcService(pVote);
		pVote->m_Page = PAGE_MENU;
		Page = PAGE_MENU;
	}

	// Try component dispatch first
	if(BuildMenuPage(ClientID, Page))
		return;

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

		const bool IsRpg = GS()->IsWorldType(WorldType::RPG);

		if(!IsRpg)
		{
			// === TD 菜单 ===
			AddVote_PageHeader(VL(GS(), pP, "menu.title", "玩家菜单"));
			if(pP && pP->GetAccountId() >= 0 && pP->m_AccData.m_aUsername[0])
				AddVote_PageSubtitle(pP->m_AccData.m_aUsername);
			AddVote_Separator();
			if(pP && pP->GetAccountId() >= 0)
			{
				AddVote_Section(VL(GS(), pP, "menu.section.current_equip", "当前装备"));
				static const int s_aShowSlots[] = {
					ITYPE_PICKAXE, ITYPE_AXE, ITYPE_SWORD, ITYPE_TURRET,
					ITYPE_HELMET, ITYPE_CHEST, ITYPE_LEGS,
				};
				static const char *const s_apSlotKeys[] = {
					"menu.slot.pickaxe", "menu.slot.axe", "menu.slot.sword", "menu.slot.turret",
					"menu.slot.helmet", "menu.slot.chest", "menu.slot.legs",
				};
				static const char *const s_apSlotFallback[] = {
					"  镐", "  斧", "  剑", "  炮塔",
					"  头盔", "  胸甲", "  护腿",
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
			AddVote_Section(VL(GS(), pP, "menu.section.gear", "物品与装备"));
			AddVote_Goto(PAGE_INVENTORY, VL(GS(), pP, "menu.goto.inventory", "  ☞ 背包"));
			AddVote_Goto(PAGE_EQUIPMENT, VL(GS(), pP, "menu.goto.equipment", "  ☞ 装备"));
			AddVote_Goto(PAGE_TURRET, VL(GS(), pP, "menu.goto.turret", "  ☞ 炮塔"));
			AddVote_Space();
			AddVote_Section(VL(GS(), pP, "menu.section.growth", "成长与目标"));
			AddVote_Goto(PAGE_DUTIES, VL(GS(), pP, "menu.goto.duties", "  ☞ 日常"));
			AddVote_Goto(PAGE_ACHIEVEMENTS, VL(GS(), pP, "menu.goto.achievements", "  ☞ 成就"));
			AddVote_Space();
			AddVote_Section(VL(GS(), pP, "menu.section.power", "角色能力"));
			AddVote_Goto(PAGE_TRAITS, VL(GS(), pP, "menu.goto.traits", "  ☞ 特质"));
			AddVote_Space();
			AddVote_Section(VL(GS(), pP, "menu.section.other", "其他"));
			if(!GS()->Config() || GS()->Config()->m_SvFreeWorldTravel)
				AddVote_Goto(PAGE_WORLDS, VL(GS(), pP, "menu.goto.worlds", "  ☞ 世界传送"));
			AddVote_Goto(PAGE_COMMUNITY, VL(GS(), pP, "menu.goto.community", "  ☞ 社区与赞助"));
			AddVote_Space();
			AddVote_Goto(PAGE_COMPENDIUM, VL(GS(), pP, "menu.goto.compendium", "  ☞ 图鉴"));

			// 难度选择（仅在 Defence 世界显示）
			if(GS()->IsWorldType(WorldType::Defence))
			{
				AddVote_Space();
				AddVote_Goto(PAGE_DIFFICULTY, VL(GS(), pP, "menu.goto.difficulty", "  ☞ 难度选择"));
			}

			// 进入 F|RPG 世界入口
			{
				const int Num = GS()->Server()->GetNumWorlds();
				for(int i = 0; i < Num; i++)
				{
					const CWorldDetail *pDetail = GS()->Server()->GetWorldDetail(i);
					if(pDetail && pDetail->GetType() == WorldType::RPG)
					{
						AddVote_Space();
						char aCmd[48];
						str_format(aCmd, sizeof(aCmd), "ccv_menutravel %d", i);
						AddVote(VL(GS(), pP, "menu.goto.frpg_world", "  ☞ 进入 F|RPG 世界"), aCmd, ClientID);
						break;
					}
				}
			}
		}
		else
		{
			// RPG 主菜单 — MRPG 风格：信息区(只读) + 子菜单(可点)
			SetVoteLastPage(PAGE_MENU);
			pP->RecalcMMOStats();

			char aLine[VOTE_DESC_LENGTH];
			const int Level = pP->GetStat(AttributeIdentifier::Level);

			AddVote_GroupTitle("角色信息");
			if(pP->GetAccountId() >= 0 && pP->m_AccData.m_aUsername[0])
			{
				str_format(aLine, sizeof(aLine), "账号: %s", pP->m_AccData.m_aUsername);
				AddVote_Info(aLine);
			}
			str_format(aLine, sizeof(aLine), "等级 %d  经验 %d / %d",
				Level, pP->GetStat(AttributeIdentifier::Experience), ExpForLevel(Level));
			AddVote_Info(aLine);
			str_format(aLine, sizeof(aLine), "金币 %d  技能点 %d",
				pP->GetStat(AttributeIdentifier::Gold), pP->GetStat(AttributeIdentifier::SkillPoints));
			AddVote_Info(aLine);
			AddVote_GroupLine();

			AddVote_GroupTitle("个人菜单");
			AddVote_Goto(PAGE_ATTRIBUTES, "属性分配");
			AddVote_Goto(VOTE_PAGE_MMO_BACKPACK, "背包");
			AddVote_Goto(VOTE_PAGE_MMO_EQUIP, "装备");
			AddVote_GroupLine();

			AddVote_GroupTitle("社交与活动");
			AddVote_Goto(VOTE_PAGE_MMO_SOCIAL, "社交");
			AddVote_Goto(VOTE_PAGE_MMO_ACTIVITIES, "活动");
			AddVote_GroupLine();

			AddVote_GroupTitle("经济与生活");
			AddVote_Goto(VOTE_PAGE_MMO_ECONOMY, "经济");
			AddVote_Goto(VOTE_PAGE_MMO_LIFESTYLE, "生活");
			AddVote_GroupLine();

			AddVote_GroupTitle("PvP");
			AddVote_Goto(VOTE_PAGE_MMO_PVP, "公会 / Boss");
			AddVote_GroupLine();

			AddVote_GroupTitle("小游戏");
			AddVote_Goto(VOTE_PAGE_MMO_DEFENCE, "塔防模式 (Defence)");
			AddVote_Footer();
		}
	}
	break;

	case PAGE_DIFFICULTY:
	{
		auto *pCtrl = dynamic_cast<CGameControllerDefence *>(GS()->m_pController);
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "difficulty.title", "难度选择"));
		AddVote_Separator();
		if(pCtrl)
		{
			char aLine[128];
			static const char *const s_apKeys[NUM_TD_DIFF] = {"difficulty.easy", "difficulty.normal", "difficulty.hard"};
			static const char *const s_apFallback[NUM_TD_DIFF] = {"简单", "普通", "困难"};
			for(int d = 0; d < NUM_TD_DIFF; d++)
			{
				char aCmd[48];
				str_format(aCmd, sizeof(aCmd), "ccv_menudifficulty %d", d);
				const char *pLabel = VL(GS(), pP, s_apKeys[d], s_apFallback[d]);
				if(pCtrl->GetTdDifficulty() == d)
					str_format(aLine, sizeof(aLine), VL(GS(), pP, "difficulty.selected", "✓ %s"), pLabel);
				else
					str_copy(aLine, pLabel, sizeof(aLine));
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
		}
		else
			AddVote_EmptyHint(VL(GS(), pP, "difficulty.unavailable", "当前无法更改难度"));
		AddVote_EmptyHint(VL(GS(), pP, "difficulty.hint", "仅第 1 波开始前可更改"));
		AddVote_PageFooter();
	}
	break;

	case PAGE_COMMUNITY:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "community.title", "社区与赞助"));
		AddVote_Separator();
		{
			char aLine[128];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "community.qq_group", "  QQ 群：%d"), GS()->TdQQGroup());
			AddVote_TextLine(aLine);
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "community.sponsor", "  赞助 QQ：%d"), GS()->TdQQSponsor());
			AddVote_TextLine(aLine);
		}
		AddVote_EmptyHint(VL(GS(), pP, "community.thanks", "感谢支持服务器与模式开发"));
		AddVote_PageFooter();
	}
	break;

	case PAGE_TURRET:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "turret.title", "炮塔"));
		const int TurretID = Data.m_Holding[ITYPE_TURRET];
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "turret.equipped", "◇ 装备：%s"), GS()->LocItemName(ClientID, TurretID, false));
			AddVote_TextLine(aLine);
		}
		AddVote_Separator();
		if(pP->IsTurretPlacing())
		{
			AddVote_TextLine(VL(GS(), pP, "turret.place.aiming", "◎ 瞄准中 — 左键部署"));
			AddVote(VL(GS(), pP, "turret.place.cancel", "✗ 取消部署"), "ccv_menuturretcancel", ClientID);
			AddVote_Space();
		}
		if(pP->HasDeployedTurret())
		{
			CTurret *pT = pP->GetDeployedTurret();
			if(pT)
			{
				char aHp[VOTE_DESC_LENGTH];
				if(pT->IsBroken())
					str_copy(aHp, VL(GS(), pP, "turret.health.broken", "炮塔状态: 已损坏"), sizeof(aHp));
				else
					str_format(aHp, sizeof(aHp), VL(GS(), pP, "turret.health", "炮塔生命: %d / %d"), pT->GetHealth(), pT->GetMaxHealth());
				AddVote_TextLine(aHp);
			}
			AddVote(VL(GS(), pP, "turret.recall", "↩ 收回炮塔"), "ccv_menurecallturret", ClientID);
			if(pT && pT->IsBroken())
			{
				AddVote(VL(GS(), pP, "turret.repair", "🔧 修复炮塔"), "ccv_menurepairturret", ClientID);
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
						str_format(aLine, sizeof(aLine), VL(GS(), pP, "turret.repair.cost", "  · %s × %d"), GS()->LocItemName(ClientID, i), Need);
						AddVote_TextLine(aLine);
						AnyCost = true;
					}
					if(!AnyCost)
						AddVote_TextLine(VL(GS(), pP, "turret.repair.no_cost", "  （无材料修复需求）"));
				}
			}
			AddVote_Space();
		}
		AddVote(VL(GS(), pP, "turret.deploy", "⚙ 部署炮塔（瞄准放置）"), "ccv_menusetupturret", ClientID);
		AddVote_Goto(PAGE_TURRET_AMMO, VL(GS(), pP, "turret.ammo_mix", "☞ 子弹材料配比"));
		AddVote_Space();

		if(TurretID > 0 && Data.m_aItems[TurretID].m_Num > 0)
			CVoteMenuManager::AddVotesHostCardManage(GS(), this, ClientID, pP, TurretID);
		else
			AddVote_EmptyHint(VL(GS(), pP, "turret.need_equip", "请先在背包中装备炮塔"));
		AddVote_Separator();
		AddVote_Section(VL(GS(), pP, "turret.section.equip", "装备炮塔"));
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
		AddVote_PageHeader(VL(GS(), pP, "turret.ammo.title", "子弹配比"));
		AddVote_EmptyHint(VL(GS(), pP, "turret.ammo.hint", "合计须为 100%"));
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
			str_format(aLbl, sizeof(aLbl), VL(GS(), pP, "turret.ammo.bump", "+5%% %s (%d%%)"), pMat, Mix.m_aPct[m]);
			AddVote(aLbl, aCmd, ClientID);
		}
		AddVote(VL(GS(), pP, "turret.ammo.reset", "↺ 重置为纯木材"), "ccv_menuresetammo", ClientID);
		AddVote_PageFooter();
	}
	break;

	case PAGE_WORLDS:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "menu.section.worlds", "世界传送"));
		AddVote_Separator();
		if(Core() && Core()->WorldManager())
			Core()->WorldManager()->AddVotes(ClientID);
		else
			AddVote_EmptyHint(VL(GS(), pP, "worlds.unavailable", "暂无可传送的世界"));
		AddVote_Space();
		AddVote_PageFooter();
	}
	break;

	case PAGE_CRAFT:
	{
		SetVoteLastPage(PAGE_MENU);
		AddVote_PageHeader(VL(GS(), pP, "craft.title", "合成"));
		AddVote_Separator();
		for(int i = 0; i < NUM_ITYPE; i++)
		{
			if(pVote->m_Select[SPlayerVote::ITEMLIST] != i)
			{
				char aCmd[64];
				str_format(aCmd, sizeof(aCmd), "ccv_menuselitem %d %d", SPlayerVote::ITEMLIST, i);
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.category", "▹ %s"), ItemTypeLoc(GS(), pP, i));
				AddVote(aLine, aCmd, m_VoteBuildClientID);
			}
			else
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.category.open", "▾ %s"), ItemTypeLoc(GS(), pP, i));
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
		AddVote_PageHeader(VL(GS(), pP, "craft.title", "合成"));
		AddVote_Separator();
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.name", "◇ %s"), GS()->LocItemName(ClientID, Sel));
			AddVote_TextLine(aLine);
		}
		AddVoteItemDesc(this, GS(), pP, Sel);
		{
			const int T = GS()->ItemHelper()->GetType(Sel);
			if(T == ITYPE_CARD || GS()->ItemHelper()->IsPartItem(Sel))
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.capacity_need", "需要容量: %d"), GS()->ItemHelper()->GetMaxCapacity(Sel));
				AddVote_TextLine(aLine);
			}
			else if(T != ITYPE_MATERIAL)
			{
				char aLine[VOTE_DESC_LENGTH];
				str_format(aLine, sizeof(aLine), VL(GS(), pP, "item.capacity", "容量上限: %d"), GS()->ItemHelper()->GetMaxCapacity(Sel));
				AddVote_TextLine(aLine);
			}
		}
		AddVotePlaceableTypes(this, GS(), pP, Sel);
		{
			char aLine[VOTE_DESC_LENGTH];
			str_format(aLine, sizeof(aLine), VL(GS(), pP, "craft.owned", "持有：%d"), Data.m_aItems[Sel].m_Num);
			AddVote_TextLine(aLine);
		}
		AddVote_Section(VL(GS(), pP, "craft.recipe", "所需材料"));
		AddVote_ListFormula(Sel);
		AddVote_Separator();
		AddVote(VL(GS(), pP, "craft.make", "★ 开始合成"), "ccv_menumake", m_VoteBuildClientID);
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
}

void CVoteMenuManager::ClearVoteOptions(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	GetPlayerVote(ClientID)->m_aVoteOptions.clear();

	CNetMsg_Sv_VoteClearOptions ClearMsg;
	GS()->Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);
}

bool CVoteMenuManager::TryHandleVoteMenuOption(int ClientID, const char *pDescription, int ReasonNumber, const char *pReason)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pDescription)
		return false;

	auto DescriptionsMatch = [](const char *pClient, const char *pServer) -> bool {
		if(!pClient || !pServer)
			return false;
		if(str_comp_nocase(pClient, pServer) == 0)
			return true;
		const int ClientLen = str_length(pClient);
		const int ServerLen = str_length(pServer);
		if(ClientLen >= 8 && ClientLen < ServerLen && str_comp_nocase_num(pServer, pClient, ClientLen) == 0)
			return true;
		if(ServerLen >= 8 && ServerLen < ClientLen && str_comp_nocase_num(pClient, pServer, ServerLen) == 0)
			return true;
		return false;
	};

	SPlayerVote *pV = GetPlayerVote(ClientID);
	for(int i = 0; i < pV->m_aVoteOptions.size(); i++)
	{
		if(!DescriptionsMatch(pDescription, pV->m_aVoteOptions[i].m_aDescription))
			continue;

		const char *pCmd = pV->m_aVoteOptions[i].m_aCommand;
		if(!pCmd[0])
			return true;

		if(str_comp(pCmd, "ccv_null") == 0)
			return true;

		if(str_length(pCmd) >= 4 && str_comp_nocase_num(pCmd, "ccv_", 4) == 0)
		{
			ProcessVoteMenuCommand(ClientID, pCmd, ReasonNumber, pReason);
			return true;
		}

		return true;
	}

	return false;
}

void CVoteMenuManager::ProcessVoteMenuCommand(int ClientID, const char *pCmdLine, int ReasonNumber, const char *pReason)
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

	char aMergedArgs[VOTE_CMD_LENGTH];
	if((!pArgs || !pArgs[0]) && pReason && pReason[0])
	{
		str_copy(aMergedArgs, pReason, sizeof(aMergedArgs));
		pArgs = aMergedArgs;
	}

	if(str_comp(aName, "null") != 0)
	{
		if(str_comp(aName, "menugoto") == 0)
			PlayUiMenuOpen(GS()->m_World, ClientID);
		else
			PlayUiMenuSelect(GS()->m_World, ClientID);
	}

	bool Handled = false;
	if(Core() && Core()->DispatchPlayerVoteCommand(ClientID, aName, pArgs, ReasonNumber, pReason))
		Handled = true;
	else
	{
		char aVoteCmdArgs[VOTE_CMD_LENGTH];
		const char *pVoteCmdArgs = pArgs;
		if(pReason && pReason[0] && pArgs && pArgs[0] && !str_find(pArgs, " "))
		{
			str_format(aVoteCmdArgs, sizeof(aVoteCmdArgs), "%s %s", pArgs, pReason);
			pVoteCmdArgs = aVoteCmdArgs;
		}
		if(GS()->CommandManager()->OnVoteCommand(aName, pVoteCmdArgs, ClientID) == 0)
			Handled = true;
	}
	if(!Handled)
		GS()->SendChatLoc(ClientID, "vote.cmd.invalid", "该选项当前不可用。");
}

static void ComChatMenu(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame->Accounts() || !pGame->Accounts()->IsEnabled())
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
	if(pGame->Core() && pGame->Core()->VoteMenuManager())
	{
		PlayUiMenuOpen(pGame->m_World, pCtx->m_ClientID);
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

