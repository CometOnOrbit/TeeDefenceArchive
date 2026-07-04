#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>

#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/worlds/portal_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CPortalManager::CPortalManager()
{
	m_NumPortals = 0;
	mem_zero(m_aDwellStart, sizeof(m_aDwellStart));
	mem_zero(m_aInsidePortal, sizeof(m_aInsidePortal));
}

void CPortalManager::LoadPortals()
{
	m_NumPortals = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/portals.json", Storage());
	if(!pRoot)
	{
		dbg_msg("portal", "portals.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["portals"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumPortals < MAX_PORTALS; i++)
	{
		const json_value &P = Arr[(int)i];
		if(P.type != json_object || P["id"].type != json_string)
			continue;
		SPortalDef &Def = m_aPortals[m_NumPortals++];
		mem_zero(&Def, sizeof(Def));
		str_copy(Def.m_aId, P["id"].u.string.ptr, sizeof(Def.m_aId));
		if(P["world"].type == json_integer)
			Def.m_World = (int)P["world"].u.integer;
		if(P["x"].type == json_integer)
			Def.m_X = (float)P["x"].u.integer;
		else if(P["x"].type == json_double)
			Def.m_X = (float)P["x"].u.dbl;
		if(P["y"].type == json_integer)
			Def.m_Y = (float)P["y"].u.integer;
		else if(P["y"].type == json_double)
			Def.m_Y = (float)P["y"].u.dbl;
		if(P["radius"].type == json_integer)
			Def.m_Radius = (float)P["radius"].u.integer;
		else if(P["radius"].type == json_double)
			Def.m_Radius = (float)P["radius"].u.dbl;
		if(Def.m_Radius <= 0.f)
			Def.m_Radius = 64.f;
		if(P["dest_world"].type == json_integer)
			Def.m_DestWorld = (int)P["dest_world"].u.integer;
		if(P["dest_x"].type == json_integer)
			Def.m_DestX = (float)P["dest_x"].u.integer;
		if(P["dest_y"].type == json_integer)
			Def.m_DestY = (float)P["dest_y"].u.integer;
		if(P["require_quest"].type == json_string)
			str_copy(Def.m_aRequireQuest, P["require_quest"].u.string.ptr, sizeof(Def.m_aRequireQuest));
		if(P["require_item"].type == json_integer)
			Def.m_RequireItem = (int)P["require_item"].u.integer;
	}
	dbg_msg("portal", "loaded %d portals", m_NumPortals);
}

void CPortalManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	LoadPortals();
}

void CPortalManager::OnTick()
{
	if(!GS())
		return;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(!pP || pP->IsDummy() || !pP->GetCharacter() || !pP->GetCharacter()->IsAlive())
			continue;
		TryPortalTravel(pP, pP->GetCharacter()->GetPos());
	}
}

void CPortalManager::OnClientReset(int ClientID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
	{
		m_aDwellStart[ClientID] = 0;
		m_aInsidePortal[ClientID] = -1;
	}
}

const SPortalDef *CPortalManager::FindPortal(const char *pId) const
{
	if(!pId)
		return nullptr;
	for(int i = 0; i < m_NumPortals; i++)
	{
		if(str_comp(m_aPortals[i].m_aId, pId) == 0)
			return &m_aPortals[i];
	}
	return nullptr;
}

const SPortalDef *CPortalManager::FindPortalAt(int World, vec2 Pos) const
{
	for(int i = 0; i < m_NumPortals; i++)
	{
		const SPortalDef &P = m_aPortals[i];
		if(P.m_World != World)
			continue;
		if(distance(Pos, vec2(P.m_X, P.m_Y)) <= P.m_Radius)
			return &P;
	}
	return nullptr;
}

bool CPortalManager::CanTravelToWorld(CPlayer *pPlayer, int DestWorld, char *pReason, int ReasonSize) const
{
	if(!pPlayer || !GS() || !Server())
		return false;
	if(DestWorld < 0 || DestWorld >= Server()->GetNumWorlds())
		return false;
	if(DestWorld == Server()->GetClientWorldID(pPlayer->GetCID()))
		return true;

	const CWorldDetail *pDetail = Server()->GetWorldDetail(DestWorld);
	if(!pDetail)
		return true;

	if(pDetail->GetTravelLocked())
	{
		if(pDetail->GetRequiredQuest()[0] && Core() && Core()->QuestManager())
		{
			if(!Core()->QuestManager()->HasTravelUnlock(pPlayer, pDetail->GetRequiredQuest()))
			{
				if(pReason && ReasonSize > 0)
					str_copy(pReason, GS()->Loc(pPlayer->GetCID(), "travel.need_quest", "尚未解锁该世界的传送权限。"), ReasonSize);
				return false;
			}
		}
	}

	(void)pReason;
	(void)ReasonSize;
	return true;
}

bool CPortalManager::TravelDirect(CPlayer *pPlayer, const char *pPortalId)
{
	const SPortalDef *pPortal = FindPortal(pPortalId);
	if(!pPlayer || !pPortal || !Core() || !Core()->WorldManager())
		return false;

	if(pPortal->m_aRequireQuest[0] && Core()->QuestManager() && !Core()->QuestManager()->HasTravelUnlock(pPlayer, pPortal->m_aRequireQuest))
	{
		GS()->SendChatLoc(pPlayer->GetCID(), "travel.need_quest", "尚未解锁该传送门。");
		return false;
	}

	if(pPortal->m_RequireItem > 0 && pPlayer->m_AccData.m_aItems[pPortal->m_RequireItem].m_Num <= 0)
	{
		GS()->SendChatLoc(pPlayer->GetCID(), "travel.need_item", "缺少所需物品，无法传送。");
		return false;
	}

	vec2 DestPos = vec2(pPortal->m_DestX, pPortal->m_DestY);
	return Core()->WorldManager()->ExecuteWithSpawn(pPlayer->GetCID(), pPortal->m_DestWorld, &DestPos, true);
}

void CPortalManager::TryPortalTravel(CPlayer *pPlayer, vec2 Pos)
{
	if(!pPlayer || !GS() || !Server() || !Core() || !Core()->WorldManager())
		return;

	const int CID = pPlayer->GetCID();
	const int World = Server()->GetClientWorldID(CID);
	const SPortalDef *pPortal = FindPortalAt(World, Pos);
	const int Tick = GS()->Server()->Tick();

	if(!pPortal)
	{
		m_aInsidePortal[CID] = -1;
		m_aDwellStart[CID] = 0;
		return;
	}

	int PortalIdx = -1;
	for(int i = 0; i < m_NumPortals; i++)
	{
		if(&m_aPortals[i] == pPortal)
		{
			PortalIdx = i;
			break;
		}
	}

	if(m_aInsidePortal[CID] != PortalIdx)
	{
		m_aInsidePortal[CID] = PortalIdx;
		m_aDwellStart[CID] = Tick;
		GS()->SendChatLoc(CID, "portal.enter", "你 sensing a rift… stand still to cross.");
		return;
	}

	const int DwellTicks = GS()->Server()->TickSpeed();
	if(m_aDwellStart[CID] > 0 && Tick - m_aDwellStart[CID] >= DwellTicks)
	{
		m_aInsidePortal[CID] = -1;
		m_aDwellStart[CID] = 0;
		TravelDirect(pPlayer, pPortal->m_aId);
	}
}
