#ifndef GAME_SERVER_WORLDMODES_DEFENCE_H
#define GAME_SERVER_WORLDMODES_DEFENCE_H

#include <game/server/gamecontroller.h>

class CTowerMain;
class CSpiderBoss;
class CZombieAI;

enum
{
	NUM_TD_ZOMB = 16,
	TD_MAX_ACTIVE_ZOMBIES = MAX_ZOMBIE_CLIENTS,
	TD_REMOVE_QUEUE = MAX_CLIENTS,
	TD_DIFF_EASY = 0,
	TD_DIFF_NORMAL = 1,
	TD_DIFF_HARD = 2,
	NUM_TD_DIFF = 3,
};

class CGameControllerDefence : public CGameController
{
	// TeeDefense state
	int m_TdWarmup;
	int m_TdGameOverTick;
	int m_TdZombStart;
	int m_TdWave;
	int m_TdZombie[NUM_TD_ZOMB];
	int m_TdZombLeft;
	int m_aTdDummyRemove[TD_REMOVE_QUEUE];
	int m_TdDummyRemoveLen;
	CTowerMain *m_pTower;
	class CZombieAI *m_apZombieAIs[MAX_CLIENTS];
	CSpiderBoss *m_pSpiderBoss;
	bool m_TdBossWave;
	bool m_TdSpiderBossPending;
	int m_TdPendingZomb;
	int m_TdDifficulty;
	bool m_TdMMOActive;

	float TdDifficultyZombieMul() const;
	float TdDifficultyHealthMul() const;
	float TdDifficultyTowerMul() const;
	void TdApplyDifficultyToZombieCounts();
	void TdRefreshTowerMaxHealth();

	void TdResetPendingRemoves();
	void TdDoWarmup(int Seconds);
	bool TdSkipWarmup();
	void TdStartRound();
	void TdEndRound();
	void TdDoWincheck();
	void TdStartWave(int Wave);
	void TdCheckZombie();
	void TdTrySpawnSpiderBoss();
	int TdRandZomb();
	int TdCountZombiePopulation() const;
	bool TdIsWaveCleared() const;
	bool TdEndWave();
	void TdDoZombMessage(int Left);
	void TdSetWaveAlg(int Modulus, int WaveThird, int Wave);
	static int TdZombieBaseHealth(int Wave);
	int TdGetZombieOrder(int WaveThird);
	void TdBroadcastGameInfo();
	void TdBroadcastBossHealth();
	void TdRunZombieBrain(class CPlayer *pP);
	void TdClearZombieAI(int ClientID);
	void TdDestroySpiderBoss();
	bool TdHasSpiderBossPlayer() const;
	vec2 TdGetZombieRallyPos() const;

public:
	CGameControllerDefence(CGameContext *pGameServer);
	virtual ~CGameControllerDefence();

	void PreTick() override;
	void OnBotPlayerCreated(CPlayer *pPlayer) override;
	int OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon) override;
	void OnCharacterSpawn(CCharacter *pChr) override;
	bool OnEntity(int Index, vec2 Pos) override;
	void Tick() override;
	void Snap(int SnappingClient) override;
	void SendGameInfo(int ClientID) override;
	void OnPlayerConnect(CPlayer *pPlayer) override;
	void OnPlayerDisconnect(CPlayer *pPlayer) override;

	int GetTdWave() const { return m_TdWave; }
	CTowerMain *GetTower() const { return m_pTower; }
	CSpiderBoss *GetSpiderBoss() const { return m_pSpiderBoss; }
	bool IsSpiderBossCore(CCharacter *pChr) const;
	void TdPurgeZombieDummies();

	void TdSetWave(int Wave);
	void TdSetTowerHealth(int Health);
	int GetTdDifficulty() const { return m_TdDifficulty; }
	void TdAddZombiePool(int ZombType, int Count);
	class CZombieAI *TdGetZombieAI(int ClientID) const { return m_apZombieAIs[ClientID]; }
	float TdDifficultyAiMul() const;
	bool TdCanChangeDifficulty() const;
	bool TdSetDifficulty(int Difficulty);
	int TdGetDifficultyTowerMaxHealth() const;

	vec2 TdGetZombieMarchGoal() const;

	static void ConTdSetWave(IConsole::IResult *pResult, void *pUser);
	static void ConTdSetTowerHealth(IConsole::IResult *pResult, void *pUser);
	static void ConTdSetDifficulty(IConsole::IResult *pResult, void *pUser);
	static void ConTdSkipWarmup(IConsole::IResult *pResult, void *pUser);
	static void RegisterTeeDefenseConsoleCommands(CGameContext *pCtx);
};

#endif
