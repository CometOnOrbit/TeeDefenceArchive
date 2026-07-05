#include "defence_lobby_manager.h"

#include <base/system.h>
#include <engine/shared/world_detail.h>
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
#include <game/server/worldmodes/defence.h>

#include <vector>
#include <string>

static constexpr int DEFENCE_LOBBY_SECONDS = 60;
static constexpr int DEFENCE_LOBBY_TICKS = DEFENCE_LOBBY_SECONDS * 10;
static constexpr int DEFENCE_MAX_PLAYERS = 16;

static array<SDefenceLobby> gs_aLobbies;
static int gs_aPendingInviteLobby[MAX_CLIENTS];
static int gs_aPendingApplicantLobby[MAX_CLIENTS];
static int gs_aDraftWorld[MAX_CLIENTS];
static int gs_aDraftDifficulty[MAX_CLIENTS];
static CDefenceLobbyManager *gs_pPrimaryManager = nullptr;

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

static CVoteWrapper DefencePage(int ClientID, CGameContext *pGS, CVoteMenuManager *pVote, int LastPage, const char *pTitle)
{
	pVote->SetVoteLastPage(LastPage);
	pVote->SetVoteBuildClientID(ClientID);
	CVoteWrapper V(ClientID, pGS, pVote);
	V.GroupTitle(pTitle);
	return V;
}

static const char *DifficultyLabel(int Diff)
{
	static const char *const s_ap[] = {"简单", "普通", "困难"};
	const int D = clamp(Diff, 0, NUM_TD_DIFF - 1);
	return s_ap[D];
}

CDefenceLobbyManager::CDefenceLobbyManager()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		gs_aPendingInviteLobby[i] = -1;
		gs_aPendingApplicantLobby[i] = -1;
		gs_aDraftWorld[i] = -1;
		gs_aDraftDifficulty[i] = 1;
	}
	if(!gs_pPrimaryManager)
		gs_pPrimaryManager = this;
}

CDefenceLobbyManager *CDefenceLobbyManager::ActiveManager() const
{
	return gs_pPrimaryManager ? gs_pPrimaryManager : const_cast<CDefenceLobbyManager *>(this);
}

void CDefenceLobbyManager::OnConsoleInit()
{
	(void)this;
}

int CDefenceLobbyManager::FindLobbyByHost(int HostCID) const
{
	for(int i = 0; i < gs_aLobbies.size(); i++)
	{
		if(gs_aLobbies[i].m_HostClientID == HostCID && gs_aLobbies[i].m_Status == DEFENCE_LOBBY_WAITING)
			return i;
	}
	return -1;
}

int CDefenceLobbyManager::FindLobbyByParticipant(int ClientID) const
{
	for(int i = 0; i < gs_aLobbies.size(); i++)
	{
		const SDefenceLobby &L = gs_aLobbies[i];
		if(L.m_Status != DEFENCE_LOBBY_WAITING && L.m_Status != DEFENCE_LOBBY_ACTIVE)
			continue;
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			if(L.m_aParticipants[pi].m_ClientID == ClientID)
				return i;
		}
	}
	return -1;
}

int CDefenceLobbyManager::FindLobbyIndex(int ClientID) const
{
	const int HostIdx = FindLobbyByHost(ClientID);
	if(HostIdx >= 0)
		return HostIdx;
	return FindLobbyByParticipant(ClientID);
}

int CDefenceLobbyManager::CountAccepted(int LobbyIdx) const
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
		return 0;
	int Count = 0;
	const SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		if(L.m_aParticipants[pi].m_Accepted)
			Count++;
	}
	return Count;
}

bool CDefenceLobbyManager::IsDefenceWorldIndex(int WorldIndex) const
{
	if(!Server() || WorldIndex < 0 || WorldIndex >= Server()->GetNumWorlds())
		return false;
	const CWorldDetail *pDetail = Server()->GetWorldDetail(WorldIndex);
	return pDetail && pDetail->GetType() == WorldType::Defence;
}

bool CDefenceLobbyManager::IsPlayerBusy(int ClientID, int ExcludeLobbyIdx) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return true;

	CPlayer *pP = FindPlayerCrossWorld(Server(), ClientID);
	if(!pP || pP->GetAccountId() <= 0)
		return true;

	if(gs_aPendingInviteLobby[ClientID] >= 0 && gs_aPendingInviteLobby[ClientID] != ExcludeLobbyIdx)
		return true;
	if(gs_aPendingApplicantLobby[ClientID] >= 0 && gs_aPendingApplicantLobby[ClientID] != ExcludeLobbyIdx)
		return true;

	for(int i = 0; i < gs_aLobbies.size(); i++)
	{
		if(i == ExcludeLobbyIdx)
			continue;
		const SDefenceLobby &L = gs_aLobbies[i];
		if(L.m_Status != DEFENCE_LOBBY_WAITING && L.m_Status != DEFENCE_LOBBY_ACTIVE)
			continue;
		if(L.m_HostClientID == ClientID)
			return true;
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			if(L.m_aParticipants[pi].m_ClientID == ClientID)
				return true;
		}
	}
	return false;
}

