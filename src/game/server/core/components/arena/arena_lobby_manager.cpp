#include "arena_lobby_manager.h"

#include <game/server/core/components/defence/defence_lobby_manager.h>

#include <base/system.h>
#include <game/commands.h>
#include <game/server/gamecontext.h>
#include <game/server/global_state.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/components/vote/vote_wrapper.h>
#include <game/server/core/components/vote/vote_menu_types.h>
#include <game/server/core/components/worlds/world_manager.h>
#include <game/server/core/components/guilds/guild_match_mode.h>
#include <game/server/core/components/guilds/guild_arena_maps.h>

#include <vector>
#include <string>

static constexpr int ARENA_LOBBY_COST = 100;
static constexpr int ARENA_LOBBY_SECONDS = 60;
static constexpr int ARENA_LOBBY_TICKS = ARENA_LOBBY_SECONDS * 10; // ~10 tick/s
static constexpr int ARENA_MAX_INVITEES = 15;

// Cross-world lobby state (shared by all world instances)
static array<SArenaLobby> gs_aLobbies;
static int gs_aPendingInviteLobby[MAX_CLIENTS];
static char gs_aDraftMode[MAX_CLIENTS][16];
static char gs_aDraftMap[MAX_CLIENTS][128];
static CArenaLobbyManager *gs_pPrimaryManager = nullptr;

static CPlayer *FindPlayerCrossWorld(IServer *pServer, int ClientID)
{
	if(!pServer || ClientID < 0 || ClientID >= MAX_CLIENTS)
		return nullptr;
	const int WID = pServer->GetClientWorldID(ClientID);
	if(WID < 0)
		return nullptr;
	IGameServer *pIGS = pServer->GameServer(WID);
	if(!pIGS)
		return nullptr;
	return ((CGameContext *)pIGS)->m_apPlayers[ClientID];
}

static void SendChatToCross(IServer *pServer, int ClientID, const char *pText)
{
	if(!pServer || ClientID < 0 || !pText)
		return;
	const int WID = pServer->GetClientWorldID(ClientID);
	if(WID < 0)
		return;
	IGameServer *pIGS = pServer->GameServer(WID);
	if(!pIGS)
		return;
	((CGameContext *)pIGS)->SendChatTo(ClientID, pText);
}

static CWorldManager *WorldManagerForClient(IServer *pServer, int ClientID)
{
	if(!pServer || ClientID < 0)
		return nullptr;
	const int WID = pServer->GetClientWorldID(ClientID);
	if(WID < 0)
		return nullptr;
	IGameServer *pIGS = pServer->GameServer(WID);
	if(!pIGS)
		return nullptr;
	CGameContext *pGS = (CGameContext *)pIGS;
	if(!pGS->Core())
		return nullptr;
	return pGS->Core()->WorldManager();
}

static bool CrossWorldExecuteSpawn(IServer *pServer, int ClientID, int WorldID, vec2 *pPos)
{
	CWorldManager *pWM = WorldManagerForClient(pServer, ClientID);
	if(!pWM)
		return false;
	return pWM->ExecuteWithSpawn(ClientID, WorldID, pPos, true);
}

static const char *VoteArgText(const char *pArgs, const char *pReason)
{
	if(pArgs && pArgs[0])
		return pArgs;
	if(pReason && pReason[0])
		return pReason;
	return nullptr;
}

static void ParsePlayerNames(const char *pText, std::vector<std::string> &Out)
{
	Out.clear();
	if(!pText || !pText[0])
		return;

	char aBuf[512];
	str_copy(aBuf, pText, sizeof(aBuf));
	char *pWrite = aBuf;
	for(char *p = aBuf; *p; p++)
	{
		if(*p == ',' || *p == ';')
			*p = ' ';
	}
	while(*pWrite)
	{
		while(*pWrite == ' ')
			pWrite++;
		if(!*pWrite)
			break;
		char *pStart = pWrite;
		while(*pWrite && *pWrite != ' ')
			pWrite++;
		if(*pWrite)
			*pWrite++ = '\0';
		if(pStart[0])
			Out.emplace_back(pStart);
	}
}

static CVoteWrapper ArenaPage(int ClientID, CGameContext *pGS, CVoteMenuManager *pVote, int LastPage, const char *pTitle)
{
	pVote->SetVoteLastPage(LastPage);
	pVote->SetVoteBuildClientID(ClientID);
	CVoteWrapper V(ClientID, pGS, pVote);
	V.GroupTitle(pTitle);
	return V;
}

CArenaLobbyManager::CArenaLobbyManager()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		gs_aPendingInviteLobby[i] = -1;
		gs_aDraftMode[i][0] = '\0';
		gs_aDraftMap[i][0] = '\0';
	}
	if(!gs_pPrimaryManager)
		gs_pPrimaryManager = this;
}

CArenaLobbyManager *CArenaLobbyManager::ActiveManager() const
{
	return gs_pPrimaryManager ? gs_pPrimaryManager : const_cast<CArenaLobbyManager *>(this);
}

void CArenaLobbyManager::OnConsoleInit()
{
	(void)this;
}

int CArenaLobbyManager::FindLobbyByHost(int HostCID) const
{
	for(int i = 0; i < gs_aLobbies.size(); i++)
	{
		const SArenaLobby &L = gs_aLobbies[i];
		if(L.m_HostClientID == HostCID && (L.m_Status == LOBBY_WAITING || L.m_Status == LOBBY_ACTIVE))
			return i;
	}
	return -1;
}

int CArenaLobbyManager::FindLobbyByParticipant(int ClientID) const
{
	for(int i = 0; i < gs_aLobbies.size(); i++)
	{
		const SArenaLobby &L = gs_aLobbies[i];
		if(L.m_Status != LOBBY_WAITING && L.m_Status != LOBBY_ACTIVE)
			continue;
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			if(L.m_aParticipants[pi].m_ClientID == ClientID)
				return i;
		}
	}
	return -1;
}

