#ifndef GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_MANAGER_H

#include <game/server/core/tworld_component.h>
#include <ctime>
#include "guild_data.h"

class CCommandManager;

class CGuildManager : public TWorldComponent
{
	array<SGuildData> m_aGuilds;
	int m_NextGuildID;

	// Pending invites: [target ClientID] = { guild index, inviter name, expiry tick }
	struct SInviteInfo
	{
		int m_GuildIdx;
		char m_aInviterName[MAX_NAME_LENGTH];
		int m_ExpiryTick;
		SInviteInfo() : m_GuildIdx(-1), m_ExpiryTick(0) { m_aInviterName[0] = '\0'; }
	};
	SInviteInfo m_aPendingInvites[MAX_CLIENTS];

public:
	CGuildManager();

	void OnPreInit() override;
	void OnTick() override;
	void OnConsoleInit() override;
	void OnClientReset(int ClientID) override;

	// Guild CRUD
	bool CreateGuild(int ClientID, const char *pName, const char *pTag);
	bool DisbandGuild(int ClientID);  // leader only

	// Member management
	bool InviteMember(int ClientID, int TargetCID);
	bool AcceptInvite(int ClientID);
	bool DeclineInvite(int ClientID);
	bool KickMember(int ClientID, int TargetCID);
	bool LeaveGuild(int ClientID);
	bool ChangeRank(int ClientID, int TargetCID, EGuildRank NewRank);
	bool PromoteMember(int ClientID, int TargetCID);
	bool DemoteMember(int ClientID, int TargetCID);
	bool TransferLeadership(int ClientID, int TargetCID);

	// Guild operations
	bool DonateGold(int ClientID, int Amount);
	bool SetMotd(int ClientID, const char *pMotd);

	// Guild info
	SGuildData *GetGuild(int GuildID);
	SGuildData *GetPlayerGuild(int ClientID);
	int GetPlayerGuildID(int ClientID) const;
	int FindGuildByName(const char *pName) const;

	void ShowGuildInfo(int ClientID);
	void ShowGuildMembers(int ClientID);

	// Join requests (MRPG-style apply to join)
	bool RequestJoinGuild(int ClientID, int GuildID);
	bool AcceptJoinRequest(int ClientID, int RequestAccountID);
	bool DenyJoinRequest(int ClientID, int RequestAccountID);
	bool HasJoinRequest(int GuildID, int AccountID) const;

	int GetNumGuilds() const { return m_aGuilds.size(); }
	SGuildData *GetGuildByIndex(int Index);
	const SGuildData *GetGuildByIndex(int Index) const;
	int FindClientByAccountID(int AccountID) const;

	// Serialization
	bool SaveGuilds();
	bool LoadGuilds();

	// Commands
	void RegisterChatCommands(CCommandManager *pManager);
	void RegisterVoteCommands(CCommandManager *pManager);

	static void ConGuildCreate(IConsole::IResult *pResult, void *pUser);
	static void ConGuildDisband(IConsole::IResult *pResult, void *pUser);
	static void ConGuildInvite(IConsole::IResult *pResult, void *pUser);
	static void ConGuildAccept(IConsole::IResult *pResult, void *pUser);
	static void ConGuildDecline(IConsole::IResult *pResult, void *pUser);
	static void ConGuildKick(IConsole::IResult *pResult, void *pUser);
	static void ConGuildLeave(IConsole::IResult *pResult, void *pUser);
	static void ConGuildPromote(IConsole::IResult *pResult, void *pUser);
	static void ConGuildDemote(IConsole::IResult *pResult, void *pUser);
	static void ConGuildLeader(IConsole::IResult *pResult, void *pUser);
	static void ConGuildDonate(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMotd(IConsole::IResult *pResult, void *pUser);
	static void ConGuildInfo(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMembers(IConsole::IResult *pResult, void *pUser);

	// Guild Match (约战系统)
	static void ConGuildMatchChallenge(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMatchAccept(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMatchSetMode(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMatchJoin(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMatchLeave(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMatchStart(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMatchStatus(IConsole::IResult *pResult, void *pUser);
	static void ConGuildMatchCancel(IConsole::IResult *pResult, void *pUser);

private:
	int FindGuild(const char *pName) const;
	int FindGuildByMember(int ClientID) const;
	void BroadcastToGuild(int GuildIdx, const char *pMsg, int ExcludeCID = -1);
	void ResetPlayerGuildID(int ClientID);
	bool IsValidGuildName(const char *pName) const;
	bool IsValidGuildTag(const char *pTag) const;
	int FindClientByName(const char *pName) const;
	int GuildExpForLevel(int Level) const;
	void CleanupInvites();

	// Match helpers
	struct SMatchParticipant
	{
		int m_ClientID;
		int m_OriginalWorld;
		vec2 m_OriginalPos;
		int m_StartTeam; // TEAM_RED or TEAM_BLUE (-1 if not set)
	};
	struct SMatchState
	{
		int m_MatchID;
		int m_ChallengerGuild; // GuildID
		int m_ChallengedGuild;
		int m_Status; // 0=pending 1=accepted 2=ready 3=active 4=ended 5=cancelled
		char m_aMode[16];
		int m_TeamSize;
		int m_TargetScore;
		int m_StartTick;
		int m_ScoreA;
		int m_ScoreB;
		int m_ArenaWorldID; // -1 if not created yet
		array<SMatchParticipant> m_aParticipants;
	};
	array<SMatchState> m_aMatches;
	int FindMatchByPlayer(int ClientID) const;
	int FindActiveMatchByGuild(int GuildID) const;
	int FindPendingMatchByGuildPair(int ChallengerGID, int ChallengedGID) const;
	int GetPvPWorldIndex() const;
	void BroadcastToMatch(int MatchIdx, const char *pMsg, int ExcludeCID = -1);
	void FinishMatch(int MatchIdx, int WinnerGuildID);
	void CancelMatch(int MatchIdx);
	void SaveMatchToDB(int MatchIdx);
	bool LoadActiveMatchesFromDB();
	int FindGuildIndexByID(int GuildID) const;
};

#endif