bool CDefenceLobbyManager::AddParticipant(SDefenceLobby &L, int ClientID)
{
	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		if(L.m_aParticipants[pi].m_ClientID == ClientID)
		{
			L.m_aParticipants[pi].m_Accepted = true;
			return true;
		}
	}

	if(L.m_aParticipants.size() >= DEFENCE_MAX_PLAYERS)
		return false;

	CPlayer *pP = FindPlayerCrossWorld(Server(), ClientID);
	if(!pP)
		return false;

	SDefenceLobbyParticipant Part;
	Part.m_ClientID = ClientID;
	Part.m_OriginalWorld = Server()->GetClientWorldID(ClientID);
	CCharacter *pChar = pP->GetCharacter();
	Part.m_OriginalPos = pChar ? pChar->GetCore()->m_Pos : vec2(0.0f, 0.0f);
	Part.m_Accepted = true;
	L.m_aParticipants.add(Part);
	return true;
}

bool CDefenceLobbyManager::SetDraftWorld(int ClientID, int WorldIndex)
{
	if(!IsDefenceWorldIndex(WorldIndex))
	{
		SendChatToCross(Server(), ClientID, "⚠ 无效的塔防世界。");
		return false;
	}
	gs_aDraftWorld[ClientID] = WorldIndex;
	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "✅ 已选择塔防地图：%s", Server()->GetWorldName(WorldIndex));
	SendChatToCross(Server(), ClientID, aMsg);
	return true;
}

bool CDefenceLobbyManager::SetDraftDifficulty(int ClientID, int Difficulty)
{
	const int D = clamp(Difficulty, 0, NUM_TD_DIFF - 1);
	gs_aDraftDifficulty[ClientID] = D;
	char aMsg[64];
	str_format(aMsg, sizeof(aMsg), "✅ 难度：%s", DifficultyLabel(D));
	SendChatToCross(Server(), ClientID, aMsg);
	return true;
}

bool CDefenceLobbyManager::GetActiveLobbyInfo(int ClientID, int *pWorldIndex, int *pDifficulty, int *pStatus, int *pSecondsLeft) const
{
	const int LobbyIdx = FindLobbyIndex(ClientID);
	if(LobbyIdx < 0)
		return false;

	const SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	if(pWorldIndex)
		*pWorldIndex = L.m_DefenceWorldID;
	if(pDifficulty)
		*pDifficulty = L.m_Difficulty;
	if(pStatus)
		*pStatus = L.m_Status;
	if(pSecondsLeft && Server())
		*pSecondsLeft = maximum(0, (L.m_ExpiryTick - Server()->Tick()) / 10);
	return true;
}

bool CDefenceLobbyManager::CreateLobby(int HostCID, int WorldIndex, int Difficulty)
{
	if(!Server())
		return false;

	CPlayer *pHost = FindPlayerCrossWorld(Server(), HostCID);
	if(!pHost || pHost->GetAccountId() <= 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 请先登录。");
		return false;
	}

	if(FindLobbyByHost(HostCID) >= 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你已有一个进行中的塔防大厅。");
		return false;
	}

	int World = WorldIndex;
	if(World < 0)
		World = gs_aDraftWorld[HostCID];
	if(!IsDefenceWorldIndex(World))
	{
		SendChatToCross(Server(), HostCID, "⚠ 请先选择塔防地图。");
		return false;
	}

	for(int i = 0; i < gs_aLobbies.size(); i++)
	{
		const SDefenceLobby &L = gs_aLobbies[i];
		if(L.m_Status == DEFENCE_LOBBY_WAITING && L.m_DefenceWorldID == World)
		{
			SendChatToCross(Server(), HostCID, "⚠ 该塔防地图已有等待中的大厅，请稍后再试或申请加入。");
			return false;
		}
	}

	const int Diff = clamp(Difficulty >= 0 ? Difficulty : gs_aDraftDifficulty[HostCID], 0, NUM_TD_DIFF - 1);

	SDefenceLobby Lobby;
	Lobby.m_HostClientID = HostCID;
	Lobby.m_DefenceWorldID = World;
	Lobby.m_Difficulty = Diff;
	Lobby.m_ExpiryTick = Server()->Tick() + DEFENCE_LOBBY_TICKS;
	Lobby.m_Status = DEFENCE_LOBBY_WAITING;
	Lobby.m_aParticipants.clear();
	Lobby.m_aPendingInvitees.clear();
	Lobby.m_aPendingApplicants.clear();

	if(!AddParticipant(Lobby, HostCID))
		return false;

	const int LobbyIdx = gs_aLobbies.add(Lobby);

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"🏰 塔防大厅已创建！地图：%s | 难度：%s | %d 秒内可邀请/接受申请。",
		Server()->GetWorldName(World), DifficultyLabel(Diff), DEFENCE_LOBBY_SECONDS);
	SendChatToCross(Server(), HostCID, aMsg);
	BroadcastLobby(LobbyIdx, aMsg, HostCID);
	return true;
}

