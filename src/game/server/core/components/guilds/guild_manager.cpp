#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <game/commands.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>

#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/entities/character.h>
#include <game/server/account.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_query.h>
#include <mysql.h>
#include "guild_manager.h"

// ─── Constants ───────────────────────────────────────────────────────

static constexpr int GUILD_CREATE_COST = 500;       // gold cost to create
static constexpr int GUILD_NAME_MIN_LEN = 2;
static constexpr int GUILD_NAME_MAX_LEN = 16;
static constexpr int GUILD_TAG_MAX_LEN = 6;
static constexpr int GUILD_INVITE_TIMEOUT = 300;    // ticks (~30s at 10 tick/s)
static constexpr int GUILD_BASE_MEMBERS = 20;
static constexpr int GUILD_MAX_MEMBERS = 50;
static constexpr int GUILD_EXP_PER_LEVEL = 1000;

// ─── Constructor ─────────────────────────────────────────────────────

CGuildManager::CGuildManager()
	: m_NextGuildID(1)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aPendingInvites[i] = SInviteInfo();
}

// ─── Lifecycle ───────────────────────────────────────────────────────

void CGuildManager::OnPreInit()
{
	m_aGuilds.clear();
	m_NextGuildID = 1;
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aPendingInvites[i] = SInviteInfo();

	LoadGuilds();
}

void CGuildManager::OnConsoleInit()
{
	if(!Core()) return;
	// Guild commands are registered via gamecontroller.cpp's RegisterChatCommands
	// which calls RegisterChatCommands(pManager) on this component.
	// We keep OnConsoleInit for potential future console-only commands.
}

// Forward declaration for match mode enforcement
static void ApplyMatchModeRules(CPlayer *pPlayer, const char *pMode);

void CGuildManager::OnTick()
{
	CleanupInvites();

	// ─── Match Tick Processing ───
	// Check active matches every ~10 ticks
	static int s_MatchCheckTick = 0;
	s_MatchCheckTick++;
	if(s_MatchCheckTick % 10 != 0)
		return;

	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		SMatchState &M = m_aMatches[mi];
		if(M.m_Status != 3) // only active matches
			continue;

		// Collect scores from participants
		int ScoreA = 0, ScoreB = 0;
		for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
		{
			int CID = M.m_aParticipants[pi].m_ClientID;
			CPlayer *pP = GS()->m_apPlayers[CID];
			if(!pP)
				continue;
			int Team = pP->GetTeam();
			if(Team == TEAM_RED)
				ScoreA += pP->m_Score;
			else if(Team == TEAM_BLUE)
				ScoreB += pP->m_Score;
		}
		M.m_ScoreA = ScoreA;
		M.m_ScoreB = ScoreB;

		// Apply mode-specific rules continuously (FNG: strip weapons, iDM: refill ammo)
		for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
		{
			int CID = M.m_aParticipants[pi].m_ClientID;
			CPlayer *pP = GS()->m_apPlayers[CID];
			ApplyMatchModeRules(pP, M.m_aMode);
		}

		// Check for win condition
		if(ScoreA >= M.m_TargetScore || ScoreB >= M.m_TargetScore)
		{
			int WinnerGuild = -1;
			if(ScoreA > ScoreB)
				WinnerGuild = M.m_ChallengerGuild;
			else if(ScoreB > ScoreA)
				WinnerGuild = M.m_ChallengedGuild;
			// else draw = WinnerGuild stays -1

			FinishMatch(mi, WinnerGuild);
		}
	}
}

void CGuildManager::OnClientReset(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	// Clean up pending invites for this client
	m_aPendingInvites[ClientID] = SInviteInfo();

	// Update last online tick for guild members
	int idx = FindGuildByMember(ClientID);
	if(idx >= 0)
	{
		for(int i = 0; i < m_aGuilds[idx].m_Members.size(); i++)
		{
			if(m_aGuilds[idx].m_Members[i].m_ClientID == ClientID)
			{
				m_aGuilds[idx].m_Members[i].m_LastOnlineTick = Server()->Tick();
				break;
			}
		}
	}

	// Clean up invites where this client was the inviter
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aPendingInvites[i].m_GuildIdx >= 0)
		{
			// Check if the guild still exists, find inviter
			if(m_aPendingInvites[i].m_GuildIdx < (int)m_aGuilds.size())
			{
				SGuildData &G = m_aGuilds[m_aPendingInvites[i].m_GuildIdx];
				if(G.IsMember(ClientID))
				{
					// Inviter disconnected - remove the invite
					m_aPendingInvites[i] = SInviteInfo();
				}
			}
		}
	}
}

// ─── Helpers ─────────────────────────────────────────────────────────

int CGuildManager::FindGuild(const char *pName) const
{
	for(int i = 0; i < m_aGuilds.size(); i++)
	{
		if(str_comp_nocase(m_aGuilds[i].m_aName, pName) == 0)
			return i;
	}
	return -1;
}

int CGuildManager::FindGuildByMember(int ClientID) const
{
	for(int i = 0; i < m_aGuilds.size(); i++)
	{
		if(m_aGuilds[i].IsMember(ClientID))
			return i;
	}
	return -1;
}

void CGuildManager::BroadcastToGuild(int GuildIdx, const char *pMsg, int ExcludeCID)
{
	if(GuildIdx < 0 || GuildIdx >= (int)m_aGuilds.size())
		return;
	SGuildData &G = m_aGuilds[GuildIdx];
	for(int i = 0; i < G.m_Members.size(); i++)
	{
		int CID = G.m_Members[i].m_ClientID;
		if(CID == ExcludeCID)
			continue;
		CPlayer *pP = GS()->m_apPlayers[CID];
		if(pP)
			pP->GetCharacter(); // just to check they exist
		GS()->SendChat(CID, CHAT_ALL, -1, pMsg);
	}
}

void CGuildManager::ResetPlayerGuildID(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(pP)
		pP->m_GuildID = -1;
}

bool CGuildManager::IsValidGuildName(const char *pName) const
{
	if(!pName) return false;
	int len = str_length(pName);
	if(len < GUILD_NAME_MIN_LEN || len > GUILD_NAME_MAX_LEN)
		return false;
	// Only allow alphanumeric and some special characters
	for(int i = 0; i < len; i++)
	{
		char c = pName[i];
		if(!( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ' '
			|| (unsigned char)c >= 0x80 )) // allow CJK/unicode
			return false;
	}
	return true;
}

bool CGuildManager::IsValidGuildTag(const char *pTag) const
{
	if(!pTag || pTag[0] == '\0')
		return true; // tag is optional
	int len = str_length(pTag);
	if(len > GUILD_TAG_MAX_LEN)
		return false;
	for(int i = 0; i < len; i++)
	{
		char c = pTag[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '[' || c == ']'))
			return false;
	}
	return true;
}

int CGuildManager::FindClientByName(const char *pName) const
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *p = GS()->m_apPlayers[i];
		if(!p || p->IsDummy() || p->GetTeam() == TEAM_SPECTATORS)
			continue;
		if(str_comp_nocase(Server()->ClientName(i), pName) == 0)
			return i;
	}
	return -1;
}

int CGuildManager::GuildExpForLevel(int Level) const
{
	if(Level <= 1) return GUILD_EXP_PER_LEVEL;
	return Level * GUILD_EXP_PER_LEVEL;
}

void CGuildManager::CleanupInvites()
{
	int nowTick = Server()->Tick();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aPendingInvites[i].m_GuildIdx >= 0)
		{
			if(m_aPendingInvites[i].m_ExpiryTick > 0 && nowTick >= m_aPendingInvites[i].m_ExpiryTick)
			{
				m_aPendingInvites[i] = SInviteInfo();
			}
		}
	}
}

// ─── Guild CRUD ──────────────────────────────────────────────────────

bool CGuildManager::CreateGuild(int ClientID, const char *pName, const char *pTag)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GS()->m_apPlayers[ClientID])
		return false;

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(pP->GetGuildID() >= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你已经在公会中了。");
		return false;
	}

	if(!pName || !pName[0])
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请输入公会名称。");
		return false;
	}

	if(!IsValidGuildName(pName))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 公会名称无效（2-16字符，支持字母/数字/中文/_-空格）。");
		return false;
	}

	if(pTag && pTag[0] && !IsValidGuildTag(pTag))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 公会标签无效（最多6字符，字母数字_-[]）。");
		return false;
	}

	if(FindGuild(pName) >= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该公会名称已被使用。");
		return false;
	}

	// Check gold cost
	if(pP->GetStat(AttributeIdentifier::Gold) < GUILD_CREATE_COST)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "⚠ 创建公会需要 %d 金币，你只有 %d。",
			GUILD_CREATE_COST, pP->GetStat(AttributeIdentifier::Gold));
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
		return false;
	}
	pP->SetStat(AttributeIdentifier::Gold, pP->GetStat(AttributeIdentifier::Gold) - GUILD_CREATE_COST);

	// Check tag uniqueness
	if(pTag && pTag[0])
	{
		for(int i = 0; i < m_aGuilds.size(); i++)
		{
			if(str_comp_nocase(m_aGuilds[i].m_aTag, pTag) == 0)
			{
				GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该公会标签已被使用。");
				return false;
			}
		}
	}

	SGuildData Guild;
	Guild.m_ID = m_NextGuildID++;
	str_copy(Guild.m_aName, pName, sizeof(Guild.m_aName));
	str_copy(Guild.m_aTag, pTag && pTag[0] ? pTag : "", sizeof(Guild.m_aTag));
	Guild.m_LeaderCID = ClientID;
	str_copy(Guild.m_aLeaderName, Server()->ClientName(ClientID), sizeof(Guild.m_aLeaderName));
	Guild.m_Gold = 0;
	Guild.m_Level = 1;
	Guild.m_Experience = 0;
	Guild.m_CreationTick = Server()->Tick();
	Guild.m_MaxMembers = GUILD_BASE_MEMBERS;
	Guild.m_aMotd[0] = '\0';

	SGuildMember Founder;
	Founder.m_ClientID = ClientID;
	str_copy(Founder.m_aName, Server()->ClientName(ClientID), sizeof(Founder.m_aName));
	Founder.m_Rank = GUILDRANK_LEADER;
	Founder.m_JoinTick = Server()->Tick();
	Founder.m_LastOnlineTick = Server()->Tick();
	Founder.m_ContributionGold = GUILD_CREATE_COST;
	Guild.m_Members.add(Founder);

	m_aGuilds.add(Guild);
	ResetPlayerGuildID(ClientID);
	GS()->m_apPlayers[ClientID]->m_GuildID = Guild.m_ID;

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "✅ 公会「%s」创建成功！", pName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	if(pTag && pTag[0])
	{
		str_format(aBuf, sizeof(aBuf), "标签：%s", pTag);
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	}

	SaveGuilds();
	return true;
}

