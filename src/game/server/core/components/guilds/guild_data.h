#ifndef GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_DATA_H
#define GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_DATA_H

#include <engine/shared/protocol.h>
#include <base/tl/array.h>

#include <ctime>

enum EGuildRank
{
	GUILDRANK_APPLICANT = -1,
	GUILDRANK_MEMBER = 0,
	GUILDRANK_OFFICER,
	GUILDRANK_CO_LEADER,
	GUILDRANK_LEADER,
	GUILDRANK_NUM_RANKS
};

struct SGuildJoinRequest
{
	int m_AccountID;
	char m_aName[MAX_NAME_LENGTH];
	int m_RequestTick;

	SGuildJoinRequest()
	{
		m_AccountID = -1;
		m_aName[0] = '\0';
		m_RequestTick = 0;
	}
};

struct SGuildMember
{
	int m_ClientID;
	char m_aName[MAX_NAME_LENGTH];
	EGuildRank m_Rank;
	int m_JoinTick;
	int m_LastOnlineTick;
	int m_ContributionGold;  // total gold donated
	int m_KillsPvE;
	int m_KillsPvP;

	SGuildMember()
	{
		m_ClientID = -1;
		m_aName[0] = '\0';
		m_Rank = GUILDRANK_MEMBER;
		m_JoinTick = 0;
		m_LastOnlineTick = 0;
		m_ContributionGold = 0;
		m_KillsPvE = 0;
		m_KillsPvP = 0;
	}
};

struct SGuildData
{
	int m_ID;
	char m_aName[MAX_NAME_LENGTH];
	char m_aTag[8]; // short tag like [GUILD]
	int m_LeaderCID;
	char m_aLeaderName[MAX_NAME_LENGTH];
	array<SGuildMember> m_Members;
	array<SGuildJoinRequest> m_JoinRequests;
	int m_Gold;  // guild bank
	int m_Level;
	int m_Experience;
	int m_CreationTick;
	int m_MaxMembers;
	char m_aMotd[256];  // message of the day

	SGuildData()
	{
		m_ID = -1;
		m_aName[0] = '\0';
		m_aTag[0] = '\0';
		m_LeaderCID = -1;
		m_aLeaderName[0] = '\0';
		m_Gold = 0;
		m_Level = 1;
		m_Experience = 0;
		m_CreationTick = 0;
		m_MaxMembers = 20;
		m_aMotd[0] = '\0';
	}

	bool IsMember(int ClientID) const;
	EGuildRank GetRank(int ClientID) const;
	int MemberCount() const { return m_Members.size(); }

};

#endif
