/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTROLLER_H
#define GAME_SERVER_GAMECONTROLLER_H

#include <base/tl/array.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/commands.h>

#include <generated/protocol.h>

class CTowerMain;
class CPlayer;
class CSpiderBoss;

enum
{
	NUM_TD_ZOMB = 13,
	TD_MAX_ACTIVE_ZOMBIES = MAX_ZOMBIE_CLIENTS,
	TD_REMOVE_QUEUE = MAX_CLIENTS,
	TD_DIFF_EASY = 0,
	TD_DIFF_NORMAL = 1,
	TD_DIFF_HARD = 2,
	NUM_TD_DIFF = 3,
};

/*
	Class: Game Controller
		Controls the main game logic. Keeping track of team and player score,
		winning conditions and specific game logic.
*/
class CGameController
{
	class CGameContext *m_pGameServer;
	class CConfig *m_pConfig;
	class IServer *m_pServer;

	// activity
	void DoActivityCheck();
	bool GetPlayersReadyState(int WithoutID = -1);
	void SetPlayersReadyState(bool ReadyState);

	int ClampTeam(int Team) const;

	// TeeDefense (private state)
	int m_TdWarmup;
	int m_TdGameOverTick;
	int m_TdZombStart;
	int m_TdWave;
	int m_TdZombie[NUM_TD_ZOMB];
	int m_TdZombLeft;
	int m_aTdDummyRemove[TD_REMOVE_QUEUE];
	int m_TdDummyRemoveLen;
	CTowerMain *m_pTower;
	class CZombieBot *m_apZombieBots[MAX_CLIENTS];
	class CSpiderBoss *m_pSpiderBoss;
	bool m_TdBossWave;
	bool m_TdSpiderBossPending;

	int m_TdPendingZomb;
	int m_TdDifficulty;

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
	void TdDoZombMessage(int Which);
	void TdSetWaveAlg(int Modulus, int WaveThird, int Wave);
	static int TdZombieBaseHealth(int Wave);
	int TdGetZombieOrder(int WaveThird);
	void TdBroadcastGameInfo();
	void TdBroadcastBossHealth();
	void TdRunZombieBrain(class CPlayer *pP);
	void TickLoginReminders();
	void TdClearZombieBot(int ClientID);
	void TdDestroySpiderBoss();
	bool TdHasSpiderBossPlayer() const;
	vec2 TdGetZombieRallyPos() const;

protected:
	// spawn
	struct CSpawnEval
	{
		CSpawnEval()
		{
			m_Got = false;
			m_FriendlyTeam = -1;
			m_Pos = vec2(100, 100);
		}

		vec2 m_Pos;
		bool m_Got;
		bool m_RandomSpawn;
		int m_FriendlyTeam;
		float m_Score;
	};
	array<vec2> m_alSpawnPoints[3];

	float EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos) const;
	void EvaluateSpawnType(CSpawnEval *pEval, int Type) const;

	CGameContext *GameServer() const { return m_pGameServer; }
	CConfig *Config() const { return m_pConfig; }
	IServer *Server() const { return m_pServer; }

	// game
	int m_GameStartTick;
	int m_RealPlayerNum;

	void SendGameInfo(int ClientID);

public:
	CGameController(class CGameContext *pGameServer);
	virtual ~CGameController();

	void PreTick();
	int GetDummyTeam() const;
	void OnBotPlayerCreated(class CPlayer *pPlayer);
	vec2 TdGetZombieMarchGoal() const;
	int GetTdWave() const { return m_TdWave; }
	CTowerMain *GetTower() const { return m_pTower; }
	CSpiderBoss *GetSpiderBoss() const { return m_pSpiderBoss; }
	bool IsSpiderBossCore(class CCharacter *pChr) const;

	// event
	/*
		Function: on_CCharacter_death
			Called when a CCharacter in the world dies.

		Arguments:
			victim - The CCharacter that died.
			killer - The player that killed it.
			weapon - What weapon that killed it. Can be -1 for undefined
				weapon when switching team or player suicides.
	*/
	int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	/*
		Function: on_CCharacter_spawn
			Called when a CCharacter spawns into the game world.

		Arguments:
			chr - The CCharacter that was spawned.
	*/
	void OnCharacterSpawn(class CCharacter *pChr);

	/*
		Function: on_entity
			Called when the map is loaded to process an entity
			in the map.

		Arguments:
			index - Entity index.
			pos - Where the entity is located in the world.

		Returns:
			bool?
	*/
	bool OnEntity(int Index, vec2 Pos);
	bool OnExtraTile(int Index, vec2 Pos);

	void OnPlayerConnect(class CPlayer *pPlayer);
	void OnPlayerDisconnect(class CPlayer *pPlayer);
	void OnPlayerInfoChange(class CPlayer *pPlayer);
	void OnPlayerReadyChange(class CPlayer *pPlayer);

	// general
	void Snap(int SnappingClient);
	void Tick();

	// info
	bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const;
	bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const;
	int GetPlayerCheckTeam(class CPlayer *pPlayer) const;

	bool CanSpawn(int Team, vec2 *pPos) const;
	vec2 TdSnapSpawnToGround(vec2 Pos, float PhysSize = 28.0f) const;
	bool GetStartRespawnState() const;

	// team
	bool CanJoinTeam(int Team, int NotThisID) const;
	bool CanChangeTeam(class CPlayer *pPplayer, int JoinTeam) const;

	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true);

	int GetRealPlayerNum() const { return m_RealPlayerNum; }
	int GetStartTeam();

	void HandleCharacterTiles(class CCharacter *pChr, vec2 LastPos, vec2 NewPos);
	static void Com_About(IConsole::IResult *pResult, void *pContext);
	static void Com_Community(IConsole::IResult *pResult, void *pContext);
	void RegisterChatCommands(CCommandManager *pManager);

	bool CanCharacterPickup(class CCharacter *pChr) const;
	bool CanCharacterWeaponFullAuto(class CCharacter *pChr, int Weapon);

	// return: Reload timer
	int OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon);

	void TdSetWave(int Wave);
	void TdSetTowerHealth(int Health);
	int GetTdDifficulty() const { return m_TdDifficulty; }
	float TdDifficultyAiMul() const;
	bool TdCanChangeDifficulty() const;
	bool TdSetDifficulty(int Difficulty);
	int TdGetDifficultyTowerMaxHealth() const;

	void NotifyPlayerConnected(class CPlayer *pPlayer);

	static void ConTdSetWave(IConsole::IResult *pResult, void *pUser);
	static void ConTdSetTowerHealth(IConsole::IResult *pResult, void *pUser);
	static void ConTdSetDifficulty(IConsole::IResult *pResult, void *pUser);
	static void ConTdSkipWarmup(IConsole::IResult *pResult, void *pUser);
	static void RegisterTeeDefenseConsoleCommands(CGameContext *pCtx);
};

#endif
