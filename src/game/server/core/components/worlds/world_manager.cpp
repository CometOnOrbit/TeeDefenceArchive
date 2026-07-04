#include <base/system.h>

#include <engine/shared/config.h>
#include <engine/server/multi_worlds.h>

#include <game/server/core/components/localization/localization_manager.h>
#include <game/server/core/components/quests/quest_manager.h>
#include <game/server/core/components/worlds/portal_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <engine/shared/world_detail.h>
#include <game/voting.h>

#include "world_manager.h"

// Track arena world IDs for IsArenaWorld check
static bool gs_aArenaWorldIDs[ENGINE_MAX_WORLDS] = {false};

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
			else if(pDetail->GetType() == WorldType::Story)
			{
				pModeKey = "worlds.mode.story";
				pModeFallback = "story";
			}
			else if(pDetail->GetType() == WorldType::RPG)
			{
				pModeKey = "worlds.mode.frpg";
				pModeFallback = "F|RPG";
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

bool CWorldManager::ExecuteWithSpawn(int ClientID, int WorldIndex, vec2 *pSpawnPos, bool AllowGatedTravel)
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

	if(!AllowGatedTravel && GS()->Config() && !GS()->Config()->m_SvFreeWorldTravel)
	{
		GS()->SendChatLoc(ClientID, "travel.locked", "无法自由切换世界，请使用传送门或任务入口。");
		return false;
	}

	// Level gate: check if player meets the world's required level
	const CWorldDetail *pDetail = Server()->GetWorldDetail(WorldIndex);
	if(pDetail && pDetail->GetRequiredLevel() > 0)
	{
		const int PlayerLevel = pPlayer->GetStat(AttributeIdentifier::Level);
		if(PlayerLevel < pDetail->GetRequiredLevel())
		{
			char aTitle[64];
			GS()->LocFormat(aTitle, sizeof(aTitle), ClientID, "worlds.level_gate",
				"需要等级 %d 才能进入该世界（当前 %d）",
				pDetail->GetRequiredLevel(), PlayerLevel);
			GS()->SendChatTo(ClientID, aTitle);
			return false;
		}
	}

	char aReason[128];
	if(Core() && Core()->PortalManager() && !Core()->PortalManager()->CanTravelToWorld(pPlayer, WorldIndex, aReason, sizeof(aReason)))
	{
		GS()->SendChatLoc(ClientID, "travel.need_quest", aReason[0] ? aReason : "尚未解锁该世界。");
		return false;
	}

	char aTitle[128];
	FormatWorldTitle(ClientID, WorldIndex, aTitle, sizeof(aTitle));
	pPlayer->ChangeWorld(WorldIndex, pSpawnPos);
	GS()->SendChatLocF(ClientID, "travel.to", "Traveling to %s", aTitle);
	return true;
}

bool CWorldManager::Execute(int ClientID, int WorldIndex)
{
	return ExecuteWithSpawn(ClientID, WorldIndex, nullptr, false);
}

vec2 CWorldManager::FindPosition(int WorldID, vec2 Pos) const
{
	// Utility: return the world-relative position for targeting
	// The caller handles teleportation via ExecuteWithSpawn()
	(void)WorldID;
	return Pos;
}

void CWorldManager::NotifyUnlockedZonesByLeveling(CPlayer *pPlayer) const
{
	if(!pPlayer || !Server() || !GS())
		return;

	const int PlayerLevel = pPlayer->GetStat(AttributeIdentifier::Level);
	const int Num = NumWorlds();
	int NewlyUnlocked = 0;

	for(int i = 0; i < Num; i++)
	{
		const CWorldDetail *pDetail = Server()->GetWorldDetail(i);
		if(!pDetail || pDetail->GetRequiredLevel() <= 0)
			continue;

		if(pDetail->GetRequiredLevel() == PlayerLevel)
		{
			const char *pName = Server()->GetWorldName(i);
			if(pName && pName[0])
			{
				GS()->SendChatLocF(pPlayer->GetCID(), "worlds.unlocked",
					"🗺️ 新区域已开放：%s（要求等级 %d）",
					pName, pDetail->GetRequiredLevel());
				NewlyUnlocked++;
			}
		}
	}

	if(NewlyUnlocked == 0)
	{
		// Check next-level thresholds
		for(int i = 0; i < Num; i++)
		{
			const CWorldDetail *pDetail = Server()->GetWorldDetail(i);
			if(!pDetail || pDetail->GetRequiredLevel() <= 0)
				continue;

			if(pDetail->GetRequiredLevel() == PlayerLevel + 1)
			{
				const char *pName = Server()->GetWorldName(i);
				if(pName && pName[0])
				{
					GS()->SendChatLocF(pPlayer->GetCID(), "worlds.next_unlock",
						"🔒 再升一级即可前往：%s", pName);
				}
			}
		}
	}
}

int CWorldManager::CreateArenaWorld(const char *pName, const char *pMode, const char *pMapPath)
{
	if(!Server() || !GS() || !Server()->MultiWorlds())
		return -1;

	IKernel *pKernel = GS()->GetKernel();
	IStorage *pStorage = GS()->Storage();
	if(!pKernel || !pStorage)
		return -1;

	// Determine WorldType from mode
	WorldType Type = WorldType::PvP;
	if(pMode && pMode[0])
	{
		if(str_comp_nocase(pMode, "hub") == 0)
			Type = WorldType::Hub;
		else if(str_comp_nocase(pMode, "story") == 0)
			Type = WorldType::Story;
		else if(str_comp_nocase(pMode, "rpg") == 0 || str_comp_nocase(pMode, "frpg") == 0 ||
			str_comp_nocase(pMode, "f|rpg") == 0)
			Type = WorldType::RPG;
	}

	// Arena worlds are travel_locked to prevent players from entering via normal travel
	const CWorldDetail Detail(Type, 0, 0, 0, true, "");

	int WorldID = Server()->MultiWorlds()->AddWorld(pKernel, pStorage, pName, pMapPath, Detail);
	if(WorldID >= 0 && WorldID < ENGINE_MAX_WORLDS)
		gs_aArenaWorldIDs[WorldID] = true;

	return WorldID;
}

bool CWorldManager::DestroyArenaWorld(int WorldID)
{
	if(!Server() || !Server()->MultiWorlds())
		return false;
	if(WorldID < 0 || WorldID >= ENGINE_MAX_WORLDS)
		return false;

	// Evict all players from this arena world back to world 0
	for(int cid = 0; cid < MAX_CLIENTS; cid++)
	{
		if(Server()->GetClientWorldID(cid) == WorldID)
		{
			vec2 Origin(0.0f, 0.0f);
			Server()->ChangeWorld(cid, 0);
		}
	}

	bool Result = Server()->MultiWorlds()->RemoveWorld(WorldID);
	if(WorldID >= 0 && WorldID < ENGINE_MAX_WORLDS)
		gs_aArenaWorldIDs[WorldID] = false;

	return Result;
}

bool CWorldManager::IsArenaWorld(int WorldID) const
{
	if(WorldID < 0 || WorldID >= ENGINE_MAX_WORLDS)
		return false;
	return gs_aArenaWorldIDs[WorldID];
}
