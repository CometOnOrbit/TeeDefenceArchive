#ifndef GAME_SERVER_COMPONENT_QUEST_DIRECTION_NAVIGATOR_H
#define GAME_SERVER_COMPONENT_QUEST_DIRECTION_NAVIGATOR_H

#include <game/server/entity.h>

class CEntityDirNavigator : public CEntity
{
	int m_ClientID;
	int m_Type;
	int m_Subtype;
	float m_Clipped;
	vec2 m_PosTo;
	vec2 m_Start;

public:
	CEntityDirNavigator(CGameContext *pGameServer, vec2 Start, vec2 End, int ClientID, int WorldID = -1, int Type = 0, int Subtype = 0, float Clipped = 32.f);
	~CEntityDirNavigator() override;

	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif
