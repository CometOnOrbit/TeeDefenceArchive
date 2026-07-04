#include "mmo_manager.h"
#include <game/server/data_center.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/commands.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/mmo_exp.h>
#include <game/server/core/components/skills/skill_defs.h>
#include <game/server/core/components/economy/shop_data.h>
#include <game/server/entities/character.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <game/server/account.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_query.h>
#include <game/server/entities/pet.h>
#include <mysql.h>

static CCharacter *GetCharacterSafe(CPlayer *pP)
{
	return pP && pP->GetCharacter() ? pP->GetCharacter() : nullptr;
}

void CMMOManager::RegisterMMOVoteCommands(CCommandManager *pManager)
{
	if(!pManager) return;
	CGameContext *pGame = GS();
	pManager->AddCommand("mmo", "打开 MMO 背包", "", ConMMO, pGame);
	pManager->AddCommand("mmomenu", "", "", ConMMO, pGame);
	pManager->AddCommand("mmoitem", "打开背包物品详情", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->ShowMMOItemDetail(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);
	pManager->AddCommand("mmoequip", "装备到指定栏位", "ii", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		const int Slot = pR->GetInteger(0);
		const int LoadoutSlot = pR->GetInteger(1);
		if(pG->Core()->GetMMOManager()->EquipWeapon(pG->m_apPlayers[pCtx->m_ClientID], Slot, LoadoutSlot))
			pG->Core()->GetMMOManager()->ShowMMOItemDetail(pCtx->m_ClientID, Slot);
	}, pGame);
	pManager->AddCommand("mmounequip", "", "", ConMMOUnequip, pGame);
	pManager->AddCommand("mmoequippage", "", "", ConMMOEquip, pGame);
	pManager->AddCommand("mmospawn", "<怪物名/ID>", "s", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->ConMMOSpawn(pR->GetString(0), pG->m_apPlayers[pCtx->m_ClientID]);
	}, pGame);
}

void CMMOManager::ConMMO(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GetMMOManager()) return;

	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "请先登录。");
		return;
	}
	pGame->Core()->GetMMOManager()->ShowMMOInventory(pCtx->m_ClientID);
}

void CMMOManager::ConMMOEquip(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GetMMOManager()) return;
	pGame->Core()->GetMMOManager()->ShowMMOEquip(pCtx->m_ClientID);
}

void CMMOManager::ConMMOUnequip(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GetMMOManager()) return;
	pGame->Core()->GetMMOManager()->UnequipWeapon(pGame->m_apPlayers[pCtx->m_ClientID]);
}

void CMMOManager::ConStory(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GetMMOManager()) return;

	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() < 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "请先登录。");
		return;
	}

	if(pP->m_StoryFlags.empty())
	{
		pGame->SendChatTo(pCtx->m_ClientID, "暂无剧情进度。");
		return;
	}

	pGame->SendChatTo(pCtx->m_ClientID, "━ 剧情进度 ━");
	for(auto it = pP->m_StoryFlags.begin(); it != pP->m_StoryFlags.end(); ++it)
	{
		char aLine[128];
		str_format(aLine, sizeof(aLine), "  %s: 阶段 %d", it->first.c_str(), it->second);
		pGame->SendChatTo(pCtx->m_ClientID, aLine);
	}
}

void CMMOManager::RegisterMMOCommands(CCommandManager *pManager)
{
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("stats", "查看角色状态", "", ConStats, pGame);
	pManager->AddCommand("addstat", "分配属性点: /addstat str|con", "s", ConAddStat, pGame);
	pManager->AddCommand("skills", "查看已学习的技能", "", ConMMOSkills, pGame);
	pManager->AddCommand("learn", "学习或升级技能: /learn <技能名>", "s", ConMMOLearn, pGame);
	pManager->AddCommand("shop", "查看商店: /shop <NPC名字>", "s", ConShop, pGame);
	pManager->AddCommand("buy", "购买物品: /buy <物品ID> <数量>", "ii", ConBuy, pGame);
	pManager->AddCommand("use", "使用物品: /use <背包格> [数量]", "ii", ConUse, pGame);
	pManager->AddCommand("sell", "出售物品: /sell <背包格> <数量>", "ii", ConSell, pGame);
	pManager->AddCommand("mmosell", "出售背包物品: /mmosell <背包格>", "i", ConMMOSell, pGame);
	pManager->AddCommand("addstatvote", "分配属性点: /addstatvote str|con", "s", ConAddStatVote, pGame);
	pManager->AddCommand("itemslot", "绑定物品到表情键: /itemslot <表情槽0-3> <背包格|-1清空>", "ii", ConItemSlot, pGame);
	pManager->AddCommand("story", "查看剧情进度", "", ConStory, pGame);
}

void CMMOManager::RegisterDebugCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	pManager->AddCommand("mmogiveall", "[调试] 获得所有 MMO 物品", "", [](IConsole::IResult *pR, void *pU) {
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
		int Given = 0, Skipped = 0;
		if(!pMMO->GiveAllItems(pP, &Given, &Skipped))
		{
			pG->SendChatTo(pCtx->m_ClientID, "发放失败（背包可能已满）。");
			return;
		}
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "[调试] 已发放 %d 种物品（跳过 %d 种已有/失败）。", Given, Skipped);
		pG->SendChatTo(pCtx->m_ClientID, aBuf);
	}, pGame);

	if(!pGame || !pGame->Console())
		return;

	pGame->Console()->Register("mmo_giveall", "i[client]", CFGFLAG_SERVER, [](IConsole::IResult *pR, void *pU) {
		CGameContext *pG = (CGameContext *)pU;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		const int CID = pR->GetInteger(0);
		if(CID < 0 || CID >= MAX_CLIENTS || !pG->m_apPlayers[CID])
		{
			pG->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mmo", "invalid client id");
			return;
		}
		CPlayer *pP = pG->m_apPlayers[CID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mmo", "player not logged in");
			return;
		}
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		int Given = 0, Skipped = 0;
		if(!pMMO->GiveAllItems(pP, &Given, &Skipped))
		{
			pG->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mmo", "giveall failed");
			return;
		}
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "mmo_giveall: client %d got %d items (%d skipped)", CID, Given, Skipped);
		pG->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mmo", aBuf);
	}, pGame, "Give all MMO items to a player (debug)");
}

void CMMOManager::RegisterPartyCommands(CCommandManager *pManager)
{
	// Group/trade commands are now registered globally via CGlobalState::
	// from gamecontroller.cpp. This method is kept as a no-op for ABI compat.
	(void)pManager;
}

void CMMOManager::ConGroupList(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	CMMOManager *pMMO = pGame->Core()->GetMMOManager();

	int GID = pMMO->GetPlayerGroupID(pCtx->m_ClientID);
	if(GID < 0) { pGame->SendChatTo(pCtx->m_ClientID, "你不在任何队伍中。"); return; }

	const SMMOGroup &G = pMMO->m_aGroups[GID];
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══ 队伍 (%zu/%d) ═══", G.m_vMemberAccountIDs.size(), MMO_GROUP_MAX_MEMBERS);
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	for(const auto &AID : G.m_vMemberAccountIDs)
	{
		const char *pName = "离线";
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *p = pGame->m_apPlayers[i];
			if(p && p->GetAccountId() == AID) { pName = pGame->Server()->ClientName(i); break; }
		}
		str_format(aBuf, sizeof(aBuf), "%s %s (ID:%lld)",
			AID == G.m_LeaderAccountID ? "👑" : "  ", pName, (long long)AID);
		pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	}
	(void)pResult;
}

void CMMOManager::ConGroupCreate(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->GroupCreate(pCtx->m_ClientID);
	(void)pResult;
}

void CMMOManager::ConGroupInvite(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;

	int CID = pCtx->m_ClientID;
	int Target = pGame->Core()->GetMMOManager()->FindClientByName(pResult->GetString(0));
	if(Target < 0) { pGame->SendChatTo(CID, "未找到该玩家。"); return; }
	pGame->Core()->GetMMOManager()->GroupInvite(CID, Target);
}

void CMMOManager::ConGroupAccept(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->GroupAccept(pCtx->m_ClientID);
	(void)pResult;
}

void CMMOManager::ConGroupLeave(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->GroupLeave(pCtx->m_ClientID);
	(void)pResult;
}

void CMMOManager::ConGroupKick(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;

	int CID = pCtx->m_ClientID;
	int Target = pGame->Core()->GetMMOManager()->FindClientByName(pResult->GetString(0));
	if(Target < 0) { pGame->SendChatTo(CID, "未找到该玩家。"); return; }
	pGame->Core()->GetMMOManager()->GroupKick(CID, Target);
}

void CMMOManager::ConGroupChat(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->GroupChat(pCtx->m_ClientID, pResult->GetString(0));
}

void CMMOManager::ConTradeStart(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;

	int CID = pCtx->m_ClientID;
	int Target = pGame->Core()->GetMMOManager()->FindClientByName(pResult->GetString(0));
	if(Target < 0) { pGame->SendChatTo(CID, "未找到该玩家。"); return; }
	pGame->Core()->GetMMOManager()->TradeRequest(CID, Target);
}

void CMMOManager::ConTradeAccept(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->TradeAccept(pCtx->m_ClientID);
	(void)pResult;
}

void CMMOManager::ConTradeDecline(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->TradeDecline(pCtx->m_ClientID);
	(void)pResult;
}

void CMMOManager::ConTradeAddItem(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->TradeAddItem(pCtx->m_ClientID, pResult->GetInteger(0), pResult->GetInteger(1));
}

void CMMOManager::ConTradeAddGold(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->TradeAddGold(pCtx->m_ClientID, pResult->GetInteger(0));
}

void CMMOManager::ConTradeConfirm(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->TradeConfirm(pCtx->m_ClientID);
	(void)pResult;
}

void CMMOManager::ConTradeCancel(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core()) return;
	pGame->Core()->GetMMOManager()->TradeCancel(pCtx->m_ClientID);
	(void)pResult;
}

