#include <base/math.h>

#include "entity_manager.h"

#include <engine/shared/config.h>
#include <generated/server_data.h>

#include <game/server/account.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/tools/event_listener.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

CEntityManager::CEntityManager(CGameContext *pGS)
	: m_pGS(pGS)
{
}

static int ParseLeadingPlusAmount(const char *pText)
{
	if(!pText || pText[0] != '+')
		return 0;

	int Amount = 0;
	const char *p = pText + 1;
	while(*p >= '0' && *p <= '9')
	{
		Amount = Amount * 10 + (*p - '0');
		p++;
	}
	return Amount;
}

static void ShowFloatingText(CGameContext *pGS, vec2 Pos, int ClientID, const char *pText)
{
	if(!pGS || !pText || !pText[0])
		return;

	const int Amount = ParseLeadingPlusAmount(pText);
	const int64 Mask = ClientID >= 0 ? CmaskOne(ClientID) : -1;
	const int DamageClient = ClientID >= 0 ? ClientID : 0;

	if(Amount > 0)
		pGS->m_World.CreateFloatingAmount(Pos, DamageClient, Amount, Mask);

	if(ClientID >= 0)
		pGS->SendBroadcast(ClientID, pText);
}

void CEntityManager::Text(vec2 Pos, const char *pText, int LifeTicks) const
{
	(void)LifeTicks;
	ShowFloatingText(m_pGS, Pos, -1, pText);
}

void CEntityManager::TextForClient(int ClientID, vec2 Pos, const char *pText) const
{
	if(!m_pGS || ClientID < 0 || ClientID >= MAX_CLIENTS || !pText || !pText[0])
		return;
	if(!m_pGS->m_apPlayers[ClientID])
		return;

	ShowFloatingText(m_pGS, Pos, ClientID, pText);
	m_pGS->m_World.CreateSound(Pos, SOUND_PICKUP_ARMOR, CmaskOne(ClientID));
}

void CEntityManager::DropItem(vec2 Pos, int ClientID, int ItemId, int Num, vec2 Force) const
{
	(void)Force;
	if(!m_pGS || Num <= 0 || ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	CPlayer *pP = m_pGS->m_apPlayers[ClientID];
	CItemHelper *pH = m_pGS->ItemHelper();
	if(!pP || !pH || !pH->CheckItemValid(ItemId))
		return;

	int GrantNum = Num;
	const int FortuneStacks = pH->SumArmorEffectStacks(pP, ITEM_CARD_FORTUNE, "fortune");
	if(FortuneStacks > 0)
	{
		const int Chance = FortuneStacks * pH->GetProba(ITEM_CARD_FORTUNE);
		if(Chance > 0 && (random_int() % 100) < Chance)
			GrantNum *= 2;
	}

	pP->m_AccData.m_aItems[ItemId].m_Num += GrantNum;
	if(TWorldController *pCore = m_pGS->Core())
		pCore->Events().EmitPlayerGotItem(pP, ItemId, GrantNum);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "+%d %s", GrantNum, m_pGS->LocItemName(ClientID, ItemId));
	TextForClient(ClientID, Pos, aBuf);

	if(m_pGS->Accounts() && m_pGS->Accounts()->IsEnabled() && pP->GetAccountId() >= 0)
		m_pGS->Accounts()->RequestSaveItems(ClientID);
}
