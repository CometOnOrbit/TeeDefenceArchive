#ifndef GAME_SERVER_COMPONENT_DEFENCE_BOT_MANAGER_H
#define GAME_SERVER_COMPONENT_DEFENCE_BOT_MANAGER_H

#include <base/vmath.h>
#include <engine/shared/protocol.h>
#include <generated/protocol.h>

#include <game/server/core/tworld_component.h>

class CDefence;

class CDefenceBotManager : public TWorldComponent
{
	CDefence *m_apDefence[MAX_CLIENTS];
	CNetObj_PlayerInput m_aLastInp[MAX_CLIENTS];
	vec2 m_aGoal[MAX_CLIENTS];
	int m_aGoalTick[MAX_CLIENTS];

	void ClearClient(int ClientID);

protected:
	void OnPreInit() override;
	void OnClientReset(int ClientID) override;
	void OnCharacterSpawn(CPlayer *pPlayer) override;

public:
	~CDefenceBotManager() override;

	void TickPlayer(CPlayer *pPlayer);
};

#endif
