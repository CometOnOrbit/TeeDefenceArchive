#include <base/system.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonparser.h>
#include <game/commands.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>

#include <game/server/core/components/mmo/mmo_manager.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/entities/character.h>
#include <game/server/account.h>
#include <game/server/sql_pool.h>
#include <game/server/sql_query.h>
#include <mysql.h>
#include "guild_manager.h"
#include "guild_arena_maps.h"
#include "guild_match_mode.h"

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

// ─── Match mode helpers ──────────────────────────────────────────────

static bool IsWarOfficer(const SGuildData *pGuild, int ClientID)
{
	if(!pGuild)
		return false;
	const EGuildRank Rank = pGuild->GetRank(ClientID);
	return Rank == GUILDRANK_LEADER || Rank == GUILDRANK_CO_LEADER;
}

void CGuildManager::OnCharacterSpawn(CPlayer *pPlayer)
{
	(void)pPlayer;
	// Arena worldmodes apply loadout via CGameController::OnCharacterSpawn.
}

bool CGuildManager::OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason)
{
	(void)pPlayer;
	(void)pCmd;
	(void)pArgs;
	(void)ReasonNumber;
	(void)pReason;
	return false;
}

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

bool CGuildManager::HasPendingInvite(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return m_aPendingInvites[ClientID].m_GuildIdx >= 0;
}

const char *CGuildManager::GetPendingInviterName(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aPendingInvites[ClientID].m_GuildIdx < 0)
		return "";
	return m_aPendingInvites[ClientID].m_aInviterName;
}

void CGuildManager::RegisterChatCommands(CCommandManager *pManager)
{
	if(!pManager) return;
	CGameContext *pGame = GS();

	pManager->AddCommand("guild_info", "查看公会信息", "", ConGuildInfo, pGame);
	// 其余公会操作仅通过投票菜单 ccv_guild_*（RegisterVoteCommands）
}

