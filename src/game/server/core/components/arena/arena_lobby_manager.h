#ifndef GAME_SERVER_CORE_COMPONENTS_ARENA_ARENA_LOBBY_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_ARENA_ARENA_LOBBY_MANAGER_H

#include <base/vmath.h>
#include <engine/console.h>
#include <game/server/core/tworld_component.h>

class CCommandManager;
class CPlayer;

enum EArenaLobbyStatus
{
	LOBBY_WAITING = 0,
	LOBBY_ACTIVE = 1,
	LOBBY_FINISHED = 2,
	LOBBY_CANCELLED = 3
};

struct SArenaLobbyParticipant
{
	int m_ClientID;
	int m_OriginalWorld;
	vec2 m_OriginalPos;
	bool m_Accepted; // host always accepted
};

struct SArenaLobby
{
	int m_HostClientID;
	char m_aMode[16];
	char m_aMap[128];
	int m_TargetScore;
	int m_ExpiryTick; // 60s from lobby creation
	int m_ArenaWorldID;
	int m_Status;
	int m_PaidGold; // for refund
	array<SArenaLobbyParticipant> m_aParticipants;
	array<int> m_aPendingInvitees; // ClientIDs invited but not yet accepted/declined
};

class CArenaLobbyManager : public TWorldComponent
{
public:
	CArenaLobbyManager();

	void OnTick() override;
	void OnConsoleInit() override;
	void OnClientReset(int ClientID) override;
	void OnCharacterSpawn(CPlayer *pPlayer) override;

	void RegisterChatCommands(CCommandManager *pManager);
	void RegisterArenaVoteCommands(CCommandManager *pManager);
	bool OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason) override;
	bool OnVoteMenuPage(int ClientID, int Page) override;

	bool CreateLobby(int HostCID, const char *pMode, const char *pMap);
	bool InvitePlayers(int HostCID, const char *pNames);
	bool AcceptInvite(int ClientID);
	bool EarlyStart(int HostCID);
	bool CancelLobby(int HostCID);
	void ArenaStatus(int ClientID);
	bool SetArenaMode(int HostCID, const char *pMode);
	bool SetArenaMap(int HostCID, const char *pMap);
	bool GetActiveLobbyInfo(int ClientID, char *pMode, int ModeSize, char *pMap, int MapSize, int *pStatus, int *pSecondsLeft) const;

	static void ConArena(IConsole::IResult *pResult, void *pUser);
	static void ConArenaCreate(IConsole::IResult *pResult, void *pUser);
	static void ConArenaInvite(IConsole::IResult *pResult, void *pUser);
	static void ConYes(IConsole::IResult *pResult, void *pUser);
	static void ConArenaStart(IConsole::IResult *pResult, void *pUser);
	static void ConArenaCancel(IConsole::IResult *pResult, void *pUser);
	static void ConArenaStatus(IConsole::IResult *pResult, void *pUser);

private:
	void TryStartLobby(int LobbyIdx, bool ForceEarly = false);
	void CancelLobbyWithRefund(int LobbyIdx, const char *pReason);
	void FinishLobby(int LobbyIdx, int WinnerTeamOrMinusOne);
	void BroadcastLobby(int LobbyIdx, const char *pMsg, int ExcludeCID = -1);
	int FindLobbyByHost(int HostCID) const;
	int FindLobbyByParticipant(int ClientID) const;
	int FindLobbyIndex(int ClientID) const;
	int CountAccepted(int LobbyIdx) const;
	bool IsPlayerBusyForArena(int ClientID, int ExcludeLobbyIdx = -1) const;
	void RemoveClientFromLobbies(int ClientID);
	CArenaLobbyManager *ActiveManager() const;
};

#endif
