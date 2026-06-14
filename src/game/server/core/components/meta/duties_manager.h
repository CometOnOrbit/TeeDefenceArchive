#ifndef GAME_SERVER_CORE_COMPONENTS_META_DUTIES_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_META_DUTIES_MANAGER_H

#include <game/server/core/components/meta/player_meta.h>
#include <game/server/core/tworld_component.h>
#include <game/server/core/tools/event_listener.h>

class CCommandManager;
class CPlayer;

struct SDutyDef
{
	char m_aId[32];
	char m_aTitle[48];
	char m_aDesc[96];
	char m_aType[16];
	int m_Count;
	int m_RewardItem;
	int m_RewardNum;
	int m_Tier;
};

class CDutiesManager : public TWorldComponent, public IGameEventListener
{
	SDutyDef m_aDefs[MAX_META_DUTIES];
	int m_NumDefs;

public:
	CDutiesManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnShutdown() override;
	void OnPlayerLogin(CPlayer *pPlayer) override;
	void OnClientReset(int ClientID) override;

	void OnPlayerKill(CPlayer *pKiller, int ZombId) override;
	void OnPlayerCraft(CPlayer *pPlayer, int ItemId, int Amount) override;
	void OnWaveComplete(int Wave) override;

	void RegisterVoteCommands(CCommandManager *pMgr);
	bool OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs);
	void BuildDutiesPage(int ClientID);
	bool TryClaim(CPlayer *pPlayer, int Idx);

	void RequestPersist(int ClientID);
	void OnPlayerMine(CPlayer *pPlayer, int MatId) override;

private:
	void LoadDefs();
	void BumpProgress(CPlayer *pPlayer, const char *pType, int Amount);
};

#endif