void CGuildManager::RegisterVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "guild_info", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->ShowGuildInfo(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_leave", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->LeaveGuild(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_challenge", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		const char *pName = pCtx->m_pArgs;
		if(!pName || !pName[0])
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写目标公会名。");
		else
			pG->Core()->GuildManager()->WarChallenge(pCtx->m_ClientID, pName);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_accept", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->WarAccept(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_setmode", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		const char *pMode = pCtx->m_pArgs;
		if(!pMode || !pMode[0])
			pG->SendChatTo(pCtx->m_ClientID, "请选择模式：fng / ctf / tdm / itdm");
		else
			pG->Core()->GuildManager()->WarSetMode(pCtx->m_ClientID, pMode);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_setmap", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		const char *pMap = pCtx->m_pArgs;
		if(!pMap || !pMap[0])
			pG->SendChatTo(pCtx->m_ClientID, "请指定地图路径，例如：def/TDef-Deeply 或 fng/AliveFNG");
		else
			pG->Core()->GuildManager()->WarSetMap(pCtx->m_ClientID, pMap);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_join", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->WarJoin(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_leave", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->WarLeave(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_start", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->WarStart(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_status", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->WarStatus(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_war_cancel", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GuildManager())
			pG->Core()->GuildManager()->WarCancel(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_browse", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_BROWSE, VOTE_PAGE_MMO_GUILD);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_detail", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GetMMOManager())
			return;
		const int GuildID = pR->GetInteger(0);
		if(GuildID <= 0)
			return;
		CVoteMenuManager *pVote = pG->Core()->VoteMenuManager();
		SPlayerVote *pSVote = pVote ? pVote->GetPlayerVote(pCtx->m_ClientID) : nullptr;
		if(pSVote)
		{
			pSVote->m_Select[SPlayerVote::ITEMLIST] = GuildID;
			pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_DETAIL, VOTE_PAGE_MMO_GUILD_BROWSE);
		}
	}, pGame);

	VOTE_CMD(pManager, "guild_apply", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core())
			return;
		const int GuildID = pR->GetInteger(0);
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		if(pGuildMgr && GuildID > 0)
			pGuildMgr->RequestJoinGuild(pCtx->m_ClientID, GuildID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_DETAIL, VOTE_PAGE_MMO_GUILD_BROWSE);
	}, pGame);

	VOTE_CMD(pManager, "guild_create", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core())
			return;
		const char *pText = pCtx->m_pArgs;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		if(!pText || !pText[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写公会名称（可选：名称 标签）。");
			return;
		}
		if(pGuildMgr)
		{
			char aName[MAX_NAME_LENGTH];
			char aTag[8];
			aName[0] = 0;
			aTag[0] = 0;
			const char *pSpace = str_find(pText, " ");
			if(pSpace)
			{
				str_copy(aName, pText, minimum((int)(pSpace - pText) + 1, (int)sizeof(aName)));
				str_copy(aTag, str_skip_whitespaces_const(pSpace + 1), sizeof(aTag));
			}
			else
				str_copy(aName, pText, sizeof(aName));
			pGuildMgr->CreateGuild(pCtx->m_ClientID, aName, aTag[0] ? aTag : nullptr);
		}
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD, VOTE_PAGE_MMO_PVP);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_members_page", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_MEMBERS, VOTE_PAGE_MMO_GUILD);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_requests", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(pG && pG->Core() && pG->Core()->GetMMOManager())
			pG->Core()->GetMMOManager()->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_REQUESTS, VOTE_PAGE_MMO_GUILD);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_req_accept", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core())
			return;
		const int AccountID = pR->GetInteger(0);
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		if(pGuildMgr && AccountID > 0)
			pGuildMgr->AcceptJoinRequest(pCtx->m_ClientID, AccountID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_REQUESTS, VOTE_PAGE_MMO_GUILD);
	}, pGame);

	VOTE_CMD(pManager, "guild_req_deny", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core())
			return;
		const int AccountID = pR->GetInteger(0);
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		if(pGuildMgr && AccountID > 0)
			pGuildMgr->DenyJoinRequest(pCtx->m_ClientID, AccountID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_REQUESTS, VOTE_PAGE_MMO_GUILD);
	}, pGame);

	VOTE_CMD(pManager, "guild_invite", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pName = pCtx->m_pArgs;
		if(!pName || !pName[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
			return;
		}
		const int TargetCID = pGuildMgr->FindClientByName(pName);
		if(TargetCID < 0)
			pG->SendChatTo(pCtx->m_ClientID, "⚠ 找不到该玩家。");
		else
			pGuildMgr->InviteMember(pCtx->m_ClientID, TargetCID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD, VOTE_PAGE_MMO_PVP);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_accept", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pGuildMgr->AcceptInvite(pCtx->m_ClientID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD, VOTE_PAGE_MMO_PVP);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_decline", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pGuildMgr->DeclineInvite(pCtx->m_ClientID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD, VOTE_PAGE_MMO_PVP);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_kick", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pName = pCtx->m_pArgs;
		if(!pName || !pName[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
			return;
		}
		const int TargetCID = pGuildMgr->FindClientByName(pName);
		if(TargetCID < 0)
			pG->SendChatTo(pCtx->m_ClientID, "⚠ 找不到该玩家。");
		else
			pGuildMgr->KickMember(pCtx->m_ClientID, TargetCID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_MEMBERS, VOTE_PAGE_MMO_GUILD);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_promote", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pName = pCtx->m_pArgs;
		if(!pName || !pName[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
			return;
		}
		const int TargetCID = pGuildMgr->FindClientByName(pName);
		if(TargetCID < 0)
			pG->SendChatTo(pCtx->m_ClientID, "⚠ 找不到该玩家。");
		else
			pGuildMgr->PromoteMember(pCtx->m_ClientID, TargetCID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_MEMBERS, VOTE_PAGE_MMO_GUILD);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_demote", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pName = pCtx->m_pArgs;
		if(!pName || !pName[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
			return;
		}
		const int TargetCID = pGuildMgr->FindClientByName(pName);
		if(TargetCID < 0)
			pG->SendChatTo(pCtx->m_ClientID, "⚠ 找不到该玩家。");
		else
			pGuildMgr->DemoteMember(pCtx->m_ClientID, TargetCID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_MEMBERS, VOTE_PAGE_MMO_GUILD);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_leader", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pName = pCtx->m_pArgs;
		if(!pName || !pName[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
			return;
		}
		const int TargetCID = pGuildMgr->FindClientByName(pName);
		if(TargetCID < 0)
			pG->SendChatTo(pCtx->m_ClientID, "⚠ 找不到该玩家。");
		else
			pGuildMgr->TransferLeadership(pCtx->m_ClientID, TargetCID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD_MEMBERS, VOTE_PAGE_MMO_GUILD);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_donate", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pAmountText = pCtx->m_pArgs;
		if(!pAmountText || !pAmountText[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写捐赠金币数量。");
			return;
		}
		pGuildMgr->DonateGold(pCtx->m_ClientID, str_toint(pAmountText));
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD, VOTE_PAGE_MMO_PVP);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_motd", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		const char *pMotd = pCtx->m_pArgs;
		if(!pMotd || !pMotd[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写公告内容。");
			return;
		}
		pGuildMgr->SetMotd(pCtx->m_ClientID, pMotd);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD, VOTE_PAGE_MMO_PVP);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "guild_disband", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!pG || !pG->Core() || !pG->Core()->GuildManager())
			return;
		CGuildManager *pGuildMgr = pG->Core()->GuildManager();
		CMMOManager *pMMO = pG->Core()->GetMMOManager();
		pGuildMgr->DisbandGuild(pCtx->m_ClientID);
		if(pMMO)
			pMMO->OpenVotePage(pCtx->m_ClientID, VOTE_PAGE_MMO_GUILD, VOTE_PAGE_MMO_PVP);
		(void)pR;
	}, pGame);
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
			"═ 公会操作 ─────────────────────╕\n"
			"请打开投票菜单 → PvP → 公会\n"
			"创建、邀请、捐赠、公告等均通过 ccv_guild_* 选项完成\n"
			"/guild_info 仍可查看公会信息（聊天）\n"
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
				M.m_aMap[0] = '\0';
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

// ─── Guild War API ───────────────────────────────────────────────────

bool CGuildManager::WarChallenge(int ClientID, const char *pTargetName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pTargetName || !pTargetName[0])
		return false;

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	SGuildData *pGuild = GetGuild(PlayerGuildID);
	if(!pGuild)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 公会数据异常。");
		return false;
	}

	if(!IsWarOfficer(pGuild, ClientID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能发起约战。");
		return false;
	}

	const int TargetGuildID = FindGuildByName(pTargetName);
	if(TargetGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 找不到该公会。");
		return false;
	}

	if(TargetGuildID == PlayerGuildID)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 不能向自己的公会发起约战。");
		return false;
	}

	if(FindActiveMatchByGuild(PlayerGuildID) >= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你的公会已有进行中的比赛。");
		return false;
	}

	if(FindActiveMatchByGuild(TargetGuildID) >= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 目标公会已有进行中的比赛。");
		return false;
	}

	SMatchState M;
	M.m_MatchID = 0;
	M.m_ChallengerGuild = PlayerGuildID;
	M.m_ChallengedGuild = TargetGuildID;
	M.m_Status = 0;
	M.m_aMode[0] = '\0';
	M.m_aMap[0] = '\0';
	M.m_TeamSize = 3;
	M.m_TargetScore = 30;
	M.m_StartTick = 0;
	M.m_ScoreA = 0;
	M.m_ScoreB = 0;
	M.m_ArenaWorldID = -1;
	m_aMatches.add(M);
	const int MatchIdx = m_aMatches.size() - 1;

	SaveMatchToDB(MatchIdx);

	const SGuildData *pTargetGuild = GetGuild(TargetGuildID);
	const char *pTargetName2 = pTargetGuild ? pTargetGuild->m_aName : "?";

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"⚔️ 公会「%s」向「%s」发起约战！被挑战方请使用 /guild_war_accept 接受。",
		pGuild->m_aName, pTargetName2);
	GS()->SendChat(-1, CHAT_ALL, -1, aMsg);
	GS()->SendChat(ClientID, CHAT_ALL, -1, "✅ 约战已发起，等待对方接受。");
	return true;
}