bool CGuildManager::DisbandGuild(int ClientID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	if(m_aGuilds[idx].GetRank(ClientID) != GUILDRANK_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有公会会长才能解散公会。");
		return false;
	}

	char aName[64];
	str_copy(aName, m_aGuilds[idx].m_aName, sizeof(aName));
	int GuildID = m_aGuilds[idx].m_ID;

	// Notify all members and clear their guild IDs
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "💔 公会「%s」已被会长解散。", aName);
	for(int i = 0; i < m_aGuilds[idx].m_Members.size(); i++)
	{
		int CID = m_aGuilds[idx].m_Members[i].m_ClientID;
		CPlayer *pP = GS()->m_apPlayers[CID];
		if(pP)
		{
			pP->m_GuildID = -1;
			if(CID != ClientID)
				GS()->SendChat(CID, CHAT_ALL, -1, aBuf);
		}
	}

	// Clean up pending invites pointing to this guild
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aPendingInvites[i].m_GuildIdx >= 0)
		{
			// Find the guild index that corresponds to this guild ID
			for(int gi = 0; gi < m_aGuilds.size(); gi++)
			{
				if(m_aGuilds[gi].m_ID == GuildID)
				{
					if(m_aPendingInvites[i].m_GuildIdx == gi)
						m_aPendingInvites[i] = SInviteInfo();
					break;
				}
			}
		}
	}

	m_aGuilds.remove_index(idx);
	str_format(aBuf, sizeof(aBuf), "✅ 公会「%s」已解散。", aName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	SaveGuilds();
	return true;
}

// ─── Member Management ──────────────────────────────────────────────

bool CGuildManager::InviteMember(int ClientID, int TargetCID)
{
	if(ClientID < 0 || TargetCID < 0 || !GS()->m_apPlayers[ClientID] || !GS()->m_apPlayers[TargetCID])
		return false;

	if(ClientID == TargetCID)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 不能邀请自己。");
		return false;
	}

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank rank = m_aGuilds[idx].GetRank(ClientID);
	if(rank != GUILDRANK_LEADER && rank != GUILDRANK_CO_LEADER && rank != GUILDRANK_OFFICER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你没有权限邀请成员（需要管理员以上）。");
		return false;
	}

	if(m_aGuilds[idx].IsMember(TargetCID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家已在公会中。");
		return false;
	}

	if(GS()->m_apPlayers[TargetCID]->GetGuildID() >= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家已在其他公会中。");
		return false;
	}

	if(m_aGuilds[idx].MemberCount() >= m_aGuilds[idx].m_MaxMembers)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 公会成员已满。");
		return false;
	}

	// Store pending invite
	m_aPendingInvites[TargetCID].m_GuildIdx = idx;
	str_copy(m_aPendingInvites[TargetCID].m_aInviterName, Server()->ClientName(ClientID),
		sizeof(m_aPendingInvites[TargetCID].m_aInviterName));
	m_aPendingInvites[TargetCID].m_ExpiryTick = Server()->Tick() + GUILD_INVITE_TIMEOUT;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "📨 已邀请 %s 加入公会。", Server()->ClientName(TargetCID));
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	str_format(aBuf, sizeof(aBuf), "📨 %s 邀请你加入公会「%s」。\n输入 /guild_accept 接受，/guild_decline 拒绝。",
		Server()->ClientName(ClientID), m_aGuilds[idx].m_aName);
	GS()->SendChat(TargetCID, CHAT_ALL, -1, aBuf);
	return true;
}

bool CGuildManager::AcceptInvite(int ClientID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	SInviteInfo &Invite = m_aPendingInvites[ClientID];
	if(Invite.m_GuildIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 没有待处理的公会邀请。");
		return false;
	}

	if(GS()->m_apPlayers[ClientID]->GetGuildID() >= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你已经在公会中了。");
		Invite = SInviteInfo();
		return false;
	}

	if(Invite.m_GuildIdx >= m_aGuilds.size())
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该公会已不存在。");
		Invite = SInviteInfo();
		return false;
	}

	// Check expiry
	int nowTick = Server()->Tick();
	if(Invite.m_ExpiryTick > 0 && nowTick >= Invite.m_ExpiryTick)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 邀请已过期。");
		Invite = SInviteInfo();
		return false;
	}

	SGuildData &G = m_aGuilds[Invite.m_GuildIdx];

	if(G.MemberCount() >= G.m_MaxMembers)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 公会成员已满。");
		Invite = SInviteInfo();
		return false;
	}

	// Add as member
	SGuildMember NewMember;
	NewMember.m_ClientID = ClientID;
	str_copy(NewMember.m_aName, Server()->ClientName(ClientID), sizeof(NewMember.m_aName));
	NewMember.m_Rank = GUILDRANK_MEMBER;
	NewMember.m_JoinTick = Server()->Tick();
	NewMember.m_LastOnlineTick = Server()->Tick();
	G.m_Members.add(NewMember);

	GS()->m_apPlayers[ClientID]->m_GuildID = G.m_ID;
	int GuildIdx = Invite.m_GuildIdx;
	Invite = SInviteInfo();

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "✅ 你已加入公会「%s」！", G.m_aName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	if(G.m_aMotd[0])
	{
		str_format(aBuf, sizeof(aBuf), "公会公告：%s", G.m_aMotd);
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	}

	str_format(aBuf, sizeof(aBuf), "🎉 %s 加入了公会！", Server()->ClientName(ClientID));
	BroadcastToGuild(GuildIdx, aBuf, ClientID);

	SaveGuilds();
	return true;
}

bool CGuildManager::DeclineInvite(int ClientID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	if(m_aPendingInvites[ClientID].m_GuildIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 没有待处理的公会邀请。");
		return false;
	}

	int InviterCID = -1;
	char aInviterName[MAX_NAME_LENGTH];
	str_copy(aInviterName, m_aPendingInvites[ClientID].m_aInviterName, sizeof(aInviterName));

	// Find inviter by name
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *p = GS()->m_apPlayers[i];
		if(p && str_comp(Server()->ClientName(i), aInviterName) == 0)
		{
			InviterCID = i;
			break;
		}
	}

	m_aPendingInvites[ClientID] = SInviteInfo();

	GS()->SendChat(ClientID, CHAT_ALL, -1, "已拒绝公会邀请。");
	if(InviterCID >= 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "%s 拒绝了公会邀请。", Server()->ClientName(ClientID));
		GS()->SendChat(InviterCID, CHAT_ALL, -1, aBuf);
	}
	return true;
}