int CArenaLobbyManager::FindLobbyIndex(int ClientID) const
{
	const int HostIdx = FindLobbyByHost(ClientID);
	if(HostIdx >= 0)
		return HostIdx;
	return FindLobbyByParticipant(ClientID);
}

int CArenaLobbyManager::CountAccepted(int LobbyIdx) const
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
		return 0;
	int Count = 0;
	const SArenaLobby &L = gs_aLobbies[LobbyIdx];
	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		if(L.m_aParticipants[pi].m_Accepted)
			Count++;
	}
	return Count;
}

bool CArenaLobbyManager::IsPlayerBusyForArena(int ClientID, int ExcludeLobbyIdx) const
{
	if(!Server())
		return true;

	CPlayer *pP = FindPlayerCrossWorld(Server(), ClientID);
	if(!pP || pP->GetAccountId() <= 0)
		return true;
	if(pP->m_MatchTeam != 0)
		return true;

	for(int i = 0; i < gs_aLobbies.size(); i++)
	{
		if(i == ExcludeLobbyIdx)
			continue;
		const SArenaLobby &L = gs_aLobbies[i];
		if(L.m_Status != LOBBY_WAITING && L.m_Status != LOBBY_ACTIVE)
			continue;
		if(L.m_HostClientID == ClientID)
			return true;
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			if(L.m_aParticipants[pi].m_ClientID == ClientID)
				return true;
		}
		if(L.m_Status == LOBBY_WAITING)
		{
			for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
			{
				if(L.m_aPendingInvitees[pi] == ClientID)
					return true;
			}
		}
	}
	return false;
}

bool CArenaLobbyManager::GetActiveLobbyInfo(int ClientID, char *pMode, int ModeSize, char *pMap, int MapSize, int *pStatus, int *pSecondsLeft) const
{
	const int LobbyIdx = FindLobbyIndex(ClientID);
	if(LobbyIdx < 0)
		return false;

	const SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(pMode && ModeSize > 0)
		str_copy(pMode, L.m_aMode, ModeSize);
	if(pMap && MapSize > 0)
		str_copy(pMap, L.m_aMap, MapSize);
	if(pStatus)
		*pStatus = L.m_Status;
	if(pSecondsLeft && Server())
	{
		const int Remaining = L.m_ExpiryTick - Server()->Tick();
		*pSecondsLeft = Remaining > 0 ? Remaining / 10 : 0;
	}
	return true;
}

bool CArenaLobbyManager::SetArenaMode(int HostCID, const char *pMode)
{
	if(!pMode || !IsValidGuildWarMode(pMode))
	{
		SendChatToCross(Server(), HostCID, "⚠ 无效模式。可选：fng / ctf / tdm / itdm");
		return false;
	}

	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx >= 0)
	{
		SArenaLobby &L = gs_aLobbies[LobbyIdx];
		if(L.m_Status != LOBBY_WAITING)
		{
			SendChatToCross(Server(), HostCID, "⚠ 比赛已开始，无法更换模式。");
			return false;
		}
		str_copy(L.m_aMode, pMode, sizeof(L.m_aMode));
		L.m_TargetScore = GuildWarDefaultTargetScore(pMode);
		L.m_aMap[0] = '\0';
	}
	else
	{
		str_copy(gs_aDraftMode[HostCID], pMode, sizeof(gs_aDraftMode[HostCID]));
		gs_aDraftMap[HostCID][0] = '\0';
	}

	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "✅ 竞技场模式：%s（目标 %d 分）",
		GuildWarModeDisplayName(pMode), GuildWarDefaultTargetScore(pMode));
	SendChatToCross(Server(), HostCID, aMsg);
	return true;
}

bool CArenaLobbyManager::SetArenaMap(int HostCID, const char *pMap)
{
	if(!pMap || !pMap[0])
	{
		SendChatToCross(Server(), HostCID, "⚠ 请指定地图路径。");
		return false;
	}

	const char *pMode = nullptr;
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx >= 0)
	{
		SArenaLobby &L = gs_aLobbies[LobbyIdx];
		if(L.m_Status != LOBBY_WAITING)
		{
			SendChatToCross(Server(), HostCID, "⚠ 比赛已开始，无法更换地图。");
			return false;
		}
		pMode = L.m_aMode;
	}
	else if(gs_aDraftMode[HostCID][0])
	{
		pMode = gs_aDraftMode[HostCID];
	}
	else
	{
		SendChatToCross(Server(), HostCID, "⚠ 请先选择比赛模式。");
		return false;
	}

	if(!IsArenaMapAllowedForMode(pMap, pMode) || !ArenaMapFileExists(Storage(), pMap))
	{
		SendChatToCross(Server(), HostCID, "⚠ 该地图不适用于当前模式或文件不存在。");
		return false;
	}

	if(LobbyIdx >= 0)
		str_copy(gs_aLobbies[LobbyIdx].m_aMap, pMap, sizeof(gs_aLobbies[LobbyIdx].m_aMap));
	else
		str_copy(gs_aDraftMap[HostCID], pMap, sizeof(gs_aDraftMap[HostCID]));

	char aMsg[160];
	str_format(aMsg, sizeof(aMsg), "✅ 竞技场地图：%s", pMap);
	SendChatToCross(Server(), HostCID, aMsg);
	return true;
}