bool CGuildManager::WarAccept(int ClientID)
{
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	SGuildData *pGuild = GetGuild(PlayerGuildID);
	if(!pGuild)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 公会数据异常。");
		return false;
	}

	if(!IsWarOfficer(pGuild, ClientID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能接受约战。");
		return false;
	}

	int MatchIdx = -1;
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		if(m_aMatches[mi].m_Status == 0 && m_aMatches[mi].m_ChallengedGuild == PlayerGuildID)
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 没有待接受的约战。");
		return false;
	}

	m_aMatches[MatchIdx].m_Status = 1;

	const SGuildData *pChallenger = GetGuild(m_aMatches[MatchIdx].m_ChallengerGuild);
	const char *pChallengerName = pChallenger ? pChallenger->m_aName : "?";

	GS()->SendChat(ClientID, CHAT_ALL, -1,
		"✅ 约战已接受！被挑战方请选择模式：/guild_war_setmode <fng|ctf|tdm|itdm> 或投票菜单。");

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"⚔️ 公会「%s」接受了「%s」的约战！正在选择模式...",
		pGuild->m_aName, pChallengerName);
	BroadcastToGuild(FindGuildIndexByID(m_aMatches[MatchIdx].m_ChallengedGuild), aMsg);
	BroadcastToGuild(FindGuildIndexByID(m_aMatches[MatchIdx].m_ChallengerGuild), aMsg);

	SaveMatchToDB(MatchIdx);
	return true;
}