bool CGuildManager::KickMember(int ClientID, int TargetCID)
{
	if(ClientID < 0 || TargetCID < 0 || !GS()->m_apPlayers[ClientID] || !GS()->m_apPlayers[TargetCID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank callerRank = m_aGuilds[idx].GetRank(ClientID);
	EGuildRank targetRank = m_aGuilds[idx].GetRank(TargetCID);

	if(callerRank != GUILDRANK_LEADER && callerRank != GUILDRANK_CO_LEADER && callerRank != GUILDRANK_OFFICER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你没有权限踢出成员。");
		return false;
	}

	if(!m_aGuilds[idx].IsMember(TargetCID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家不在你的公会中。");
		return false;
	}

	if(targetRank == GUILDRANK_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 不能踢出会长。");
		return false;
	}

	// Can't kick same or higher rank
	if(targetRank >= callerRank && callerRank != GUILDRANK_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 不能踢出同权限或更高权限的成员。");
		return false;
	}

	char aTargetName[MAX_NAME_LENGTH];
	str_copy(aTargetName, Server()->ClientName(TargetCID), sizeof(aTargetName));
	char aGuildName[64];
	str_copy(aGuildName, m_aGuilds[idx].m_aName, sizeof(aGuildName));

	// Remove member
	for(int i = 0; i < m_aGuilds[idx].m_Members.size(); i++)
	{
		if(m_aGuilds[idx].m_Members[i].m_ClientID == TargetCID)
		{
			m_aGuilds[idx].m_Members.remove_index(i);
			break;
		}
	}

	ResetPlayerGuildID(TargetCID);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "⚠ 你已被踢出公会「%s」。", aGuildName);
	GS()->SendChat(TargetCID, CHAT_ALL, -1, aBuf);

	str_format(aBuf, sizeof(aBuf), "👢 已踢出 %s。", aTargetName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	str_format(aBuf, sizeof(aBuf), "👢 %s 被踢出了公会。", aTargetName);
	BroadcastToGuild(idx, aBuf, ClientID);

	SaveGuilds();
	return true;
}

bool CGuildManager::LeaveGuild(int ClientID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	// Leader cannot leave, must disband or transfer leadership first
	if(m_aGuilds[idx].GetRank(ClientID) == GUILDRANK_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 会长不能退出公会。请先 /guild_leader <玩家名> 转让会长，或 /guild_disband 解散公会。");
		return false;
	}

	char aGuildName[64];
	str_copy(aGuildName, m_aGuilds[idx].m_aName, sizeof(aGuildName));
	char aPlayerName[MAX_NAME_LENGTH];
	str_copy(aPlayerName, Server()->ClientName(ClientID), sizeof(aPlayerName));

	for(int i = 0; i < m_aGuilds[idx].m_Members.size(); i++)
	{
		if(m_aGuilds[idx].m_Members[i].m_ClientID == ClientID)
		{
			m_aGuilds[idx].m_Members.remove_index(i);
			break;
		}
	}

	ResetPlayerGuildID(ClientID);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "🚪 你已退出公会「%s」。", aGuildName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	str_format(aBuf, sizeof(aBuf), "🚪 %s 退出了公会。", aPlayerName);
	BroadcastToGuild(idx, aBuf, ClientID);

	SaveGuilds();
	return true;
}

bool CGuildManager::ChangeRank(int ClientID, int TargetCID, EGuildRank NewRank)
{
	if(ClientID < 0 || TargetCID < 0 || !GS()->m_apPlayers[ClientID] || !GS()->m_apPlayers[TargetCID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank callerRank = m_aGuilds[idx].GetRank(ClientID);
	if(callerRank != GUILDRANK_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长才能更改成员权限。");
		return false;
	}

	if(!m_aGuilds[idx].IsMember(TargetCID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家不在公会中。");
		return false;
	}

	if(NewRank == GUILDRANK_LEADER || NewRank == GUILDRANK_APPLICANT)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请使用 /guild_leader 转让会长。");
		return false;
	}

	for(int i = 0; i < m_aGuilds[idx].m_Members.size(); i++)
	{
		if(m_aGuilds[idx].m_Members[i].m_ClientID == TargetCID)
		{
			m_aGuilds[idx].m_Members[i].m_Rank = NewRank;
			break;
		}
	}

	const char *pRankName = (NewRank == GUILDRANK_CO_LEADER) ? "副会长" :
		(NewRank == GUILDRANK_OFFICER) ? "管理员" : "成员";
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "已将 %s 的权限设为 %s。", Server()->ClientName(TargetCID), pRankName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	str_format(aBuf, sizeof(aBuf), "你在公会中的权限已变更为：%s。", pRankName);
	GS()->SendChat(TargetCID, CHAT_ALL, -1, aBuf);

	SaveGuilds();
	return true;
}

bool CGuildManager::PromoteMember(int ClientID, int TargetCID)
{
	if(ClientID < 0 || TargetCID < 0 || !GS()->m_apPlayers[ClientID] || !GS()->m_apPlayers[TargetCID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank callerRank = m_aGuilds[idx].GetRank(ClientID);
	if(callerRank != GUILDRANK_LEADER && callerRank != GUILDRANK_CO_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你没有权限晋升成员（需要副会长以上）。");
		return false;
	}

	if(!m_aGuilds[idx].IsMember(TargetCID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家不在公会中。");
		return false;
	}

	EGuildRank targetRank = m_aGuilds[idx].GetRank(TargetCID);
	EGuildRank newRank;

	switch(targetRank)
	{
	case GUILDRANK_MEMBER:
		newRank = GUILDRANK_OFFICER;
		break;
	case GUILDRANK_OFFICER:
		if(callerRank != GUILDRANK_LEADER)
		{
			GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长才能晋升管理员为副会长。");
			return false;
		}
		newRank = GUILDRANK_CO_LEADER;
		break;
	case GUILDRANK_CO_LEADER:
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 已是最高权限（副会长）。");
		return false;
	case GUILDRANK_LEADER:
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 不能晋升会长。");
		return false;
	default:
		return false;
	}

	return ChangeRank(ClientID, TargetCID, newRank);
}

bool CGuildManager::DemoteMember(int ClientID, int TargetCID)
{
	if(ClientID < 0 || TargetCID < 0 || !GS()->m_apPlayers[ClientID] || !GS()->m_apPlayers[TargetCID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank callerRank = m_aGuilds[idx].GetRank(ClientID);
	if(callerRank != GUILDRANK_LEADER && callerRank != GUILDRANK_CO_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你没有权限降级成员。");
		return false;
	}

	if(!m_aGuilds[idx].IsMember(TargetCID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家不在公会中。");
		return false;
	}

	EGuildRank targetRank = m_aGuilds[idx].GetRank(TargetCID);
	EGuildRank newRank;

	switch(targetRank)
	{
	case GUILDRANK_CO_LEADER:
		if(callerRank != GUILDRANK_LEADER)
		{
			GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长才能降级副会长。");
			return false;
		}
		newRank = GUILDRANK_OFFICER;
		break;
	case GUILDRANK_OFFICER:
		newRank = GUILDRANK_MEMBER;
		break;
	case GUILDRANK_MEMBER:
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 已是普通成员，无法继续降级。");
		return false;
	case GUILDRANK_LEADER:
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 不能降级会长。");
		return false;
	default:
		return false;
	}

	return ChangeRank(ClientID, TargetCID, newRank);
}

bool CGuildManager::TransferLeadership(int ClientID, int TargetCID)
{
	if(ClientID < 0 || TargetCID < 0 || !GS()->m_apPlayers[ClientID] || !GS()->m_apPlayers[TargetCID])
		return false;

	if(ClientID == TargetCID)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你已经是会长了。");
		return false;
	}

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	if(m_aGuilds[idx].GetRank(ClientID) != GUILDRANK_LEADER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长才能转让会长。");
		return false;
	}

	if(!m_aGuilds[idx].IsMember(TargetCID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家不在公会中。");
		return false;
	}

	// Transfer
	for(int i = 0; i < m_aGuilds[idx].m_Members.size(); i++)
	{
		if(m_aGuilds[idx].m_Members[i].m_ClientID == ClientID)
			m_aGuilds[idx].m_Members[i].m_Rank = GUILDRANK_CO_LEADER;
		if(m_aGuilds[idx].m_Members[i].m_ClientID == TargetCID)
			m_aGuilds[idx].m_Members[i].m_Rank = GUILDRANK_LEADER;
	}

	m_aGuilds[idx].m_LeaderCID = TargetCID;
	str_copy(m_aGuilds[idx].m_aLeaderName, Server()->ClientName(TargetCID),
		sizeof(m_aGuilds[idx].m_aLeaderName));

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "👑 %s 已成为新的会长！", Server()->ClientName(TargetCID));
	BroadcastToGuild(idx, aBuf);

	GS()->SendChat(ClientID, CHAT_ALL, -1, "✅ 会长已成功转让。");
	GS()->SendChat(TargetCID, CHAT_ALL, -1, "✅ 你已成为公会会长！");

	SaveGuilds();
	return true;
}

// ─── Guild Operations ──────────────────────────────────────────────

bool CGuildManager::DonateGold(int ClientID, int Amount)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	if(Amount <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请输入有效的金币数量。");
		return false;
	}

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(pP->GetStat(AttributeIdentifier::Gold) < Amount)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 金币不足。");
		return false;
	}
	pP->SetStat(AttributeIdentifier::Gold, pP->GetStat(AttributeIdentifier::Gold) - Amount);

	m_aGuilds[idx].m_Gold += Amount;

	// Track contribution
	for(int i = 0; i < m_aGuilds[idx].m_Members.size(); i++)
	{
		if(m_aGuilds[idx].m_Members[i].m_ClientID == ClientID)
		{
			m_aGuilds[idx].m_Members[i].m_ContributionGold += Amount;
			break;
		}
	}

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "💰 你捐赠了 %d 金币到公会银行（公会总资产：%d）。", Amount, m_aGuilds[idx].m_Gold);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	SaveGuilds();
	return true;
}

bool CGuildManager::SetMotd(int ClientID, const char *pMotd)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank rank = m_aGuilds[idx].GetRank(ClientID);
	if(rank != GUILDRANK_LEADER && rank != GUILDRANK_CO_LEADER && rank != GUILDRANK_OFFICER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你没有权限设置公会公告（需要管理员以上）。");
		return false;
	}

	if(!pMotd || !pMotd[0])
	{
		m_aGuilds[idx].m_aMotd[0] = '\0';
		GS()->SendChat(ClientID, CHAT_ALL, -1, "✅ 公会公告已清除。");
	}
	else
	{
		str_copy(m_aGuilds[idx].m_aMotd, pMotd, sizeof(m_aGuilds[idx].m_aMotd));
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "📢 公会公告已更新：%s", m_aGuilds[idx].m_aMotd);
		BroadcastToGuild(idx, aBuf);
	}

	SaveGuilds();
	return true;
}

// ─── Guild Info ─────────────────────────────────────────────────────

SGuildData *CGuildManager::GetGuild(int GuildID)
{
	for(int i = 0; i < m_aGuilds.size(); i++)
	{
		if(m_aGuilds[i].m_ID == GuildID)
			return &m_aGuilds[i];
	}
	return 0;
}

SGuildData *CGuildManager::GetPlayerGuild(int ClientID)
{
	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
		return 0;
	return &m_aGuilds[idx];
}

int CGuildManager::GetPlayerGuildID(int ClientID) const
{
	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
		return -1;
	return m_aGuilds[idx].m_ID;
}

int CGuildManager::FindGuildByName(const char *pName) const
{
	int idx = FindGuild(pName);
	if(idx < 0)
		return -1;
	return m_aGuilds[idx].m_ID;
}

SGuildData *CGuildManager::GetGuildByIndex(int Index)
{
	if(Index < 0 || Index >= m_aGuilds.size())
		return nullptr;
	return &m_aGuilds[Index];
}

const SGuildData *CGuildManager::GetGuildByIndex(int Index) const
{
	if(Index < 0 || Index >= m_aGuilds.size())
		return nullptr;
	return &m_aGuilds[Index];
}

int CGuildManager::FindClientByAccountID(int AccountID) const
{
	if(AccountID < 0)
		return -1;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pP = GS()->m_apPlayers[i];
		if(pP && pP->GetAccountId() == AccountID)
			return i;
	}
	return -1;
}

bool CGuildManager::HasJoinRequest(int GuildID, int AccountID) const
{
	if(AccountID < 0)
		return false;
	for(int g = 0; g < m_aGuilds.size(); g++)
	{
		if(m_aGuilds[g].m_ID != GuildID)
			continue;
		for(int i = 0; i < m_aGuilds[g].m_JoinRequests.size(); i++)
		{
			if(m_aGuilds[g].m_JoinRequests[i].m_AccountID == AccountID)
				return true;
		}
		return false;
	}
	return false;
}

bool CGuildManager::RequestJoinGuild(int ClientID, int GuildID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !GS()->m_apPlayers[ClientID])
		return false;

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(pP->GetGuildID() >= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你已经在公会中了。");
		return false;
	}

	int idx = FindGuildIndexByID(GuildID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 找不到该公会。");
		return false;
	}

	SGuildData &G = m_aGuilds[idx];
	if(G.MemberCount() >= G.m_MaxMembers)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该公会成员已满。");
		return false;
	}

	const int AccountID = pP->GetAccountId();
	if(AccountID <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	if(HasJoinRequest(GuildID, AccountID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你已提交过加入申请。");
		return false;
	}

	for(int g = 0; g < m_aGuilds.size(); g++)
	{
		for(int r = 0; r < m_aGuilds[g].m_JoinRequests.size(); r++)
		{
			if(m_aGuilds[g].m_JoinRequests[r].m_AccountID == AccountID)
			{
				GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你已有待处理的加入申请。");
				return false;
			}
		}
	}

	SGuildJoinRequest Req;
	Req.m_AccountID = AccountID;
	str_copy(Req.m_aName, Server()->ClientName(ClientID), sizeof(Req.m_aName));
	Req.m_RequestTick = Server()->Tick();
	G.m_JoinRequests.add(Req);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "📨 %s 申请加入公会「%s」。", Req.m_aName, G.m_aName);
	BroadcastToGuild(idx, aBuf, ClientID);

	str_format(aBuf, sizeof(aBuf), "✅ 已向公会「%s」提交加入申请。", G.m_aName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	SaveGuilds();
	return true;
}

bool CGuildManager::AcceptJoinRequest(int ClientID, int RequestAccountID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank Rank = m_aGuilds[idx].GetRank(ClientID);
	if(Rank != GUILDRANK_LEADER && Rank != GUILDRANK_CO_LEADER && Rank != GUILDRANK_OFFICER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你没有权限审批申请（需要管理员以上）。");
		return false;
	}

	SGuildData &G = m_aGuilds[idx];
	int ReqIdx = -1;
	for(int i = 0; i < G.m_JoinRequests.size(); i++)
	{
		if(G.m_JoinRequests[i].m_AccountID == RequestAccountID)
		{
			ReqIdx = i;
			break;
		}
	}
	if(ReqIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 找不到该加入申请。");
		return false;
	}

	if(G.MemberCount() >= G.m_MaxMembers)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 公会成员已满。");
		return false;
	}

	const int TargetCID = FindClientByAccountID(RequestAccountID);
	if(TargetCID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家不在线，无法批准加入。");
		return false;
	}

	CPlayer *pTarget = GS()->m_apPlayers[TargetCID];
	if(!pTarget || pTarget->GetGuildID() >= 0)
	{
		G.m_JoinRequests.remove_index(ReqIdx);
		SaveGuilds();
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该玩家已在其他公会中，申请已移除。");
		return false;
	}

	SGuildMember NewMember;
	NewMember.m_ClientID = TargetCID;
	str_copy(NewMember.m_aName, Server()->ClientName(TargetCID), sizeof(NewMember.m_aName));
	NewMember.m_Rank = GUILDRANK_MEMBER;
	NewMember.m_JoinTick = Server()->Tick();
	NewMember.m_LastOnlineTick = Server()->Tick();
	G.m_Members.add(NewMember);
	G.m_JoinRequests.remove_index(ReqIdx);

	ResetPlayerGuildID(TargetCID);
	pTarget->m_GuildID = G.m_ID;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "✅ %s 已加入公会「%s」。", NewMember.m_aName, G.m_aName);
	BroadcastToGuild(idx, aBuf);
	GS()->SendChat(TargetCID, CHAT_ALL, -1, aBuf);

	SaveGuilds();
	return true;
}

bool CGuildManager::DenyJoinRequest(int ClientID, int RequestAccountID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return false;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	EGuildRank Rank = m_aGuilds[idx].GetRank(ClientID);
	if(Rank != GUILDRANK_LEADER && Rank != GUILDRANK_CO_LEADER && Rank != GUILDRANK_OFFICER)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你没有权限审批申请（需要管理员以上）。");
		return false;
	}

	SGuildData &G = m_aGuilds[idx];
	for(int i = 0; i < G.m_JoinRequests.size(); i++)
	{
		if(G.m_JoinRequests[i].m_AccountID == RequestAccountID)
		{
			const char *pName = G.m_JoinRequests[i].m_aName;
			G.m_JoinRequests.remove_index(i);

			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "❌ 已拒绝 %s 的加入申请。", pName);
			GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

			const int TargetCID = FindClientByAccountID(RequestAccountID);
			if(TargetCID >= 0)
			{
				str_format(aBuf, sizeof(aBuf), "❌ 你的加入申请已被公会「%s」拒绝。", G.m_aName);
				GS()->SendChat(TargetCID, CHAT_ALL, -1, aBuf);
			}

			SaveGuilds();
			return true;
		}
	}

	GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 找不到该加入申请。");
	return false;
}