bool CArenaLobbyManager::CreateLobby(int HostCID, const char *pMode, const char *pMap)
{
	if(!Server() || !GS())
		return false;

	CPlayer *pHost = FindPlayerCrossWorld(Server(), HostCID);
	if(!pHost || pHost->GetAccountId() <= 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 请先登录。");
		return false;
	}

	if(FindLobbyByHost(HostCID) >= 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你已有一个进行中的竞技场大厅。");
		return false;
	}

	char aMode[16];
	if(pMode && pMode[0])
		str_copy(aMode, pMode, sizeof(aMode));
	else if(gs_aDraftMode[HostCID][0])
		str_copy(aMode, gs_aDraftMode[HostCID], sizeof(aMode));
	else
	{
		SendChatToCross(Server(), HostCID, "⚠ 请指定模式：fng / ctf / tdm / itdm");
		return false;
	}

	if(!IsValidGuildWarMode(aMode))
	{
		SendChatToCross(Server(), HostCID, "⚠ 无效模式。可选：fng / ctf / tdm / itdm");
		return false;
	}

	char aMap[128];
	if(pMap && pMap[0])
		str_copy(aMap, pMap, sizeof(aMap));
	else if(gs_aDraftMap[HostCID][0])
		str_copy(aMap, gs_aDraftMap[HostCID], sizeof(aMap));
	else
		GuildWarDefaultArenaMap(Storage(), aMode, aMap, sizeof(aMap));

	if(!IsArenaMapAllowedForMode(aMap, aMode) || !ArenaMapFileExists(Storage(), aMap))
	{
		SendChatToCross(Server(), HostCID, "⚠ 地图无效或不存在。");
		return false;
	}

	if(pHost->GetStat(AttributeIdentifier::Gold) < ARENA_LOBBY_COST)
	{
		char aMsg[128];
		str_format(aMsg, sizeof(aMsg), "⚠ 创建竞技场需要 %d 金币。", ARENA_LOBBY_COST);
		SendChatToCross(Server(), HostCID, aMsg);
		return false;
	}

	pHost->SetStat(AttributeIdentifier::Gold, pHost->GetStat(AttributeIdentifier::Gold) - ARENA_LOBBY_COST);
	pHost->m_MMODirty = true;

	SArenaLobby Lobby;
	Lobby.m_HostClientID = HostCID;
	str_copy(Lobby.m_aMode, aMode, sizeof(Lobby.m_aMode));
	str_copy(Lobby.m_aMap, aMap, sizeof(Lobby.m_aMap));
	Lobby.m_TargetScore = GuildWarDefaultTargetScore(aMode);
	Lobby.m_ExpiryTick = Server()->Tick() + ARENA_LOBBY_TICKS;
	Lobby.m_ArenaWorldID = -1;
	Lobby.m_Status = LOBBY_WAITING;
	Lobby.m_PaidGold = ARENA_LOBBY_COST;
	Lobby.m_aParticipants.clear();
	Lobby.m_aPendingInvitees.clear();

	SArenaLobbyParticipant HostPart;
	HostPart.m_ClientID = HostCID;
	HostPart.m_OriginalWorld = Server()->GetClientWorldID(HostCID);
	CCharacter *pChar = pHost->GetCharacter();
	HostPart.m_OriginalPos = pChar ? pChar->GetCore()->m_Pos : vec2(0.0f, 0.0f);
	HostPart.m_Accepted = true;
	Lobby.m_aParticipants.add(HostPart);

	gs_aLobbies.add(Lobby);

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"⚔️ 竞技场已创建！模式：%s，地图：%s，目标 %d 分。费用 %d 金币。"
		" 使用 /arena_invite 邀请玩家，%d 秒内开始。",
		GuildWarModeDisplayName(aMode), aMap, Lobby.m_TargetScore, ARENA_LOBBY_COST, ARENA_LOBBY_SECONDS);
	SendChatToCross(Server(), HostCID, aMsg);
	return true;
}

bool CArenaLobbyManager::InvitePlayers(int HostCID, const char *pNames)
{
	if(!pNames || !pNames[0])
	{
		SendChatToCross(Server(), HostCID, "⚠ 用法：/arena_invite <玩家名> [玩家2 ...]");
		return false;
	}

	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你没有等待中的竞技场大厅。先 /arena_create");
		return false;
	}

	SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != LOBBY_WAITING)
	{
		SendChatToCross(Server(), HostCID, "⚠ 比赛已开始，无法邀请。");
		return false;
	}

	std::vector<std::string> Names;
	ParsePlayerNames(pNames, Names);
	if(Names.empty())
	{
		SendChatToCross(Server(), HostCID, "⚠ 请填写至少一个玩家名。");
		return false;
	}

	int Invited = 0;
	for(const std::string &Name : Names)
	{
		if((int)L.m_aPendingInvitees.size() + CountAccepted(LobbyIdx) - 1 >= ARENA_MAX_INVITEES)
		{
			SendChatToCross(Server(), HostCID, "⚠ 邀请人数已达上限。");
			break;
		}

		const int TargetCID = CGlobalState::FindClientByName(GS(), Name.c_str());
		if(TargetCID < 0)
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "⚠ 找不到玩家：%s", Name.c_str());
			SendChatToCross(Server(), HostCID, aMsg);
			continue;
		}
		if(TargetCID == HostCID)
		{
			SendChatToCross(Server(), HostCID, "⚠ 不能邀请自己。");
			continue;
		}
		if(IsPlayerBusyForArena(TargetCID, LobbyIdx))
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "⚠ %s 正忙（比赛/竞技场/公会战）。", Name.c_str());
			SendChatToCross(Server(), HostCID, aMsg);
			continue;
		}

		bool AlreadyPending = false;
		for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
		{
			if(L.m_aPendingInvitees[pi] == TargetCID)
			{
				AlreadyPending = true;
				break;
			}
		}
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			if(L.m_aParticipants[pi].m_ClientID == TargetCID)
			{
				AlreadyPending = true;
				break;
			}
		}
		if(AlreadyPending)
			continue;

		if(gs_aPendingInviteLobby[TargetCID] >= 0)
		{
			char aMsg[128];
			str_format(aMsg, sizeof(aMsg), "⚠ %s 已有待处理的竞技场邀请。", Name.c_str());
			SendChatToCross(Server(), HostCID, aMsg);
			continue;
		}

		L.m_aPendingInvitees.add(TargetCID);
		gs_aPendingInviteLobby[TargetCID] = LobbyIdx;

		const int Remaining = (L.m_ExpiryTick - Server()->Tick()) / 10;
		char aInvite[256];
		str_format(aInvite, sizeof(aInvite),
			"⚔️ %s 邀请你参加 %s 竞技场（地图：%s）！60 秒内输入 /yes 加入。剩余 %d 秒",
			Server()->ClientName(HostCID),
			GuildWarModeDisplayName(L.m_aMode),
			L.m_aMap,
			Remaining > 0 ? Remaining : 0);
		SendChatToCross(Server(), TargetCID, aInvite);
		Invited++;
	}

	if(Invited > 0)
	{
		char aMsg[128];
		str_format(aMsg, sizeof(aMsg), "✅ 已发送 %d 个邀请。", Invited);
		SendChatToCross(Server(), HostCID, aMsg);
	}
	return Invited > 0;
}