void CMMOManager::ConStats(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;

	CPlayer *pPlayer = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pPlayer) return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══ 角色信息 ═══");
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "等级: %d  |  经验: %d / %d",
		pPlayer->GetStat(AttributeIdentifier::Level), pPlayer->GetStat(AttributeIdentifier::Experience), ExpForLevel(pPlayer->GetStat(AttributeIdentifier::Level)));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "技能点: %d  |  金币: %d", pPlayer->GetStat(AttributeIdentifier::SkillPoints), pPlayer->GetStat(AttributeIdentifier::Gold));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "━━━ 六维属性 ━━━");
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	const int STR = pPlayer->GetStat(AttributeIdentifier::STR);
	const int DEX = pPlayer->GetStat(AttributeIdentifier::DEX);
	const int CON = pPlayer->GetStat(AttributeIdentifier::CON);
	const int INT = pPlayer->GetStat(AttributeIdentifier::INT);
	const int WIS = pPlayer->GetStat(AttributeIdentifier::WIS);
	const int CHA = pPlayer->GetStat(AttributeIdentifier::CHA);
	str_format(aBuf, sizeof(aBuf), "STR %d 力量(近战伤) | DEX %d 敏捷(远程伤) | CON %d 体质(HP)", STR, DEX, CON);
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "INT %d 智力(MP+魔法) | WIS %d 智慧(治疗+buff) | CHA %d 魅力(声望+折扣)", INT, WIS, CHA);
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "━━━ 战斗效能 ━━━");
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "近战伤: +%d  |  远程伤: +%d  |  防御: %d",
		pPlayer->GetEffectiveMeleeAttack(), pPlayer->GetEffectiveRangedAttack(), pPlayer->GetEffectiveDefense());
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "━━━ MMO 战斗面板 ━━━");
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	pPlayer->RecalcMMOStats();
	str_format(aBuf, sizeof(aBuf), "攻击: %d  |  防御: %d  |  MaxHP: %d  |  Mana: %d",
		maximum(1, pPlayer->m_MMOAttack),
		maximum(1, pPlayer->m_MMODefense),
		pPlayer->GetBaseMaxHealth(),
		pPlayer->GetMaxMana());
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "升级所需: %d 经验 | 杀同级怪约 %d 只",
		ExpForLevel(pPlayer->GetStat(AttributeIdentifier::Level)),
		maximum(1, ExpForLevel(pPlayer->GetStat(AttributeIdentifier::Level)) / 10));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "位阶: %d", pPlayer->GetStat(AttributeIdentifier::Reputation));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	(void)pResult;
}

void CMMOManager::ConAddStat(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;

	CPlayer *pPlayer = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pPlayer) return;

	if(pPlayer->GetStat(AttributeIdentifier::SkillPoints) <= 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "没有可用技能点！");
		return;
	}

	const char *pArg = pResult->GetString(0);
	if(!pArg || pArg[0] == '\0')
	{
		pGame->SendChatTo(pCtx->m_ClientID, "用法: /addstat str|dex|con|int|wis|cha");
		return;
	}

	AttributeIdentifier Attr = AttributeIdentifier::Unknown;
	const char *pStatName = "";
	const char *pDesc = "";
	if(str_comp_nocase(pArg, "str") == 0) { Attr = AttributeIdentifier::STR; pStatName = "力量"; pDesc = "近战伤害+2/每点"; }
	else if(str_comp_nocase(pArg, "dex") == 0) { Attr = AttributeIdentifier::DEX; pStatName = "敏捷"; pDesc = "远程伤害+2/每点"; }
	else if(str_comp_nocase(pArg, "con") == 0) { Attr = AttributeIdentifier::CON; pStatName = "体质"; pDesc = "防御+2, HP+5/每点"; }
	else if(str_comp_nocase(pArg, "int") == 0) { Attr = AttributeIdentifier::INT; pStatName = "智力"; pDesc = "MP上限+5, 魔法强度"; }
	else if(str_comp_nocase(pArg, "wis") == 0) { Attr = AttributeIdentifier::WIS; pStatName = "智慧"; pDesc = "治疗量+Buff持续时间"; }
	else if(str_comp_nocase(pArg, "cha") == 0) { Attr = AttributeIdentifier::CHA; pStatName = "魅力"; pDesc = "声望获取+NPC折扣"; }
	else
	{
		pGame->SendChatTo(pCtx->m_ClientID, "未知属性。可用: str dex con int wis cha");
		return;
	}

	int NewVal = pPlayer->GetStat(Attr) + 2;
	pPlayer->SetStat(Attr, NewVal);
	pPlayer->SetStat(AttributeIdentifier::SkillPoints, pPlayer->GetStat(AttributeIdentifier::SkillPoints) - 1);
	pPlayer->m_MMODirty = true;
	char aStatBuf[128];
	str_format(aStatBuf, sizeof(aStatBuf), "%s +2！(%s) 当前: %d", pStatName, pDesc, NewVal);
	pGame->SendChatTo(pCtx->m_ClientID, aStatBuf);
	(void)pResult;
}

void CMMOManager::ConMMOSkills(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══ 技能树 ═══");
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	str_format(aBuf, sizeof(aBuf), "可用技能点: %d", pP->GetStat(AttributeIdentifier::SkillPoints));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);

	for(int i = 0; i < NUM_MMO_SKILLS; i++)
	{
		const SMMOSkillDef &Def = g_aMMOSkillDefs[i];
		int Level = pP->GetMMOSkillLevel(i);
		str_format(aBuf, sizeof(aBuf), "%s: Lv.%d/%d (需要等级 %d) - %s",
			Def.m_pName, Level, Def.m_MaxLevel, Def.m_ReqPlayerLevel, Def.m_pDesc);
		pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	}
	(void)pResult;
}

void CMMOManager::ConMMOLearn(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	const char *pSkillName = pResult->GetString(0);
	if(!pSkillName || pSkillName[0] == '\0')
	{
		pGame->SendChatTo(pCtx->m_ClientID, "用法: /learn <技能名>");
		return;
	}

	if(pP->GetStat(AttributeIdentifier::SkillPoints) <= 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "没有可用技能点！");
		return;
	}

	// Find skill by name
	const SMMOSkillDef *pDef = 0;
	for(int i = 0; i < NUM_MMO_SKILLS; i++)
	{
		if(str_comp_nocase(g_aMMOSkillDefs[i].m_pName, pSkillName) == 0)
		{
			pDef = &g_aMMOSkillDefs[i];
			break;
		}
	}
	if(!pDef)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "未找到该技能。使用 /skills 查看可学习的技能。");
		return;
	}

	// Check player level requirement
	if(pP->GetStat(AttributeIdentifier::Level) < pDef->m_ReqPlayerLevel)
	{
		pGame->SendChatLocF(pCtx->m_ClientID, "learn.reqlevel",
			"需要等级 %d 才能学习 %s！", pDef->m_ReqPlayerLevel, pDef->m_pName);
		return;
	}

	int CurLevel = pP->GetMMOSkillLevel(pDef->m_ID);
	if(CurLevel >= pDef->m_MaxLevel)
	{
		pGame->SendChatLocF(pCtx->m_ClientID, "learn.maxlevel",
			"%s 已达到最高等级！", pDef->m_pName);
		return;
	}

	// Find or create skill state
	CPlayer::SMMOSkillState *pState = 0;
	for(int i = 0; i < pP->m_NumMMOSkills; i++)
	{
		if(pP->m_aMMOSkills[i].m_SkillID == pDef->m_ID)
		{
			pState = &pP->m_aMMOSkills[i];
			break;
		}
	}
	if(!pState)
	{
		if(pP->m_NumMMOSkills >= 8)
		{
			pGame->SendChatTo(pCtx->m_ClientID, "已达到最大技能数量！");
			return;
		}
		int Idx = pP->m_NumMMOSkills++;
		pP->m_aMMOSkills[Idx].m_SkillID = pDef->m_ID;
		pP->m_aMMOSkills[Idx].m_Level = 0;
		pState = &pP->m_aMMOSkills[Idx];
	}

	// Spend skill point
	pState->m_Level++;
	pP->SetStat(AttributeIdentifier::SkillPoints, pP->GetStat(AttributeIdentifier::SkillPoints) - 1);
	pP->ApplyMMOSkillBonuses();
	pP->m_MMODirty = true;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "学习了 %s Lv.%d！(剩余 %d 技能点)",
		pDef->m_pName, pState->m_Level, pP->GetStat(AttributeIdentifier::SkillPoints));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	(void)pResult;
}

void CMMOManager::ConShop(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	const char *pNpcName = pResult->GetString(0);
	if(!pNpcName || pNpcName[0] == '\0')
	{
		pGame->SendChatTo(pCtx->m_ClientID, "用法: /shop <NPC名字> (quest_master|shopkeeper|healer)");
		pGame->SendChatTo(pCtx->m_ClientID, "可用商店: quest_master, shopkeeper, healer");
		return;
	}

	const SShopEntry *pShop = FindShopByNpcID(pNpcName);
	if(!pShop)
	{
		pGame->SendChatLocF(pCtx->m_ClientID, "shop.nonexist",
			"未找到商店「%s」。可用: quest_master, shopkeeper, healer", pNpcName);
		return;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══ %s ═══", pShop->m_pName);
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	for(int i = 0; i < pShop->m_NumItems; i++)
	{
		const SShopItem &It = pShop->m_Items[i];
		str_format(aBuf, sizeof(aBuf), "#%d %s - %d 金币 (卖出: %d)",
			It.m_ItemID, It.m_pName, It.m_Price, It.m_Price / 2);
		pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	}
	str_format(aBuf, sizeof(aBuf), "你的金币: %d", pP->GetStat(AttributeIdentifier::Gold));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	(void)pResult;
}

void CMMOManager::ConBuy(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	int ItemID = pResult->GetInteger(0);
	int Qty = pResult->GetInteger(1);
	if(Qty <= 0) Qty = 1;

	// Find item price from any shop
	int TotalPrice = -1;
	const char *pItemName = "Unknown";
	for(int s = 0; s < NUM_SHOPS; s++)
	{
		const SShopEntry &Shop = g_aShopData[s];
		for(int i = 0; i < Shop.m_NumItems; i++)
		{
			if(Shop.m_Items[i].m_ItemID == ItemID)
			{
				TotalPrice = Shop.m_Items[i].m_Price * Qty;
				pItemName = Shop.m_Items[i].m_pName;
				break;
			}
		}
		if(TotalPrice >= 0) break;
	}

	if(TotalPrice < 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "该物品不在此商店出售。");
		return;
	}

	if(pP->GetStat(AttributeIdentifier::Gold) < TotalPrice)
	{
		pGame->SendChatLocF(pCtx->m_ClientID, "buy.nogold",
			"金币不足！需要 %d，你只有 %d。", TotalPrice, pP->GetStat(AttributeIdentifier::Gold));
		return;
	}

	// Try to add to inventory
	if(!pP->m_MMOInventory.Add(ItemID, Qty))
	{
		pGame->SendChatTo(pCtx->m_ClientID, "背包已满！");
		return;
	}

	pP->SetStat(AttributeIdentifier::Gold, pP->GetStat(AttributeIdentifier::Gold) - TotalPrice);
	pP->m_MMODirty = true;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "购买了 x%d %s，花费 %d 金币！(剩余: %d)",
		Qty, pItemName, TotalPrice, pP->GetStat(AttributeIdentifier::Gold));
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	(void)pResult;
}