bool CDefenceLobbyManager::InvitePlayers(int HostCID, const char *pNames)
{
	if(!pNames || !pNames[0])
	{
		SendChatToCross(Server(), HostCID, "⚠ 用法：/defence_invite <玩家名> [玩家2 ...]");
		return false;
	}

	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你没有等待中的塔防大厅。先创建大厅。");
		return false;
	}

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	std::vector<std::string> Names;
	ParsePlayerNames(pNames, Names);
	if(Names.empty())
		return false;

	int Invited = 0;
	for(const std::string &Name : Names)
	{
		if(L.m_aParticipants.size() + L.m_aPendingInvitees.size() + L.m_aPendingApplicants.size() >= DEFENCE_MAX_PLAYERS)
			break;

		const int TargetCID = CGlobalState::FindClientByName(GS(), Name.c_str());
		if(TargetCID < 0)
			continue;
		if(TargetCID == HostCID || IsPlayerBusy(TargetCID, LobbyIdx))
			continue;

		bool Already = false;
		for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
			if(L.m_aPendingInvitees[pi] == TargetCID) Already = true;
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
			if(L.m_aParticipants[pi].m_ClientID == TargetCID) Already = true;
		for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
			if(L.m_aPendingApplicants[pi] == TargetCID) Already = true;
		if(Already || gs_aPendingInviteLobby[TargetCID] >= 0)
			continue;

		L.m_aPendingInvitees.add(TargetCID);
		gs_aPendingInviteLobby[TargetCID] = LobbyIdx;

		const int Remaining = (L.m_ExpiryTick - Server()->Tick()) / 10;
		char aInvite[256];
		str_format(aInvite, sizeof(aInvite),
			"🏰 %s 邀请你参加塔防（%s · %s）！输入 /yes 或菜单接受。剩余 %d 秒",
			Server()->ClientName(HostCID),
			Server()->GetWorldName(L.m_DefenceWorldID),
			DifficultyLabel(L.m_Difficulty),
			Remaining > 0 ? Remaining : 0);
		SendChatToCross(Server(), TargetCID, aInvite);
		Invited++;
	}

	if(Invited > 0)
	{
		char aMsg[64];
		str_format(aMsg, sizeof(aMsg), "✅ 已发送 %d 个塔防邀请。", Invited);
		SendChatToCross(Server(), HostCID, aMsg);
	}
	return Invited > 0;
}

bool CDefenceLobbyManager::AcceptInvite(int ClientID)
{
	const int LobbyIdx = gs_aPendingInviteLobby[ClientID];
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
	{
		SendChatToCross(Server(), ClientID, "⚠ 你没有待处理的塔防邀请。");
		return false;
	}

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != DEFENCE_LOBBY_WAITING)
	{
		gs_aPendingInviteLobby[ClientID] = -1;
		SendChatToCross(Server(), ClientID, "⚠ 该塔防邀请已失效。");
		return false;
	}

	if(IsPlayerBusy(ClientID, LobbyIdx))
	{
		SendChatToCross(Server(), ClientID, "⚠ 你当前无法加入塔防大厅。");
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

	if(!AddParticipant(L, ClientID))
		return false;

	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "✅ %s 加入了塔防大厅！", Server()->ClientName(ClientID));
	BroadcastLobby(LobbyIdx, aMsg);
	SendChatToCross(Server(), ClientID, "✅ 你已接受塔防邀请，等待开始…");
	return true;
}

bool CDefenceLobbyManager::TryAcceptPendingInvite(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || gs_aPendingInviteLobby[ClientID] < 0 || !gs_pPrimaryManager)
		return false;
	return gs_pPrimaryManager->AcceptInvite(ClientID);
}

