/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

#include <base/system.h>
#include <base/vmath.h>

#include <generated/protocol.h>

#include "alloc.h"
#include "item_system.h"
#include "zombie_nav.h"

class CTurret;

enum
{
	WEAPON_GAME = -3, // team switching etc
	WEAPON_SELF = -2, // console kill command
	WEAPON_WORLD = -1, // death tiles etc
};

enum EZombType
{
	ZOMB_NONE = 0,
	ZOMB_ZABY = 1,
	ZOMB_ZOOKER,
	ZOMB_ZABER,
};

struct CTeeInfos
{
	char m_aaSkinPartNames[NUM_SKINPARTS][MAX_SKIN_ARRAY_SIZE];
	int m_aUseCustomColors[NUM_SKINPARTS];
	int m_aSkinPartColors[NUM_SKINPARTS];
};

// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientID, bool Dummy, bool AsSpec = false);
	~CPlayer();

	void Init(int CID);

	void TryRespawn();
	void Respawn();
	void SetTeam(int Team, bool DoChatMsg = true);
	int GetTeam() const { return m_Team; }
	int GetCID() const { return m_ClientID; }
	bool IsDummy() const { return m_Dummy; }

	int64 GetAccountId() const { return m_AccountId; }
	void SetAccountId(int64 Id) { m_AccountId = Id; }
	void ResetAccData();
	void ClearAccount();

	void InitZombie(int Zomb);
	int GetZomb() const { return m_Zomb; }
	bool PressTab() const;

	int GetHolding(int ItemType) const { return m_AccData.m_Holding[ItemType]; }
	const char *GetExtra(int ItemType) const;
	const char *GetExtraForItem(int ItemID) const;

	SAccSyncData m_AccData;

	void Tick();
	void PostTick();
	void Snap(int SnappingClient);

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect();

	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();
	bool CreateTurret();
	void DestroyTurret();

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int GetSpectatorID() const { return m_SpectatorID; }
	bool SetSpectatorID(int SpecMode, int SpectatorID);
	bool m_DeadSpecMode;
	bool DeadCanFollow(CPlayer *pPlayer) const;
	void UpdateDeadSpecMode();

	bool m_IsReadyToEnter;
	bool m_IsReadyToPlay;

	bool m_RespawnDisabled;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCallTick;
	int m_LastVoteTryTick;
	int m_LastChatTeamTick;
	int m_LastSetTeamTick;
	int m_LastSetSpectatorModeTick;
	int m_LastChangeInfoTick;
	int m_LastEmoteTick;
	int m_LastKillTick;
	int m_LastReadyChangeTick;

	// player skin
	CTeeInfos m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	int m_LastActionTick;
	int m_TeamChangeTick;

	int m_InactivityTickCounter;

	int64 m_AccountId;
	int m_Zomb;
	int m_ZombAiLowSpeedTicks;
	int m_ZombAiJumpCooldown;
	int m_ZombAiLastMoveDir;
	int m_ZombAiHookCooldown;
	int m_ZombAiHumanScanTick;
	vec2 m_ZombAiCachedHumanPos;
	float m_ZombAiCachedHumanDist;
	bool m_ZombAiCachedHasHuman;
	int m_ZombAiCachedHumanCid;
	vec2 m_ZombAiPathGoal;
	bool m_ZombAiMcJumpTried;
	CNetObj_PlayerInput m_ZombAiLastInp;

	short m_aZombNavTx[ZOMB_NAV_PATH_CAP];
	short m_aZombNavTy[ZOMB_NAV_PATH_CAP];
	int m_ZombNavLen;
	int m_ZombNavIndex;
	int m_ZombNavNextRebuildTick;
	short m_ZombNavCachedGoalTX;
	short m_ZombNavCachedGoalTY;

	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

private:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;
	bool m_Dummy;

	CTurret *m_pTurret;

	// used for spectator mode
	int m_SpecMode;
	int m_SpectatorID;
	bool m_ActiveSpecSwitch;
};

#endif