void CMMOManager::ConUse(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	int SlotIdx = pResult->GetInteger(0);
	if(SlotIdx < 0 || (size_t)SlotIdx >= pP->m_MMOInventory.size())
	{
		pGame->SendChatTo(pCtx->m_ClientID, "背包格无效！");
		return;
	}

	const CItem &Item = pP->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
	if(!pDesc)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "物品不存在！");
		return;
	}

	// Only consumable items can be used
	MMOItemType UseType = pDesc->GetType();
	if(UseType != ItemType::UseSingle && UseType != ItemType::UseMultiple)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "该物品无法使用。");
		return;
	}

	// Get the amount to heal from item's HP attribute
	int HealAmount = pDesc->GetAttributeValue(AttributeIdentifier::HP);
	if(HealAmount <= 0)
	{
		// Fallback: try to extract heal amount from description text
		const char *pDescText = pGame->Loc(pP->GetCID(), pDesc->GetDescriptionKey(), pDesc->GetDescription());
		if(str_length(pDescText) < 20)
			HealAmount = 0;
	}
	if(HealAmount <= 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "该物品没有可用的效果。");
		return;
	}

	// Apply the effect (HP recovery)
	CCharacter *pChr = GetCharacterSafe(pP);
	if(pChr)
	{
		// Get usage count (default 1)
		int UseCount = pResult->GetInteger(1);
		if(UseCount <= 0) UseCount = 1;

		// Check if player has enough items
		int AvailableCount = pP->m_MMOInventory[SlotIdx].GetValue();
		if(AvailableCount < UseCount)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "物品数量不足！当前有 %d 个，需要 %d 个。", AvailableCount, UseCount);
			pGame->SendChatTo(pCtx->m_ClientID, aBuf);
			return;
		}

		int TotalHeal = 0;
		for(int i = 0; i < UseCount; i++)
		{
			int HPBeforeEach = pChr->GetHealth();
			pChr->IncreaseHealth(HealAmount);
			TotalHeal += pChr->GetHealth() - HPBeforeEach;
		}
		int AfterHP = pChr->GetHealth();

		if(TotalHeal > 0)
		{
			// Consume items
			if(!pP->m_MMOInventory.RemoveAt(SlotIdx, UseCount))
			{
				pGame->SendChatTo(pCtx->m_ClientID, "使用失败！");
				return;
			}
			pP->m_MMODirty = true;

			// Save inventory immediately to prevent item loss on disconnect
			if(CMMOManager *pMMO = pGame->Core()->GetMMOManager())
				pMMO->SaveInventory(pP);

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "使用了 %d x %s，恢复了 %d HP！(HP: %d/%d)",
				UseCount,
				pGame->Loc(pP->GetCID(), pDesc->GetNameKey(), pDesc->GetName()),
				TotalHeal, AfterHP, pChr->GetMaxHealth());
			pGame->SendChatTo(pCtx->m_ClientID, aBuf);
		}
		else
		{
			pGame->SendChatTo(pCtx->m_ClientID, "你的生命值已满，无需使用。");
		}
	}
	else
	{
		pGame->SendChatTo(pCtx->m_ClientID, "你不在游戏中，无法使用物品。");
	}
	(void)pResult;
}

void CMMOManager::ConItemSlot(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	int EmoteSlot = pResult->GetInteger(0);
	int InvSlot = pResult->GetInteger(1);

	if(EmoteSlot < 0 || EmoteSlot >= 4)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "表情槽无效！可用: 0-3");
		return;
	}

	if(InvSlot < -1 || (size_t)InvSlot >= pP->m_MMOInventory.size())
	{
		pGame->SendChatTo(pCtx->m_ClientID, "背包格无效！用 -1 清空绑定。");
		return;
	}

	if(InvSlot >= 0)
	{
		const CItem &Item = pP->m_MMOInventory[InvSlot];
		const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
		if(!pDesc || (pDesc->GetType() != ItemType::UseSingle && pDesc->GetType() != ItemType::UseMultiple))
		{
			pGame->SendChatTo(pCtx->m_ClientID, "只能绑定可使用物品（药水/食物/卷轴）！");
			return;
		}
	}

	pP->m_aItemQuickSlots[EmoteSlot] = InvSlot;
	pP->m_MMODirty = true;

	if(InvSlot >= 0)
	{
		const CItem &Item = pP->m_MMOInventory[InvSlot];
		const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
		char aBuf[160];
		str_format(aBuf, sizeof(aBuf), "表情键 %d 已绑定到: %s (背包格 %d)",
			EmoteSlot, pDesc ? pDesc->GetName() : "?", InvSlot);
		pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	}
	else
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "表情键 %d 已清空。", EmoteSlot);
		pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	}
	(void)pResult;
}

// ─── Recycle / Sell Helpers ──────────────────────────────────────────────

static int GetCurrentDateYYYYMMDD()
{
	time_t now = time(nullptr);
	struct tm *pTM = localtime(&now);
	return (1900 + pTM->tm_year) * 10000 + (pTM->tm_mon + 1) * 100 + pTM->tm_mday;
}

void CMMOManager::ResetDailySellIfNeeded(CPlayer *pPlayer)
{
	if(!pPlayer)
		return;
	const int Today = GetCurrentDateYYYYMMDD();
	if(pPlayer->m_LastSellDate != Today)
	{
		pPlayer->m_LastSellDate = Today;
		pPlayer->m_DailySellGold = 0;
		pPlayer->m_DailySellCount = 0;
	}
}

int CMMOManager::CalcSellUnitPrice(const CMMOItemDescription *pDef) const
{
	if(!pDef)
		return 0;

	int ItemPrice = 0;
	for(int s = 0; s < NUM_SHOPS; s++)
	{
		const SShopEntry &Shop = g_aShopData[s];
		for(int i = 0; i < Shop.m_NumItems; i++)
		{
			if(Shop.m_Items[i].m_ItemID == pDef->GetID())
			{
				ItemPrice = maximum(MMO_SELL_MIN_UNIT_PRICE, Shop.m_Items[i].m_Price / MMO_SELL_PRICE_DIVISOR);
				break;
			}
		}
		if(ItemPrice > 0)
			break;
	}
	if(ItemPrice <= 0)
		ItemPrice = maximum(MMO_SELL_MIN_UNIT_PRICE, pDef->GetInitialPrice() / MMO_SELL_PRICE_DIVISOR);
	return ItemPrice;
}

bool CMMOManager::CanSellItem(CPlayer *pPlayer, int SlotIdx, const char **ppReason) const
{
	if(ppReason)
		*ppReason = nullptr;
	if(!pPlayer)
	{
		if(ppReason) *ppReason = "无效玩家。";
		return false;
	}
	if(SlotIdx < 0 || (size_t)SlotIdx >= pPlayer->m_MMOInventory.size())
	{
		if(ppReason) *ppReason = "背包格无效。";
		return false;
	}

	const CItem &Item = pPlayer->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	if(!pDef)
	{
		if(ppReason) *ppReason = "物品不存在。";
		return false;
	}
	if(pDef->GetGroup() == ItemGroup::Quest)
	{
		if(ppReason) *ppReason = "任务物品无法回收。";
		return false;
	}
	if(pDef->GetGroup() == ItemGroup::Currency)
	{
		if(ppReason) *ppReason = "货币无法回收。";
		return false;
	}
	if((pDef->GetFlags() & MMO_ITEMFLAG_CANT_TRADE) != 0)
	{
		if(ppReason) *ppReason = "该物品无法回收。";
		return false;
	}
	if(pPlayer->IsMMOWeaponEquipped(Item.GetID()) || pPlayer->m_EquippedSlots.isEquippedItem(Item.GetID()))
	{
		if(ppReason) *ppReason = "请先卸下装备再回收。";
		return false;
	}
	if(CalcSellUnitPrice(pDef) <= 0)
	{
		if(ppReason) *ppReason = "该物品没有回收价值。";
		return false;
	}

	const_cast<CMMOManager*>(this)->ResetDailySellIfNeeded(pPlayer);
	if(pPlayer->m_DailySellCount >= MMO_SELL_DAILY_COUNT_CAP)
	{
		if(ppReason) *ppReason = "今日回收次数已达上限。";
		return false;
	}
	if(pPlayer->m_DailySellGold >= MMO_SELL_DAILY_GOLD_CAP)
	{
		if(ppReason) *ppReason = "今日回收金币已达上限。";
		return false;
	}
	return true;
}

bool CMMOManager::TrySellItem(CPlayer *pPlayer, int SlotIdx, int Qty, int *pGoldOut, const char **ppReason)
{
	if(pGoldOut)
		*pGoldOut = 0;
	if(!CanSellItem(pPlayer, SlotIdx, ppReason))
		return false;

	const CItem &Item = pPlayer->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	const int UnitPrice = CalcSellUnitPrice(pDef);
	int SellCount = Qty <= 0 ? Item.GetValue() : minimum(Qty, Item.GetValue());
	if(SellCount <= 0)
	{
		if(ppReason) *ppReason = "数量无效。";
		return false;
	}

	int GrossGold = UnitPrice * SellCount;
	int NetGold = GrossGold - (GrossGold * MMO_SELL_TAX_PERCENT / 100);
	if(NetGold <= 0)
		NetGold = MMO_SELL_MIN_UNIT_PRICE;

	ResetDailySellIfNeeded(pPlayer);
	if(pPlayer->m_DailySellCount >= MMO_SELL_DAILY_COUNT_CAP)
	{
		if(ppReason) *ppReason = "今日回收次数已达上限。";
		return false;
	}
	const int GoldRemaining = MMO_SELL_DAILY_GOLD_CAP - pPlayer->m_DailySellGold;
	if(GoldRemaining <= 0)
	{
		if(ppReason) *ppReason = "今日回收金币已达上限。";
		return false;
	}
	if(NetGold > GoldRemaining)
	{
		const int MaxUnits = GoldRemaining / maximum(1, UnitPrice - (UnitPrice * MMO_SELL_TAX_PERCENT / 100));
		if(MaxUnits <= 0)
		{
			if(ppReason) *ppReason = "今日回收金币已达上限。";
			return false;
		}
		SellCount = minimum(SellCount, MaxUnits);
		GrossGold = UnitPrice * SellCount;
		NetGold = GrossGold - (GrossGold * MMO_SELL_TAX_PERCENT / 100);
		if(NetGold <= 0)
			NetGold = MMO_SELL_MIN_UNIT_PRICE;
	}

	if(!pPlayer->m_MMOInventory.RemoveAt(SlotIdx, SellCount))
	{
		if(ppReason) *ppReason = "回收失败。";
		return false;
	}

	pPlayer->SetStat(AttributeIdentifier::Gold, pPlayer->GetStat(AttributeIdentifier::Gold) + NetGold);
	pPlayer->m_DailySellGold += NetGold;
	pPlayer->m_DailySellCount += 1;
	pPlayer->m_MMODirty = true;
	SaveInventory(pPlayer);
	SaveSellData(pPlayer);

	if(pGoldOut)
		*pGoldOut = NetGold;
	return true;
}