bool CGuildManager::WarSetMode(int ClientID, const char *pMode)
{
	if(!pMode || !pMode[0])
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请指定模式：fng, ctf, tdm, itdm");
		return false;
	}

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	SGuildData *pGuild = GetGuild(PlayerGuildID);
	if(!IsWarOfficer(pGuild, ClientID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能设置模式。");
		return false;
	}

	int MatchIdx = -1;
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		if(m_aMatches[mi].m_Status == 1 && m_aMatches[mi].m_ChallengedGuild == PlayerGuildID)
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 没有等待选择模式的比赛。");
		return false;
	}

	if(!IsValidGuildWarMode(pMode))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 无效模式。可用：fng, ctf, tdm, itdm（idm 兼容旧版）");
		return false;
	}

	const EGuildWarMode Mode = ParseGuildWarMode(pMode);
	str_copy(m_aMatches[MatchIdx].m_aMode, GuildWarModeToString(Mode), sizeof(m_aMatches[MatchIdx].m_aMode));
	m_aMatches[MatchIdx].m_TargetScore = GuildWarDefaultTargetScore(m_aMatches[MatchIdx].m_aMode);
	m_aMatches[MatchIdx].m_Status = 2;
	GuildWarDefaultArenaMap(Storage(), m_aMatches[MatchIdx].m_aMode,
		m_aMatches[MatchIdx].m_aMap, sizeof(m_aMatches[MatchIdx].m_aMap));

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"✅ 比赛模式：%s（目标 %d 分）。默认地图：%s。双方 /guild_war_join 加入，"
		"会长 /guild_war_setmap 换图，/guild_war_start 开始。",
		GuildWarModeDisplayName(m_aMatches[MatchIdx].m_aMode),
		m_aMatches[MatchIdx].m_TargetScore,
		m_aMatches[MatchIdx].m_aMap[0] ? m_aMatches[MatchIdx].m_aMap : "（未设置）");
	BroadcastToMatch(MatchIdx, aMsg);
	BroadcastToGuild(FindGuildIndexByID(m_aMatches[MatchIdx].m_ChallengerGuild), aMsg);
	BroadcastToGuild(FindGuildIndexByID(m_aMatches[MatchIdx].m_ChallengedGuild), aMsg);

	SaveMatchToDB(MatchIdx);
	return true;
}

