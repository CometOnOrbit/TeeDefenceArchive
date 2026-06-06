#include <base/system.h>

#include <engine/shared/jsonparser.h>
#include <engine/shared/protocol.h>

#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "travel_manager.h"

void CTravelManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	m_NumWorlds = 0;
	if(!Storage())
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("maps/worlds.json", Storage());
	if(!pRoot || pRoot->type != json_object)
	{
		dbg_msg("worlds", "maps/worlds.json: %s", Parser.Error());
		return;
	}

	const json_value &Arr = (*pRoot)["worlds"];
	if(Arr.type != json_array)
		return;

	for(unsigned i = 0; i < Arr.u.array.length && m_NumWorlds < 32; i++)
	{
		const json_value &El = Arr[(int)i];
		if(El.type != json_object)
			continue;
		SWorldEntry &W = m_aWorlds[m_NumWorlds];
		mem_zero(&W, sizeof(W));
		if(El["map"].type == json_string)
			str_copy(W.m_aMap, El["map"].u.string.ptr, sizeof(W.m_aMap));
		else
			W.m_aMap[0] = 0;
		if(El["title"].type == json_string)
		{
			str_copy(W.m_aTitle, El["title"].u.string.ptr, sizeof(W.m_aTitle));
			W.m_CustomTitle = true;
		}
		else
		{
			str_format(W.m_aTitle, sizeof(W.m_aTitle), "World %d", m_NumWorlds + 1);
			W.m_CustomTitle = false;
		}
		if(W.m_aMap[0])
			m_NumWorlds++;
	}
}

const char *CTravelManager::WorldTitle(int Index) const
{
	if(Index < 0 || Index >= m_NumWorlds)
		return "";
	return m_aWorlds[Index].m_aTitle;
}

void CTravelManager::FormatWorldTitle(int ClientID, int Index, char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;
	pBuf[0] = 0;
	if(Index < 0 || Index >= m_NumWorlds || !GS())
		return;
	const SWorldEntry &W = m_aWorlds[Index];
	if(W.m_CustomTitle)
		str_copy(pBuf, W.m_aTitle, BufSize);
	else
		GS()->LocFormat(pBuf, BufSize, ClientID, "worlds.default_title", "World %d", Index + 1);
}

void CTravelManager::AddVotes(int ClientID)
{
	if(!GS())
		return;
	for(int i = 0; i < m_NumWorlds; i++)
	{
		char aCmd[96];
		char aTitle[128];
		FormatWorldTitle(ClientID, i, aTitle, sizeof(aTitle));
		str_format(aCmd, sizeof(aCmd), "ccv_menutravel %d", i);
		GS()->AddVote(aTitle, aCmd, ClientID);
	}
	if(m_NumWorlds == 0)
	{
		CPlayer *pP = (ClientID >= 0 && ClientID < MAX_CLIENTS) ? GS()->m_apPlayers[ClientID] : nullptr;
		const char *pText = GS()->Loc(ClientID, "worlds.empty", "(no worlds in maps/worlds.json)");
		GS()->AddVote_TextLine(pText);
	}
}

bool CTravelManager::Execute(int ClientID, int WorldIndex)
{
	if(!GS() || WorldIndex < 0 || WorldIndex >= m_NumWorlds)
		return false;
	char aCmd[192];
	str_format(aCmd, sizeof(aCmd), "sv_map %s", m_aWorlds[WorldIndex].m_aMap);
	GS()->Console()->ExecuteLine(aCmd);
	char aTitle[128];
	FormatWorldTitle(ClientID, WorldIndex, aTitle, sizeof(aTitle));
	GS()->SendChatLocF(ClientID, "travel.to", "Traveling to %s", aTitle);
	return true;
}