bool CMMOManager::DropItemAtSlot(CPlayer *pPlayer, int SlotIdx, int Qty, const char **ppReason)
{
	if(ppReason)
		*ppReason = nullptr;
	if(!pPlayer)
	{
		if(ppReason) *ppReason = "无效玩家。";
		return false;
	}
	if(SlotIdx < 0 || (size_t)SlotIdx >= pPlayer->m_MMOInventory.size())
	{
		if(ppReason) *ppReason = "背包格无效。";
		return false;
	}

	const CItem &Item = pPlayer->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	if(!pDef || !pDef->CanDrop())
	{
		if(ppReason) *ppReason = "该物品无法丢弃。";
		return false;
	}
	if(pDef->GetType() == ItemType::EquipTitle)
	{
		if(ppReason) *ppReason = "称号无法丢弃。";
		return false;
	}
	if(pPlayer->IsMMOWeaponEquipped(Item.GetID()) || pPlayer->m_EquippedSlots.isEquippedItem(Item.GetID()))
	{
		if(ppReason) *ppReason = "请先卸下装备再丢弃。";
		return false;
	}

	int DropCount = Qty <= 0 ? Item.GetValue() : minimum(Qty, Item.GetValue());
	if(DropCount <= 0)
	{
		if(ppReason) *ppReason = "数量无效。";
		return false;
	}
	if(!pPlayer->m_MMOInventory.RemoveAt(SlotIdx, DropCount))
	{
		if(ppReason) *ppReason = "丢弃失败。";
		return false;
	}

	pPlayer->m_MMODirty = true;
	SaveInventory(pPlayer);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "丢弃了 x%d %s",
		DropCount, GS()->Loc(pPlayer->GetCID(), pDef->GetNameKey(), pDef->GetName()));
	GS()->SendChatTo(pPlayer->GetCID(), aBuf);
	return true;
}

bool CMMOManager::SplitItemAtSlot(CPlayer *pPlayer, int SlotIdx, int SplitCount, const char **ppReason)
{
	if(ppReason)
		*ppReason = nullptr;
	if(!pPlayer)
	{
		if(ppReason) *ppReason = "无效玩家。";
		return false;
	}
	if(SlotIdx < 0 || (size_t)SlotIdx >= pPlayer->m_MMOInventory.size())
	{
		if(ppReason) *ppReason = "背包格无效。";
		return false;
	}

	CItem &Item = pPlayer->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDef = CMMOItemDescription::Get(Item.GetID());
	if(!pDef || !pDef->IsStackable())
	{
		if(ppReason) *ppReason = "该物品无法拆分。";
		return false;
	}
	if(Item.GetValue() <= 1)
	{
		if(ppReason) *ppReason = "数量不足，无法拆分。";
		return false;
	}
	if(SplitCount <= 0 || SplitCount >= Item.GetValue())
	{
		if(ppReason) *ppReason = "拆分数量无效。";
		return false;
	}

	CMMOItem NewStack(Item.GetID(), SplitCount, Item.GetEnchant(), Item.GetDurability(), Item.GetExpiresAt());
	NewStack.SetSettings(Item.GetSettings());
	Item.SetValue(Item.GetValue() - SplitCount);
	if(!pPlayer->m_MMOInventory.Add(NewStack))
	{
		Item.SetValue(Item.GetValue() + SplitCount);
		if(ppReason) *ppReason = "背包已满，无法拆分。";
		return false;
	}

	pPlayer->m_MMODirty = true;
	SaveInventory(pPlayer);
	return true;
}

void CMMOManager::ConSell(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GetMMOManager()) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	const int SlotIdx = pResult->GetInteger(0);
	int Qty = pResult->GetInteger(1);
	if(Qty <= 0) Qty = 1;

	if(SlotIdx < 0 || (size_t)SlotIdx >= pP->m_MMOInventory.size())
	{
		pGame->SendChatTo(pCtx->m_ClientID, "背包格无效！");
		return;
	}

	const CItem &Item = pP->m_MMOInventory[SlotIdx];
	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
	char aName[64] = "?";
	if(pDesc)
		str_copy(aName, pGame->Loc(pP->GetCID(), pDesc->GetNameKey(), pDesc->GetName()), sizeof(aName));
	const int SellQty = minimum(Qty, Item.GetValue());

	CMMOManager *pMMO = pGame->Core()->GetMMOManager();
	const char *pReason = nullptr;
	int Gold = 0;
	if(!pMMO->TrySellItem(pP, SlotIdx, SellQty, &Gold, &pReason))
	{
		pGame->SendChatTo(pCtx->m_ClientID, pReason ? pReason : "回收失败。");
		return;
	}

	char aBuf[160];
	str_format(aBuf, sizeof(aBuf), "回收了 x%d %s，获得 %d 金币（已扣税，今日 %d/%d）",
		SellQty, aName, Gold, pP->m_DailySellGold, MMO_SELL_DAILY_GOLD_CAP);
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	(void)pResult;
}

void CMMOManager::ConMMOSell(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GetMMOManager()) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	const int SlotIdx = pResult->GetInteger(0);
	CMMOManager *pMMO = pGame->Core()->GetMMOManager();
	const char *pReason = nullptr;
	int Gold = 0;
	if(!pMMO->TrySellItem(pP, SlotIdx, 0, &Gold, &pReason))
	{
		pGame->SendChatTo(pCtx->m_ClientID, pReason ? pReason : "回收失败。请从经济菜单→物品回收操作。");
		return;
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "回收成功，获得 %d 金币（今日 %d/%d）。请优先使用经济菜单→物品回收。",
		Gold, pP->m_DailySellGold, MMO_SELL_DAILY_GOLD_CAP);
	pGame->SendChatTo(pCtx->m_ClientID, aBuf);
	(void)pResult;
}

void CMMOManager::ConAddStatVote(IConsole::IResult *pResult, void *pUser)
{
	auto *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame) return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP) return;

	const char *pAttr = pResult->GetString(0);
	if(!pAttr || pAttr[0] == '\0')
	{
		pGame->SendChatTo(pCtx->m_ClientID, "用法: /addstatvote str|con");
		return;
	}

	// TRPG六维加点
	AttributeIdentifier Attr = AttributeIdentifier::Unknown;
	const char *pStatName = "";
	if(str_comp_nocase(pAttr, "str") == 0) { Attr = AttributeIdentifier::STR; pStatName = "力量"; }
	else if(str_comp_nocase(pAttr, "dex") == 0) { Attr = AttributeIdentifier::DEX; pStatName = "敏捷"; }
	else if(str_comp_nocase(pAttr, "con") == 0) { Attr = AttributeIdentifier::CON; pStatName = "体质"; }
	else if(str_comp_nocase(pAttr, "int") == 0) { Attr = AttributeIdentifier::INT; pStatName = "智力"; }
	else if(str_comp_nocase(pAttr, "wis") == 0) { Attr = AttributeIdentifier::WIS; pStatName = "智慧"; }
	else if(str_comp_nocase(pAttr, "cha") == 0) { Attr = AttributeIdentifier::CHA; pStatName = "魅力"; }
	else
	{
		pGame->SendChatTo(pCtx->m_ClientID, "未知属性。可用: str dex con int wis cha");
		(void)pResult;
		return;
	}
	if(pP->GetStat(AttributeIdentifier::SkillPoints) <= 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "没有可用技能点！");
		return;
	}
	int NewVal = pP->GetStat(Attr) + 2;
	pP->SetStat(Attr, NewVal);
	pP->SetStat(AttributeIdentifier::SkillPoints, pP->GetStat(AttributeIdentifier::SkillPoints) - 1);
	pP->m_MMODirty = true;
	pP->RecalcMMOStats();
	char aStatBuf[128];
	str_format(aStatBuf, sizeof(aStatBuf), "%s +2！当前: %d", pStatName, NewVal);
	pGame->SendChatTo(pCtx->m_ClientID, aStatBuf);
	if(pGame->Core() && pGame->Core()->VoteMenuManager())
	{
		SPlayerVote *pV = pGame->Core()->VoteMenuManager()->GetPlayerVote(pCtx->m_ClientID);
		if(pV && pV->m_Page == PAGE_ATTRIBUTES)
			pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
	}
	(void)pResult;
}

// ─── Daily Checkin ───────────────────────────────────────────────────────

static int GetDateNDaysAgo(int Date, int DaysAgo)
{
	// Parse YYYYMMDD, subtract days, return YYYYMMDD
	int Year = Date / 10000;
	int Month = (Date / 100) % 100;
	int Day = Date % 100;

	struct tm TM = {};
	TM.tm_year = Year - 1900;
	TM.tm_mon = Month - 1;
	TM.tm_mday = Day;
	TM.tm_hour = 12;
	TM.tm_isdst = -1;
	time_t t = mktime(&TM);
	t -= (time_t)DaysAgo * 86400;
	struct tm *pNew = localtime(&t);
	return (1900 + pNew->tm_year) * 10000 + (pNew->tm_mon + 1) * 100 + pNew->tm_mday;
}

void CMMOManager::ConCheckin(const char *pPlayerName, CPlayer *pPlayer)
{
	if(!pPlayer || pPlayer->GetAccountId() <= 0) return;

	int Today = GetCurrentDateYYYYMMDD();
	int Yesterday = GetDateNDaysAgo(Today, 1);

	// Already checked in today?
	if(pPlayer->m_LastCheckinDate == Today)
	{
		GS()->SendChatTo(pPlayer->GetCID(), "今天已经签到过了，明天再来吧！");
		return;
	}

	// Calculate new streak
	int NewStreak;
	if(pPlayer->m_LastCheckinDate == Yesterday)
		NewStreak = pPlayer->m_CheckinStreak + 1;
	else
		NewStreak = 1;

	// Calculate rewards
	int GoldReward = NewStreak * 10;
	pPlayer->m_LastCheckinDate = Today;
	pPlayer->m_CheckinStreak = NewStreak;

	// Apply gold reward
	AddGold(pPlayer, GoldReward);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "✦ 签到成功！连续签到 %d 天，获得 %d 金币！", NewStreak, GoldReward);
	GS()->SendChatTo(pPlayer->GetCID(), aBuf);

	// Bonus reward every 7 days
	if(NewStreak % 7 == 0)
	{
		const int BonusItemID = 1002; // 稀有奖励物品ID
		const int BonusCount = 1;
		if(GiveItem(pPlayer, BonusItemID, BonusCount))
		{
			str_format(aBuf, sizeof(aBuf), "✦ 签到 %d 天奖励！获得稀有物品！", NewStreak);
			GS()->SendChatTo(pPlayer->GetCID(), aBuf);
		}
	}

	pPlayer->m_MMODirty = true;
}