bool CArenaLobbyManager::AcceptInvite(int ClientID)
{
	const int LobbyIdx = gs_aPendingInviteLobby[ClientID];
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
	{
		SendChatToCross(Server(), ClientID, "⚠ 你没有待处理的竞技场邀请。");
		return false;
	}

	SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != LOBBY_WAITING)
	{
		gs_aPendingInviteLobby[ClientID] = -1;
		SendChatToCross(Server(), ClientID, "⚠ 该竞技场邀请已失效。");
		return false;
	}

	if(Server()->Tick() >= L.m_ExpiryTick)
	{
		gs_aPendingInviteLobby[ClientID] = -1;
		SendChatToCross(Server(), ClientID, "⚠ 邀请已过期。");
		return false;
	}

	if(IsPlayerBusyForArena(ClientID, LobbyIdx))
	{
		SendChatToCross(Server(), ClientID, "⚠ 你当前无法加入竞技场。");
		return false;
	}

	CPlayer *pP = FindPlayerCrossWorld(Server(), ClientID);
	if(!pP || pP->GetAccountId() <= 0)
	{
		SendChatToCross(Server(), ClientID, "⚠ 请先登录。");
		return false;
	}

	for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
	{
		if(L.m_aPendingInvitees[pi] == ClientID)
		{
			L.m_aPendingInvitees.remove_index(pi);
			break;
		}
	}
	gs_aPendingInviteLobby[ClientID] = -1;

	SArenaLobbyParticipant Part;
	Part.m_ClientID = ClientID;
	Part.m_OriginalWorld = Server()->GetClientWorldID(ClientID);
	CCharacter *pChar = pP->GetCharacter();
	Part.m_OriginalPos = pChar ? pChar->GetCore()->m_Pos : vec2(0.0f, 0.0f);
	Part.m_Accepted = true;
	L.m_aParticipants.add(Part);

	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "✅ %s 加入了竞技场！", Server()->ClientName(ClientID));
	BroadcastLobby(LobbyIdx, aMsg);
	SendChatToCross(Server(), ClientID, "✅ 你已接受竞技场邀请，等待比赛开始…");

	if(L.m_aPendingInvitees.size() == 0 && CountAccepted(LobbyIdx) >= 2)
		TryStartLobby(LobbyIdx, false);

	return true;
}

bool CArenaLobbyManager::EarlyStart(int HostCID)
{
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你没有进行中的竞技场大厅。");
		return false;
	}

	SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != LOBBY_WAITING)
	{
		SendChatToCross(Server(), HostCID, "⚠ 比赛已开始或已结束。");
		return false;
	}

	if(CountAccepted(LobbyIdx) < 2)
	{
		SendChatToCross(Server(), HostCID, "⚠ 至少需要 2 名已接受的玩家才能开始。");
		return false;
	}

	TryStartLobby(LobbyIdx, true);
	return true;
}

bool CArenaLobbyManager::CancelLobby(int HostCID)
{
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你没有进行中的竞技场大厅。");
		return false;
	}

	SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status == LOBBY_ACTIVE)
	{
		FinishLobby(LobbyIdx, -1);
		SendChatToCross(Server(), HostCID, "✅ 竞技场已结束。");
		return true;
	}

	CancelLobbyWithRefund(LobbyIdx, "主持人取消了竞技场。");
	SendChatToCross(Server(), HostCID, "✅ 竞技场已取消。");
	return true;
}

void CArenaLobbyManager::ArenaStatus(int ClientID)
{
	const int LobbyIdx = FindLobbyIndex(ClientID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), ClientID, "✅ 你当前没有进行中的竞技场。");
		return;
	}

	const SArenaLobby &L = gs_aLobbies[LobbyIdx];
	const char *pStatus[] = {"等待中", "进行中", "已结束", "已取消"};
	const int Remaining = (L.m_ExpiryTick - Server()->Tick()) / 10;

	char aMsg[256];
	if(L.m_Status == LOBBY_WAITING)
	{
		str_format(aMsg, sizeof(aMsg),
			"⚔️ 竞技场 [%s] 模式：%s，地图：%s，已接受 %d 人，待接受 %d 人，剩余 %d 秒",
			pStatus[L.m_Status],
			GuildWarModeDisplayName(L.m_aMode),
			L.m_aMap,
			CountAccepted(LobbyIdx),
			L.m_aPendingInvitees.size(),
			Remaining > 0 ? Remaining : 0);
	}
	else if(L.m_Status == LOBBY_ACTIVE)
	{
		int ScoreRed = 0, ScoreBlue = 0;
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			const int CID = L.m_aParticipants[pi].m_ClientID;
			CPlayer *pP = FindPlayerCrossWorld(Server(), CID);
			if(!pP)
				continue;
			if(pP->GetTeam() == TEAM_RED)
				ScoreRed += pP->m_Score;
			else if(pP->GetTeam() == TEAM_BLUE)
				ScoreBlue += pP->m_Score;
		}
		str_format(aMsg, sizeof(aMsg),
			"⚔️ 竞技场进行中：%s — 红 %d : %d 蓝（目标 %d）",
			GuildWarModeDisplayName(L.m_aMode), ScoreRed, ScoreBlue, L.m_TargetScore);
	}
	else
	{
		str_format(aMsg, sizeof(aMsg), "⚔️ 竞技场状态：%s", pStatus[L.m_Status]);
	}
	SendChatToCross(Server(), ClientID, aMsg);
}

