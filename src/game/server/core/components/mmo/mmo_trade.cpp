#include "mmo_manager.h"
#include <game/server/global_state.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/mmo/mmo_item.h>
#include <base/system.h>
#include <engine/shared/config.h>

// ══════════════════════════════════════════════════════════════════════
//  CMMOManager Trade Methods — forward to CGlobalState
// ══════════════════════════════════════════════════════════════════════

bool CMMOManager::TradeRequest(int FromCID, int ToCID)
{
	return CGlobalState::TradeRequest(GS(), FromCID, ToCID);
}

bool CMMOManager::TradeAccept(int ClientID)
{
	return CGlobalState::TradeAccept(GS(), ClientID);
}

bool CMMOManager::TradeDecline(int ClientID)
{
	return CGlobalState::TradeDecline(GS(), ClientID);
}

bool CMMOManager::TradeAddItem(int ClientID, int ItemSlotIndex, int Count)
{
	return CGlobalState::TradeAddItem(GS(), ClientID, ItemSlotIndex, Count);
}

bool CMMOManager::TradeAddGold(int ClientID, int Amount)
{
	return CGlobalState::TradeAddGold(GS(), ClientID, Amount);
}

bool CMMOManager::TradeConfirm(int ClientID)
{
	return CGlobalState::TradeConfirm(GS(), ClientID);
}

bool CMMOManager::TradeCancel(int ClientID)
{
	return CGlobalState::TradeCancel(GS(), ClientID);
}

int CMMOManager::FindClientByName(const char *pName)
{
	return CGlobalState::FindClientByName(GS(), pName);
}
