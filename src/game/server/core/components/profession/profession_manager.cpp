#include "profession_manager.h"
#include <game/server/player.h>
#include <game/server/gamecontext.h>
#include <game/server/core/tworld_controller.h>
#include <game/commands.h>

// Exp curve: level^2 * 50 (matches common MMO scaling)
int CProfessionManager::GetExpForLevel(int Level)
{
	return Level * Level * 50;
}

bool CProfessionManager::SetProfession(int ClientID, EProfession Prof)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP)
		return false;

	// Can't change profession if already set
	if(pP->GetProfession() != PROF_NONE)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "你已经选择了职业，不能更改。");
		return false;
	}

	if(Prof == PROF_NONE)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "无效的职业。");
		return false;
	}

	pP->SetProfession(Prof);
	const char *pName = CProfessionData::GetName(Prof);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "你选择了职业：%s。", pName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	str_format(aBuf, sizeof(aBuf), "生命值加成：%d + %.0f/级",
		CProfessionData::GetDef(Prof)->m_BaseHP, CProfessionData::GetDef(Prof)->m_HPPerLevel);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	return true;
}

static void ComChatProfessionList(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame)
		return;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP)
		return;

	// Show current profession info
	if(pP->GetProfession() != PROF_NONE)
	{
		const SProfessionDef *pDef = CProfessionData::GetDef(pP->GetProfession());
		if(pDef)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "当前职业：%s", pDef->m_pName);
			pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
			str_format(aBuf, sizeof(aBuf), "  HP: %d + %.0f/级  攻击: +%.1f/级  防御: +%.1f/级",
				pDef->m_BaseHP, pDef->m_HPPerLevel, pDef->m_AttackPerLevel, pDef->m_DefensePerLevel);
			pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
			str_format(aBuf, sizeof(aBuf), "  等级: Lv.%d  HP: %d  攻击: %.0f  防御: %.0f",
				pP->m_MMOLevel, pP->GetBaseMaxHealth(), pP->GetBaseAttack(), pP->GetBaseDefense());
			pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
			return;
		}
	}

	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "你还没有选择职业。可用职业：");

	// List all available professions
	for(int i = 0; i < (int)PROF_NUM; i++)
	{
		const SProfessionDef *pDef = CProfessionData::GetDef(i);
		if(!pDef) continue;
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "  %s  — %s%s",
			pDef->m_pName, pDef->m_pDescription,
			pDef->m_bCombatClass ? "" : " (采集)");
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
	}

	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "输入 /profession set <名称> 来选择职业。");
}

static void ComChatProfessionSet(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame)
		return;

	const char *pName = pResult->GetString(0);
	EProfession Prof = CProfessionData::FindByName(pName);

	if(Prof == PROF_NONE)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "未知职业：%s。使用 /profession list 查看可用职业。", pName);
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
		return;
	}

	if(pGame->Core() && pGame->Core()->ProfessionManager())
	{
		pGame->Core()->ProfessionManager()->SetProfession(pCtx->m_ClientID, Prof);
	}
}

void CProfessionManager::RegisterChatCommands(CCommandManager *pManager)
{
	CGameContext *pGame = GS();
	pManager->AddCommand("profession list", "profession.list.help", "", ComChatProfessionList, pGame);
	pManager->AddCommand("profession set", "profession.set.help", "s", ComChatProfessionSet, pGame);
}
