/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTROLLER_H
#define GAME_SERVER_GAMECONTROLLER_H

#include <base/tl/array.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/commands.h>

#include <generated/protocol.h>

class CPlayer;
class CFlag;

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
	bool GetPlayersReadyState(int WithoutID = -1);
	void SetPlayersReadyState(bool ReadyState);

	int ClampTeam(int Team) const;



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

	void DoActivityCheck();
	void TickLoginReminders();

	// game
	int m_GameStartTick;
	int m_RealPlayerNum;

public:
	virtual void SendGameInfo(int ClientID);
	CGameController(class CGameContext *pGameServer);
	virtual ~CGameController();

	virtual void PreTick();
	int GetDummyTeam() const;
	virtual void OnBotPlayerCreated(class CPlayer *pPlayer);

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
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
	virtual void OnFlagReturn(class CFlag *pFlag);
	/*
		Function: on_CCharacter_spawn
			Called when a CCharacter spawns into the game world.

		Arguments:
			chr - The CCharacter that was spawned.
	*/
	virtual void OnCharacterSpawn(class CCharacter *pChr);

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
	virtual bool OnEntity(int Index, vec2 Pos);
	virtual void OnEntitySwitch(int EntityIndex, vec2 Pos, int Flags, int Number);
	bool OnExtraTile(int Index, vec2 Pos);

	// Returns true when damage was fully handled by the mode (skip default TakeDamage).
	virtual bool OnCharacterTakeDamage(class CCharacter *pChr, vec2 &Force, int &Dmg, int From, int Weapon);

	virtual void OnPlayerConnect(class CPlayer *pPlayer);
	virtual void OnPlayerDisconnect(class CPlayer *pPlayer);
	void OnPlayerInfoChange(class CPlayer *pPlayer);
	void OnPlayerReadyChange(class CPlayer *pPlayer);

	// general
	virtual void Snap(int SnappingClient);
	virtual void Tick();

	// info
	virtual bool IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const;
	virtual bool IsFriendlyTeamFire(int Team1, int Team2, int Damage) const;
	int GetPlayerCheckTeam(class CPlayer *pPlayer) const;

	bool CanSpawn(int Team, vec2 *pPos) const;
	vec2 TdSnapSpawnToGround(vec2 Pos, float PhysSize = 28.0f) const;
	bool GetStartRespawnState() const;

	// team
	bool CanJoinTeam(int Team, int NotThisID) const;
	virtual bool CanChangeTeam(class CPlayer *pPplayer, int JoinTeam) const;

	void DoTeamChange(class CPlayer *pPlayer, int Team, bool DoChatMsg = true);

	int GetRealPlayerNum() const { return m_RealPlayerNum; }
	int GetStartTeam();

	virtual void HandleCharacterTiles(class CCharacter *pChr, vec2 LastPos, vec2 NewPos);
	static void Com_About(IConsole::IResult *pResult, void *pContext);
	static void Com_Community(IConsole::IResult *pResult, void *pContext);
	void RegisterChatCommands(CCommandManager *pManager);

	bool CanCharacterPickup(class CCharacter *pChr) const;
	bool CanCharacterWeaponFullAuto(class CCharacter *pChr, int Weapon);

	// return: Reload timer
	virtual int OnCharacterFireWeapon(class CCharacter *pChr, vec2 Direction, int Weapon);

	void NotifyPlayerConnected(class CPlayer *pPlayer);
};

#endif