void CMMOManager::RegisterCheckinCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("checkin", "每日签到领取奖励", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		pG->Core()->GetMMOManager()->ConCheckin(pG->Server()->ClientName(pCtx->m_ClientID), pP);
	}, pGame);

	pManager->AddCommand("daily", "每日签到（同 /checkin）", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		pG->Core()->GetMMOManager()->ConCheckin(pG->Server()->ClientName(pCtx->m_ClientID), pP);
	}, pGame);
}

// ─── Enchant System ───────────────────────────────────────────────────────

static bool IsWeaponItem(const CItem *pItem)
{
	if(!pItem) return false;
	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(pItem->GetID());
	if(!pDesc) return false;
	return MMOItemTypeToWeapon(pDesc->GetType()) != -1;
}

void CMMOManager::ConEnchant(int ClientID, int BagSlot)
{
	CGameContext *pGame = GS();
	CPlayer *pPlayer = pGame->m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{
		pGame->SendChatTo(ClientID, "请先登录。");
		return;
	}

	// 检查背包格
	if(BagSlot < 0 || (size_t)BagSlot >= pPlayer->m_MMOInventory.size())
	{
		pGame->SendChatTo(ClientID, "背包格无效！");
		return;
	}

	// 获取物品
	CItem &Item = pPlayer->m_MMOInventory[BagSlot];
	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
	if(!pDesc)
	{
		pGame->SendChatTo(ClientID, "物品不存在！");
		return;
	}

	// 检查是否为武器
	if(!IsWeaponItem(&Item))
	{
		pGame->SendChatTo(ClientID, "只能强化武器类物品！");
		return;
	}

	// 检查当前强化等级
	int CurrentEnchant = Item.GetEnchant();
	if(CurrentEnchant >= MMO_ENCHANT_MAX_LEVEL)
	{
		pGame->SendChatTo(ClientID, "该装备已达最大强化等级！");
		return;
	}

	// 检查金币
	int Cost = GetEnchantCost(CurrentEnchant);
	if(!SpendGold(pPlayer, Cost))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "金币不足！强化需要 %d 金币。", Cost);
		pGame->SendChatTo(ClientID, aBuf);
		return;
	}

	// 计算成功率并掷骰
	int SuccessRate = GetEnchantSuccessRate(CurrentEnchant);
	int Roll = rand() % 100;
	bool Success = Roll < SuccessRate;

	char aBuf[256];
	if(Success)
	{
		int NewEnchant = CurrentEnchant + 1;
		Item.SetEnchant(NewEnchant);
		pPlayer->m_MMODirty = true;

		str_format(aBuf, sizeof(aBuf), "✦ 强化成功！%s 已强化至 +%d！(成功率 %d%%)",
			pDesc->GetName(), NewEnchant, SuccessRate);
		pGame->SendChatTo(ClientID, aBuf);
	}
	else
	{
		if(CurrentEnchant >= 0 && CurrentEnchant <= 5)
		{
			// +0~+5: 无事发生
			str_format(aBuf, sizeof(aBuf), "✧ 强化失败，但装备安然无恙。(成功率 %d%%)", SuccessRate);
			pGame->SendChatTo(ClientID, aBuf);
		}
		else if(CurrentEnchant >= 6 && CurrentEnchant <= 10)
		{
			// +6~+10: 强化等级 -1
			int NewEnchant = CurrentEnchant - 1;
			Item.SetEnchant(NewEnchant);
			pPlayer->m_MMODirty = true;

			str_format(aBuf, sizeof(aBuf), "✧ 强化失败！%s 降级至 +%d。(成功率 %d%%)",
				pDesc->GetName(), NewEnchant, SuccessRate);
			pGame->SendChatTo(ClientID, aBuf);
		}
		else
		{
			// +11~+15: 强化等级归零，装备不消失
			Item.SetEnchant(0);
			pPlayer->m_MMODirty = true;

			str_format(aBuf, sizeof(aBuf), "✧ 强化失败！%s 强化等级归零。(成功率 %d%%)",
				pDesc->GetName(), SuccessRate);
			pGame->SendChatTo(ClientID, aBuf);
		}
	}
}

int CMMOManager::GetEnchantCost(int CurrentEnchant)
{
	return (CurrentEnchant + 1) * 100;
}

int CMMOManager::GetEnchantSuccessRate(int CurrentEnchant)
{
	return maximum(10, 100 - CurrentEnchant * 5);
}

void CMMOManager::RegisterEnchantCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("enchant", "强化武器: /enchant <背包格>", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		pG->Core()->GetMMOManager()->ConEnchant(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);
}

// ─── Ranking System ───────────────────────────────────────────────────────

static void ShowLevelRank(CGameContext *pGS, int ClientID)
{
	CSqlConnectionPool *pPool = pGS->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		pGS->SendChatTo(ClientID, "数据库连接失败。");
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `Level`, `Experience` FROM `tw_mmo_players` ORDER BY `Level` DESC, `Experience` DESC LIMIT 10");
	if(!SqlExecQuery(pSql, pGS->Config(), aQuery))
	{
		pPool->Release(pRaw);
		pGS->SendChatTo(ClientID, "查询排行榜失败。");
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		pGS->SendChatTo(ClientID, "排行榜暂无数据。");
		return;
	}

	pGS->SendChatTo(ClientID, "══════ 等级排行榜 ══════");
	int Rank = 0;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		Rank++;
		int Level = Row[0] ? atoi(Row[0]) : 0;
		int Exp = Row[1] ? atoi(Row[1]) : 0;
		char aLine[128];
		str_format(aLine, sizeof(aLine), "  %d. 等级 %d | 经验 %d", Rank, Level, Exp);
		pGS->SendChatTo(ClientID, aLine);
	}

	if(Rank == 0)
		pGS->SendChatTo(ClientID, "  暂无数据");

	pGS->SendChatTo(ClientID, "══════════════════════");

	mysql_free_result(pRes);
	pPool->Release(pRaw);
}

static void ShowGoldRank(CGameContext *pGS, int ClientID)
{
	CSqlConnectionPool *pPool = pGS->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		pGS->SendChatTo(ClientID, "数据库连接失败。");
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `Gold` FROM `tw_mmo_players` ORDER BY `Gold` DESC LIMIT 10");
	if(!SqlExecQuery(pSql, pGS->Config(), aQuery))
	{
		pPool->Release(pRaw);
		pGS->SendChatTo(ClientID, "查询排行榜失败。");
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		pGS->SendChatTo(ClientID, "排行榜暂无数据。");
		return;
	}

	pGS->SendChatTo(ClientID, "══════ 财富排行榜 ══════");
	int Rank = 0;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		Rank++;
		int Gold = Row[0] ? atoi(Row[0]) : 0;
		char aLine[128];
		str_format(aLine, sizeof(aLine), "  %d. 金币 %d", Rank, Gold);
		pGS->SendChatTo(ClientID, aLine);
	}

	if(Rank == 0)
		pGS->SendChatTo(ClientID, "  暂无数据");

	pGS->SendChatTo(ClientID, "══════════════════════");

	mysql_free_result(pRes);
	pPool->Release(pRaw);
}

void CMMOManager::ConRanking(int ClientID, const char *pType)
{
	CGameContext *pGS = GS();
	if(!pGS) return;

	if(!pType || pType[0] == '\0' || str_comp_nocase(pType, "level") == 0)
	{
		ShowLevelRank(pGS, ClientID);
	}
	else if(str_comp_nocase(pType, "gold") == 0)
	{
		ShowGoldRank(pGS, ClientID);
	}
	else
	{
		pGS->SendChatTo(ClientID, "用法: /ranking [level|gold]");
	}
}

void CMMOManager::RegisterRankingCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("rank", "排行榜: /rank [level|gold]", "s?", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		pG->Core()->GetMMOManager()->ConRanking(pCtx->m_ClientID, pR->GetString(0));
	}, pGame);

	pManager->AddCommand("ranking", "排行榜（同 /rank）", "s?", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		pG->Core()->GetMMOManager()->ConRanking(pCtx->m_ClientID, pR->GetString(0));
	}, pGame);
}

// ─── Auction System ───────────────────────────────────────────────────────

void CMMOManager::ConAuctionList(int ClientID)
{
	CGameContext *pGame = GS();
	CPlayer *pPlayer = pGame->m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{
		pGame->SendChatTo(ClientID, "请先登录。");
		return;
	}

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		pGame->SendChatTo(ClientID, "数据库连接失败。");
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `ListingID`, `ItemID`, `ItemCount`, `ItemEnchant`, `Price` FROM `tw_auction_listings` ORDER BY `Price` ASC");
	if(!SqlExecQuery(pSql, pGame->Config(), aQuery))
	{
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "查询拍卖行失败。");
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
	{
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "拍卖行暂无物品。");
		return;
	}

	pGame->SendChatTo(ClientID, "══════ 拍卖行 ══════");
	int Count = 0;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		Count++;
		int ListingID = Row[0] ? atoi(Row[0]) : 0;
		int ItemID = Row[1] ? atoi(Row[1]) : 0;
		int ItemCount = Row[2] ? atoi(Row[2]) : 0;
		int ItemEnchant = Row[3] ? atoi(Row[3]) : 0;
		int Price = Row[4] ? atoi(Row[4]) : 0;

		// Try to get item name from description
		const CMMOItemDescription *pDesc = CMMOItemDescription::Get(ItemID);
		const char *pItemName;
		char aItemNameBuf[64];
		if(pDesc)
		{
			pItemName = pDesc->GetName();
		}
		else
		{
			str_format(aItemNameBuf, sizeof(aItemNameBuf), "物品 %d", ItemID);
			pItemName = aItemNameBuf;
		}

		char aLine[192];
		str_format(aLine, sizeof(aLine), "#%d | %s x%d | +%d | %dg",
			ListingID, pItemName, ItemCount, ItemEnchant, Price);
		pGame->SendChatTo(ClientID, aLine);
	}

	if(Count == 0)
		pGame->SendChatTo(ClientID, "  拍卖行暂无物品");
	else
		pGame->SendChatTo(ClientID, "使用 /buy <ID> 购买");
	pGame->SendChatTo(ClientID, "════════════════════");

	mysql_free_result(pRes);
	pPool->Release(pRaw);
}