void CArenaLobbyManager::BroadcastLobby(int LobbyIdx, const char *pMsg, int ExcludeCID)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size() || !pMsg)
		return;
	const SArenaLobby &L = gs_aLobbies[LobbyIdx];
	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		const int CID = L.m_aParticipants[pi].m_ClientID;
		if(CID != ExcludeCID)
			SendChatToCross(Server(), CID, pMsg);
	}
	for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
	{
		const int CID = L.m_aPendingInvitees[pi];
		if(CID != ExcludeCID)
			SendChatToCross(Server(), CID, pMsg);
	}
}

void CArenaLobbyManager::TryStartLobby(int LobbyIdx, bool ForceEarly)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size() || !Server() || !Core() || !Core()->WorldManager())
		return;

	SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != LOBBY_WAITING)
		return;

	const int Accepted = CountAccepted(LobbyIdx);
	const bool Expired = Server()->Tick() >= L.m_ExpiryTick;
	const bool AllAccepted = L.m_aPendingInvitees.size() == 0;

	if(!ForceEarly)
	{
		if(Accepted < 2)
		{
			if(Expired)
				CancelLobbyWithRefund(LobbyIdx, "倒计时结束，接受人数不足，已退还金币。");
			return;
		}
		if(!Expired && !AllAccepted)
			return;
	}
	else if(Accepted < 2)
	{
		return;
	}

	const char *pArenaMap = L.m_aMap[0] ? L.m_aMap : nullptr;
	char aDefaultMap[128];
	if(!pArenaMap)
	{
		GuildWarDefaultArenaMap(Storage(), L.m_aMode, aDefaultMap, sizeof(aDefaultMap));
		pArenaMap = aDefaultMap;
	}

	const int ArenaWorldID = Core()->WorldManager()->CreateArenaWorld("Player Arena", L.m_aMode, pArenaMap);
	if(ArenaWorldID < 0)
	{
		CancelLobbyWithRefund(LobbyIdx, "无法创建竞技场世界，已退还金币。");
		return;
	}

	L.m_ArenaWorldID = ArenaWorldID;
	L.m_Status = LOBBY_ACTIVE;

	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		if(!L.m_aParticipants[pi].m_Accepted)
			continue;
		const int CID = L.m_aParticipants[pi].m_ClientID;
		CPlayer *pPlayer = FindPlayerCrossWorld(Server(), CID);
		if(pPlayer)
			pPlayer->m_Score = 0;
	}

	for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
		gs_aPendingInviteLobby[L.m_aPendingInvitees[pi]] = -1;
	L.m_aPendingInvitees.clear();

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"🎮 竞技场开始！模式：%s，地图：%s，目标 %d 分！",
		GuildWarModeDisplayName(L.m_aMode), pArenaMap, L.m_TargetScore);
	BroadcastLobby(LobbyIdx, aMsg);
	if(GS())
		GS()->SendChat(-1, CHAT_ALL, -1, aMsg);

	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		if(!L.m_aParticipants[pi].m_Accepted)
			continue;

		const int CID = L.m_aParticipants[pi].m_ClientID;
		CPlayer *pPlayer = FindPlayerCrossWorld(Server(), CID);
		if(!pPlayer)
			continue;

		const int MatchTeam = (pi % 2 == 0) ? TEAM_RED : TEAM_BLUE;
		pPlayer->SetTeam(MatchTeam);

		vec2 CenterPos(0.0f, 0.0f);
		CrossWorldExecuteSpawn(Server(), CID, ArenaWorldID, &CenterPos);
	}
}

void CArenaLobbyManager::CancelLobbyWithRefund(int LobbyIdx, const char *pReason)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
		return;

	SArenaLobby &L = gs_aLobbies[LobbyIdx];

	for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
	{
		const int CID = L.m_aPendingInvitees[pi];
		gs_aPendingInviteLobby[CID] = -1;
		if(pReason)
			SendChatToCross(Server(), CID, pReason);
	}
	L.m_aPendingInvitees.clear();

	if(L.m_PaidGold > 0)
	{
		CPlayer *pHost = FindPlayerCrossWorld(Server(), L.m_HostClientID);
		if(pHost)
		{
			pHost->SetStat(AttributeIdentifier::Gold, pHost->GetStat(AttributeIdentifier::Gold) + L.m_PaidGold);
			pHost->m_MMODirty = true;
			char aRefund[128];
			str_format(aRefund, sizeof(aRefund), "💰 已退还 %d 金币。", L.m_PaidGold);
			SendChatToCross(Server(), L.m_HostClientID, aRefund);
		}
		L.m_PaidGold = 0;
	}

	L.m_Status = LOBBY_CANCELLED;

	if(pReason)
	{
		BroadcastLobby(LobbyIdx, pReason);
		SendChatToCross(Server(), L.m_HostClientID, pReason);
	}
}

