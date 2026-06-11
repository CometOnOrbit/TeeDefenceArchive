#ifndef GAME_SERVER_COMPONENT_WORLD_MANAGER_H
#define GAME_SERVER_COMPONENT_WORLD_MANAGER_H

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
	bool Execute(int ClientID, int WorldIndex);
};

#endif
