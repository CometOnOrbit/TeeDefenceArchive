#ifndef GAME_SERVER_CORE_COMPONENTS_META_ACHIEVEMENT_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_META_ACHIEVEMENT_MANAGER_H

#include <game/server/core/components/meta/player_meta.h>
#include <game/server/core/tworld_component.h>
#include <game/server/core/tools/event_listener.h>

class CCommandManager;
class CPlayer;

struct SAchievementDef
{
	char m_aId[32];
	char m_aTitle[48];
	char m_aDesc[96];
	char m_aType[16];
	int m_Count;
	int m_RewardItem;
	int m_RewardNum;
};

class CAchievementManager : public TWorldComponent, public IGameEventListener
{
	SAchievementDef m_aDefs[MAX_META_ACHIEVEMENTS];
	int m_NumDefs;

public:
	CAchievementManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnShutdown() override;
	void OnPlayerLogin(CPlayer *pPlayer) override;
	void OnClientReset(int ClientID) override;

	void OnPlayerKill(CPlayer *pKiller, int ZombId) override;
	void OnPlayerCraft(CPlayer *pPlayer, int ItemId, int Amount) override;
	void OnPlayerMine(CPlayer *pPlayer, int MatId) override;
	void OnWaveComplete(int Wave) override;

	void RegisterVoteCommands(CCommandManager *pMgr);
	bool OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs);
	void BuildAchievementsPage(int ClientID);

	void RequestPersist(int ClientID);

private:
	void LoadDefs();
	void BumpProgress(CPlayer *pPlayer, const char *pType, int Amount);
	void TryComplete(CPlayer *pPlayer, int Idx);
	void GrantReward(CPlayer *pPlayer, const SAchievementDef &Def);
};

#endif