bool CDefenceLobbyManager::ApplyToLobby(int ClientID, int LobbyIdx)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
	{
		SendChatToCross(Server(), ClientID, "⚠ 大厅不存在。");
		return false;
	}

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != DEFENCE_LOBBY_WAITING)
	{
		SendChatToCross(Server(), ClientID, "⚠ 该大厅已关闭或已开始。");
		return false;
	}

	if(ClientID == L.m_HostClientID)
	{
		SendChatToCross(Server(), ClientID, "⚠ 你是主持人，无需申请。");
		return false;
	}

	if(IsPlayerBusy(ClientID, LobbyIdx))
	{
		SendChatToCross(Server(), ClientID, "⚠ 你当前无法申请加入塔防。");
		return false;
	}

	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		if(L.m_aParticipants[pi].m_ClientID == ClientID)
		{
			SendChatToCross(Server(), ClientID, "⚠ 你已在该大厅中。");
			return true;
		}
	}
	for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
	{
		if(L.m_aPendingInvitees[pi] == ClientID)
		{
			SendChatToCross(Server(), ClientID, "⚠ 你已有邀请，请直接 /yes 接受。");
			return false;
		}
	}
	for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
	{
		if(L.m_aPendingApplicants[pi] == ClientID)
		{
			SendChatToCross(Server(), ClientID, "⚠ 你已提交申请，等待主持人批准。");
			return true;
		}
	}

	if(L.m_aParticipants.size() + L.m_aPendingApplicants.size() >= DEFENCE_MAX_PLAYERS)
	{
		SendChatToCross(Server(), ClientID, "⚠ 该大厅已满。");
		return false;
	}

	L.m_aPendingApplicants.add(ClientID);
	gs_aPendingApplicantLobby[ClientID] = LobbyIdx;

	char aMsg[160];
	str_format(aMsg, sizeof(aMsg), "📨 %s 申请加入塔防大厅。", Server()->ClientName(ClientID));
	SendChatToCross(Server(), L.m_HostClientID, aMsg);
	SendChatToCross(Server(), ClientID, "✅ 已提交申请，等待主持人批准…");
	return true;
}

bool CDefenceLobbyManager::AcceptApplicant(int HostCID, int ApplicantCID)
{
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
		return false;

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_HostClientID != HostCID)
		return false;

	bool Found = false;
	for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
	{
		if(L.m_aPendingApplicants[pi] == ApplicantCID)
		{
			L.m_aPendingApplicants.remove_index(pi);
			Found = true;
			break;
		}
	}
	if(!Found)
	{
		SendChatToCross(Server(), HostCID, "⚠ 该玩家未在大厅申请列表中。");
		return false;
	}

	gs_aPendingApplicantLobby[ApplicantCID] = -1;

	if(IsPlayerBusy(ApplicantCID, LobbyIdx) || !AddParticipant(L, ApplicantCID))
	{
		SendChatToCross(Server(), HostCID, "⚠ 无法添加该玩家（可能已离线或正忙）。");
		return false;
	}

	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), "✅ 已批准 %s 加入塔防大厅。", Server()->ClientName(ApplicantCID));
	BroadcastLobby(LobbyIdx, aMsg);
	SendChatToCross(Server(), ApplicantCID, "✅ 主持人已批准你的塔防申请！");
	return true;
}

bool CDefenceLobbyManager::AcceptAllApplicants(int HostCID)
{
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
		return false;

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	array<int> Applicants = L.m_aPendingApplicants;
	L.m_aPendingApplicants.clear();

	int Accepted = 0;
	for(int i = 0; i < Applicants.size(); i++)
	{
		const int CID = Applicants[i];
		gs_aPendingApplicantLobby[CID] = -1;
		if(!IsPlayerBusy(CID, LobbyIdx) && AddParticipant(L, CID))
			Accepted++;
	}

	char aMsg[64];
	str_format(aMsg, sizeof(aMsg), "✅ 已批准 %d 名申请者。", Accepted);
	SendChatToCross(Server(), HostCID, aMsg);
	return Accepted > 0;
}

bool CDefenceLobbyManager::RejectApplicant(int HostCID, int ApplicantCID)
{
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
		return false;

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
	{
		if(L.m_aPendingApplicants[pi] == ApplicantCID)
		{
			L.m_aPendingApplicants.remove_index(pi);
			gs_aPendingApplicantLobby[ApplicantCID] = -1;
			SendChatToCross(Server(), ApplicantCID, "⚠ 主持人拒绝了你的塔防申请。");
			SendChatToCross(Server(), HostCID, "✅ 已拒绝该申请。");
			return true;
		}
	}
	return false;
}

bool CDefenceLobbyManager::EarlyStart(int HostCID)
{
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你没有进行中的塔防大厅。");
		return false;
	}
	TryStartLobby(LobbyIdx, true);
	return true;
}