void CMMOManager::ConAuctionSell(int ClientID, int BagSlot, int Price)
{
	CGameContext *pGame = GS();
	CPlayer *pPlayer = pGame->m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{
		pGame->SendChatTo(ClientID, "请先登录。");
		return;
	}

	if(Price <= 0)
	{
		pGame->SendChatTo(ClientID, "价格必须大于 0。");
		return;
	}

	// Check bag slot
	if(BagSlot < 0 || (size_t)BagSlot >= pPlayer->m_MMOInventory.size())
	{
		pGame->SendChatTo(ClientID, "背包格无效！");
		return;
	}

	const CItem &Item = pPlayer->m_MMOInventory[BagSlot];
	if(!Item.IsValid())
	{
		pGame->SendChatTo(ClientID, "物品无效！");
		return;
	}

	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
	if(!pDesc)
	{
		pGame->SendChatTo(ClientID, "物品数据不存在！");
		return;
	}

	int ItemID = Item.GetID();
	int ItemCount = Item.GetValue();
	int ItemEnchant = Item.GetEnchant();

	// Remove item from player's inventory
	pPlayer->m_MMOInventory.RemoveAt(BagSlot, ItemCount);
	pPlayer->m_MMODirty = true;

	// Get current date YYYYMMDD
	time_t Now = time(nullptr);
	struct tm *pTM = localtime(&Now);
	int DateYYYYMMDD = (1900 + pTM->tm_year) * 10000 + (pTM->tm_mon + 1) * 100 + pTM->tm_mday;

	// Insert into DB
	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		pGame->SendChatTo(ClientID, "数据库连接失败。");
		// Give item back
		GiveItem(pPlayer, ItemID, ItemCount, ItemEnchant);
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw)
	{
		GiveItem(pPlayer, ItemID, ItemCount, ItemEnchant);
		return;
	}
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[384];
	str_format(aQuery, sizeof(aQuery),
		"INSERT INTO `tw_auction_listings` (`SellerUserID`, `ItemID`, `ItemCount`, `ItemEnchant`, `Price`, `ListedAt`) "
		"VALUES (%lld, %d, %d, %d, %d, %d)",
		(long long)pPlayer->GetAccountId(), ItemID, ItemCount, ItemEnchant, Price, DateYYYYMMDD);

	if(!SqlExecQuery(pSql, pGame->Config(), aQuery))
	{
		pPool->Release(pRaw);
		// Give item back on DB failure
		GiveItem(pPlayer, ItemID, ItemCount, ItemEnchant);
		pGame->SendChatTo(ClientID, "上架失败，物品已归还。");
		return;
	}

	pPool->Release(pRaw);

	char aBuf[192];
	str_format(aBuf, sizeof(aBuf), "✦ 上架成功！%s x%d | +%d | 价格 %d 金币",
		pDesc->GetName(), ItemCount, ItemEnchant, Price);
	pGame->SendChatTo(ClientID, aBuf);
}

void CMMOManager::ConAuctionBuy(int ClientID, int ListingID)
{
	CGameContext *pGame = GS();
	CPlayer *pPlayer = pGame->m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{
		pGame->SendChatTo(ClientID, "请先登录。");
		return;
	}

	if(ListingID <= 0)
	{
		pGame->SendChatTo(ClientID, "ListingID 无效。");
		return;
	}

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		pGame->SendChatTo(ClientID, "数据库连接失败。");
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	// Query listing
	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `SellerUserID`, `ItemID`, `ItemCount`, `ItemEnchant`, `Price` "
		"FROM `tw_auction_listings` WHERE `ListingID`=%d", ListingID);
	if(!SqlExecQuery(pSql, pGame->Config(), aQuery))
	{
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "查询失败。");
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes || mysql_num_rows(pRes) == 0)
	{
		if(pRes) mysql_free_result(pRes);
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "该拍卖物品不存在或已售出。");
		return;
	}

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	int64 SellerUserID = Row[0] ? (int64)atoll(Row[0]) : 0;
	int ItemID = Row[1] ? atoi(Row[1]) : 0;
	int ItemCount = Row[2] ? atoi(Row[2]) : 0;
	int ItemEnchant = Row[3] ? atoi(Row[3]) : 0;
	int Price = Row[4] ? atoi(Row[4]) : 0;
	mysql_free_result(pRes);

	// Check buyer has enough gold
	if(pPlayer->GetStat(AttributeIdentifier::Gold) < Price)
	{
		pPool->Release(pRaw);
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "金币不足！需要 %d，你只有 %d。", Price, pPlayer->GetStat(AttributeIdentifier::Gold));
		pGame->SendChatTo(ClientID, aBuf);
		return;
	}

	// Can't buy your own listing
	if(SellerUserID == pPlayer->GetAccountId())
	{
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "不能购买自己的拍卖物品。");
		return;
	}

	// Spend gold from buyer
	pPlayer->SetStat(AttributeIdentifier::Gold, pPlayer->GetStat(AttributeIdentifier::Gold) - Price);

	// Give item to buyer
	if(!GiveItem(pPlayer, ItemID, ItemCount, ItemEnchant))
	{
		// Refund on failure
		pPlayer->SetStat(AttributeIdentifier::Gold, pPlayer->GetStat(AttributeIdentifier::Gold) + Price);
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "背包已满，购买失败。");
		return;
	}
	pPlayer->m_MMODirty = true;

	// Credit seller gold (UPDATE directly on DB)
	str_format(aQuery, sizeof(aQuery),
		"UPDATE `tw_mmo_players` SET `Gold` = `Gold` + %d WHERE `UserID`=%lld",
		Price, (long long)SellerUserID);
	SqlExecQuery(pSql, pGame->Config(), aQuery);

	// Delete listing
	str_format(aQuery, sizeof(aQuery),
		"DELETE FROM `tw_auction_listings` WHERE `ListingID`=%d", ListingID);
	SqlExecQuery(pSql, pGame->Config(), aQuery);

	pPool->Release(pRaw);

	// Get item name for message
	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(ItemID);
	const char *pItemName = pDesc ? pDesc->GetName() : "?";

	char aBuf[192];
	str_format(aBuf, sizeof(aBuf), "✦ 购买成功！获得 %s x%d | +%d | 花费 %d 金币，剩余 %d 金币",
		pItemName, ItemCount, ItemEnchant, Price, pPlayer->GetStat(AttributeIdentifier::Gold));
	pGame->SendChatTo(ClientID, aBuf);
}

void CMMOManager::ConAuctionCancel(int ClientID, int ListingID)
{
	CGameContext *pGame = GS();
	CPlayer *pPlayer = pGame->m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->GetAccountId() <= 0)
	{
		pGame->SendChatTo(ClientID, "请先登录。");
		return;
	}

	if(ListingID <= 0)
	{
		pGame->SendChatTo(ClientID, "ListingID 无效。");
		return;
	}

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized())
	{
		pGame->SendChatTo(ClientID, "数据库连接失败。");
		return;
	}
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	// Query listing to verify ownership
	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT `SellerUserID`, `ItemID`, `ItemCount`, `ItemEnchant` FROM `tw_auction_listings` WHERE `ListingID`=%d", ListingID);
	if(!SqlExecQuery(pSql, pGame->Config(), aQuery))
	{
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "查询失败。");
		return;
	}

	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes || mysql_num_rows(pRes) == 0)
	{
		if(pRes) mysql_free_result(pRes);
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "该拍卖物品不存在。");
		return;
	}

	MYSQL_ROW Row = mysql_fetch_row(pRes);
	int64 SellerUserID = Row[0] ? (int64)atoll(Row[0]) : 0;
	int ItemID = Row[1] ? atoi(Row[1]) : 0;
	int ItemCount = Row[2] ? atoi(Row[2]) : 0;
	int ItemEnchant = Row[3] ? atoi(Row[3]) : 0;
	mysql_free_result(pRes);

	// Verify ownership
	if(SellerUserID != pPlayer->GetAccountId())
	{
		pPool->Release(pRaw);
		pGame->SendChatTo(ClientID, "这不是你的拍卖物品，无法下架。");
		return;
	}

	// Delete listing
	str_format(aQuery, sizeof(aQuery),
		"DELETE FROM `tw_auction_listings` WHERE `ListingID`=%d", ListingID);
	SqlExecQuery(pSql, pGame->Config(), aQuery);

	pPool->Release(pRaw);

	// Return item to player
	GiveItem(pPlayer, ItemID, ItemCount, ItemEnchant);

	const CMMOItemDescription *pDesc = CMMOItemDescription::Get(ItemID);
	const char *pItemName = pDesc ? pDesc->GetName() : "?";

	char aBuf[192];
	str_format(aBuf, sizeof(aBuf), "✦ 下架成功！%s x%d | +%d 已归还到背包。", pItemName, ItemCount, ItemEnchant);
	pGame->SendChatTo(ClientID, aBuf);
}

