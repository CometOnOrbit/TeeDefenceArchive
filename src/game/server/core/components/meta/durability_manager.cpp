#include <engine/console.h>
#include <engine/shared/protocol.h>

#include <game/commands.h>
#include <game/server/account.h>
#include <game/server/core/components/meta/durability_manager.h>
#include <game/server/core/components/meta/mini_events_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

enum
{
	DURABILITY_DEFAULT = 100,
	DURABILITY_MAX = 100,
};

CDurabilityManager::CDurabilityManager()
{
}

void CDurabilityManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	if(Core())
		Core()->Events().Register(this);
}

void CDurabilityManager::OnShutdown()
{
	if(Core())
		Core()->Events().Unregister(this);
}

bool CDurabilityManager::ItemHasDurability(CItemHelper *pH, int ItemId)
{
	if(!pH || !pH->CheckItemValid(ItemId))
		return false;
	const int T = pH->GetType(ItemId);
	return T == ITYPE_PICKAXE || T == ITYPE_AXE || T == ITYPE_SWORD
		|| T == ITYPE_HELMET || T == ITYPE_CHEST || T == ITYPE_LEGS;
}

int CDurabilityManager::GetDurability(CItemHelper *pH, int ItemId, const char *pExtra)
{
	if(!ItemHasDurability(pH, ItemId))
		return DURABILITY_MAX;
	if(!pExtra || !pExtra[0])
		return DURABILITY_DEFAULT;
	const char *pKey = strstr(pExtra, "\"durability\":");
	if(!pKey)
		return DURABILITY_DEFAULT;
	int Val = str_toint(pKey + str_length("\"durability\":"));
	if(Val < 0)
		Val = 0;
	if(Val > DURABILITY_MAX)
		Val = DURABILITY_MAX;
	return Val;
}

void CDurabilityManager::SetDurability(char *pExtra, int ExtraSize, int Dur)
{
	if(!pExtra || ExtraSize <= 0)
		return;
	if(Dur < 0)
		Dur = 0;
	if(Dur > DURABILITY_MAX)
		Dur = DURABILITY_MAX;
	char aBuf[640];
	if(strstr(pExtra, "\"durability\":"))
	{
		int Len = 0;
		const char *pStart = strstr(pExtra, "\"durability\":");
		str_copy(aBuf, pExtra, minimum((int)sizeof(aBuf), (int)(pStart - pExtra) + 1));
		Len = str_length(aBuf);
		const char *pAfter = pStart + str_length("\"durability\":");
		while(*pAfter && (*pAfter < '0' || *pAfter > '9') && *pAfter != '-')
			pAfter++;
		while(*pAfter >= '0' && *pAfter <= '9')
			pAfter++;
		str_format(aBuf + Len, sizeof(aBuf) - Len, "\"durability\":%d%s", Dur, pAfter);
		Len = str_length(aBuf);
	}
	else if(pExtra[0] == '{')
	{
		if(pExtra[1] == '}')
			str_format(aBuf, sizeof(aBuf), "{\"durability\":%d}", Dur);
		else
			str_format(aBuf, sizeof(aBuf), "{\"durability\":%d,%s", Dur, pExtra + 1);
	}
	else
		str_format(aBuf, sizeof(aBuf), "{\"durability\":%d}", Dur);
	str_copy(pExtra, aBuf, ExtraSize);
}

void CDurabilityManager::EnsureDurability(char *pExtra, int ExtraSize)
{
	if(!pExtra || ExtraSize <= 0)
		return;
	if(!strstr(pExtra, "\"durability\":"))
		SetDurability(pExtra, ExtraSize, DURABILITY_DEFAULT);
}

void CDurabilityManager::DamageHoldingTool(CPlayer *pPlayer, int HoldKind, int Amount)
{
	// For now.
	/*
	if(!pPlayer || Amount <= 0 || !GS())
		return;
	CItemHelper *pH = GS()->ItemHelper();
	if(!pH)
		return;
	const int ItemId = pPlayer->GetHolding(HoldKind);
	if(!ItemHasDurability(pH, ItemId))
		return;
	char *pExtra = pPlayer->m_AccData.m_aItems[ItemId].m_aExtra;
	EnsureDurability(pExtra, sizeof(pPlayer->m_AccData.m_aItems[ItemId].m_aExtra));
	const int Dur = GetDurability(pH, ItemId, pExtra) - Amount;
	SetDurability(pExtra, sizeof(pPlayer->m_AccData.m_aItems[ItemId].m_aExtra), Dur);
	if(Dur <= 0)
	{
		GS()->SendChatLocF(pPlayer->GetCID(), "durability.broken", u8"%s 已损坏，请修理。", GS()->LocItemName(pPlayer->GetCID(), ItemId));
		pPlayer->m_AccData.m_Holding[HoldKind] = 0;
	}
	if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
		GS()->Accounts()->RequestSaveItems(pPlayer->GetCID());*/
}

