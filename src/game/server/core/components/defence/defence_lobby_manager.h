#ifndef GAME_SERVER_CORE_COMPONENTS_DEFENCE_DEFENCE_LOBBY_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_DEFENCE_DEFENCE_LOBBY_MANAGER_H

#include <base/vmath.h>
#include <engine/console.h>
#include <game/server/core/tworld_component.h>

class CCommandManager;
class CPlayer;

enum EDefenceLobbyStatus
{
	DEFENCE_LOBBY_WAITING = 0,
	DEFENCE_LOBBY_ACTIVE = 1,
	DEFENCE_LOBBY_FINISHED = 2,
	DEFENCE_LOBBY_CANCELLED = 3
};

struct SDefenceLobbyParticipant
{
	int m_ClientID;
	int m_OriginalWorld;
	vec2 m_OriginalPos;
	bool m_Accepted;
};

struct SDefenceLobby
{
	int m_HostClientID;
	int m_DefenceWorldID;
	int m_Difficulty;
	int m_ExpiryTick;
	int m_Status;
	array<SDefenceLobbyParticipant> m_aParticipants;
	array<int> m_aPendingInvitees;
	array<int> m_aPendingApplicants;
};

class CDefenceLobbyManager : public TWorldComponent
{
public:
	CDefenceLobbyManager();

	void OnTick() override;
	void OnConsoleInit() override;
	void OnClientReset(int ClientID) override;

	void RegisterChatCommands(CCommandManager *pManager);
	void RegisterDefenceVoteCommands(CCommandManager *pManager);
	bool OnVoteMenuPage(int ClientID, int Page) override;

	bool CreateLobby(int HostCID, int WorldIndex, int Difficulty);
	bool InvitePlayers(int HostCID, const char *pNames);
	bool AcceptInvite(int ClientID);
	bool ApplyToLobby(int ClientID, int LobbyIdx);
	bool AcceptApplicant(int HostCID, int ApplicantCID);
	bool AcceptAllApplicants(int HostCID);
	bool RejectApplicant(int HostCID, int ApplicantCID);
	bool EarlyStart(int HostCID);
	bool CancelLobby(int HostCID);
	void LobbyStatus(int ClientID);
	bool SetDraftWorld(int ClientID, int WorldIndex);
	bool SetDraftDifficulty(int ClientID, int Difficulty);
	bool GetActiveLobbyInfo(int ClientID, int *pWorldIndex, int *pDifficulty, int *pStatus, int *pSecondsLeft) const;

	static void ConDefence(IConsole::IResult *pResult, void *pUser);
	static void ConDefenceCreate(IConsole::IResult *pResult, void *pUser);
	static void ConDefenceInvite(IConsole::IResult *pResult, void *pUser);
	static void ConDefenceApply(IConsole::IResult *pResult, void *pUser);
	static void ConDefenceYes(IConsole::IResult *pResult, void *pUser);
	static void ConDefenceStart(IConsole::IResult *pResult, void *pUser);
	static void ConDefenceCancel(IConsole::IResult *pResult, void *pUser);
	static void ConDefenceStatus(IConsole::IResult *pResult, void *pUser);

	static bool TryAcceptPendingInvite(int ClientID);

private:
	void TryStartLobby(int LobbyIdx, bool ForceEarly = false);
	void CancelLobbyInternal(int LobbyIdx, const char *pReason);
	void FinishLobby(int LobbyIdx);
	void BroadcastLobby(int LobbyIdx, const char *pMsg, int ExcludeCID = -1);
	int FindLobbyByHost(int HostCID) const;
	int FindLobbyByParticipant(int ClientID) const;
	int FindLobbyIndex(int ClientID) const;
	int CountAccepted(int LobbyIdx) const;
	bool IsPlayerBusy(int ClientID, int ExcludeLobbyIdx = -1) const;
	bool IsDefenceWorldIndex(int WorldIndex) const;
	void RemoveClientFromLobbies(int ClientID);
	bool AddParticipant(SDefenceLobby &L, int ClientID);
	CDefenceLobbyManager *ActiveManager() const;
};

#endif