void CGuildManager::ShowGuildInfo(int ClientID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	SGuildData &G = m_aGuilds[idx];
	const char *pLeaderName = Server()->ClientName(G.m_LeaderCID);

	// Count online members
	int online = 0;
	for(int i = 0; i < G.m_Members.size(); i++)
	{
		CPlayer *pP = GS()->m_apPlayers[G.m_Members[i].m_ClientID];
		if(pP && pP->GetCharacter())
			online++;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══════ 公会信息 ═══════");
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "名称：%s", G.m_aName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	if(G.m_aTag[0])
	{
		str_format(aBuf, sizeof(aBuf), "标签：%s", G.m_aTag);
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	}
	str_format(aBuf, sizeof(aBuf), "会长：%s", pLeaderName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "等级：%d  |  经验：%d/%d  |  金币：%d",
		G.m_Level, G.m_Experience, GuildExpForLevel(G.m_Level), G.m_Gold);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "成员：%d/%d  |  在线：%d",
		G.MemberCount(), G.m_MaxMembers, online);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	if(G.m_aMotd[0])
	{
		str_format(aBuf, sizeof(aBuf), "公告：%s", G.m_aMotd);
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	}
}

void CGuildManager::ShowGuildMembers(int ClientID)
{
	if(ClientID < 0 || !GS()->m_apPlayers[ClientID])
		return;

	int idx = FindGuildByMember(ClientID);
	if(idx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	SGuildData &G = m_aGuilds[idx];

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══════ 公会成员 (%d/%d) ═══════", G.MemberCount(), G.m_MaxMembers);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	for(int i = 0; i < G.m_Members.size(); i++)
	{
		const SGuildMember &M = G.m_Members[i];
		const char *pRankName;
		switch(M.m_Rank)
		{
		case GUILDRANK_LEADER: pRankName = "👑会长"; break;
		case GUILDRANK_CO_LEADER: pRankName = "⭐副会长"; break;
		case GUILDRANK_OFFICER: pRankName = "🔧管理"; break;
		default: pRankName = "   成员"; break;
		}

		bool online = GS()->m_apPlayers[M.m_ClientID] && GS()->m_apPlayers[M.m_ClientID]->GetCharacter();
		const char *pStatus = online ? "🟢在线" : "🔴离线";

		str_format(aBuf, sizeof(aBuf), "%s %s %s (贡献：%d)",
			pRankName, M.m_aName, pStatus, M.m_ContributionGold);
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	}
}

// ─── Serialization ──────────────────────────────────────────────────

bool CGuildManager::SaveGuilds()
{
	if(!Storage())
		return false;

	// TDA's str_format returns void, so we build JSON manually with snprintf-like patterns
	// or just use str_format without assignment
	char aBuf[16 * 1024];
	int Offset = 0;

	// Manual buffer append helper
	auto Append = [&](const char *pStr)
	{
		int Len = str_length(pStr);
		if(Offset + Len < (int)sizeof(aBuf))
		{
			str_copy(aBuf + Offset, pStr, (int)sizeof(aBuf) - Offset);
			Offset += Len;
		}
	};

	Append("{\"guilds\":[");

	for(int g = 0; g < m_aGuilds.size(); g++)
	{
		SGuildData &G = m_aGuilds[g];
		if(g > 0) Append(",");

		char aEntry[1024];
		str_format(aEntry, sizeof(aEntry),
			"{\"id\":%d,\"name\":\"%s\",\"tag\":\"%s\",\"leader_cid\":%d,"
			"\"leader_name\":\"%s\",\"gold\":%d,\"level\":%d,\"exp\":%d,"
			"\"creation_tick\":%d,\"max_members\":%d,\"motd\":\"%s\",\"members\":[",
			G.m_ID, G.m_aName, G.m_aTag, G.m_LeaderCID,
			G.m_aLeaderName, G.m_Gold, G.m_Level, G.m_Experience,
			G.m_CreationTick, G.m_MaxMembers, G.m_aMotd);
		Append(aEntry);

		for(int m = 0; m < G.m_Members.size(); m++)
		{
			SGuildMember &M = G.m_Members[m];
			if(m > 0) Append(",");

			char aMember[512];
			str_format(aMember, sizeof(aMember),
				"{\"cid\":%d,\"name\":\"%s\",\"rank\":%d,\"join_tick\":%d,"
				"\"last_online\":%d,\"contrib\":%d,\"kills_pve\":%d,\"kills_pvp\":%d}",
				M.m_ClientID, M.m_aName, (int)M.m_Rank, M.m_JoinTick,
				M.m_LastOnlineTick, M.m_ContributionGold, M.m_KillsPvE, M.m_KillsPvP);
			Append(aMember);
		}

		Append("]");

		if(G.m_JoinRequests.size() > 0)
		{
			Append(",\"join_requests\":[");
			for(int r = 0; r < G.m_JoinRequests.size(); r++)
			{
				SGuildJoinRequest &R = G.m_JoinRequests[r];
				if(r > 0) Append(",");
				char aReq[256];
				str_format(aReq, sizeof(aReq),
					"{\"account_id\":%d,\"name\":\"%s\",\"request_tick\":%d}",
					R.m_AccountID, R.m_aName, R.m_RequestTick);
				Append(aReq);
			}
			Append("]");
		}

		Append("}");
	}

	Append("]}");

	// Write to file
	IOHANDLE File = Storage()->OpenFile("server_content/guilds.json", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		dbg_msg("guild", "Failed to save guilds.json");
		return false;
	}
	io_write(File, aBuf, Offset);
	io_close(File);

	dbg_msg("guild", "Saved %d guilds", m_aGuilds.size());
	return true;
}

bool CGuildManager::LoadGuilds()
{
	if(!Storage())
		return false;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_content/guilds.json", Storage());
	if(!pRoot)
	{
		dbg_msg("guild", "No guilds.json found, starting fresh");
		return false;
	}

	const json_value &Arr = (*pRoot)["guilds"];
	if(Arr.type != json_array)
	{
		dbg_msg("guild", "guilds.json: missing 'guilds' array");
		return false;
	}

	int Loaded = 0;
	m_aGuilds.clear();

	for(unsigned i = 0; i < Arr.u.array.length; i++)
	{
		const json_value &S = Arr[(int)i];
		if(S.type != json_object) continue;

		SGuildData G;
		if(S["id"].type == json_integer) G.m_ID = (int)S["id"].u.integer;
		else continue;

		if(S["name"].type == json_string) str_copy(G.m_aName, S["name"].u.string.ptr, sizeof(G.m_aName));
		if(S["tag"].type == json_string) str_copy(G.m_aTag, S["tag"].u.string.ptr, sizeof(G.m_aTag));
		if(S["leader_cid"].type == json_integer) G.m_LeaderCID = (int)S["leader_cid"].u.integer;
		if(S["leader_name"].type == json_string) str_copy(G.m_aLeaderName, S["leader_name"].u.string.ptr, sizeof(G.m_aLeaderName));
		if(S["gold"].type == json_integer) G.m_Gold = (int)S["gold"].u.integer;
		if(S["level"].type == json_integer) G.m_Level = (int)S["level"].u.integer;
		if(S["exp"].type == json_integer) G.m_Experience = (int)S["exp"].u.integer;
		if(S["creation_tick"].type == json_integer) G.m_CreationTick = (int)S["creation_tick"].u.integer;
		if(S["max_members"].type == json_integer) G.m_MaxMembers = (int)S["max_members"].u.integer;
		if(S["motd"].type == json_string) str_copy(G.m_aMotd, S["motd"].u.string.ptr, sizeof(G.m_aMotd));

		const json_value &Members = S["members"];
		if(Members.type == json_array)
		{
			for(unsigned m = 0; m < Members.u.array.length; m++)
			{
				const json_value &MS = Members[(int)m];
				if(MS.type != json_object) continue;

				SGuildMember Member;
				if(MS["cid"].type == json_integer) Member.m_ClientID = (int)MS["cid"].u.integer;
				if(MS["name"].type == json_string) str_copy(Member.m_aName, MS["name"].u.string.ptr, sizeof(Member.m_aName));
				if(MS["rank"].type == json_integer) Member.m_Rank = (EGuildRank)(int)MS["rank"].u.integer;
				if(MS["join_tick"].type == json_integer) Member.m_JoinTick = (int)MS["join_tick"].u.integer;
				if(MS["last_online"].type == json_integer) Member.m_LastOnlineTick = (int)MS["last_online"].u.integer;
				if(MS["contrib"].type == json_integer) Member.m_ContributionGold = (int)MS["contrib"].u.integer;
				if(MS["kills_pve"].type == json_integer) Member.m_KillsPvE = (int)MS["kills_pve"].u.integer;
				if(MS["kills_pvp"].type == json_integer) Member.m_KillsPvP = (int)MS["kills_pvp"].u.integer;

				G.m_Members.add(Member);
			}
		}

		const json_value &Requests = S["join_requests"];
		if(Requests.type == json_array)
		{
			for(unsigned r = 0; r < Requests.u.array.length; r++)
			{
				const json_value &RS = Requests[(int)r];
				if(RS.type != json_object) continue;

				SGuildJoinRequest Req;
				if(RS["account_id"].type == json_integer) Req.m_AccountID = (int)RS["account_id"].u.integer;
				if(RS["name"].type == json_string) str_copy(Req.m_aName, RS["name"].u.string.ptr, sizeof(Req.m_aName));
				if(RS["request_tick"].type == json_integer) Req.m_RequestTick = (int)RS["request_tick"].u.integer;
				if(Req.m_AccountID >= 0)
					G.m_JoinRequests.add(Req);
			}
		}

		m_aGuilds.add(G);
		Loaded++;
	}

	if(m_aGuilds.size() > 0 && m_NextGuildID <= m_aGuilds[m_aGuilds.size() - 1].m_ID)
		m_NextGuildID = m_aGuilds[m_aGuilds.size() - 1].m_ID + 1;

	dbg_msg("guild", "Loaded %d guilds", Loaded);
	return true;
}

// ─── Chat Command Registration ─────────────────────────────────────

void CGuildManager::RegisterChatCommands(CCommandManager *pManager)
{
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("guild", "公会命令。使用 /guild_<子命令> 查看帮助", "", ConGuildCreate, pGame);
	pManager->AddCommand("guild_create", "<名称> [标签] - 创建公会", "s?r", ConGuildCreate, pGame);
	pManager->AddCommand("guild_disband", "解散公会（会长）", "", ConGuildDisband, pGame);
	pManager->AddCommand("guild_invite", "<玩家名> - 邀请加入公会", "s", ConGuildInvite, pGame);
	pManager->AddCommand("guild_accept", "接受公会邀请", "", ConGuildAccept, pGame);
	pManager->AddCommand("guild_decline", "拒绝公会邀请", "", ConGuildDecline, pGame);
	pManager->AddCommand("guild_kick", "<玩家名> - 踢出成员", "s", ConGuildKick, pGame);
	pManager->AddCommand("guild_leave", "退出公会", "", ConGuildLeave, pGame);
	pManager->AddCommand("guild_promote", "<玩家名> - 晋升成员", "s", ConGuildPromote, pGame);
	pManager->AddCommand("guild_demote", "<玩家名> - 降级成员", "s", ConGuildDemote, pGame);
	pManager->AddCommand("guild_leader", "<玩家名> - 转让会长", "s", ConGuildLeader, pGame);
	pManager->AddCommand("guild_donate", "<金币数> - 捐赠金币到公会银行", "i", ConGuildDonate, pGame);
	pManager->AddCommand("guild_motd", "<消息> - 设置公会公告", "r", ConGuildMotd, pGame);
	pManager->AddCommand("guild_info", "查看公会信息", "", ConGuildInfo, pGame);
	pManager->AddCommand("guild_members", "查看公会成员列表", "", ConGuildMembers, pGame);

	// Guild match commands
	pManager->AddCommand("guild_war", "约战命令：/guild_war challenge <公会名> 等", "", ConGuildMatchChallenge, pGame);
	pManager->AddCommand("guild_war_challenge", "<公会名> - 向目标公会发起约战（会长/副会长）", "s", ConGuildMatchChallenge, pGame);
	pManager->AddCommand("guild_war_accept", "接受约战（被挑战方会长/副会长）", "", ConGuildMatchAccept, pGame);
	pManager->AddCommand("guild_war_setmode", "<fng|idm|tdm> - 被挑战方选择比赛模式", "s", ConGuildMatchSetMode, pGame);
	pManager->AddCommand("guild_war_join", "加入比赛队伍", "", ConGuildMatchJoin, pGame);
	pManager->AddCommand("guild_war_leave", "离开比赛队伍", "", ConGuildMatchLeave, pGame);
	pManager->AddCommand("guild_war_start", "开始比赛（双方会长/副会长）", "", ConGuildMatchStart, pGame);
	pManager->AddCommand("guild_war_status", "查看当前比赛状态", "", ConGuildMatchStatus, pGame);
	pManager->AddCommand("guild_war_cancel", "取消比赛", "", ConGuildMatchCancel, pGame);
}

void CGuildManager::RegisterVoteCommands(CCommandManager *pManager)
{
	(void)pManager;
	// Vote menu commands can be added later
}

// ─── Static Callbacks ───────────────────────────────────────────────

void CGuildManager::ConGuildCreate(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	if(pResult->NumArguments() == 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1,
			"═ 公会命令 ─────────────────────╕\n"
			"/guild_create <名称> [标签]  - 创建公会\n"
			"/guild_disband               - 解散公会（会长）\n"
			"/guild_invite <玩家名>       - 邀请加入\n"
			"/guild_accept                - 接受邀请\n"
			"/guild_decline               - 拒绝邀请\n"
			"/guild_kick <玩家名>         - 踢出成员\n"
			"/guild_leave                 - 退出公会\n"
			"/guild_promote <玩家名>      - 晋升\n"
			"/guild_demote <玩家名>       - 降级\n"
			"/guild_leader <玩家名>       - 转让会长\n"
			"/guild_donate <金币>         - 捐赠金币\n"
			"/guild_motd <消息>           - 设置公告\n"
			"/guild_info                  - 公会信息\n"
			"/guild_members               - 成员列表\n"
			"/guild_war_challenge <公会>   - 约战\n"
			"/guild_war_status            - 比赛状态\n"
			"╘────────────────────────────────╛");
		return;
	}

	const char *pName = pResult->GetString(0);
	const char *pTag = pResult->NumArguments() > 1 ? pResult->GetString(1) : 0;

	pGame->Core()->GuildManager()->CreateGuild(pCtx->m_ClientID, pName, pTag);
}