void CDurabilityManager::DamageEquipped(CPlayer *pPlayer, int Amount)
{
	if(!pPlayer || Amount <= 0)
		return;
	DamageHoldingTool(pPlayer, ITYPE_SWORD, Amount);
	DamageHoldingTool(pPlayer, ITYPE_HELMET, Amount / 2);
	DamageHoldingTool(pPlayer, ITYPE_CHEST, Amount / 2);
	DamageHoldingTool(pPlayer, ITYPE_LEGS, Amount / 2);
}

void CDurabilityManager::OnCharacterDeath(CPlayer *pVictim, CPlayer *pKiller, int Weapon)
{
	(void)pKiller;
	(void)Weapon;
	if(!pVictim || pVictim->IsDummy())
		return;
	DamageEquipped(pVictim, 8);
}

void CDurabilityManager::OnPlayerMine(CPlayer *pPlayer, int MatId)
{
	(void)MatId;
	if(!pPlayer)
		return;
	DamageHoldingTool(pPlayer, ITYPE_PICKAXE, 1);
	DamageHoldingTool(pPlayer, ITYPE_AXE, 1);
}

bool CDurabilityManager::TryRepair(CPlayer *pPlayer, int ItemId, char *pErr, int ErrSize, char *pErrKey, int KeySize)
{
	auto Fail = [&](const char *pKey, const char *pMsg) {
		if(pErr && ErrSize > 0)
			str_copy(pErr, pMsg, ErrSize);
		if(pErrKey && KeySize > 0)
			str_copy(pErrKey, pKey, KeySize);
		return false;
	};

	if(!pPlayer || !GS())
		return Fail("durability.repair.err.invalid_player", u8"无效玩家。");
	CItemHelper *pH = GS()->ItemHelper();
	if(!pH || !ItemHasDurability(pH, ItemId))
		return Fail("durability.repair.err.no_need", u8"该物品无需修理。");
	if(pPlayer->m_AccData.m_aItems[ItemId].m_Num <= 0)
		return Fail("durability.repair.err.not_owned", u8"你没有该物品。");

	char *pExtra = pPlayer->m_AccData.m_aItems[ItemId].m_aExtra;
	EnsureDurability(pExtra, sizeof(pPlayer->m_AccData.m_aItems[ItemId].m_aExtra));
	const int Dur = GetDurability(pH, ItemId, pExtra);
	if(Dur >= DURABILITY_MAX)
		return Fail("durability.repair.err.full", u8"耐久已满。");

	int Cost = maximum(1, (DURABILITY_MAX - Dur) / 5);
	if(Core() && Core()->MiniEventsManager())
	{
		const int Disc = Core()->MiniEventsManager()->GetRepairDiscountPercent();
		Cost = maximum(1, Cost * (100 - Disc) / 100);
	}
	if(pPlayer->m_AccData.m_aItems[ITEM_GOLD].m_Num < Cost)
		return Fail("durability.repair.err.no_gold", u8"金币不足。");

	pPlayer->m_AccData.m_aItems[ITEM_GOLD].m_Num -= Cost;
	SetDurability(pExtra, sizeof(pPlayer->m_AccData.m_aItems[ItemId].m_aExtra), DURABILITY_MAX);
	if(GS()->Accounts() && GS()->Accounts()->IsEnabled() && pPlayer->GetAccountId() >= 0)
	{
		GS()->Accounts()->RequestSaveItems(pPlayer->GetCID());
		GS()->Accounts()->RequestSaveAccount(pPlayer->GetCID());
	}
	return true;
}

static void ComVoteRepair(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pGame->Core() || !pGame->Core()->DurabilityManager() || !pP)
		return;
	char aErr[128];
	char aErrKey[48] = "";
	if(!pGame->Core()->DurabilityManager()->TryRepair(pP, pResult->GetInteger(0), aErr, sizeof(aErr), aErrKey, sizeof(aErrKey)))
		pGame->SendChatLoc(pCtx->m_ClientID, aErrKey[0] ? aErrKey : "err.unknown", aErr);
	else
		pGame->SendChatLoc(pCtx->m_ClientID, "durability.repaired", u8"修理完成。");
	pGame->Core()->VoteMenuManager()->ClearVotes(pCtx->m_ClientID);
}

void CDurabilityManager::RegisterVoteCommands(CCommandManager *pMgr)
{
	if(!pMgr || !GS())
		return;
	pMgr->AddCommand("menurepair", "", "i", ComVoteRepair, GS());
}

bool CDurabilityManager::OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs)
{
	(void)pPlayer;
	(void)pCmd;
	(void)pArgs;
	return false;
}