void CMMOManager::RegisterAuctionCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	// /auction list|sell|buy|cancel
	pManager->AddCommand("auction", "拍卖行: /auction list|sell <格子> <价格>|buy <ID>|cancel <ID>", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		const char *pSub = pR->GetString(0);
		if(!pSub || pSub[0] == '\0')
		{
			pG->SendChatTo(pCtx->m_ClientID, "用法: /auction list|sell <格子> <价格>|buy <ID>|cancel <ID>");
			return;
		}
		if(str_comp_nocase(pSub, "list") == 0)
			pG->Core()->GetMMOManager()->ConAuctionList(pCtx->m_ClientID);
		else if(str_comp_nocase(pSub, "sell") == 0)
			pG->Core()->GetMMOManager()->ConAuctionSell(pCtx->m_ClientID, pR->GetInteger(1), pR->GetInteger(2));
		else if(str_comp_nocase(pSub, "buy") == 0)
			pG->Core()->GetMMOManager()->ConAuctionBuy(pCtx->m_ClientID, pR->GetInteger(1));
		else if(str_comp_nocase(pSub, "cancel") == 0)
			pG->Core()->GetMMOManager()->ConAuctionCancel(pCtx->m_ClientID, pR->GetInteger(1));
		else
			pG->SendChatTo(pCtx->m_ClientID, "未知子命令，可用: list, sell, buy, cancel");
	}, pGame);

	// /ah - shortcut for /auction
	pManager->AddCommand("ah", "拍卖行快捷（同 /auction）", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		const char *pSub = pR->GetString(0);
		if(!pSub || pSub[0] == '\0')
		{
			pG->SendChatTo(pCtx->m_ClientID, "用法: /ah list|sell <格子> <价格>|buy <ID>|cancel <ID>");
			return;
		}
		if(str_comp_nocase(pSub, "list") == 0)
			pG->Core()->GetMMOManager()->ConAuctionList(pCtx->m_ClientID);
		else if(str_comp_nocase(pSub, "sell") == 0)
			pG->Core()->GetMMOManager()->ConAuctionSell(pCtx->m_ClientID, pR->GetInteger(1), pR->GetInteger(2));
		else if(str_comp_nocase(pSub, "buy") == 0)
			pG->Core()->GetMMOManager()->ConAuctionBuy(pCtx->m_ClientID, pR->GetInteger(1));
		else if(str_comp_nocase(pSub, "cancel") == 0)
			pG->Core()->GetMMOManager()->ConAuctionCancel(pCtx->m_ClientID, pR->GetInteger(1));
		else
			pG->SendChatTo(pCtx->m_ClientID, "未知子命令，可用: list, sell, buy, cancel");
	}, pGame);

	// Dedicated commands
	pManager->AddCommand("ah_list", "浏览拍卖行", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		pG->Core()->GetMMOManager()->ConAuctionList(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	pManager->AddCommand("ah_sell", "<格子> <价格> - 上架物品到拍卖行", "ii", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		pG->Core()->GetMMOManager()->ConAuctionSell(pCtx->m_ClientID, pR->GetInteger(0), pR->GetInteger(1));
	}, pGame);

	pManager->AddCommand("ah_buy", "<ID> - 购买拍卖行物品", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		pG->Core()->GetMMOManager()->ConAuctionBuy(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);

	pManager->AddCommand("ah_cancel", "<ID> - 取消拍卖行上架", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		pG->Core()->GetMMOManager()->ConAuctionCancel(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);
}

// ─── Mount System ───────────────────────────────────────────────────────

void CMMOManager::RegisterMountCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("mount", "切换坐骑状态（上/下坐骑）", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}
		if(!pP->GetCharacter())
		{
			pG->SendChatTo(pCtx->m_ClientID, "你没有角色。");
			return;
		}

		pP->m_IsMounted = !pP->m_IsMounted;
		if(pP->m_IsMounted)
		{
			pG->SendChatTo(pCtx->m_ClientID, "🐎 你骑上了坐骑！移动速度 +50%");
			// Set emote for mount visual
			CCharacter *pChr = pP->GetCharacter();
			if(pChr)
				pChr->SetEmote(EMOTE_HAPPY, pG->Server()->Tick() + 999999);
		}
		else
		{
			pG->SendChatTo(pCtx->m_ClientID, "你从坐骑上下来了。");
			CCharacter *pChr = pP->GetCharacter();
			if(pChr)
				pChr->SetEmote(EMOTE_NORMAL, -1);
		}
	}, pGame);
}

void CMMOManager::RegisterAutoPathCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("follow", "<玩家名>", "s", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || !pP->GetCharacter())
		{
			pG->SendChatTo(pCtx->m_ClientID, "你没有角色。");
			return;
		}

		const char *pTargetName = pR->GetString(0);
		int TargetCID = -1;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *pTarget = pG->m_apPlayers[i];
			if(!pTarget || pTarget->IsDummy() || !pTarget->GetCharacter())
				continue;
			if(str_comp_nocase(pG->Server()->ClientName(i), pTargetName) == 0)
			{
				TargetCID = i;
				break;
			}
		}

		if(TargetCID < 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "未找到目标玩家。");
			return;
		}

		pP->m_FollowTargetCID = TargetCID;
		pP->m_AutoMoving = true;
		pP->m_AutoTargetX = (int)pG->m_apPlayers[TargetCID]->GetCharacter()->GetCore()->m_Pos.x;
		pP->m_AutoTargetY = (int)pG->m_apPlayers[TargetCID]->GetCharacter()->GetCore()->m_Pos.y;
		pG->SendChatTo(pCtx->m_ClientID, "开始跟随：");
		pG->SendChatTo(pCtx->m_ClientID, pG->Server()->ClientName(TargetCID));
	}, pGame);

	pManager->AddCommand("stop", "停止自动移动", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP) return;

		pP->m_AutoMoving = false;
		pP->m_FollowTargetCID = -1;
		pG->SendChatTo(pCtx->m_ClientID, "已停止自动移动。");
	}, pGame);

	pManager->AddCommand("way", "<x> <y>", "ii", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || !pP->GetCharacter())
		{
			pG->SendChatTo(pCtx->m_ClientID, "你没有角色。");
			return;
		}

		pP->m_AutoTargetX = pR->GetInteger(0);
		pP->m_AutoTargetY = pR->GetInteger(1);
		pP->m_AutoMoving = true;
		pP->m_FollowTargetCID = -1;
		pG->SendChatTo(pCtx->m_ClientID, "开始移动至目标坐标。");
	}, pGame);
}

// ─── Fashion Commands ──────────────────────────────────────────────────

void CMMOManager::RegisterFashionCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("fashion", "查看/设置时装: /fashion set <背包格> | /fashion clear | /fashion", "s?", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}

		const char *pSub = pR->GetString(0);
		if(!pSub || pSub[0] == '\0')
		{
			// Show current fashion info
			if(pP->m_FashionItemID > 0)
			{
				const CMMOItemDescription *pDesc = CMMOItemDescription::Get(pP->m_FashionItemID);
					char aNameBuf[64];
				const char *pName;
				if(pDesc)
					pName = pDesc->GetName();
				else
				{
					str_format(aNameBuf, sizeof(aNameBuf), "物品 #%d", pP->m_FashionItemID);
					pName = aNameBuf;
				}
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "当前时装: %s (ID: %d)", pName, pP->m_FashionItemID);
				pG->SendChatTo(pCtx->m_ClientID, aBuf);
			}
			else
			{
				pG->SendChatTo(pCtx->m_ClientID, "当前没有装备时装。使用 /fashion set <背包格> 装备时装。");
			}
			return;
		}

		if(str_comp_nocase(pSub, "set") == 0)
		{
			int SlotIdx = pR->GetInteger(1);
			if(SlotIdx < 0 || (size_t)SlotIdx >= pP->m_MMOInventory.size())
			{
				pG->SendChatTo(pCtx->m_ClientID, "背包格无效！");
				return;
			}

			const CItem &Item = pP->m_MMOInventory[SlotIdx];
			if(!Item.IsValid())
			{
				pG->SendChatTo(pCtx->m_ClientID, "该背包格没有物品！");
				return;
			}

			const CMMOItemDescription *pDesc = CMMOItemDescription::Get(Item.GetID());
			if(!pDesc)
			{
				pG->SendChatTo(pCtx->m_ClientID, "物品数据不存在！");
				return;
			}

			pP->m_FashionItemID = Item.GetID();
			pP->m_MMODirty = true;

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "✨ 已装备时装: %s！(物品ID: %d)", pDesc->GetName(), pP->m_FashionItemID);
			pG->SendChatTo(pCtx->m_ClientID, aBuf);
		}
		else if(str_comp_nocase(pSub, "clear") == 0)
		{
			if(pP->m_FashionItemID == 0)
			{
				pG->SendChatTo(pCtx->m_ClientID, "当前没有装备时装。");
				return;
			}

			const CMMOItemDescription *pDesc = CMMOItemDescription::Get(pP->m_FashionItemID);
			const char *pName = pDesc ? pDesc->GetName() : "未知";

			pP->m_FashionItemID = 0;
			pP->m_MMODirty = true;

			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "已清除时装 %s，恢复默认外观。", pName);
			pG->SendChatTo(pCtx->m_ClientID, aBuf);
		}
		else
		{
			pG->SendChatTo(pCtx->m_ClientID, "用法: /fashion set <背包格> | /fashion clear | /fashion");
		}
	}, pGame);
}

// ─── Pet Commands ────────────────────────────────────────────────────

void CMMOManager::RegisterPetCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	// /pet - 查看宠物状态
	pManager->AddCommand("pet", "查看宠物状态或执行操作", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}

		if(pP->m_PetID <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "🐾 你没有宠物。使用 /pet summon 召唤。");
			return;
		}

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf),
			"🐾 宠物: %s | 模板ID: %d | 等级: %d | 状态: %s",
			pP->m_aPetName[0] ? pP->m_aPetName : "未命名",
			pP->m_PetID,
			pP->m_PetLevel,
			pP->m_pPet ? "已召唤✨" : "未召唤");
		pG->SendChatTo(pCtx->m_ClientID, aBuf);
		pG->SendChatTo(pCtx->m_ClientID, "可用命令: /pet summon, /pet dismiss, /pet name <新名字>");
	}, pGame);

	// /pet summon - 召唤宠物
	pManager->AddCommand("petsummon", "召唤宠物", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}

		if(pP->m_PetID <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "🐾 你没有宠物。");
			return;
		}

		if(pP->m_pPet)
		{
			pG->SendChatTo(pCtx->m_ClientID, "🐾 宠物已在身边。使用 /pet dismiss 收起。");
			return;
		}

		if(!pP->GetCharacter())
		{
			pG->SendChatTo(pCtx->m_ClientID, "🐾 你没有角色。");
			return;
		}

		CPet *pPet = new CPet(pG, pP->GetCID(), pP->m_PetID);
		if(pP->m_aPetName[0])
			pPet->SetName(pP->m_aPetName);
		pP->m_pPet = pPet;

		pG->SendChatTo(pCtx->m_ClientID, "🐾 宠物已召唤！");
	}, pGame);

	// /pet dismiss - 收起宠物
	pManager->AddCommand("petdismiss", "收起宠物", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP) return;

		if(!pP->m_pPet)
		{
			pG->SendChatTo(pCtx->m_ClientID, "🐾 没有已召唤的宠物。");
			return;
		}

		pP->m_pPet->MarkForDestroy();
		pP->m_pPet = nullptr;
		pG->SendChatTo(pCtx->m_ClientID, "🐾 宠物已收起。");
	}, pGame);

	// /pet name <新名字> - 改名
	pManager->AddCommand("petname", "为宠物改名", "s", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "请先登录。");
			return;
		}

		if(pP->m_PetID <= 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "🐾 你没有宠物，无法改名。");
			return;
		}

		const char *pNewName = pR->GetString(0);
		if(!pNewName || !pNewName[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "用法: /pet name <新名字>");
			return;
		}

		str_copy(pP->m_aPetName, pNewName, sizeof(pP->m_aPetName));
		if(pP->m_pPet)
			pP->m_pPet->SetName(pP->m_aPetName);

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "🐾 宠物已更名为: %s", pP->m_aPetName);
		pG->SendChatTo(pCtx->m_ClientID, aBuf);

		// 立即保存改名
		if(pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->SavePetData(pP);
	}, pGame);
}