bool CGuildManager::WarSetMap(int ClientID, const char *pMap)
{
	if(!pMap || !pMap[0])
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请指定地图路径，例如：def/TDef-Deeply");
		return false;
	}

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	SGuildData *pGuild = GetGuild(PlayerGuildID);
	if(!IsWarOfficer(pGuild, ClientID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能设置地图。");
		return false;
	}

	int MatchIdx = -1;
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		const SMatchState &M = m_aMatches[mi];
		if(M.m_Status == 2 && (M.m_ChallengerGuild == PlayerGuildID || M.m_ChallengedGuild == PlayerGuildID))
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 没有可设置地图的比赛（需先选择模式且未开始）。");
		return false;
	}

	SMatchState &M = m_aMatches[MatchIdx];
	if(M.m_aMode[0] == '\0')
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 尚未选择比赛模式。");
		return false;
	}

	if(!IsArenaMapAllowedForMode(pMap, M.m_aMode))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 该地图不适用于当前比赛模式。");
		return false;
	}

	if(!ArenaMapFileExists(Storage(), pMap))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 找不到该地图文件。");
		return false;
	}

	str_copy(M.m_aMap, pMap, sizeof(M.m_aMap));

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"🗺️ 比赛地图已设为：%s（模式：%s）",
		M.m_aMap, GuildWarModeDisplayName(M.m_aMode));
	BroadcastToMatch(MatchIdx, aMsg);
	BroadcastToGuild(FindGuildIndexByID(M.m_ChallengerGuild), aMsg);
	BroadcastToGuild(FindGuildIndexByID(M.m_ChallengedGuild), aMsg);

	SaveMatchToDB(MatchIdx);
	return true;
}

bool CGuildManager::GetActiveWarMatchInfo(int ClientID, char *pMode, int ModeSize, char *pMap, int MapSize, int *pStatus)
{
	if(pMode && ModeSize > 0)
		pMode[0] = '\0';
	if(pMap && MapSize > 0)
		pMap[0] = '\0';
	if(pStatus)
		*pStatus = -1;

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
		return false;

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
		return false;

	const int MatchIdx = FindActiveMatchByGuild(PlayerGuildID);
	if(MatchIdx < 0)
		return false;

	const SMatchState &M = m_aMatches[MatchIdx];
	if(pStatus)
		*pStatus = M.m_Status;
	if(pMode && ModeSize > 0)
		str_copy(pMode, M.m_aMode, ModeSize);
	if(pMap && MapSize > 0)
		str_copy(pMap, M.m_aMap, MapSize);
	return true;
}

bool CGuildManager::WarJoin(int ClientID)
{
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	int MatchIdx = -1;
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		const SMatchState &M = m_aMatches[mi];
		if(M.m_Status == 2 && (M.m_ChallengerGuild == PlayerGuildID || M.m_ChallengedGuild == PlayerGuildID))
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 没有等待加入的比赛（请先接受约战并设置模式）。");
		return false;
	}

	for(int pi = 0; pi < m_aMatches[MatchIdx].m_aParticipants.size(); pi++)
	{
		if(m_aMatches[MatchIdx].m_aParticipants[pi].m_ClientID == ClientID)
		{
			GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你已在比赛队伍中。");
			return false;
		}
	}

	int ChallengerCount = 0, ChallengedCount = 0;
	for(int pi = 0; pi < m_aMatches[MatchIdx].m_aParticipants.size(); pi++)
	{
		const int CID = m_aMatches[MatchIdx].m_aParticipants[pi].m_ClientID;
		if(CID < 0)
			continue;
		const int GID = GS()->m_apPlayers[CID] ? GS()->m_apPlayers[CID]->GetGuildID() : -1;
		if(GID == m_aMatches[MatchIdx].m_ChallengerGuild)
			ChallengerCount++;
		else if(GID == m_aMatches[MatchIdx].m_ChallengedGuild)
			ChallengedCount++;
	}

	const int MaxSize = m_aMatches[MatchIdx].m_TeamSize;
	if(PlayerGuildID == m_aMatches[MatchIdx].m_ChallengerGuild && ChallengerCount >= MaxSize)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 挑战方队伍已满。");
		return false;
	}
	if(PlayerGuildID == m_aMatches[MatchIdx].m_ChallengedGuild && ChallengedCount >= MaxSize)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 被挑战方队伍已满。");
		return false;
	}

	SMatchParticipant Part;
	Part.m_ClientID = ClientID;
	Part.m_OriginalWorld = GS()->Server()->GetClientWorldID(ClientID);
	CCharacter *pChar = pP->GetCharacter();
	Part.m_OriginalPos = pChar ? pChar->GetCore()->m_Pos : vec2(0.0f, 0.0f);
	Part.m_StartTeam = -1;
	m_aMatches[MatchIdx].m_aParticipants.add(Part);
	pP->m_MatchTeam = (PlayerGuildID == m_aMatches[MatchIdx].m_ChallengerGuild) ? -1 : 1;
	pP->m_OriginalWorld = Part.m_OriginalWorld;

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"✅ %s 已加入比赛队伍（%s方）。当前人数：挑战方 %d/%d，被挑战方 %d/%d",
		GS()->Server()->ClientName(ClientID),
		(PlayerGuildID == m_aMatches[MatchIdx].m_ChallengerGuild) ? "挑战" : "被挑战",
		(PlayerGuildID == m_aMatches[MatchIdx].m_ChallengerGuild) ? ChallengerCount + 1 : ChallengerCount,
		MaxSize,
		(PlayerGuildID == m_aMatches[MatchIdx].m_ChallengedGuild) ? ChallengedCount + 1 : ChallengedCount,
		MaxSize);
	BroadcastToMatch(MatchIdx, aMsg);
	return true;
}