void CGuildManager::ConGuildDisband(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->DisbandGuild(pCtx->m_ClientID);
}

void CGuildManager::ConGuildInvite(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	const char *pName = pResult->GetString(0);
	int TargetCID = pGame->Core()->GuildManager()->FindClientByName(pName);
	if(TargetCID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 找不到该玩家。");
		return;
	}
	pGame->Core()->GuildManager()->InviteMember(pCtx->m_ClientID, TargetCID);
}

void CGuildManager::ConGuildAccept(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->AcceptInvite(pCtx->m_ClientID);
}

void CGuildManager::ConGuildDecline(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->DeclineInvite(pCtx->m_ClientID);
}

void CGuildManager::ConGuildKick(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	const char *pName = pResult->GetString(0);
	int TargetCID = pGame->Core()->GuildManager()->FindClientByName(pName);
	if(TargetCID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 找不到该玩家。");
		return;
	}
	pGame->Core()->GuildManager()->KickMember(pCtx->m_ClientID, TargetCID);
}

void CGuildManager::ConGuildLeave(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->LeaveGuild(pCtx->m_ClientID);
}

void CGuildManager::ConGuildPromote(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	const char *pName = pResult->GetString(0);
	int TargetCID = pGame->Core()->GuildManager()->FindClientByName(pName);
	if(TargetCID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 找不到该玩家。");
		return;
	}
	pGame->Core()->GuildManager()->PromoteMember(pCtx->m_ClientID, TargetCID);
}

void CGuildManager::ConGuildDemote(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	const char *pName = pResult->GetString(0);
	int TargetCID = pGame->Core()->GuildManager()->FindClientByName(pName);
	if(TargetCID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 找不到该玩家。");
		return;
	}
	pGame->Core()->GuildManager()->DemoteMember(pCtx->m_ClientID, TargetCID);
}

void CGuildManager::ConGuildLeader(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	const char *pName = pResult->GetString(0);
	int TargetCID = pGame->Core()->GuildManager()->FindClientByName(pName);
	if(TargetCID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 找不到该玩家。");
		return;
	}
	pGame->Core()->GuildManager()->TransferLeadership(pCtx->m_ClientID, TargetCID);
}

void CGuildManager::ConGuildDonate(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	int Amount = pResult->GetInteger(0);
	pGame->Core()->GuildManager()->DonateGold(pCtx->m_ClientID, Amount);
}

void CGuildManager::ConGuildMotd(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	const char *pMotd = pResult->GetString(0);
	pGame->Core()->GuildManager()->SetMotd(pCtx->m_ClientID, pMotd);
}

void CGuildManager::ConGuildInfo(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->ShowGuildInfo(pCtx->m_ClientID);
}

void CGuildManager::ConGuildMembers(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->ShowGuildMembers(pCtx->m_ClientID);
}