bool CDefenceLobbyManager::CancelLobby(int HostCID)
{
	const int LobbyIdx = FindLobbyByHost(HostCID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), HostCID, "⚠ 你没有进行中的塔防大厅。");
		return false;
	}
	CancelLobbyInternal(LobbyIdx, "主持人已取消塔防大厅。");
	return true;
}

void CDefenceLobbyManager::LobbyStatus(int ClientID)
{
	const int LobbyIdx = FindLobbyIndex(ClientID);
	if(LobbyIdx < 0)
	{
		SendChatToCross(Server(), ClientID, "✅ 你当前没有进行中的塔防大厅。");
		return;
	}

	const SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"🏰 塔防大厅 | 地图：%s | 难度：%s | 成员 %d | 邀请 %d | 申请 %d | 剩余 %d 秒",
		Server()->GetWorldName(L.m_DefenceWorldID),
		DifficultyLabel(L.m_Difficulty),
		CountAccepted(LobbyIdx),
		L.m_aPendingInvitees.size(),
		L.m_aPendingApplicants.size(),
		maximum(0, (L.m_ExpiryTick - Server()->Tick()) / 10));
	SendChatToCross(Server(), ClientID, aMsg);
}

void CDefenceLobbyManager::BroadcastLobby(int LobbyIdx, const char *pMsg, int ExcludeCID)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size() || !pMsg)
		return;
	const SDefenceLobby &L = gs_aLobbies[LobbyIdx];
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
	for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
	{
		const int CID = L.m_aPendingApplicants[pi];
		if(CID != ExcludeCID)
			SendChatToCross(Server(), CID, pMsg);
	}
}

void CDefenceLobbyManager::TryStartLobby(int LobbyIdx, bool ForceEarly)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size() || !Server())
		return;

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	if(L.m_Status != DEFENCE_LOBBY_WAITING)
		return;

	const int Accepted = CountAccepted(LobbyIdx);
	const bool Expired = Server()->Tick() >= L.m_ExpiryTick;
	if(!ForceEarly && !Expired)
		return;
	if(Accepted < 1)
	{
		if(Expired)
			CancelLobbyInternal(LobbyIdx, "倒计时结束，无人加入，塔防大厅已取消。");
		return;
	}

	// Approve remaining applicants automatically when timer expires (optional - user wanted apply flow with host approval)
	// On force/expire start: reject pending invites/applicants not yet accepted
	for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
		gs_aPendingInviteLobby[L.m_aPendingInvitees[pi]] = -1;
	L.m_aPendingInvitees.clear();
	for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
	{
		const int CID = L.m_aPendingApplicants[pi];
		gs_aPendingApplicantLobby[CID] = -1;
		SendChatToCross(Server(), CID, "⚠ 塔防已开始，你的申请未获批准。");
	}
	L.m_aPendingApplicants.clear();

	const int WorldID = L.m_DefenceWorldID;
	if(IGameServer *pIGS = Server()->GameServer(WorldID))
	{
		CGameContext *pDefGS = (CGameContext *)pIGS;
		if(auto *pCtrl = dynamic_cast<CGameControllerDefence *>(pDefGS->m_pController))
			pCtrl->TdSetDifficulty(L.m_Difficulty);
	}

	L.m_Status = DEFENCE_LOBBY_ACTIVE;

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg),
		"🎮 塔防开始！地图：%s | 难度：%s | 共 %d 人",
		Server()->GetWorldName(WorldID), DifficultyLabel(L.m_Difficulty), Accepted);
	BroadcastLobby(LobbyIdx, aMsg);

	for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
	{
		if(!L.m_aParticipants[pi].m_Accepted)
			continue;
		const int CID = L.m_aParticipants[pi].m_ClientID;
		vec2 SpawnPos(0.0f, 0.0f);
		CrossWorldExecuteSpawn(Server(), CID, WorldID, &SpawnPos);
		SendChatToCross(Server(), CID, "🏰 已进入塔防世界！ESC 菜单可返回 F|RPG。");
	}
}

void CDefenceLobbyManager::CancelLobbyInternal(int LobbyIdx, const char *pReason)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
		return;

	SDefenceLobby &L = gs_aLobbies[LobbyIdx];
	if(pReason)
		BroadcastLobby(LobbyIdx, pReason);

	for(int pi = 0; pi < L.m_aPendingInvitees.size(); pi++)
		gs_aPendingInviteLobby[L.m_aPendingInvitees[pi]] = -1;
	for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
		gs_aPendingApplicantLobby[L.m_aPendingApplicants[pi]] = -1;

	L.m_Status = DEFENCE_LOBBY_CANCELLED;
}

void CDefenceLobbyManager::FinishLobby(int LobbyIdx)
{
	if(LobbyIdx < 0 || LobbyIdx >= gs_aLobbies.size())
		return;
	gs_aLobbies[LobbyIdx].m_Status = DEFENCE_LOBBY_FINISHED;
}