void CArenaLobbyManager::FinishLobby(int LobbyIdx, int WinnerTeamOrMinusOne)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
		return;

	SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != LOBBY_ACTIVE && L.m_Status != LOBBY_WAITING)
		return;

	int ScoreRed = 0, ScoreBlue = 0;
	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		const int CID = L.m_aParticipants[pi].m_ClientID;
		CPlayer *pP = FindPlayerCrossWorld(Server(), CID);
		if(!pP)
			continue;
		if(pP->GetTeam() == TEAM_RED)
			ScoreRed += pP->m_Score;
		else if(pP->GetTeam() == TEAM_BLUE)
			ScoreBlue += pP->m_Score;
	}

	char aMsg[256];
	if(WinnerTeamOrMinusOne == TEAM_RED)
	{
		str_format(aMsg, sizeof(aMsg),
			"🏆 竞技场结束！红队获胜 %d : %d！", ScoreRed, ScoreBlue);
	}
	else if(WinnerTeamOrMinusOne == TEAM_BLUE)
	{
		str_format(aMsg, sizeof(aMsg),
			"🏆 竞技场结束！蓝队获胜 %d : %d！", ScoreRed, ScoreBlue);
	}
	else
	{
		str_format(aMsg, sizeof(aMsg),
			"🤝 竞技场结束！平局 %d : %d", ScoreRed, ScoreBlue);
	}
	BroadcastLobby(LobbyIdx, aMsg);
	if(GS())
		GS()->SendChat(-1, CHAT_ALL, -1, aMsg);

	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		const SArenaLobbyParticipant &P = L.m_aParticipants[pi];
		const int CID = P.m_ClientID;
		CPlayer *pPlayer = FindPlayerCrossWorld(Server(), CID);
		if(!pPlayer)
			continue;

		pPlayer->SetTeam(TEAM_SPECTATORS);
		vec2 SpawnPos = P.m_OriginalPos;
		CrossWorldExecuteSpawn(Server(), CID, P.m_OriginalWorld, &SpawnPos);
		SendChatToCross(Server(), CID, "⚔️ 你已返回原世界。");
	}

	if(L.m_ArenaWorldID >= 0 && Core() && Core()->WorldManager())
	{
		Core()->WorldManager()->DestroyArenaWorld(L.m_ArenaWorldID);
		L.m_ArenaWorldID = -1;
	}

	L.m_Status = LOBBY_FINISHED;
	L.m_PaidGold = 0;
}

void CArenaLobbyManager::RemoveClientFromLobbies(int ClientID)
{
	gs_aPendingInviteLobby[ClientID] = -1;

	const int HostIdx = FindLobbyByHost(ClientID);
	if(HostIdx >= 0)
	{
		SArenaLobby &L = gs_aLobbies[HostIdx];
		if(L.m_Status == LOBBY_WAITING)
			CancelLobbyWithRefund(HostIdx, "主持人已离线，竞技场已取消。");
		else if(L.m_Status == LOBBY_ACTIVE)
			FinishLobby(HostIdx, -1);
		return;
	}

	const int PartIdx = FindLobbyByParticipant(ClientID);
	if(PartIdx < 0)
		return;

	SArenaLobby &L = gs_aLobbies[PartIdx];
	if(L.m_Status == LOBBY_WAITING)
	{
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			if(L.m_aParticipants[pi].m_ClientID == ClientID)
			{
				L.m_aParticipants.remove_index(pi);
				break;
			}
		}
		for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
		{
			if(L.m_aPendingInvitees[pi] == ClientID)
			{
				L.m_aPendingInvitees.remove_index(pi);
				break;
			}
		}
	}
}

void CArenaLobbyManager::OnClientReset(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || this != gs_pPrimaryManager)
		return;
	RemoveClientFromLobbies(ClientID);
}

void CArenaLobbyManager::OnCharacterSpawn(CPlayer *pPlayer)
{
	if(!pPlayer || !Server())
		return;

	const int LobbyIdx = FindLobbyByParticipant(pPlayer->GetCID());
	if(LobbyIdx < 0)
		return;

	const SArenaLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status == LOBBY_ACTIVE)
		ApplyGuildWarModeRules(pPlayer, L.m_aMode);
}

void CArenaLobbyManager::OnTick()
{
	if(!Server() || this != gs_pPrimaryManager)
		return;

	static int s_LastTick = -1;
	const int Tick = Server()->Tick();
	if(Tick == s_LastTick)
		return;
	s_LastTick = Tick;

	static int s_CountdownTick = 0;
	s_CountdownTick++;

	for(int li = gs_aLobbies.size() - 1; li >= 0; li--)
	{
		SArenaLobby &L = gs_aLobbies[li];

		if(L.m_Status == LOBBY_WAITING)
		{
			if(s_CountdownTick % 10 == 0)
			{
				const int Remaining = (L.m_ExpiryTick - Tick) / 10;
				if(Remaining > 0 && (L.m_aPendingInvitees.size() > 0 || CountAccepted(li) > 1))
				{
					char aCountdown[128];
					str_format(aCountdown, sizeof(aCountdown),
						"⏱ 竞技场倒计时：剩余 %d 秒（已接受 %d 人）",
						Remaining, CountAccepted(li));
					BroadcastLobby(li, aCountdown);
				}
			}

			if(Tick >= L.m_ExpiryTick)
				TryStartLobby(li, false);
		}
		else if(L.m_Status == LOBBY_ACTIVE)
		{
			if(s_CountdownTick % 10 != 0)
				continue;

			int ScoreRed = 0, ScoreBlue = 0;
			for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
			{
				const int CID = L.m_aParticipants[pi].m_ClientID;
				CPlayer *pP = FindPlayerCrossWorld(Server(), CID);
				if(!pP)
					continue;
				if(pP->GetTeam() == TEAM_RED)
					ScoreRed += pP->m_Score;
				else if(pP->GetTeam() == TEAM_BLUE)
					ScoreBlue += pP->m_Score;
			}

			if(ScoreRed >= L.m_TargetScore || ScoreBlue >= L.m_TargetScore)
			{
				int Winner = -1;
				if(ScoreRed > ScoreBlue)
					Winner = TEAM_RED;
				else if(ScoreBlue > ScoreRed)
					Winner = TEAM_BLUE;
				FinishLobby(li, Winner);
			}
		}
		else if(L.m_Status == LOBBY_FINISHED || L.m_Status == LOBBY_CANCELLED)
		{
			gs_aLobbies.remove_index(li);
		}
	}
}

bool CArenaLobbyManager::OnPlayerVoteCommand(CPlayer *pPlayer, const char *pCmd, const char *pArgs, int ReasonNumber, const char *pReason)
{
	(void)pPlayer;
	(void)pCmd;
	(void)pArgs;
	(void)ReasonNumber;
	(void)pReason;
	return false;
}