bool CGuildManager::WarLeave(int ClientID)
{
	const int MatchIdx = FindMatchByPlayer(ClientID);
	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你未加入任何比赛。");
		return false;
	}

	if(m_aMatches[MatchIdx].m_Status >= 3)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 比赛已经开始，无法离开。");
		return false;
	}

	for(int pi = 0; pi < m_aMatches[MatchIdx].m_aParticipants.size(); pi++)
	{
		if(m_aMatches[MatchIdx].m_aParticipants[pi].m_ClientID == ClientID)
		{
			m_aMatches[MatchIdx].m_aParticipants.remove_index(pi);
			break;
		}
	}

	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(pP)
	{
		pP->m_MatchTeam = 0;
		pP->m_OriginalWorld = 0;
	}

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "🚪 %s 离开了比赛队伍。", GS()->Server()->ClientName(ClientID));
	BroadcastToMatch(MatchIdx, aMsg);
	GS()->SendChat(ClientID, CHAT_ALL, -1, "✅ 已离开比赛队伍。");
	return true;
}

bool CGuildManager::WarStart(int ClientID)
{
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	SGuildData *pGuild = GetGuild(PlayerGuildID);
	if(!IsWarOfficer(pGuild, ClientID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能开始比赛。");
		return false;
	}

	int MatchIdx = -1;
	for(int mi = 0; mi < m_aMatches.size(); mi++)
	{
		const SMatchState &M = m_aMatches[mi];
		if(M.m_Status == 2 && (M.m_ChallengerGuild == PlayerGuildID || M.m_ChallengedGuild == PlayerGuildID))
		{
			MatchIdx = mi;
			break;
		}
	}

	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 没有准备就绪的比赛。请先完成模式设置。");
		return false;
	}

	SMatchState &M = m_aMatches[MatchIdx];
	if(M.m_aMode[0] == '\0')
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 尚未选择比赛模式。");
		return false;
	}

	int ChallengerCount = 0, ChallengedCount = 0;
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		const int CID = M.m_aParticipants[pi].m_ClientID;
		if(CID < 0)
			continue;
		const int GID = GS()->m_apPlayers[CID] ? GS()->m_apPlayers[CID]->GetGuildID() : -1;
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
		GS()->SendChat(ClientID, CHAT_ALL, -1, aMsg);
		return false;
	}

	const char *pArenaMap = M.m_aMap[0] ? M.m_aMap : nullptr;
	char aDefaultMap[128];
	if(!pArenaMap)
	{
		GuildWarDefaultArenaMap(Storage(), M.m_aMode, aDefaultMap, sizeof(aDefaultMap));
		pArenaMap = aDefaultMap;
	}
	const int ArenaWorldID = Core()->WorldManager()->CreateArenaWorld("Guild Arena", M.m_aMode, pArenaMap);
	if(ArenaWorldID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 无法创建竞技场。");
		return false;
	}
	M.m_ArenaWorldID = ArenaWorldID;
	M.m_Status = 3;
	M.m_StartTick = GS()->Server()->Tick();
	M.m_ScoreA = 0;
	M.m_ScoreB = 0;

	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		const int CID = M.m_aParticipants[pi].m_ClientID;
		CPlayer *pPlayer = GS()->m_apPlayers[CID];
		if(pPlayer)
			pPlayer->m_Score = 0;
	}

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"🎮 比赛开始！%s vs %s — 模式：%s，地图：%s，目标 %d 分！",
		GetGuild(M.m_ChallengerGuild)->m_aName,
		GetGuild(M.m_ChallengedGuild)->m_aName,
		GuildWarModeDisplayName(M.m_aMode),
		pArenaMap,
		M.m_TargetScore);
	BroadcastToMatch(MatchIdx, aMsg);
	GS()->SendChat(-1, CHAT_ALL, -1, aMsg);

	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		const SMatchParticipant &P = M.m_aParticipants[pi];
		const int CID = P.m_ClientID;
		CPlayer *pPlayer = GS()->m_apPlayers[CID];
		if(!pPlayer)
			continue;

		const int PlayerGID = pPlayer->GetGuildID();
		const int MatchTeam = (PlayerGID == M.m_ChallengerGuild) ? TEAM_RED : TEAM_BLUE;
		pPlayer->SetTeam(MatchTeam);

		vec2 CenterPos(0.0f, 0.0f);
		if(Core() && Core()->WorldManager())
			Core()->WorldManager()->ExecuteWithSpawn(CID, ArenaWorldID, &CenterPos);
	}

	SaveMatchToDB(MatchIdx);
	return true;
}