void CDefenceLobbyManager::RemoveClientFromLobbies(int ClientID)
{
	gs_aPendingInviteLobby[ClientID] = -1;
	gs_aPendingApplicantLobby[ClientID] = -1;

	const int HostIdx = FindLobbyByHost(ClientID);
	if(HostIdx >= 0)
	{
		CancelLobbyInternal(HostIdx, "主持人已离线，塔防大厅已取消。");
		return;
	}

	const int PartIdx = FindLobbyByParticipant(ClientID);
	if(PartIdx < 0)
		return;

	SDefenceLobby &L = gs_aLobbies[PartIdx];
	if(L.m_Status == DEFENCE_LOBBY_WAITING)
	{
		for(int pi = 0; pi < L.m_aParticipants.size(); pi++)
		{
			if(L.m_aParticipants[pi].m_ClientID == ClientID)
			{
				L.m_aParticipants.remove_index(pi);
				break;
			}
		}
	}
}

void CDefenceLobbyManager::OnClientReset(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || this != gs_pPrimaryManager)
		return;
	RemoveClientFromLobbies(ClientID);
}

void CDefenceLobbyManager::OnTick()
{
	if(!Server() || this != gs_pPrimaryManager)
		return;

	static int s_LastTick = -1;
	const int Tick = Server()->Tick();
	if(Tick == s_LastTick)
		return;
	s_LastTick = Tick;

	for(int li = gs_aLobbies.size() - 1; li >= 0; li--)
	{
		SDefenceLobby &L = gs_aLobbies[li];

		if(L.m_Status == DEFENCE_LOBBY_WAITING)
		{
			if(Tick >= L.m_ExpiryTick)
				TryStartLobby(li, false);
		}
		else if(L.m_Status == DEFENCE_LOBBY_FINISHED || L.m_Status == DEFENCE_LOBBY_CANCELLED)
		{
			gs_aLobbies.remove_index(li);
		}
	}
}

