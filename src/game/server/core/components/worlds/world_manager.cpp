#include <base/system.h>

#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <engine/shared/world_detail.h>
#include <game/voting.h>

#include "world_manager.h"

void CWorldManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
}

int CWorldManager::NumWorlds() const
{
	return Server() ? Server()->GetNumWorlds() : 0;
}

const char *CWorldManager::WorldTitle(int Index) const
{
	if(Index < 0 || Index >= NumWorlds() || !Server())
		return "";
	const char *pName = Server()->GetWorldName(Index);
	return pName ? pName : "";
}

void CWorldManager::FormatWorldTitle(int ClientID, int Index, char *pBuf, int BufSize) const
{
	if(!pBuf || BufSize <= 0)
		return;
	pBuf[0] = 0;
	if(Index < 0 || Index >= NumWorlds() || !GS())
		return;
	const char *pName = Server()->GetWorldName(Index);
	if(pName && pName[0])
		str_copy(pBuf, pName, BufSize);
	else
		GS()->LocFormat(pBuf, BufSize, ClientID, "worlds.default_title", "World %d", Index + 1);
}

void CWorldManager::AddVotes(int ClientID)
{
	if(!GS())
		return;
	const int Num = NumWorlds();
	for(int i = 0; i < Num; i++)
	{
		char aCmd[96];
		char aTitle[128];
		char aLine[VOTE_DESC_LENGTH];
		FormatWorldTitle(ClientID, i, aTitle, sizeof(aTitle));
		const int PlayerNum = Server()->GetNumPlayersInWorld(i);
		const char *pModeKey = "worlds.mode.defence";
		const char *pModeFallback = "defence";
		if(const CWorldDetail *pDetail = Server()->GetWorldDetail(i))
		{
			if(pDetail->GetType() == WorldType::Hub)
			{
				pModeKey = "worlds.mode.hub";
				pModeFallback = "hub";
			}
			else if(pDetail->GetType() == WorldType::PvP)
			{
				pModeKey = "worlds.mode.pvp";
				pModeFallback = "pvp";
			}
		}
		char aMode[32];
		GS()->LocFormat(aMode, sizeof(aMode), ClientID, pModeKey, pModeFallback);
		GS()->LocFormat(aLine, sizeof(aLine), ClientID, "worlds.entry", "%s (%d) [%s]", aTitle, PlayerNum, aMode);
		str_format(aCmd, sizeof(aCmd), "ccv_menutravel %d", i);
		GS()->AddVote(aLine, aCmd, ClientID);
	}
	if(Num == 0)
	{
		const char *pText = GS()->Loc(ClientID, "worlds.empty", "(no worlds in maps/worlds.json)");
		GS()->AddVote_TextLine(pText);
	}
}

bool CWorldManager::Execute(int ClientID, int WorldIndex)
{
	if(!GS() || WorldIndex < 0 || WorldIndex >= NumWorlds())
		return false;
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GS()->m_apPlayers[ClientID])
		return false;

	CPlayer *pPlayer = GS()->m_apPlayers[ClientID];
	if(pPlayer->IsDummy())
		return false;
	if(WorldIndex == Server()->GetClientWorldID(ClientID))
		return true;

	char aTitle[128];
	FormatWorldTitle(ClientID, WorldIndex, aTitle, sizeof(aTitle));
	pPlayer->ChangeWorld(WorldIndex);
	GS()->SendChatLocF(ClientID, "travel.to", "Traveling to %s", aTitle);
	return true;
}