// ─── Guild Match (约战系统) ──────────────────────────────────────────

int CGuildManager::FindGuildIndexByID(int GuildID) const
{
	for(int i = 0; i < m_aGuilds.size(); i++)
		if(m_aGuilds[i].m_ID == GuildID)
			return i;
	return -1;
}

int CGuildManager::FindMatchByPlayer(int ClientID) const
{
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		const SMatchState &M = m_aMatches[mi];
		if(M.m_Status == 4 || M.m_Status == 5)
			continue;
		for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
			if(M.m_aParticipants[pi].m_ClientID == ClientID)
				return mi;
	}
	return -1;
}

int CGuildManager::FindActiveMatchByGuild(int GuildID) const
{
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		const SMatchState &M = m_aMatches[mi];
		if(M.m_Status == 4 || M.m_Status == 5)
			continue;
		if(M.m_ChallengerGuild == GuildID || M.m_ChallengedGuild == GuildID)
			return mi;
	}
	return -1;
}

int CGuildManager::FindPendingMatchByGuildPair(int ChallengerGID, int ChallengedGID) const
{
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		const SMatchState &M = m_aMatches[mi];
		if(M.m_Status >= 4)
			continue;
		if(M.m_ChallengerGuild == ChallengerGID && M.m_ChallengedGuild == ChallengedGID)
			return mi;
		if(M.m_ChallengerGuild == ChallengedGID && M.m_ChallengedGuild == ChallengerGID)
			return mi;
	}
	return -1;
}

int CGuildManager::GetPvPWorldIndex() const
{
	const int Num = Core() ? Core()->WorldManager()->NumWorlds() : 0;
	for(int i = 0; i < Num; i++)
	{
		const CWorldDetail *pDetail = Server()->GetWorldDetail(i);
		if(pDetail && pDetail->GetType() == WorldType::PvP)
			return i;
	}
	return -1;
}

void CGuildManager::BroadcastToMatch(int MatchIdx, const char *pMsg, int ExcludeCID)
{
	if(MatchIdx < 0 || MatchIdx >= m_aMatches.size())
		return;
	const SMatchState &M = m_aMatches[MatchIdx];
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		int CID = M.m_aParticipants[pi].m_ClientID;
		if(CID == ExcludeCID)
			continue;
		if(GS()->m_apPlayers[CID])
			GS()->SendChat(CID, CHAT_ALL, -1, pMsg);
	}
}

void CGuildManager::FinishMatch(int MatchIdx, int WinnerGuildID)
{
	if(MatchIdx < 0 || MatchIdx >= m_aMatches.size())
		return;
	SMatchState &M = m_aMatches[MatchIdx];
	if(M.m_Status != 3)
		return;
	M.m_Status = 4;
	M.m_StartTick = Server()->Tick(); // reuse as end marker

	// Get guild names
	const char *pChallengerName = "?";
	const char *pChallengedName = "?";
	for(int gi = 0; gi < m_aGuilds.size(); gi++)
	{
		if(m_aGuilds[gi].m_ID == M.m_ChallengerGuild)
			pChallengerName = m_aGuilds[gi].m_aName;
		if(m_aGuilds[gi].m_ID == M.m_ChallengedGuild)
			pChallengedName = m_aGuilds[gi].m_aName;
	}

	const int GoldWin = 300, GoldLose = 100, GoldDraw = 200;
	const int RepWin = 100;

	char aMsg[256];
	if(WinnerGuildID < 0)
	{
		str_format(aMsg, sizeof(aMsg),
			"🤝 公会比赛平局！ %s (%d) vs %s (%d) — 双方各获 %d 金币。",
			pChallengerName, M.m_ScoreA, pChallengedName, M.m_ScoreB, GoldDraw);
		GS()->SendChat(-1, CHAT_ALL, -1, aMsg);
	}
	else
	{
		const char *pWinnerName = (WinnerGuildID == M.m_ChallengerGuild) ? pChallengerName : pChallengedName;
		str_format(aMsg, sizeof(aMsg),
			"🏆 公会比赛结束！ %s 获胜！ %s %d : %d %s — 胜方每人 %d 金币+%d声望，败方 %d 金币。",
			pWinnerName,
			pChallengerName, M.m_ScoreA, M.m_ScoreB, pChallengedName,
			GoldWin, RepWin, GoldLose);
		GS()->SendChat(-1, CHAT_ALL, -1, aMsg);
	}

	BroadcastToMatch(MatchIdx, aMsg);

	// Distribute rewards and teleport back
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		const SMatchParticipant &P = M.m_aParticipants[pi];
		int CID = P.m_ClientID;
		CPlayer *pPlayer = GS()->m_apPlayers[CID];
		if(!pPlayer)
			continue;

		// Determine if this participant's guild won
		int PlayerGuildID = pPlayer->GetGuildID();
		int RewardGold = GoldLose;
		int RewardRep = 0;
		if(PlayerGuildID == WinnerGuildID)
		{
			RewardGold = GoldWin;
			RewardRep = RepWin;
		}
		else if(WinnerGuildID < 0)
		{
			RewardGold = GoldDraw;
		}

		pPlayer->SetStat(AttributeIdentifier::Gold, pPlayer->GetStat(AttributeIdentifier::Gold) + RewardGold);
		if(RewardRep > 0)
			pPlayer->SetStat(AttributeIdentifier::Reputation, pPlayer->GetStat(AttributeIdentifier::Reputation) + RewardRep);

		// Reset team to spectator
		pPlayer->SetTeam(TEAM_SPECTATORS);

		// Teleport back to original world
		vec2 SpawnPos = P.m_OriginalPos;
		if(Core() && Core()->WorldManager())
			Core()->WorldManager()->ExecuteWithSpawn(CID, P.m_OriginalWorld, &SpawnPos);

		// Notify
		char aReward[128];
		str_format(aReward, sizeof(aReward),
			"🏆 比赛结束！获得 %d 金币%s", RewardGold, RewardRep > 0 ? "，100声望" : "");
		GS()->SendChat(CID, CHAT_ALL, -1, aReward);
	}

	// Destroy the dynamic arena world
	if(M.m_ArenaWorldID >= 0)
	{
		if(Core() && Core()->WorldManager())
			Core()->WorldManager()->DestroyArenaWorld(M.m_ArenaWorldID);
		M.m_ArenaWorldID = -1;
	}

	SaveMatchToDB(MatchIdx);
}

void CGuildManager::CancelMatch(int MatchIdx)
{
	if(MatchIdx < 0 || MatchIdx >= m_aMatches.size())
		return;
	SMatchState &M = m_aMatches[MatchIdx];
	const bool WasActive = (M.m_Status == 3);
	M.m_Status = 5;

	// Teleport participants back if match was active
	if(WasActive)
	{
		for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
		{
			const SMatchParticipant &P = M.m_aParticipants[pi];
			int CID = P.m_ClientID;
			CPlayer *pPlayer = GS()->m_apPlayers[CID];
			if(!pPlayer) continue;
			pPlayer->SetTeam(TEAM_SPECTATORS);
			vec2 SpawnPos = P.m_OriginalPos;
			if(Core() && Core()->WorldManager())
				Core()->WorldManager()->ExecuteWithSpawn(CID, P.m_OriginalWorld, &SpawnPos);
		}
	}

	// Destroy the dynamic arena world if created
	if(M.m_ArenaWorldID >= 0)
	{
		if(Core() && Core()->WorldManager())
			Core()->WorldManager()->DestroyArenaWorld(M.m_ArenaWorldID);
		M.m_ArenaWorldID = -1;
	}

	SaveMatchToDB(MatchIdx);

	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "❌ 比赛已取消。");
	BroadcastToMatch(MatchIdx, aMsg);
}

void CGuildManager::SaveMatchToDB(int MatchIdx)
{
	if(MatchIdx < 0 || MatchIdx >= m_aMatches.size())
		return;
	const SMatchState &M = m_aMatches[MatchIdx];

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[512];
	const int Now = (int)time(nullptr);

	if(M.m_MatchID > 0)
	{
		// Update existing
		str_format(aQuery, sizeof(aQuery),
			"UPDATE tw_guild_matches SET Status=%d,Mode='%s',TeamSize=%d,TargetScore=%d,"
			"StartedAt=%d,FinishedAt=%d,ScoreA=%d,ScoreB=%d,WinnerGuild=%d "
			"WHERE MatchID=%d",
			M.m_Status, M.m_aMode, M.m_TeamSize, M.m_TargetScore,
			M.m_StartTick, Now, M.m_ScoreA, M.m_ScoreB, -1,
			M.m_MatchID);
		SqlExecQuery(pSql, GS()->Config(), aQuery);
	}
	else
	{
		// Insert new
		str_format(aQuery, sizeof(aQuery),
			"INSERT INTO tw_guild_matches (ChallengerGuild,ChallengedGuild,Status,Mode,TeamSize,TargetScore,CreatedAt) "
			"VALUES (%d,%d,%d,'%s',%d,%d,%d)",
			M.m_ChallengerGuild, M.m_ChallengedGuild, M.m_Status,
			M.m_aMode, M.m_TeamSize, M.m_TargetScore, Now);
		if(SqlExecQuery(pSql, GS()->Config(), aQuery))
		{
			// Get auto-generated MatchID
			str_format(aQuery, sizeof(aQuery), "SELECT LAST_INSERT_ID()");
			if(SqlExecQuery(pSql, GS()->Config(), aQuery))
			{
				MYSQL_RES *pRes = mysql_store_result(pSql);
				if(pRes)
				{
					MYSQL_ROW Row = mysql_fetch_row(pRes);
					if(Row)
						const_cast<SMatchState&>(M).m_MatchID = atoi(Row[0]);
					mysql_free_result(pRes);
				}
			}
		}
	}

	pPool->Release(pRaw);
}