void CDefenceLobbyManager::RegisterDefenceVoteCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	VOTE_CMD(pManager, "defence_create", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->CreateLobby(pCtx->m_ClientID, -1, -1);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "defence_invite", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		CGameContext *pG = (CGameContext *)pCtx->m_pContext;
		if(!gs_pPrimaryManager)
			return;
		const char *pNames = pCtx->m_pArgs;
		if(!pNames || !pNames[0])
			pG->SendChatTo(pCtx->m_ClientID, "请在 Reason 栏填写玩家名。");
		else
			gs_pPrimaryManager->InvitePlayers(pCtx->m_ClientID, pNames);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "defence_start", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->EarlyStart(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "defence_cancel", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->CancelLobby(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "defence_yes", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->AcceptInvite(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "defence_setworld", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->SetDraftWorld(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);

	VOTE_CMD(pManager, "defence_setdiff", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->SetDraftDifficulty(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);

	VOTE_CMD(pManager, "defence_apply", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->ApplyToLobby(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);

	VOTE_CMD(pManager, "defence_accept", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->AcceptApplicant(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);

	VOTE_CMD(pManager, "defence_reject", "i", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->RejectApplicant(pCtx->m_ClientID, pR->GetInteger(0));
	}, pGame);

	VOTE_CMD(pManager, "defence_acceptall", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->AcceptAllApplicants(pCtx->m_ClientID);
		(void)pR;
	}, pGame);

	VOTE_CMD(pManager, "defence_status", "", [](IConsole::IResult *pR, void *pU) {
		auto *pCtx = (CCommandManager::SCommandContext *)pU;
		if(gs_pPrimaryManager)
			gs_pPrimaryManager->LobbyStatus(pCtx->m_ClientID);
		(void)pR;
	}, pGame);
}

bool CDefenceLobbyManager::OnVoteMenuPage(int ClientID, int Page)
{
	if(!GS() || !Core() || !Core()->VoteMenuManager() || !Server())
		return false;

	CVoteMenuManager *pVote = Core()->VoteMenuManager();
	CDefenceLobbyManager *pMgr = gs_pPrimaryManager;

	auto RenderHub = [&]() {
		CVoteWrapper V = DefencePage(ClientID, GS(), pVote, PAGE_MENU, "塔防大厅");
		char aLine[VOTE_DESC_LENGTH];

		int World = -1, Diff = 1, Status = -1, Seconds = 0;
		if(pMgr && pMgr->GetActiveLobbyInfo(ClientID, &World, &Diff, &Status, &Seconds))
		{
			const char *pStatus[] = {"等待中", "进行中", "已结束", "已取消"};
			str_format(aLine, sizeof(aLine), "当前大厅：%s | %s | %s",
				World >= 0 ? Server()->GetWorldName(World) : "?",
				DifficultyLabel(Diff),
				Status >= 0 && Status < 4 ? pStatus[Status] : "?");
			V.Info(aLine);
			if(Status == DEFENCE_LOBBY_WAITING)
			{
				str_format(aLine, sizeof(aLine), "剩余 %d 秒", Seconds);
				V.Info(aLine);
			}
		}
		else
		{
			if(gs_aDraftWorld[ClientID] >= 0)
			{
				str_format(aLine, sizeof(aLine), "预选地图：%s", Server()->GetWorldName(gs_aDraftWorld[ClientID]));
				V.Info(aLine);
			}
			str_format(aLine, sizeof(aLine), "预选难度：%s", DifficultyLabel(gs_aDraftDifficulty[ClientID]));
			V.Info(aLine);
		}

		V.GoToPage(VOTE_PAGE_MMO_DEFENCE_MAP, "选择塔防地图");
		V.GoToPage(VOTE_PAGE_MMO_DEFENCE_DIFF, "选择难度");
		V.Option("ccv_defence_create", "创建塔防大厅");
		V.Info("邀请玩家：选下方选项，在 Reason 填写玩家名");
		V.Option("ccv_defence_invite", "邀请玩家");
		V.GoToPage(VOTE_PAGE_MMO_DEFENCE_APPLICANTS, "管理加入申请");
		V.GoToPage(VOTE_PAGE_MMO_DEFENCE_BROWSE, "浏览并申请加入");
		V.Option("ccv_defence_yes", "接受塔防邀请");
		V.Option("ccv_defence_start", "提前开始");
		V.Option("ccv_defence_cancel", "取消大厅");
		V.Option("ccv_defence_status", "查看状态");
		V.Footer();
	};

	switch(Page)
	{
	case VOTE_PAGE_MMO_DEFENCE:
		RenderHub();
		return true;

	case VOTE_PAGE_MMO_DEFENCE_MAP:
	{
		CVoteWrapper V = DefencePage(ClientID, GS(), pVote, VOTE_PAGE_MMO_DEFENCE, "选择塔防地图");
		const int Num = Server()->GetNumWorlds();
		for(int i = 0; i < Num; i++)
		{
			const CWorldDetail *pDetail = Server()->GetWorldDetail(i);
			if(!pDetail || pDetail->GetType() != WorldType::Defence)
				continue;
			char aCmd[VOTE_CMD_LENGTH];
			char aLine[VOTE_DESC_LENGTH];
			str_format(aCmd, sizeof(aCmd), "ccv_defence_setworld %d", i);
			str_format(aLine, sizeof(aLine), "%s (%d)", Server()->GetWorldName(i), Server()->GetNumPlayersInWorld(i));
			V.Option(aCmd, aLine);
		}
		V.Footer();
		return true;
	}

	case VOTE_PAGE_MMO_DEFENCE_DIFF:
	{
		CVoteWrapper V = DefencePage(ClientID, GS(), pVote, VOTE_PAGE_MMO_DEFENCE, "选择难度");
		for(int d = 0; d < NUM_TD_DIFF; d++)
		{
			char aCmd[VOTE_CMD_LENGTH];
			str_format(aCmd, sizeof(aCmd), "ccv_defence_setdiff %d", d);
			V.Option(aCmd, DifficultyLabel(d));
		}
		V.Footer();
		return true;
	}

	case VOTE_PAGE_MMO_DEFENCE_BROWSE:
	{
		CVoteWrapper V = DefencePage(ClientID, GS(), pVote, VOTE_PAGE_MMO_DEFENCE, "浏览塔防大厅");
		int Listed = 0;
		for(int li = 0; li < gs_aLobbies.size(); li++)
		{
			const SDefenceLobby &L = gs_aLobbies[li];
			if(L.m_Status != DEFENCE_LOBBY_WAITING)
				continue;
			if(L.m_HostClientID == ClientID)
				continue;
			char aCmd[VOTE_CMD_LENGTH];
			char aLine[VOTE_DESC_LENGTH];
			str_format(aCmd, sizeof(aCmd), "ccv_defence_apply %d", li);
			str_format(aLine, sizeof(aLine), "%s · %s · 主持:%s · %d/%d人",
				Server()->GetWorldName(L.m_DefenceWorldID),
				DifficultyLabel(L.m_Difficulty),
				Server()->ClientName(L.m_HostClientID),
				CountAccepted(li),
				DEFENCE_MAX_PLAYERS);
			V.Option(aCmd, aLine);
			Listed++;
		}
		if(Listed == 0)
			V.Info("（当前没有可申请的塔防大厅）");
		V.Footer();
		return true;
	}

	case VOTE_PAGE_MMO_DEFENCE_APPLICANTS:
	{
		CVoteWrapper V = DefencePage(ClientID, GS(), pVote, VOTE_PAGE_MMO_DEFENCE, "加入申请");
		const int LobbyIdx = pMgr ? pMgr->FindLobbyByHost(ClientID) : -1;
		if(LobbyIdx < 0)
		{
			V.Info("（你不是主持人）");
			V.Footer();
			return true;
		}
		const SDefenceLobby &L = gs_aLobbies[LobbyIdx];
		if(L.m_aPendingApplicants.size() == 0)
		{
			V.Info("（暂无申请）");
		}
		else
		{
			V.Option("ccv_defence_acceptall", "全部批准");
			for(int pi = 0; pi < L.m_aPendingApplicants.size(); pi++)
			{
				const int CID = L.m_aPendingApplicants[pi];
				char aAccept[VOTE_CMD_LENGTH];
				char aReject[VOTE_CMD_LENGTH];
				char aLine[VOTE_DESC_LENGTH];
				str_format(aAccept, sizeof(aAccept), "ccv_defence_accept %d", CID);
				str_format(aReject, sizeof(aReject), "ccv_defence_reject %d", CID);
				str_format(aLine, sizeof(aLine), "批准 %s", Server()->ClientName(CID));
				V.Option(aAccept, aLine);
				str_format(aLine, sizeof(aLine), "拒绝 %s", Server()->ClientName(CID));
				V.Option(aReject, aLine);
			}
		}
		V.Footer();
		return true;
	}

	default:
		return false;
	}
}

void CDefenceLobbyManager::RegisterChatCommands(CCommandManager *pManager)
{
	if(!pManager)
		return;
	CGameContext *pGame = GS();

	pManager->AddCommand("defence", "塔防大厅帮助", "", ConDefence, pGame);
	pManager->AddCommand("defence_create", "创建塔防大厅", "", ConDefenceCreate, pGame);
	pManager->AddCommand("defence_invite", "<玩家名> [...] - 邀请玩家", "r", ConDefenceInvite, pGame);
	pManager->AddCommand("defence_apply", "<大厅编号> - 申请加入", "i", ConDefenceApply, pGame);
	pManager->AddCommand("defence_yes", "接受塔防邀请", "", ConDefenceYes, pGame);
	pManager->AddCommand("defence_start", "提前开始（主持人）", "", ConDefenceStart, pGame);
	pManager->AddCommand("defence_cancel", "取消塔防大厅（主持人）", "", ConDefenceCancel, pGame);
	pManager->AddCommand("defence_status", "查看塔防大厅状态", "", ConDefenceStatus, pGame);
}

void CDefenceLobbyManager::ConDefence(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	pGame->SendChatTo(pCtx->m_ClientID,
		"塔防大厅：/defence_create | /defence_invite | /defence_apply <编号> | /yes 接受邀请 | /defence_start | /defence_cancel");
}

void CDefenceLobbyManager::ConDefenceCreate(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	if(gs_pPrimaryManager)
		gs_pPrimaryManager->CreateLobby(pCtx->m_ClientID, -1, -1);
}

void CDefenceLobbyManager::ConDefenceInvite(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	if(!gs_pPrimaryManager)
		return;
	char aNames[512];
	aNames[0] = '\0';
	for(int i = 0; i < pResult->NumArguments(); i++)
	{
		if(i > 0)
			str_append(aNames, " ", sizeof(aNames));
		str_append(aNames, pResult->GetString(i), sizeof(aNames));
	}
	gs_pPrimaryManager->InvitePlayers(pCtx->m_ClientID, aNames);
}

void CDefenceLobbyManager::ConDefenceApply(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	if(gs_pPrimaryManager && pResult->NumArguments() >= 1)
		gs_pPrimaryManager->ApplyToLobby(pCtx->m_ClientID, pResult->GetInteger(0));
}

void CDefenceLobbyManager::ConDefenceYes(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	if(gs_pPrimaryManager)
		gs_pPrimaryManager->AcceptInvite(pCtx->m_ClientID);
}

void CDefenceLobbyManager::ConDefenceStart(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	if(gs_pPrimaryManager)
		gs_pPrimaryManager->EarlyStart(pCtx->m_ClientID);
}

void CDefenceLobbyManager::ConDefenceCancel(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	if(gs_pPrimaryManager)
		gs_pPrimaryManager->CancelLobby(pCtx->m_ClientID);
}

void CDefenceLobbyManager::ConDefenceStatus(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	if(gs_pPrimaryManager)
		gs_pPrimaryManager->LobbyStatus(pCtx->m_ClientID);
}
