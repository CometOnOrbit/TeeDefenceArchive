#ifndef GAME_SERVER_COMPONENT_WORLD_MANAGER_H
#define GAME_SERVER_COMPONENT_WORLD_MANAGER_H

#include <base/vmath.h>

#include <game/server/core/tworld_component.h>

class CWorldManager : public TWorldComponent
{
protected:
	void OnInitWorld(const char *pWhereLocalWorld) override;

public:
	int NumWorlds() const;
	const char *WorldTitle(int Index) const;
	void FormatWorldTitle(int ClientID, int Index, char *pBuf, int BufSize) const;
	void AddVotes(int ClientID);
	void AddDefenceMiniGameVotes(int ClientID);
	bool Execute(int ClientID, int WorldIndex);
	bool ExecuteWithSpawn(int ClientID, int WorldIndex, vec2 *pSpawnPos, bool AllowGatedTravel = false);

	// Find a position on a given world, used by quest/NPC systems for coordination
	// If the target world matches the current, returns Pos directly.
	// Otherwise returns the position for teleportation purposes.
	vec2 FindPosition(int WorldID, vec2 Pos) const;

	// Notify the player of newly unlocked worlds after leveling up
	void NotifyUnlockedZonesByLeveling(CPlayer *pPlayer) const;

	// Dynamic arena world management (for guild matches)
	int CreateArenaWorld(const char *pName, const char *pMode, const char *pMapPath);
	bool DestroyArenaWorld(int WorldID);
	bool IsArenaWorld(int WorldID) const;
};

#endif