void CGuildManager::WarStatus(int ClientID)
{
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return;
	}

	const int MatchIdx = FindActiveMatchByGuild(PlayerGuildID);
	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "✅ 你的公会当前没有进行中的比赛。");
		return;
	}

	const SMatchState &M = m_aMatches[MatchIdx];
	const char *pStatusStr[] = {"待接受", "选模式中", "准备中", "进行中", "已结束", "已取消"};
	const char *pChallengerName = GetGuild(M.m_ChallengerGuild) ? GetGuild(M.m_ChallengerGuild)->m_aName : "?";
	const char *pChallengedName = GetGuild(M.m_ChallengedGuild) ? GetGuild(M.m_ChallengedGuild)->m_aName : "?";

	int ChallengerCount = 0, ChallengedCount = 0;
	for(int pi = 0; pi < M.m_aParticipants.size(); pi++)
	{
		const int CID = M.m_aParticipants[pi].m_ClientID;
		if(CID < 0)
			continue;
		const int GID = GS()->m_apPlayers[CID] ? GS()->m_apPlayers[CID]->GetGuildID() : -1;
		if(GID == M.m_ChallengerGuild)
			ChallengerCount++;
		else if(GID == M.m_ChallengedGuild)
			ChallengedCount++;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "═══════ 比赛信息 ═══════");
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "状态：%s", pStatusStr[M.m_Status > 5 ? 5 : M.m_Status]);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "%s（挑战） vs %s（被挑战）", pChallengerName, pChallengedName);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	str_format(aBuf, sizeof(aBuf), "模式：%s | 人数：%d vs %d（目标 %d/%d 人）",
		M.m_aMode[0] ? GuildWarModeDisplayName(M.m_aMode) : "未选择",
		ChallengerCount, ChallengedCount, M.m_TeamSize, M.m_TeamSize);
	GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);

	if(M.m_aMode[0] != '\0')
	{
		char aMapBuf[128];
		if(M.m_aMap[0])
			str_copy(aMapBuf, M.m_aMap, sizeof(aMapBuf));
		else
			GuildWarDefaultArenaMap(Storage(), M.m_aMode, aMapBuf, sizeof(aMapBuf));
		str_format(aBuf, sizeof(aBuf), "地图：%s%s", aMapBuf, M.m_aMap[0] ? "" : "（默认）");
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	}

	if(M.m_Status == 3)
	{
		str_format(aBuf, sizeof(aBuf), "比分：%d : %d（目标 %d 分）", M.m_ScoreA, M.m_ScoreB, M.m_TargetScore);
		GS()->SendChat(ClientID, CHAT_ALL, -1, aBuf);
	}
}

