#ifndef GAME_SERVER_CORE_COMPONENTS_NPCS_NPC_SERVICE_H
#define GAME_SERVER_CORE_COMPONENTS_NPCS_NPC_SERVICE_H

#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/npcs/npc_manager.h>
#include <game/server/core/components/economy/shop_data.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

static constexpr float NPC_SERVICE_RANGE = 180.f;

static inline bool IsNpcServicePage(int Page)
{
	switch(Page)
	{
	case PAGE_SKILLS:
	case PAGE_SKILL_SELECT:
	case PAGE_QUESTS:
	case PAGE_QUEST_DETAIL:
	case PAGE_CRAFT:
	case PAGE_CRAFT_SELECTED:
	case VOTE_PAGE_MMO_SHOP_ITEMS:
	case VOTE_PAGE_MMO_ENCHANT:
	case VOTE_PAGE_MMO_ENCHANT_SELECT:
		return true;
	default:
		return false;
	}
}

static inline bool IsRemoteShopBrowserPage(int Page)
{
	return Page == VOTE_PAGE_MMO_SHOP || Page == VOTE_PAGE_MMO_SHOP_LIST;
}

static inline bool NpcProvidesServicePage(const char *pNpcId, int Page)
{
	if(!pNpcId || !pNpcId[0])
		return false;

	switch(Page)
	{
	case PAGE_SKILLS:
	case PAGE_SKILL_SELECT:
		return str_comp(pNpcId, "dream_keeper") == 0 || str_comp(pNpcId, "trainer") == 0;
	case PAGE_QUESTS:
	case PAGE_QUEST_DETAIL:
		return str_comp(pNpcId, "quest_master") == 0;
	case PAGE_CRAFT:
	case PAGE_CRAFT_SELECTED:
	case VOTE_PAGE_MMO_ENCHANT:
	case VOTE_PAGE_MMO_ENCHANT_SELECT:
		return str_comp(pNpcId, "blacksmith") == 0;
	case VOTE_PAGE_MMO_SHOP_ITEMS:
		return FindShopByNpcID(pNpcId) != nullptr;
	default:
		return false;
	}
}

static inline void BindNpcService(SPlayerVote *pVote, const char *pNpcId)
{
	if(!pVote || !pNpcId)
		return;
	str_copy(pVote->m_aServiceNpcId, pNpcId, sizeof(pVote->m_aServiceNpcId));
}

static inline void ClearNpcService(SPlayerVote *pVote)
{
	if(pVote)
		pVote->m_aServiceNpcId[0] = 0;
}

static inline bool IsPlayerNearServiceNpc(CGameContext *pGS, CPlayer *pP, const char *pNpcId)
{
	if(!pGS || !pP || !pNpcId || !pNpcId[0])
		return false;
	CNpcManager *pNpcMgr = pGS->Core() ? pGS->Core()->NpcManager() : nullptr;
	return pNpcMgr && pNpcMgr->IsPlayerNearNpc(pP, pNpcId, NPC_SERVICE_RANGE);
}

static inline bool EnsureNpcServiceAccess(CGameContext *pGS, CPlayer *pP, SPlayerVote *pVote)
{
	if(!pGS || !pP || !pVote)
		return false;
	if(!IsNpcServicePage(pVote->m_Page) && !IsRemoteShopBrowserPage(pVote->m_Page))
		return true;
	if(IsRemoteShopBrowserPage(pVote->m_Page))
		return true;
	if(pVote->m_Page == VOTE_PAGE_MMO_ENCHANT || pVote->m_Page == VOTE_PAGE_MMO_ENCHANT_SELECT)
	{
		if(pVote->m_aServiceNpcId[0])
			return IsPlayerNearServiceNpc(pGS, pP, pVote->m_aServiceNpcId);
		return IsPlayerNearServiceNpc(pGS, pP, "blacksmith");
	}
	if(!pVote->m_aServiceNpcId[0])
		return false;
	return IsPlayerNearServiceNpc(pGS, pP, pVote->m_aServiceNpcId);
}

static inline void NotifyNpcServiceDenied(CGameContext *pGS, int ClientID)
{
	if(pGS)
		pGS->SendChatLoc(ClientID, "npc.service.too_far", "请靠近对应 NPC 再使用此功能。");
}

static inline bool TryBindNpcServiceFromDialog(CGameContext *pGS, CPlayer *pP, SPlayerVote *pVote, int Page, const char *pNpcId)
{
	if(!pGS || !pP || !pVote || !pNpcId || !pNpcId[0])
		return false;
	if(!IsPlayerNearServiceNpc(pGS, pP, pNpcId))
	{
		NotifyNpcServiceDenied(pGS, pP->GetCID());
		return false;
	}
	if(!NpcProvidesServicePage(pNpcId, Page))
		return false;

	BindNpcService(pVote, pNpcId);
	pVote->m_LastPage = PAGE_MENU;
	pVote->m_Page = Page;
	if(Page == VOTE_PAGE_MMO_SHOP_ITEMS)
		str_copy(pVote->m_aExtraText, pNpcId, sizeof(pVote->m_aExtraText));
	return true;
}

#endif