// ══════════════════════════════════════════════════════════════════════
//  Housing Commands
// ══════════════════════════════════════════════════════════════════════

void CMMOManager::RegisterHouseCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("house", "房屋系统 /house buy|tp", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0) { pG->SendChatTo(pCtx->m_ClientID, "请先登录。"); return; }
		if(pP->m_HasHouse)
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "🏠 你的房屋等级: %d | /house tp 传送回家", pP->m_HouseLevel);
			pG->SendChatTo(pCtx->m_ClientID, aBuf);
		}
		else
		{
			pG->SendChatTo(pCtx->m_ClientID, "🏠 你还没有房屋。使用 /house buy (10000金币) 购买房屋。");
		}
	}, pGame);

	pManager->AddCommand("house_buy", "购买房屋 (10000金币)", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0) { pG->SendChatTo(pCtx->m_ClientID, "请先登录。"); return; }
		if(pP->m_HasHouse) { pG->SendChatTo(pCtx->m_ClientID, "你已经拥有房屋了！"); return; }
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		if(!pMMO->SpendGold(pP, 10000))
		{ pG->SendChatTo(pCtx->m_ClientID, "金币不足！购买房屋需要 10000 金币。"); return; }
		pP->m_HasHouse = true;
		pP->m_HouseLevel = 1;
		pP->m_MMODirty = true;
		pMMO->SaveHouseData(pP);
		pG->SendChatTo(pCtx->m_ClientID, "🏠 恭喜！你购买了一栋房屋！使用 /house tp 传送回家。");
	}, pGame);

	pManager->AddCommand("house_tp", "传送回房屋", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0) { pG->SendChatTo(pCtx->m_ClientID, "请先登录。"); return; }
		if(!pP->m_HasHouse) { pG->SendChatTo(pCtx->m_ClientID, "你没有房屋。"); return; }
		CCharacter *pChar = pP->GetCharacter();
		if(!pChar) { pG->SendChatTo(pCtx->m_ClientID, "你没有角色。"); return; }
		pChar->SetCharacterPos(vec2(1000, 1000));
		pG->SendChatTo(pCtx->m_ClientID, "🏠 传送回房屋！");
	}, pGame);
}

// ─── Marriage System ───────────────────────────────────────────────────────

static std::map<int64, int64> gs_Proposals;  // ProposerAID → TargetAID

void CMMOManager::RegisterMarriageCommands()
{
	CCommandManager *pManager = GS()->CommandManager();
	if(!pManager) return;
	CGameContext *pGame = GS();

	// /marry <玩家名> — 求婚
	pManager->AddCommand("marry", "向玩家求婚: /marry <玩家名>", "s", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0) { pG->SendChatTo(pCtx->m_ClientID, "请先登录。"); return; }

		// Check proposer is single
		if(pP->m_SpouseAccountID > 0)
		{ pG->SendChatTo(pCtx->m_ClientID, "你已经结婚了！使用 /divorce 离婚后可以再求婚。"); return; }

		const char *pTargetName = pR->GetString(0);
		int TargetCID = pG->Core()->GetMMOManager()->FindClientByName(pTargetName);
		if(TargetCID < 0) { pG->SendChatTo(pCtx->m_ClientID, "未找到该玩家。"); return; }
		if(TargetCID == pCtx->m_ClientID) { pG->SendChatTo(pCtx->m_ClientID, "不能向自己求婚！"); return; }

		CPlayer *pTarget = pG->m_apPlayers[TargetCID];
		if(!pTarget || pTarget->GetAccountId() <= 0) { pG->SendChatTo(pCtx->m_ClientID, "目标未登录。"); return; }

		// Check target is single
		if(pTarget->m_SpouseAccountID > 0)
		{ pG->SendChatTo(pCtx->m_ClientID, "对方已经结婚了。"); return; }

		int64 ProposerAID = pP->GetAccountId();
		int64 TargetAID = pTarget->GetAccountId();

		// Store proposal
		gs_Proposals[ProposerAID] = TargetAID;

		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "💍 %s 向你求婚了！使用 /marry_accept 接受", pG->Server()->ClientName(pCtx->m_ClientID));
		pG->SendChatTo(TargetCID, aBuf);

		str_format(aBuf, sizeof(aBuf), "💍 你向 %s 求婚了，等待对方回应...", pTargetName);
		pG->SendChatTo(pCtx->m_ClientID, aBuf);
	}, pGame);

	// /marry_accept — 接受求婚
	pManager->AddCommand("marry_accept", "接受求婚", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0) { pG->SendChatTo(pCtx->m_ClientID, "请先登录。"); return; }
		(void)pR;

		if(pP->m_SpouseAccountID > 0)
		{ pG->SendChatTo(pCtx->m_ClientID, "你已经结婚了！"); return; }

		// Find proposal targeting this player
		int64 TargetAID = pP->GetAccountId();
		int64 ProposerAID = 0;
		for(auto &Pair : gs_Proposals)
		{
			if(Pair.second == TargetAID)
			{
				ProposerAID = Pair.first;
				break;
			}
		}

		if(ProposerAID == 0)
		{ pG->SendChatTo(pCtx->m_ClientID, "你没有被求婚。"); return; }

		// Find the proposer online
		int ProposerCID = -1;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *p = pG->m_apPlayers[i];
			if(p && p->GetAccountId() == ProposerAID)
			{ ProposerCID = i; break; }
		}

		if(ProposerCID < 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "求婚者已离线。");
			gs_Proposals.erase(ProposerAID);
			return;
		}

		CPlayer *pProposer = pG->m_apPlayers[ProposerCID];
		if(!pProposer || pProposer->m_SpouseAccountID > 0)
		{
			pG->SendChatTo(pCtx->m_ClientID, "求婚者已结婚。");
			gs_Proposals.erase(ProposerAID);
			return;
		}

		// Insert into DB
		CSqlConnectionPool *pPool = pG->Accounts()->GetSqlPool();
		if(!pPool || !pPool->IsInitialized()) { pG->SendChatTo(pCtx->m_ClientID, "数据库错误。"); return; }
		void *pRaw = pPool->Acquire();
		if(!pRaw) { pG->SendChatTo(pCtx->m_ClientID, "数据库错误。"); return; }
		MYSQL *pSql = (MYSQL *)pRaw;

		char aDate[16];
		{
			time_t Now = time(nullptr);
			struct tm *pTM = localtime(&Now);
			strftime(aDate, sizeof(aDate), "%Y%m%d", pTM);
		}

		char aQuery[256];
		str_format(aQuery, sizeof(aQuery),
			"INSERT INTO `tw_marriages` (`SpouseA`, `SpouseB`, `MarriedAt`) VALUES (%lld, %lld, %s)",
			(long long)ProposerAID, (long long)TargetAID, aDate);
		if(!SqlExecQuery(pSql, pG->Config(), aQuery))
		{
			pPool->Release(pRaw);
			pG->SendChatTo(pCtx->m_ClientID, "结婚失败，数据库错误。");
			return;
		}
		pPool->Release(pRaw);

		// Update in-memory state
		pP->m_SpouseAccountID = ProposerAID;
		pP->m_MarriageDate = str_toint(aDate);
		pP->m_MMODirty = true;
		pProposer->m_SpouseAccountID = TargetAID;
		pProposer->m_MarriageDate = str_toint(aDate);
		pProposer->m_MMODirty = true;

		// Cleanup proposal
		gs_Proposals.erase(ProposerAID);

		// Broadcast wedding announcement
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "🎊 💍 %s 和 %s 喜结连理！祝他们幸福快乐！💍 🎊",
			pG->Server()->ClientName(ProposerCID),
			pG->Server()->ClientName(pCtx->m_ClientID));
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(pG->m_apPlayers[i] && pG->Server()->ClientIngame(i))
				pG->SendChatTo(i, aBuf);
		}
	}, pGame);

	// /divorce — 离婚
	pManager->AddCommand("divorce", "与配偶离婚", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager()) return;
		CPlayer *pP = pG->m_apPlayers[pCtx->m_ClientID];
		if(!pP || pP->GetAccountId() <= 0) { pG->SendChatTo(pCtx->m_ClientID, "请先登录。"); return; }
		(void)pR;

		if(pP->m_SpouseAccountID == 0)
		{ pG->SendChatTo(pCtx->m_ClientID, "你是单身，无法离婚。"); return; }

		int64 UserId = pP->GetAccountId();
		int64 SpouseAID = pP->m_SpouseAccountID;

		// Delete from DB
		CSqlConnectionPool *pPool = pG->Accounts()->GetSqlPool();
		if(!pPool || !pPool->IsInitialized()) { pG->SendChatTo(pCtx->m_ClientID, "数据库错误。"); return; }
		void *pRaw = pPool->Acquire();
		if(!pRaw) { pG->SendChatTo(pCtx->m_ClientID, "数据库错误。"); return; }
		MYSQL *pSql = (MYSQL *)pRaw;

		char aQuery[256];
		str_format(aQuery, sizeof(aQuery),
			"DELETE FROM `tw_marriages` WHERE (`SpouseA`=%lld AND `SpouseB`=%lld) OR (`SpouseA`=%lld AND `SpouseB`=%lld)",
			(long long)UserId, (long long)SpouseAID,
			(long long)SpouseAID, (long long)UserId);
		SqlExecQuery(pSql, pG->Config(), aQuery);
		pPool->Release(pRaw);

		// Clear local state
		pP->m_SpouseAccountID = 0;
		pP->m_MarriageDate = 0;
		pP->m_MMODirty = true;

		// Clear spouse's local state if online
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CPlayer *p = pG->m_apPlayers[i];
			if(p && p->GetAccountId() == SpouseAID)
			{
				p->m_SpouseAccountID = 0;
				p->m_MarriageDate = 0;
				p->m_MMODirty = true;
				pG->SendChatTo(i, "💔 你的配偶与你离婚了。");
				break;
			}
		}

		pG->SendChatTo(pCtx->m_ClientID, "💔 离婚成功。");
	}, pGame);
}