void CArenaLobbyManager::RegisterArenaVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "arena_create", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(!pMgr)
			return;
		const char *pMode = pCtx->m_pArgs;
		pMgr->CreateLobby(pCtx->m_ClientID, pMode, nullptr);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_invite", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(!pMgr)
			return;
		const char *pNames = pCtx->m_pArgs;
		if(!pNames || !pNames[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
			return;
		}
		pMgr->InvitePlayers(pCtx->m_ClientID, pNames);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_start", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(pMgr)
			pMgr->EarlyStart(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_cancel", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(pMgr)
			pMgr->CancelLobby(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_yes", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(pMgr)
			pMgr->AcceptInvite(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_accept", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(pMgr)
			pMgr->AcceptInvite(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_setmode", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(!pMgr)
			return;
		const char *pMode = pCtx->m_pArgs;
		if(!pMode || !pMode[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请选择模式：fng / ctf / tdm / itdm");
			return;
		}
		pMgr->SetArenaMode(pCtx->m_ClientID, pMode);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_setmap", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(!pMgr)
			return;
		const char *pMap = pCtx->m_pArgs;
		if(!pMap || !pMap[0])
		{
			pG->SendChatTo(pCtx->m_ClientID, "请指定地图路径。");
			return;
		}
		pMgr->SetArenaMap(pCtx->m_ClientID, pMap);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "arena_status", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CArenaLobbyManager *pMgr = gs_pPrimaryManager;
		if(pMgr)
			pMgr->ArenaStatus(pCtx->m_ClientID);
		(void)pR;
	}, pGame);
}

bool CArenaLobbyManager::OnVoteMenuPage(int ClientID, int Page)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager())
		return false;

	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	CArenaLobbyManager *pMgr = gs_pPrimaryManager;

	auto RenderArenaHub = [&]() {
		CVoteWrapper V = ArenaPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_PVP, "竞技场");
		char aLine[VOTE_DESC_LENGTH];
		str_format(aLine, sizeof(aLine), "创建竞技场需 %d 金币", ARENA_LOBBY_COST);
		V.Info(aLine);

		char aMode[16];
		char aMap[128];
		int Status = -1;
		int SecondsLeft = 0;
		if(pMgr && pMgr->GetActiveLobbyInfo(ClientID, aMode, sizeof(aMode), aMap, sizeof(aMap), &Status, &SecondsLeft))
		{
			const char *pStatus[] = {"等待中", "进行中", "已结束", "已取消"};
			if(Status >= 0 && Status < 4)
			{
				str_format(aLine, sizeof(aLine), "当前大厅：%s | %s | %s",
					pStatus[Status], GuildWarModeDisplayName(aMode), aMap);
				V.Info(aLine);
				if(Status == LOBBY_WAITING)
				{
					str_format(aLine, sizeof(aLine), "剩余 %d 秒", SecondsLeft);
					V.Info(aLine);
				}
			}
		}
		else if(gs_aDraftMode[ClientID][0])
		{
			str_format(aLine, sizeof(aLine), "预选模式：%s", GuildWarModeDisplayName(gs_aDraftMode[ClientID]));
			V.Info(aLine);
			if(gs_aDraftMap[ClientID][0])
			{
				str_format(aLine, sizeof(aLine), "预选地图：%s", gs_aDraftMap[ClientID]);
				V.Info(aLine);
			}
		}

		V.GoToPage(VOTE_PAGE_MMO_ARENA_MODE, "选择比赛模式");
		V.GoToPage(VOTE_PAGE_MMO_ARENA_MAP, "选择比赛地图");
		V.Option("ccv_arena_create", "创建竞技场");
		V.Info("邀请玩家：选下方选项，在 Reason 填写玩家名");
		V.Option("ccv_arena_invite", "邀请玩家");
		V.Option("ccv_arena_start", "提前开始");
		V.Option("ccv_arena_cancel", "取消竞技场");
		V.Option("ccv_arena_status", "查看状态");
		V.Footer();
	};

	switch(Page)
	{
	case VOTE_PAGE_MMO_ARENA:
		RenderArenaHub();
		return true;
	case VOTE_PAGE_MMO_ARENA_MODE:
	{
		CVoteWrapper V = ArenaPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_ARENA, "选择模式");
		V.Info("创建前或等待中可更换模式");
		for(int i = 0; i < NUM_GUILDWAR_MODES; i++)
		{
			if(i == GUILDWAR_MODE_IDM)
				continue;
			const SGuildWarModeDef *pDef = GetGuildWarModeDef((EGuildWarMode)i);
			if(!pDef)
				continue;
			char aCmd[VOTE_CMD_LENGTH];
			char aLine[VOTE_DESC_LENGTH];
			str_format(aCmd, sizeof(aCmd), "ccv_arena_setmode %s", pDef->m_pId);
			str_format(aLine, sizeof(aLine), "%s — %s（目标 %d 分）",
				pDef->m_pDisplayName, pDef->m_pDescription, pDef->m_DefaultTargetScore);
			V.Option(aCmd, aLine);
		}
		V.Footer();
		return true;
	}
	case VOTE_PAGE_MMO_ARENA_MAP:
	{
		CVoteWrapper V = ArenaPage(ClientID, GS(), pVote, VOTE_PAGE_MMO_ARENA, "选择地图");
		char aMode[16];
		char aSelectedMap[128];
		int Status = -1;
		int SecondsLeft = 0;

		if(pMgr && pMgr->GetActiveLobbyInfo(ClientID, aMode, sizeof(aMode), aSelectedMap, sizeof(aSelectedMap), &Status, &SecondsLeft))
		{
			if(Status >= LOBBY_ACTIVE)
			{
				V.Info("比赛已开始，无法更换地图");
				V.Footer();
				return true;
			}
		}
		else if(gs_aDraftMode[ClientID][0])
		{
			str_copy(aMode, gs_aDraftMode[ClientID], sizeof(aMode));
			str_copy(aSelectedMap, gs_aDraftMap[ClientID], sizeof(aSelectedMap));
		}
		else
		{
			V.Info("请先选择比赛模式");
			V.GoToPage(VOTE_PAGE_MMO_ARENA_MODE, "选择比赛模式");
			V.Footer();
			return true;
		}

		char aLine[VOTE_DESC_LENGTH];
		str_format(aLine, sizeof(aLine), "模式：%s", GuildWarModeDisplayName(aMode));
		V.Info(aLine);
		if(aSelectedMap[0])
		{
			str_format(aLine, sizeof(aLine), "当前：%s", aSelectedMap);
			V.Info(aLine);
		}

		std::vector<std::string> Maps;
		ListArenaMapsForMode(Storage(), aMode, Maps);
		if(Maps.empty())
		{
			V.Info("没有可用地图");
			str_format(aLine, sizeof(aLine), "模式 %s：需 maps/vanilla（TDM）/ maps/fng（FNG）/ ctf 前缀（CTF）", GuildWarModeDisplayName(aMode));
			V.Info(aLine);
			V.Info("若已放地图仍为空，请从含 maps/ 的目录启动服务器");
			V.Footer();
			return true;
		}

		char aCmd[VOTE_CMD_LENGTH];
		const int MaxShow = 20;
		for(int i = 0; i < (int)Maps.size() && i < MaxShow; i++)
		{
			str_format(aCmd, sizeof(aCmd), "ccv_arena_setmap %s", Maps[i].c_str());
			V.Option(aCmd, Maps[i].c_str());
		}
		if((int)Maps.size() > MaxShow)
			V.Info("仅显示前 20 张地图");
		V.Footer();
		return true;
	}
	default:
		return false;
	}
}

