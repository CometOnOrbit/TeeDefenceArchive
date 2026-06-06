#ifndef GAME_SERVER_COMPONENT_TRAVEL_MANAGER_H
#define GAME_SERVER_COMPONENT_TRAVEL_MANAGER_H

#include <game/server/core/tworld_component.h>

class CTravelManager : public TWorldComponent
{
	struct SWorldEntry
	{
		char m_aMap[128];
		char m_aTitle[128];
		bool m_CustomTitle;
	};

	SWorldEntry m_aWorlds[32];
	int m_NumWorlds;

protected:
	void OnInitWorld(const char *pWhereLocalWorld) override;

public:
	int NumWorlds() const { return m_NumWorlds; }
	const char *WorldTitle(int Index) const;
	void FormatWorldTitle(int ClientID, int Index, char *pBuf, int BufSize) const;
	void AddVotes(int ClientID);
	bool Execute(int ClientID, int WorldIndex);
};

#endif