bool CGuildManager::LoadActiveMatchesFromDB()
{
	m_aMatches.clear();

	CSqlConnectionPool *pPool = GS()->Accounts()->GetSqlPool();
	if(!pPool || !pPool->IsInitialized()) return false;
	void *pRaw = pPool->Acquire();
	if(!pRaw) return false;
	MYSQL *pSql = (MYSQL *)pRaw;

	char aQuery[256];
	str_format(aQuery, sizeof(aQuery),
		"SELECT MatchID,ChallengerGuild,ChallengedGuild,Status,Mode,TeamSize,TargetScore,"
		"CreatedAt,StartedAt,ScoreA,ScoreB,WinnerGuild FROM tw_guild_matches "
		"WHERE Status<4 ORDER BY CreatedAt DESC LIMIT 50");

	if(SqlExecQuery(pSql, GS()->Config(), aQuery))
	{
		MYSQL_RES *pRes = mysql_store_result(pSql);
		if(pRes)
		{
			MYSQL_ROW Row;
			while((Row = mysql_fetch_row(pRes)))
			{
				SMatchState M;
				M.m_MatchID = atoi(Row[0]);
				M.m_ChallengerGuild = atoi(Row[1]);
				M.m_ChallengedGuild = atoi(Row[2]);
				M.m_Status = atoi(Row[3]);
				str_copy(M.m_aMode, Row[4] ? Row[4] : "", sizeof(M.m_aMode));
				M.m_TeamSize = atoi(Row[5]);
				M.m_TargetScore = atoi(Row[6]);
				M.m_ArenaWorldID = -1; // fresh on restart; no stale arena world
				// CreatedAt, StartedAt, ScoreA/B, WinnerGuild - stored for history
				m_aMatches.add(M);
			}
			mysql_free_result(pRes);
		}
	}

	pPool->Release(pRaw);
	dbg_msg("guild_match", "Loaded %d matches from DB", m_aMatches.size());
	return true;
}

// ─── Match Command Handlers ─────────────────────────────────────────

void CGuildManager::ConGuildMatchChallenge(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	if(pResult->NumArguments() == 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1,
			"═ 约战命令 ─────────────────────╕\n"
			"/guild_war_challenge <公会名> - 发起挑战\n"
			"/guild_war_accept            - 接受挑战\n"
			"/guild_war_setmode <fng|idm|tdm> - 选模式（被挑战方）\n"
			"/guild_war_join              - 加入比赛\n"
			"/guild_war_leave             - 离开比赛\n"
			"/guild_war_start             - 开始比赛\n"
			"/guild_war_status            - 比赛状态\n"
			"/guild_war_cancel            - 取消比赛\n"
			"╘────────────────────────────────╛");
		return;
	}

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	SGuildData *pGuild = pMgr->GetGuild(PlayerGuildID);
	if(!pGuild)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 公会数据异常。");
		return;
	}

	EGuildRank rank = pGuild->GetRank(pCtx->m_ClientID);
	if(rank != GUILDRANK_LEADER && rank != GUILDRANK_CO_LEADER)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能发起约战。");
		return;
	}

	const char *pTargetName = pResult->GetString(0);
	int TargetGuildID = pMgr->FindGuildByName(pTargetName);
	if(TargetGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 找不到该公会。");
		return;
	}

	if(TargetGuildID == PlayerGuildID)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 不能向自己的公会发起约战。");
		return;
	}

	if(pMgr->FindActiveMatchByGuild(PlayerGuildID) >= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你的公会已有进行中的比赛。");
		return;
	}

	if(pMgr->FindActiveMatchByGuild(TargetGuildID) >= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 目标公会已有进行中的比赛。");
		return;
	}

	// Create match in memory
	SMatchState M;
	M.m_MatchID = 0; // will be assigned on DB save
	M.m_ChallengerGuild = PlayerGuildID;
	M.m_ChallengedGuild = TargetGuildID;
	M.m_Status = 0; // pending
	M.m_aMode[0] = '\0';
	M.m_TeamSize = 3;
	M.m_TargetScore = 30;
	M.m_StartTick = 0;
	M.m_ScoreA = 0;
	M.m_ScoreB = 0;
	M.m_ArenaWorldID = -1;
	pMgr->m_aMatches.add(M);
	const int MatchIdx = pMgr->m_aMatches.size() - 1;

	pMgr->SaveMatchToDB(MatchIdx);

	const SGuildData *pTargetGuild = pMgr->GetGuild(TargetGuildID);
	const char *pTargetName2 = pTargetGuild ? pTargetGuild->m_aName : "?";

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"⚔️ 公会「%s」向「%s」发起约战！被挑战方请使用 /guild_war_accept 接受。",
		pGuild->m_aName, pTargetName2);
	pGame->SendChat(-1, CHAT_ALL, -1, aMsg);

	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "✅ 约战已发起，等待对方接受。");
}

void CGuildManager::ConGuildMatchAccept(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	SGuildData *pGuild = pMgr->GetGuild(PlayerGuildID);
	if(!pGuild)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 公会数据异常。");
		return;
	}

	EGuildRank rank = pGuild->GetRank(pCtx->m_ClientID);
	if(rank != GUILDRANK_LEADER && rank != GUILDRANK_CO_LEADER)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能接受约战。");
		return;
	}

	// Find pending match where this guild is the challenged
	int MatchIdx = -1;
	for(int mi = 0; mi < pMgr->m_aMatches.size(); mi++)
	{
		if(pMgr->m_aMatches[mi].m_Status == 0 && pMgr->m_aMatches[mi].m_ChallengedGuild == PlayerGuildID)
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 没有待接受的约战。");
		return;
	}

	pMgr->m_aMatches[MatchIdx].m_Status = 1; // accepted, choosing mode

	const SGuildData *pChallenger = pMgr->GetGuild(pMgr->m_aMatches[MatchIdx].m_ChallengerGuild);
	const char *pChallengerName = pChallenger ? pChallenger->m_aName : "?";

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"✅ 约战已接受！被挑战方请使用 /guild_war_setmode <fng|idm|tdm> 选择模式。");
	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aMsg);

	str_format(aMsg, sizeof(aMsg),
		"⚔️ 公会「%s」接受了「%s」的约战！正在选择模式...",
		pGuild->m_aName, pChallengerName);
	pMgr->BroadcastToGuild(pMgr->FindGuildIndexByID(pMgr->m_aMatches[MatchIdx].m_ChallengerGuild), aMsg);

	pMgr->SaveMatchToDB(MatchIdx);
}

void CGuildManager::ConGuildMatchSetMode(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	SGuildData *pGuild = pMgr->GetGuild(PlayerGuildID);
	EGuildRank rank = pGuild ? pGuild->GetRank(pCtx->m_ClientID) : GUILDRANK_APPLICANT;
	if(rank != GUILDRANK_LEADER && rank != GUILDRANK_CO_LEADER)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能设置模式。");
		return;
	}

	// Find match where this guild is challenged and status=1
	int MatchIdx = -1;
	for(int mi = 0; mi < pMgr->m_aMatches.size(); mi++)
	{
		if(pMgr->m_aMatches[mi].m_Status == 1 && pMgr->m_aMatches[mi].m_ChallengedGuild == PlayerGuildID)
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 没有等待选择模式的比赛。");
		return;
	}

	const char *pMode = pResult->GetString(0);
	if(str_comp_nocase(pMode, "fng") != 0 && str_comp_nocase(pMode, "idm") != 0 && str_comp_nocase(pMode, "tdm") != 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 无效模式。可用模式：fng, idm, tdm");
		return;
	}

	str_copy(pMgr->m_aMatches[MatchIdx].m_aMode, pMode, sizeof(pMgr->m_aMatches[MatchIdx].m_aMode));
	pMgr->m_aMatches[MatchIdx].m_Status = 2; // ready

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"✅ 比赛模式已设为 %s，请双方使用 /guild_war_join 加入比赛队伍，然后使用 /guild_war_start 开始。",
		pMode);
	pMgr->BroadcastToMatch(MatchIdx, aMsg);
	pMgr->BroadcastToGuild(pMgr->FindGuildIndexByID(pMgr->m_aMatches[MatchIdx].m_ChallengerGuild), aMsg);
	pMgr->BroadcastToGuild(pMgr->FindGuildIndexByID(pMgr->m_aMatches[MatchIdx].m_ChallengedGuild), aMsg);

	pMgr->SaveMatchToDB(MatchIdx);
}

void CGuildManager::ConGuildMatchJoin(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	// Find match where this guild is participating and status=2 (ready)
	int MatchIdx = -1;
	for(int mi = 0; mi < pMgr->m_aMatches.size(); mi++)
	{
		const SMatchState &M = pMgr->m_aMatches[mi];
		if(M.m_Status == 2 && (M.m_ChallengerGuild == PlayerGuildID || M.m_ChallengedGuild == PlayerGuildID))
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 没有等待加入的比赛（请先接受约战并设置模式）。");
		return;
	}

	// Check if already joined
	for(int pi = 0; pi < pMgr->m_aMatches[MatchIdx].m_aParticipants.size(); pi++)
	{
		if(pMgr->m_aMatches[MatchIdx].m_aParticipants[pi].m_ClientID == pCtx->m_ClientID)
		{
			pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你已在比赛队伍中。");
			return;
		}
	}

	// Check max team size not exceeded
	int ChallengerCount = 0, ChallengedCount = 0;
	for(int pi = 0; pi < pMgr->m_aMatches[MatchIdx].m_aParticipants.size(); pi++)
	{
		int CID = pMgr->m_aMatches[MatchIdx].m_aParticipants[pi].m_ClientID;
		if(CID < 0) continue;
		int GID = pGame->m_apPlayers[CID] ? pGame->m_apPlayers[CID]->GetGuildID() : -1;
		if(GID == pMgr->m_aMatches[MatchIdx].m_ChallengerGuild)
			ChallengerCount++;
		else if(GID == pMgr->m_aMatches[MatchIdx].m_ChallengedGuild)
			ChallengedCount++;
	}

	int MaxSize = pMgr->m_aMatches[MatchIdx].m_TeamSize;
	if(PlayerGuildID == pMgr->m_aMatches[MatchIdx].m_ChallengerGuild && ChallengerCount >= MaxSize)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 挑战方队伍已满。");
		return;
	}
	if(PlayerGuildID == pMgr->m_aMatches[MatchIdx].m_ChallengedGuild && ChallengedCount >= MaxSize)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 被挑战方队伍已满。");
		return;
	}

	// Save original position
	SMatchParticipant P;
	P.m_ClientID = pCtx->m_ClientID;
	P.m_OriginalWorld = pGame->Server()->GetClientWorldID(pCtx->m_ClientID);
	CCharacter *pChar = pP->GetCharacter();
	if(pChar)
		P.m_OriginalPos = pChar->GetCore()->m_Pos;
	else
		P.m_OriginalPos = vec2(0.0f, 0.0f);
	P.m_StartTeam = -1;

	pMgr->m_aMatches[MatchIdx].m_aParticipants.add(P);
	pP->m_MatchTeam = (PlayerGuildID == pMgr->m_aMatches[MatchIdx].m_ChallengerGuild) ? -1 : 1;
	pP->m_OriginalWorld = P.m_OriginalWorld;

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"✅ %s 已加入比赛队伍（%s方）。当前人数：挑战方 %d/%d，被挑战方 %d/%d",
		pGame->Server()->ClientName(pCtx->m_ClientID),
		(PlayerGuildID == pMgr->m_aMatches[MatchIdx].m_ChallengerGuild) ? "挑战" : "被挑战",
		(PlayerGuildID == pMgr->m_aMatches[MatchIdx].m_ChallengerGuild) ? ChallengerCount + 1 : ChallengerCount,
		MaxSize,
		(PlayerGuildID == pMgr->m_aMatches[MatchIdx].m_ChallengedGuild) ? ChallengedCount + 1 : ChallengedCount,
		MaxSize);
	pMgr->BroadcastToMatch(MatchIdx, aMsg);
	pMgr->BroadcastToGuild(pMgr->FindGuildIndexByID(pMgr->m_aMatches[MatchIdx].m_ChallengerGuild), aMsg);
	pMgr->BroadcastToGuild(pMgr->FindGuildIndexByID(pMgr->m_aMatches[MatchIdx].m_ChallengedGuild), aMsg);
}