void CArenaLobbyManager::RegisterChatCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	pManager->AddCommand("arena", "竞技场命令帮助", "", ConArena, pGame);
	pManager->AddCommand("arena_create", "<模式> [地图] - 创建竞技场（100 金币）", "s?r", ConArenaCreate, pGame);
	pManager->AddCommand("arena_invite", "<玩家名> [...] - 邀请玩家", "r", ConArenaInvite, pGame);
	pManager->AddCommand("yes", "接受竞技场邀请", "", ConYes, pGame);
	pManager->AddCommand("arena_start", "提前开始竞技场（主持人）", "", ConArenaStart, pGame);
	pManager->AddCommand("arena_cancel", "取消竞技场（主持人）", "", ConArenaCancel, pGame);
	pManager->AddCommand("arena_status", "查看竞技场状态", "", ConArenaStatus, pGame);
}

static CArenaLobbyManager *GetArenaMgr(CGameContext *pGame)
{
	(void)pGame;
	return gs_pPrimaryManager;
}

void CArenaLobbyManager::ConArena(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	if(!pGame)
		return;

	char aHelp[512];
	str_format(aHelp, sizeof(aHelp),
		"═ 竞技场命令 ───────────────────╕\n"
		"/arena_create <模式> [地图]  - 创建（%d 金币）\n"
		"/arena_invite <玩家> [...]    - 邀请玩家\n"
		"/yes                          - 接受邀请\n"
		"/arena_start                  - 提前开始\n"
		"/arena_cancel                 - 取消\n"
		"/arena_status                 - 查看状态\n"
		"模式：fng / ctf / tdm / itdm\n"
		"╘────────────────────────────────╛",
		ARENA_LOBBY_COST);
	pGame->SendChatTo(pCtx->m_ClientID, aHelp);
}

void CArenaLobbyManager::ConArenaCreate(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CArenaLobbyManager *pMgr = GetArenaMgr(pGame);
	if(!pMgr)
		return;

	if(pResult->NumArguments() == 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "用法：/arena_create <fng|ctf|tdm|itdm> [地图]");
		return;
	}

	const char *pMode = pResult->GetString(0);
	const char *pMap = pResult->NumArguments() > 1 ? pResult->GetString(1) : nullptr;
	pMgr->CreateLobby(pCtx->m_ClientID, pMode, pMap);
}

void CArenaLobbyManager::ConArenaInvite(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CArenaLobbyManager *pMgr = GetArenaMgr(pGame);
	if(!pMgr)
		return;

	if(pResult->NumArguments() == 0)
	{
		pGame->SendChatTo(pCtx->m_ClientID, "用法：/arena_invite <玩家名> [玩家2 ...]");
		return;
	}

	char aNames[512];
	aNames[0] = '\0';
	for(int i = 0; i < pResult->NumArguments(); i++)
	{
		if(i > 0)
			str_append(aNames, " ", sizeof(aNames));
		str_append(aNames, pResult->GetString(i), sizeof(aNames));
	}
	pMgr->InvitePlayers(pCtx->m_ClientID, aNames);
}

void CArenaLobbyManager::ConYes(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CArenaLobbyManager *pMgr = GetArenaMgr(pGame);
	if(!pMgr)
		return;

	if(gs_aPendingInviteLobby[pCtx->m_ClientID] >= 0)
	{
		pMgr->AcceptInvite(pCtx->m_ClientID);
		return;
	}

	if(CDefenceLobbyManager::TryAcceptPendingInvite(pCtx->m_ClientID))
		return;

	pGame->SendChatTo(pCtx->m_ClientID, "⚠ 你没有待处理的竞技场或塔防邀请。");
}

void CArenaLobbyManager::ConArenaStart(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CArenaLobbyManager *pMgr = GetArenaMgr(pGame);
	if(!pMgr)
		return;
	pMgr->EarlyStart(pCtx->m_ClientID);
}

void CArenaLobbyManager::ConArenaCancel(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CArenaLobbyManager *pMgr = GetArenaMgr(pGame);
	if(!pMgr)
		return;
	pMgr->CancelLobby(pCtx->m_ClientID);
}

void CArenaLobbyManager::ConArenaStatus(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CArenaLobbyManager *pMgr = GetArenaMgr(pGame);
	if(!pMgr)
		return;
	pMgr->ArenaStatus(pCtx->m_ClientID);
}