bool CGuildManager::WarCancel(int ClientID)
{
	CPlayer *pP = GS()->m_apPlayers[ClientID];
	if(!pP || pP->GetAccountId() <= 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 请先登录。");
		return false;
	}

	const int PlayerGuildID = pP->GetGuildID();
	if(PlayerGuildID < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你不在公会中。");
		return false;
	}

	SGuildData *pGuild = GetGuild(PlayerGuildID);
	if(!IsWarOfficer(pGuild, ClientID))
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 只有会长和副会长才能取消比赛。");
		return false;
	}

	const int MatchIdx = FindActiveMatchByGuild(PlayerGuildID);
	if(MatchIdx < 0)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 你的公会没有进行中的比赛。");
		return false;
	}

	if(m_aMatches[MatchIdx].m_Status >= 4)
	{
		GS()->SendChat(ClientID, CHAT_ALL, -1, "⚠ 比赛已经结束或已取消。");
		return false;
	}

	CancelMatch(MatchIdx);

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "❌ 比赛已被 %s 取消。", GS()->Server()->ClientName(ClientID));
	BroadcastToGuild(FindGuildIndexByID(m_aMatches[MatchIdx].m_ChallengerGuild), aMsg);
	BroadcastToGuild(FindGuildIndexByID(m_aMatches[MatchIdx].m_ChallengedGuild), aMsg);
	GS()->SendChat(ClientID, CHAT_ALL, -1, "✅ 比赛已取消。");
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
			"/guild_war_setmode <fng|ctf|tdm|itdm> - 选模式（被挑战方）\n"
			"/guild_war_setmap <路径>         - 选地图（会长/副会长）\n"
			"/guild_war_join              - 加入比赛\n"
			"/guild_war_leave             - 离开比赛\n"
			"/guild_war_start             - 开始比赛\n"
			"/guild_war_status            - 比赛状态\n"
			"/guild_war_cancel            - 取消比赛\n"
			"╘────────────────────────────────╛");
		return;
	}

	pGame->Core()->GuildManager()->WarChallenge(pCtx->m_ClientID, pResult->GetString(0));
}

void CGuildManager::ConGuildMatchAccept(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->WarAccept(pCtx->m_ClientID);
}

void CGuildManager::ConGuildMatchSetMode(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->WarSetMode(pCtx->m_ClientID, pResult->GetString(0));
}

void CGuildManager::ConGuildMatchSetMap(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	if(pResult->NumArguments() == 0)
	{
		pGame->SendChat(pCtx->m_ClientID, CHAT_ALL, -1, "用法：/guild_war_setmap <地图路径>，例如 def/TDef-Deeply");
		return;
	}
	pGame->Core()->GuildManager()->WarSetMap(pCtx->m_ClientID, pResult->GetString(0));
}

void CGuildManager::ConGuildMatchJoin(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->WarJoin(pCtx->m_ClientID);
}

void CGuildManager::ConGuildMatchLeave(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->WarLeave(pCtx->m_ClientID);
}

void CGuildManager::ConGuildMatchStart(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->WarStart(pCtx->m_ClientID);
}

void CGuildManager::ConGuildMatchStatus(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->WarStatus(pCtx->m_ClientID);
}

void CGuildManager::ConGuildMatchCancel(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame || !pGame->Core() || !pGame->Core()->GuildManager())
		return;
	pGame->Core()->GuildManager()->WarCancel(pCtx->m_ClientID);
}