void CGuildManager::ConGuildMatchLeave(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	int MatchIdx = pMgr->FindMatchByPlayer(pCtx->m_ClientID);
	if(MatchIdx < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你未加入任何比赛。");
		return;
	}

	if(pMgr->m_aMatches[MatchIdx].m_Status >= 3)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 比赛已经开始，无法离开。");
		return;
	}

	for(int pi = 0; pi < pMgr->m_aMatches[MatchIdx].m_aParticipants.size(); pi++)
	{
		if(pMgr->m_aMatches[MatchIdx].m_aParticipants[pi].m_ClientID == pCtx->m_ClientID)
		{
			pMgr->m_aMatches[MatchIdx].m_aParticipants.remove_index(pi);
			break;
		}
	}

	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(pP)
	{
		pP->m_MatchTeam = 0;
		pP->m_OriginalWorld = 0;
	}

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "🚪 %s 离开了比赛队伍。", pGame->Server()->ClientName(pCtx->m_ClientID));
	pMgr->BroadcastToMatch(MatchIdx, aMsg);
	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "✅ 已离开比赛队伍。");
}

void CGuildManager::ConGuildMatchStart(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	SGuildData *pGuild = pMgr->GetGuild(PlayerGuildID);
	EGuildRank rank = pGuild ? pGuild->GetRank(pCtx->m_ClientID) : GUILDRANK_APPLICANT;
	if(rank != GUILDRANK_LEADER && rank != GUILDRANK_CO_LEADER)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能开始比赛。");
		return;
	}

	// Find match where this guild is participating and status=2 (ready)
	int MatchIdx = -1;
	for(int mi = 0; mi < pMgr->m_aMatches.size(); mi++)
	{
		const SMatchState &M = pMgr->m_aMatches[mi];
		if(M.m_Status == 2 && (M.m_ChallengerGuild == PlayerGuildID || M.m_ChallengedGuild == PlayerGuildID))
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 没有准备就绪的比赛。请先完成模式设置。");
		return;
	}

	SMatchState &M = pMgr->m_aMatches[MatchIdx];

	// Count participants per guild
	int ChallengerCount = 0, ChallengedCount = 0;
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		int CID = M.m_aParticipants[pi].m_ClientID;
		if(CID < 0) continue;
		int GID = pGame->m_apPlayers[CID] ? pGame->m_apPlayers[CID]->GetGuildID() : -1;
		if(GID == M.m_ChallengerGuild)
			ChallengerCount++;
		else if(GID == M.m_ChallengedGuild)
			ChallengedCount++;
	}

	if(ChallengerCount < M.m_TeamSize || ChallengedCount < M.m_TeamSize)
	{
		char aMsg[256];
		str_format(aMsg, sizeof(aMsg),
			"⚠ 双方人数不足！需要至少 %d 人（当前：挑战方 %d，被挑战方 %d）",
			M.m_TeamSize, ChallengerCount, ChallengedCount);
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aMsg);
		return;
	}

	// Create dynamic arena world for this match
	int ArenaWorldID = pGame->Core()->WorldManager()->CreateArenaWorld(
		"Guild Arena", "pvp", "TDef-Deeply");
	if(ArenaWorldID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 无法创建竞技场。");
		return;
	}
	M.m_ArenaWorldID = ArenaWorldID;

	// Start the match
	M.m_Status = 3;
	M.m_StartTick = pGame->Server()->Tick();
	M.m_ScoreA = 0;
	M.m_ScoreB = 0;

	// Clear all player scores
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		int CID = M.m_aParticipants[pi].m_ClientID;
		CPlayer *pPlayer = pGame->m_apPlayers[CID];
		if(!pPlayer) continue;
		pPlayer->m_Score = 0;
	}

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"🎮 比赛开始！%s vs %s — 模式：%s，目标 %d 分！",
		pMgr->GetGuild(M.m_ChallengerGuild)->m_aName,
		pMgr->GetGuild(M.m_ChallengedGuild)->m_aName,
		M.m_aMode, M.m_TargetScore);
	pMgr->BroadcastToMatch(MatchIdx, aMsg);
	pGame->SendChat(-1, CHAT_ALL, -1, aMsg);

	// Teleport participants to arena world
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		const SMatchParticipant &P = M.m_aParticipants[pi];
		int CID = P.m_ClientID;
		CPlayer *pPlayer = pGame->m_apPlayers[CID];
		if(!pPlayer) continue;

		// Determine team: challenger=red, challenged=blue
		int PlayerGID = pPlayer->GetGuildID();
		int MatchTeam = (PlayerGID == M.m_ChallengerGuild) ? TEAM_RED : TEAM_BLUE;
		pPlayer->SetTeam(MatchTeam);

		// Teleport to dynamic arena world
		vec2 CenterPos(0.0f, 0.0f);
		if(pGame->Core() && pGame->Core()->WorldManager())
			pGame->Core()->WorldManager()->ExecuteWithSpawn(CID, ArenaWorldID, &CenterPos);

		// Apply mode-specific weapon rules
		ApplyMatchModeRules(pPlayer, M.m_aMode);
	}

	pMgr->SaveMatchToDB(MatchIdx);
}

void CGuildManager::ConGuildMatchStatus(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	int MatchIdx = pMgr->FindActiveMatchByGuild(PlayerGuildID);
	if(MatchIdx < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "✅ 你的公会当前没有进行中的比赛。");
		return;
	}

	const SMatchState &M = pMgr->m_aMatches[MatchIdx];
	const char *pStatusStr[] = {"待接受", "选模式中", "准备中", "进行中", "已结束", "已取消"};
	const char *pChallengerName = pMgr->GetGuild(M.m_ChallengerGuild) ? pMgr->GetGuild(M.m_ChallengerGuild)->m_aName : "?";
	const char *pChallengedName = pMgr->GetGuild(M.m_ChallengedGuild) ? pMgr->GetGuild(M.m_ChallengedGuild)->m_aName : "?";

	int ChallengerCount = 0, ChallengedCount = 0;
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		int CID = M.m_aParticipants[pi].m_ClientID;
		if(CID < 0) continue;
		int GID = pGame->m_apPlayers[CID] ? pGame->m_apPlayers[CID]->GetGuildID() : -1;
		if(GID == M.m_ChallengerGuild) ChallengerCount++;
		else if(GID == M.m_ChallengedGuild) ChallengedCount++;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══════ 比赛信息 ═══════");
	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "状态：%s", pStatusStr[M.m_Status > 5 ? 5 : M.m_Status]);
	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "%s（挑战） vs %s（被挑战）", pChallengerName, pChallengedName);
	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "模式：%s | 人数：%d vs %d（目标 %d/%d 人）",
		M.m_aMode[0] ? M.m_aMode : "未选择",
		ChallengerCount, ChallengedCount, M.m_TeamSize, M.m_TeamSize);
	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);

	if(M.m_Status == 3)
	{
		str_format(aBuf, sizeof(aBuf), "比分：%d : %d（目标 %d 分）", M.m_ScoreA, M.m_ScoreB, M.m_TargetScore);
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, aBuf);
	}
}

void CGuildManager::ConGuildMatchCancel(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;

	CGuildManager *pMgr = pGame->Core()->GuildManager();
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	SGuildData *pGuild = pMgr->GetGuild(PlayerGuildID);
	EGuildRank rank = pGuild ? pGuild->GetRank(pCtx->m_ClientID) : GUILDRANK_APPLICANT;
	if(rank != GUILDRANK_LEADER && rank != GUILDRANK_CO_LEADER)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能取消比赛。");
		return;
	}

	int MatchIdx = pMgr->FindActiveMatchByGuild(PlayerGuildID);
	if(MatchIdx < 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 你的公会没有进行中的比赛。");
		return;
	}

	if(pMgr->m_aMatches[MatchIdx].m_Status >= 4)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "⚠ 比赛已经结束或已取消。");
		return;
	}

	pMgr->CancelMatch(MatchIdx);

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "❌ 比赛已被 %s 取消。", pGame->Server()->ClientName(pCtx->m_ClientID));
	pMgr->BroadcastToGuild(pMgr->FindGuildIndexByID(pMgr->m_aMatches[MatchIdx].m_ChallengerGuild), aMsg);
	pMgr->BroadcastToGuild(pMgr->FindGuildIndexByID(pMgr->m_aMatches[MatchIdx].m_ChallengedGuild), aMsg);
	pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "✅ 比赛已取消。");
}



// ─── Mode Enforcement Helpers ────────────────────────────────────────

static void ApplyMatchModeRules(CPlayer *pPlayer, const char *pMode)
{
	if(!pPlayer)
		return;
	CCharacter *pChar = pPlayer->GetCharacter();
	if(!pChar)
		return;

	if(str_comp(pMode, "fng") == 0)
	{
		// FNG: Only fists/hammer allowed
		for(int w = 1; w < NUM_WEAPONS; w++)
			pChar->RemoveWeapon(w);
		pChar->SetWeapon(WEAPON_HAMMER);
	}
	else if(str_comp(pMode, "idm") == 0)
	{
		// iDM: Give all weapons with full ammo
		const int MAX_AMMO = 999;
		for(int w = 0; w < NUM_WEAPONS; w++)
			pChar->GiveWeapon(w, MAX_AMMO);
		pChar->SetWeapon(WEAPON_GUN);
	}
	// TDM: no special weapon rules
}

