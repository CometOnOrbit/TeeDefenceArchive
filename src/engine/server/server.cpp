/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/masterserver.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/http_request.h>
#include <engine/shared/http_thread.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/mapchecker.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>

#include <mastersrv/mastersrv.h>

#include <game/version.h>

#include "multi_worlds.h"
#include "register.h"
#include "server.h"

#include <signal.h>

volatile sig_atomic_t InterruptSignaled = 0;

void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer *pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s[id|ip|range] ?i[minutes] r[reason]", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanExt, this, "Ban player with IP/IP range/client id for x minutes for any reason");
}

template<class T>
int CServerBan::BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason)
{
	// validate address
	if(Server()->m_RconClientID >= 0 && Server()->m_RconClientID < MAX_CLIENTS &&
		Server()->m_aClients[Server()->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if(NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientID)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(i == Server()->m_RconClientID || Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if(Server()->m_RconClientID == IServer::RCON_CID_VOTE)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if(Server()->m_aClients[i].m_Authed != CServer::AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}

	int Result = Ban(pBanPool, pData, Seconds, pReason);
	if(Result != 0)
		return Result;

	// drop banned clients
	typename T::CDataType Data = *pData;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			continue;

		if(NetMatch(&Data, Server()->m_NetServer.ClientAddr(i)))
		{
			CNetHash NetHash(&Data);
			char aBuf[256];
			MakeBanInfo(pBanPool->Find(&Data, &NetHash), aBuf, sizeof(aBuf), MSGTYPE_PLAYER);
			Server()->m_NetServer.Drop(i, aBuf);
		}
	}

	return Result;
}

int CServerBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason)
{
	return BanExt(&m_BanAddrPool, pAddr, Seconds, pReason);
}

int CServerBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if(pRange->IsValid())
		return BanExt(&m_BanRangePool, pRange, Seconds, pReason);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

void CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments() > 1 ? clamp(pResult->GetInteger(1), 0, 44640) : 30;
	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "No reason given";

	if(!str_is_number(pStr))
	{
		int ClientID = str_toint(pStr);
		if(ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->Server()->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
			pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes * 60, pReason);
	}
	else
		ConBan(pResult, pUser);
}

void CServer::CClient::Reset()
{
	// reset input
	for(int i = 0; i < 200; i++)
		m_aInputs[i].m_GameTick = -1;
	m_CurrentInput = 0;
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));

	m_Snapshots.PurgeAll();
	m_LastAckedSnapshot = -1;
	m_LastInputTick = -1;
	m_SnapRate = CClient::SNAPRATE_INIT;
	m_Score = 0;
	m_MapChunk = 0;
}

CServer::CServer() : m_DemoRecorder(&m_SnapshotDelta)
{
	m_TickSpeed = SERVER_TICK_SPEED;

	m_pGameServer = 0;
	m_pMultiWorlds = 0;

	m_CurrentGameTick = 0;
	m_RunServer = true;

	str_copy(m_aShutdownReason, "Server shutdown", sizeof(m_aShutdownReason));

	m_pCurrentMapData = 0;
	m_CurrentMapSize = 0;

	m_MapReload = false;
	m_ServerInfoNeedsUpdate = false;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	m_RconPasswordSet = 0;
	m_GeneratedRconPassword = 0;

	Init();
}

void CServer::SetClientName(int ClientID, const char *pName)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY || !pName)
		return;

	const char *pDefaultName = "(1)";
	pName = str_utf8_skip_whitespaces(pName);
	str_utf8_copy_num(m_aClients[ClientID].m_aName, *pName ? pName : pDefaultName, sizeof(m_aClients[ClientID].m_aName), MAX_NAME_LENGTH);
}

void CServer::SetClientClan(int ClientID, const char *pClan)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY || !pClan)
		return;

	str_utf8_copy_num(m_aClients[ClientID].m_aClan, pClan, sizeof(m_aClients[ClientID].m_aClan), MAX_CLAN_LENGTH);
}

void CServer::SetClientCountry(int ClientID, int Country)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_Country = Country;
}

void CServer::SetClientScore(int ClientID, int Score)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;
	m_aClients[ClientID].m_Score = Score;
}

void CServer::Kick(int ClientID, const char *pReason)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if(m_RconClientID == ClientID)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
		return;
	}
	else if(m_aClients[ClientID].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
		return;
	}

	if(ClientID >= MAX_HUMAN_CLIENTS && GameServerPlayer(ClientID) && GameServerPlayer(ClientID)->IsClientBot(ClientID))
	{
		DummyRemove(ClientID);
		return;
	}

	m_NetServer.Drop(ClientID, pReason);
}

int64 CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq() * Tick) / SERVER_TICK_SPEED;
}

int CServer::Init()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClients[i].m_State = CClient::STATE_EMPTY;
		m_aClients[i].m_aName[0] = 0;
		m_aClients[i].m_aClan[0] = 0;
		m_aClients[i].m_Country = -1;
		m_aClients[i].m_WorldID = INITIALIZER_WORLD_ID;
		m_aClients[i].m_OldWorldID = INITIALIZER_WORLD_ID;
		m_aClients[i].m_ChangeWorld = false;
		m_aClients[i].m_ChangeWorldEnter = false;
		m_aClients[i].m_ChangeWorldDestID = -1;
		m_aClients[i].m_ChangeWorldWasReady = false;
		m_aClients[i].m_HasChangeWorldSession = false;
		m_aClients[i].m_ChangeWorldAccountId = -1;
		m_aClients[i].m_ChangeWorldSessionSize = 0;
		m_aClients[i].m_HasChangeWorldSpawnPos = false;
		m_aClients[i].m_ChangeWorldSpawnPos = vec2(0, 0);
		m_aClients[i].m_Snapshots.Init();
	}

	m_CurrentGameTick = 0;

	return 0;
}

IGameServer *CServer::GameServer(int WorldID) const
{
	if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(WorldID))
		return m_pGameServer;
	return m_pMultiWorlds->GetWorld(WorldID)->GameServer();
}

IGameServer *CServer::GameServerPlayer(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return GameServer(0);
	return GameServer(GetClientWorldID(ClientID));
}

int CServer::GetClientWorldID(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_AUTH)
		return INITIALIZER_WORLD_ID;
	return m_aClients[ClientID].m_WorldID;
}

void CServer::SetClientWorldID(int ClientID, int WorldID)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aClients[ClientID].m_WorldID = WorldID;
}

bool CServer::IsClientChangingWorld(int ClientID) const
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_ChangeWorld
		&& m_aClients[ClientID].m_State >= CClient::STATE_CONNECTING && m_aClients[ClientID].m_State < CClient::STATE_INGAME;
}

void CServer::SetChangeWorldSession(int ClientID, int64 AccountId, const void *pData, int Size)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pData || Size <= 0 || Size > CHANGE_WORLD_SESSION_MAX)
		return;
	m_aClients[ClientID].m_HasChangeWorldSession = true;
	m_aClients[ClientID].m_ChangeWorldAccountId = AccountId;
	m_aClients[ClientID].m_ChangeWorldSessionSize = Size;
	mem_copy(m_aClients[ClientID].m_aChangeWorldSession, pData, Size);
}

int CServer::GetChangeWorldDestID(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return -1;
	return m_aClients[ClientID].m_ChangeWorldDestID;
}

bool CServer::ConsumeChangeWorldEnter(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	const bool Was = m_aClients[ClientID].m_ChangeWorldEnter;
	m_aClients[ClientID].m_ChangeWorldEnter = false;
	return Was;
}

void CServer::SetChangeWorldWasReady(int ClientID, bool Ready)
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS)
		m_aClients[ClientID].m_ChangeWorldWasReady = Ready;
}

bool CServer::GetChangeWorldWasReady(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;
	return m_aClients[ClientID].m_ChangeWorldWasReady;
}

void CServer::SetChangeWorldSpawnPos(int ClientID, vec2 Pos)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	m_aClients[ClientID].m_HasChangeWorldSpawnPos = true;
	m_aClients[ClientID].m_ChangeWorldSpawnPos = Pos;
}

bool CServer::ConsumeChangeWorldSpawnPos(int ClientID, vec2 *pPos)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !pPos || !m_aClients[ClientID].m_HasChangeWorldSpawnPos)
		return false;
	*pPos = m_aClients[ClientID].m_ChangeWorldSpawnPos;
	m_aClients[ClientID].m_HasChangeWorldSpawnPos = false;
	return true;
}

bool CServer::PopChangeWorldSession(int ClientID, int64 *pAccountId, void *pData, int *pSize)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_aClients[ClientID].m_HasChangeWorldSession)
		return false;
	if(pAccountId)
		*pAccountId = m_aClients[ClientID].m_ChangeWorldAccountId;
	if(pData && pSize && *pSize >= m_aClients[ClientID].m_ChangeWorldSessionSize)
		mem_copy(pData, m_aClients[ClientID].m_aChangeWorldSession, m_aClients[ClientID].m_ChangeWorldSessionSize);
	if(pSize)
		*pSize = m_aClients[ClientID].m_ChangeWorldSessionSize;
	m_aClients[ClientID].m_HasChangeWorldSession = false;
	m_aClients[ClientID].m_ChangeWorldAccountId = -1;
	m_aClients[ClientID].m_ChangeWorldSessionSize = 0;
	return true;
}

void CServer::ChangeWorld(int ClientID, int NewWorldID)
{
	if(ClientID < 0 || ClientID >= MAX_HUMAN_CLIENTS || NewWorldID == m_aClients[ClientID].m_WorldID)
		return;
	if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(NewWorldID))
		return;
	if(m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_OldWorldID = m_aClients[ClientID].m_WorldID;
	m_aClients[ClientID].m_ChangeWorldDestID = NewWorldID;
	GameServer(m_aClients[ClientID].m_OldWorldID)->ExportChangeWorldSession(ClientID);
	GameServer(m_aClients[ClientID].m_OldWorldID)->OnClientPrepareChangeWorld(ClientID);

	m_aClients[ClientID].m_WorldID = NewWorldID;
	GameServer(m_aClients[ClientID].m_WorldID)->OnClientPrepareChangeWorld(ClientID);
	m_aClients[ClientID].m_ChangeWorldDestID = -1;

	m_aClients[ClientID].Reset();
	m_aClients[ClientID].m_ChangeWorld = true;
	m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
	SendMap(ClientID);
}

int CServer::GetNumWorlds() const
{
	return m_pMultiWorlds ? m_pMultiWorlds->GetWorldCount() : 0;
}

const char *CServer::GetWorldName(int WorldID) const
{
	return m_pMultiWorlds ? m_pMultiWorlds->GetWorldName(WorldID) : "";
}

const char *CServer::GetWorldMapPath(int WorldID) const
{
	if(m_pMultiWorlds)
	{
		CWorld *pWorld = m_pMultiWorlds->GetWorld(WorldID);
		if(pWorld && pWorld->GetPath()[0])
			return pWorld->GetPath();
	}
	if(!m_pConfig)
		return "";
	const char *pMapShortName = m_pConfig->m_SvMap;
	for(int i = 0; m_pConfig->m_SvMap[i] && m_pConfig->m_SvMap[i + 1]; i++)
	{
		if(m_pConfig->m_SvMap[i] == '/' || m_pConfig->m_SvMap[i] == '\\')
			pMapShortName = &m_pConfig->m_SvMap[i + 1];
	}
	return pMapShortName;
}

const CWorldDetail *CServer::GetWorldDetail(int WorldID) const
{
	if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(WorldID))
		return nullptr;
	CWorld *pWorld = m_pMultiWorlds->GetWorld(WorldID);
	return pWorld ? pWorld->GetDetail() : nullptr;
}

void CServer::ReleaseClientInAllWorlds(int ClientID)
{
	if(!m_pMultiWorlds || ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
	{
		if(!m_pMultiWorlds->IsValid(w))
			continue;
		GameServer(w)->ReleaseClientPlayer(ClientID);
	}
}

void CServer::ReleaseClientInOtherWorlds(int ClientID, int KeepWorldID)
{
	if(!m_pMultiWorlds || ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
	{
		if(w == KeepWorldID || !m_pMultiWorlds->IsValid(w))
			continue;
		GameServer(w)->ReleaseClientPlayer(ClientID);
	}
}

bool CServer::HasHumanInWorld(int WorldID) const
{
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(m_aClients[i].m_WorldID != WorldID)
			continue;
		if(m_aClients[i].m_State >= CClient::STATE_READY)
			return true;
	}
	return false;
}

int CServer::GetNumPlayersInWorld(int WorldID) const
{
	int Num = 0;
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(m_aClients[i].m_WorldID != WorldID)
			continue;
		if(m_aClients[i].m_State != CClient::STATE_INGAME)
			continue;
		Num++;
	}
	return Num;
}

void CServer::SyncLegacyMapFromWorld(int WorldID)
{
	if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(WorldID))
		return;
	CMapDetail *pMap = m_pMultiWorlds->GetWorld(WorldID)->MapDetail();
	if(!pMap || !pMap->IsLoaded())
		return;
	str_copy(m_aCurrentMap, m_pMultiWorlds->GetWorld(WorldID)->GetPath(), sizeof(m_aCurrentMap));
	m_CurrentMapSha256 = pMap->GetSha256();
	m_CurrentMapCrc = pMap->GetCrc();
	m_CurrentMapSize = (int)pMap->GetSize();
	if(m_pCurrentMapData)
		mem_free(m_pCurrentMapData);
	m_pCurrentMapData = nullptr;
	if(pMap->GetData() && pMap->GetSize() > 0)
	{
		m_pCurrentMapData = (unsigned char *)mem_alloc(pMap->GetSize());
		mem_copy(m_pCurrentMapData, pMap->GetData(), pMap->GetSize());
	}
	m_pMap = pMap->GetMap();
}

void CServer::SetRconCID(int ClientID)
{
	m_RconClientID = ClientID;
}

bool CServer::IsAuthed(int ClientID) const
{
	return m_aClients[ClientID].m_Authed;
}

bool CServer::IsBanned(int ClientID)
{
	return m_ServerBan.IsBanned(m_NetServer.ClientAddr(ClientID), 0, 0, 0);
}

int CServer::GetClientInfo(int ClientID, CClientInfo *pInfo) const
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");
	dbg_assert(pInfo != 0, "info can not be null");

	if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_pName = m_aClients[ClientID].m_aName;
		pInfo->m_Latency = m_aClients[ClientID].m_Latency;
		return 1;
	}
	return 0;
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}

int CServer::GetClientVersion(int ClientID) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Version;
	return 0;
}

const char *CServer::ClientName(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "(invalid)";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aName;
	else
		return "(connecting)";
}

const char *CServer::ClientClan(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aClan;
	else
		return "";
}

int CServer::ClientCountry(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return -1;
	if(m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientID) const
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME;
}

bool CServer::IsClientSlotEmpty(int ClientID) const
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_EMPTY;
}

void CServer::DummyJoin(int ClientID, const char *pName, int WorldID)
{
	if(ClientID < MAX_HUMAN_CLIENTS || ClientID >= MAX_CLIENTS)
		return;
	if(m_aClients[ClientID].m_State != CClient::STATE_EMPTY)
		return;

	m_aClients[ClientID].m_State = CClient::STATE_INGAME;
	m_aClients[ClientID].m_Version = CLIENT_VERSION;
	str_copy(m_aClients[ClientID].m_aName, pName, sizeof(m_aClients[ClientID].m_aName));
	m_aClients[ClientID].m_aClan[0] = 0;
	m_aClients[ClientID].m_Country = -1;
	m_aClients[ClientID].m_WorldID = WorldID;
	m_aClients[ClientID].Reset();

	GameServer(WorldID)->OnBotConnected(ClientID);
}

void CServer::DummyRemove(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	if(!GameServerPlayer(ClientID)->IsClientBot(ClientID))
		return;
	if(m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	GameServerPlayer(ClientID)->OnClientDrop(ClientID, "teedefense zombie removed");

	m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	m_aClients[ClientID].m_aName[0] = 0;
	m_aClients[ClientID].m_aClan[0] = 0;
	m_aClients[ClientID].m_Country = -1;
	m_aClients[ClientID].m_Authed = AUTHED_NO;
	m_aClients[ClientID].m_AuthTries = 0;
	m_aClients[ClientID].m_pRconCmdToSend = 0;
	m_aClients[ClientID].m_MapListEntryToSend = -1;
	m_aClients[ClientID].m_NoRconNote = false;
	m_aClients[ClientID].m_Quitting = false;
	m_aClients[ClientID].m_Snapshots.PurgeAll();
}

void CServer::InitRconPasswordIfUnset()
{
	if(m_RconPasswordSet)
	{
		return;
	}

	static const char VALUES[] = "ABCDEFGHKLMNPRSTUVWXYZabcdefghjkmnopqt23456789";
	static const size_t NUM_VALUES = sizeof(VALUES) - 1; // Disregard the '\0'.
	static const size_t PASSWORD_LENGTH = 6;
	dbg_assert(NUM_VALUES * NUM_VALUES >= 2048, "need at least 2048 possibilities for 2-character sequences");
	// With 6 characters, we get a password entropy of log(2048) * 6/2 = 33bit.

	dbg_assert(PASSWORD_LENGTH % 2 == 0, "need an even password length");
	unsigned short aRandom[PASSWORD_LENGTH / 2];
	char aRandomPassword[PASSWORD_LENGTH + 1];
	aRandomPassword[PASSWORD_LENGTH] = 0;

	secure_random_fill(aRandom, sizeof(aRandom));
	for(size_t i = 0; i < PASSWORD_LENGTH / 2; i++)
	{
		unsigned short RandomNumber = aRandom[i] % 2048;
		aRandomPassword[2 * i + 0] = VALUES[RandomNumber / NUM_VALUES];
		aRandomPassword[2 * i + 1] = VALUES[RandomNumber % NUM_VALUES];
	}

	str_copy(Config()->m_SvRconPassword, aRandomPassword, sizeof(Config()->m_SvRconPassword));
	m_GeneratedRconPassword = 1;
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID)
{
	CNetChunk Packet;
	if(!pMsg)
		return -1;

	// drop invalid packet or bot slots (dummies: STATE_INGAME but no network connection)
	if(ClientID != -1 && (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY || m_aClients[ClientID].m_Quitting || !m_NetServer.ClientSlotOnline(ClientID) || (GameServerPlayer(ClientID) && GameServerPlayer(ClientID)->IsClientBot(ClientID))))
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientID = ClientID;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	// write message to demo recorder
	if(!(Flags & MSGFLAG_NORECORD))
		m_DemoRecorder.RecordMessage(pMsg->Data(), pMsg->Size());

	if(!(Flags & MSGFLAG_NOSEND))
	{
		if(ClientID == -1)
		{
			// broadcast
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(m_aClients[i].m_State != CClient::STATE_INGAME || m_aClients[i].m_Quitting)
					continue;
				if(GameServerPlayer(i) && GameServerPlayer(i)->IsClientBot(i))
					continue;

				Packet.m_ClientID = i;
				m_NetServer.Send(&Packet);
			}
		}
		else
			m_NetServer.Send(&Packet);
	}
	return 0;
}

void CServer::DoSnapshot(int WorldID)
{
	if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(WorldID))
		return;

	IGameServer *pGS = GameServer(WorldID);
	if(!pGS)
		return;

	pGS->OnPreSnap();

	// create snapshot for demo recording (world 0 only)
	if(WorldID == 0 && m_DemoRecorder.IsRecording())
	{
		char aData[CSnapshot::MAX_SIZE];
		int SnapshotSize;

		m_SnapshotBuilder.Init();
		pGS->OnSnap(-1);
		SnapshotSize = m_SnapshotBuilder.Finish(aData);

		m_DemoRecorder.RecordSnapshot(Tick(), aData, SnapshotSize);
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_WorldID != WorldID || m_aClients[i].m_State != CClient::STATE_INGAME)
			continue;

		if(!m_NetServer.ClientSlotOnline(i))
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick() % SERVER_TICK_SPEED) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_INIT && (Tick() % 10) != 0)
			continue;

		{
			char aData[CSnapshot::MAX_SIZE];
			CSnapshot *pData = (CSnapshot *) aData; // Fix compiler warning for strict-aliasing
			char aDeltaData[CSnapshot::MAX_SIZE];
			char aCompData[CSnapshot::MAX_SIZE];
			int SnapshotSize;
			int Crc;
			static CSnapshot EmptySnap;
			CSnapshot *pDeltashot = &EmptySnap;
			int DeltashotSize;
			int DeltaTick = -1;
			int DeltaSize;

			m_SnapshotBuilder.Init();

			pGS->OnSnap(i);

			// finish snapshot
			SnapshotSize = m_SnapshotBuilder.Finish(pData);
			Crc = pData->Crc();

			// remove old snapshos
			// keep 3 seconds worth of snapshots
			m_aClients[i].m_Snapshots.PurgeUntil(m_CurrentGameTick - SERVER_TICK_SPEED * 3);

			// save it the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0);

			// find snapshot that we can perform delta against
			EmptySnap.Clear();

			{
				DeltashotSize = m_aClients[i].m_Snapshots.Get(m_aClients[i].m_LastAckedSnapshot, 0, &pDeltashot, 0);
				if(DeltashotSize >= 0)
					DeltaTick = m_aClients[i].m_LastAckedSnapshot;
				else
				{
					// no acked package found, force client to recover rate
					if(m_aClients[i].m_SnapRate == CClient::SNAPRATE_FULL)
						m_aClients[i].m_SnapRate = CClient::SNAPRATE_RECOVER;
				}
			}

			// create delta
			DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData);

			if(DeltaSize > 0)
			{
				// compress it
				int SnapshotSize;
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;
				int NumPackets;

				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData, sizeof(aCompData));
				NumPackets = (SnapshotSize + MaxSize - 1) / MaxSize;

				for(int n = 0, Left = SnapshotSize; Left > 0; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if(NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY, true);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick - DeltaTick);
				SendMsg(&Msg, MSGFLAG_FLUSH, i);

				if(DeltaSize < 0)
				{
					char aBuf[64];
					str_format(aBuf, sizeof(aBuf), "delta pack failed! (%d)", DeltaSize);
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
				}
			}
		}
	}

	pGS->OnPostSnap();
}

int CServer::NewClientCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *) pUser;

	for(int w = 0; w < pThis->GetNumWorlds(); w++)
	{
		if(pThis->GameServer(w) && pThis->GameServer(w)->IsClientBot(ClientID))
		{
			pThis->GameServer(w)->OnClientDrop(ClientID, "removing dummy");
			break;
		}
	}

	pThis->m_aClients[ClientID].m_State = CClient::STATE_AUTH;
	pThis->m_aClients[ClientID].m_WorldID = pThis->Config()->m_SvShowWorldWhenConnect;
	pThis->m_aClients[ClientID].m_ChangeWorld = false;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_MapListEntryToSend = -1;
	pThis->m_aClients[ClientID].m_NoRconNote = false;
	pThis->m_aClients[ClientID].m_Quitting = false;
	pThis->m_aClients[ClientID].m_Latency = 0;
	pThis->m_aClients[ClientID].Reset();

	return 0;
}

int CServer::DelClientCallback(int ClientID, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *) pUser;

	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=%s reason='%s'", ClientID, aAddrStr, pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	if(pThis->m_aClients[ClientID].m_State >= CClient::STATE_READY)
		pThis->m_aClients[ClientID].m_Quitting = true;
	pThis->ReleaseClientInAllWorlds(ClientID);

	pThis->m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_MapListEntryToSend = -1;
	pThis->m_aClients[ClientID].m_NoRconNote = false;
	pThis->m_aClients[ClientID].m_Quitting = false;
	pThis->m_aClients[ClientID].m_Snapshots.PurgeAll();
	return 0;
}

void CServer::SendMap(int ClientID)
{
	const int WorldID = m_aClients[ClientID].m_WorldID;
	if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(WorldID))
		return;
	CMapDetail *pMapDetail = m_pMultiWorlds->GetWorld(WorldID)->MapDetail();

	CMsgPacker Msg(NETMSG_MAP_CHANGE, true);
	Msg.AddString(m_pMultiWorlds->GetWorld(WorldID)->GetPath(), 0);
	Msg.AddInt((int)pMapDetail->GetCrc());
	Msg.AddInt((int)pMapDetail->GetSize());
	Msg.AddInt(m_MapChunksPerRequest);
	Msg.AddInt(MAP_CHUNK_SIZE);
	Msg.AddRaw(&pMapDetail->GetSha256(), sizeof(pMapDetail->GetSha256()));
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
	m_aClients[ClientID].m_MapChunk = 0;
}

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY, true);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	Msg.AddString(pLine, 512);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconLineAuthed(const char *pLine, void *pUser, bool Highlighted)
{
	static bool s_ReentryGuard = false;
	if(s_ReentryGuard)
		return;
	s_ReentryGuard = true;

	CServer *pThis = (CServer *) pUser;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY && pThis->m_aClients[i].m_Authed >= pThis->m_RconAuthLevel)
			pThis->SendRconLine(i, pLine);
	}

	s_ReentryGuard = false;
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD, true);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM, true);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::UpdateClientRconCommands()
{
	for(int ClientID = Tick() % MAX_RCONCMD_RATIO; ClientID < MAX_CLIENTS; ClientID += MAX_RCONCMD_RATIO)
	{
		if(m_aClients[ClientID].m_State != CClient::STATE_EMPTY && m_aClients[ClientID].m_Authed)
		{
			int ConsoleAccessLevel = m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD;
			for(int i = 0; i < MAX_RCONCMD_SEND && m_aClients[ClientID].m_pRconCmdToSend; ++i)
			{
				SendRconCmdAdd(m_aClients[ClientID].m_pRconCmdToSend, ClientID);
				m_aClients[ClientID].m_pRconCmdToSend = m_aClients[ClientID].m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
			}
		}
	}
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	CMsgUnpacker Unpacker(pPacket->m_pData, pPacket->m_DataSize);
	if(Unpacker.Error())
		return;

	const int ClientID = pPacket->m_ClientID;
	if(Unpacker.System())
	{
		// system message
		if(Unpacker.Type() == NETMSG_INFO)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_AUTH)
			{
				const char *pVersion = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(str_comp(pVersion, GameServer(0)->NetVersion()) != 0)
				{
					// wrong version
					char aReason[256];
					str_format(aReason, sizeof(aReason), "Wrong version. Server is running '%s' and client '%s'", GameServer(0)->NetVersion(), pVersion);
					m_NetServer.Drop(ClientID, aReason);
					return;
				}

				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Config()->m_Password[0] != 0 && str_comp(Config()->m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientID, "Wrong password");
					return;
				}

				m_aClients[ClientID].m_Version = Unpacker.GetInt();
				m_aClients[ClientID].m_ServerInfoVersion = Unpacker.GetIntOrDefault(SERVERINFO_VERSION_LEGACY);

				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				SendMap(ClientID);
			}
		}
		else if(Unpacker.Type() == NETMSG_REQUEST_MAP_DATA)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_CONNECTING || m_aClients[ClientID].m_State == CClient::STATE_CONNECTING_AS_SPEC))
			{
				const int WorldID = m_aClients[ClientID].m_WorldID;
				if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(WorldID))
					return;
				CMapDetail *pMapDetail = m_pMultiWorlds->GetWorld(WorldID)->MapDetail();
				const int MapSize = (int)pMapDetail->GetSize();
				unsigned char *pMapData = pMapDetail->GetData();
				int ChunkSize = MAP_CHUNK_SIZE;

				for(int i = 0; i < m_MapChunksPerRequest && m_aClients[ClientID].m_MapChunk >= 0; ++i)
				{
					int Chunk = m_aClients[ClientID].m_MapChunk;
					int Offset = Chunk * ChunkSize;

					if(Offset + ChunkSize >= MapSize)
					{
						ChunkSize = MapSize - Offset;
						m_aClients[ClientID].m_MapChunk = -1;
					}
					else
						m_aClients[ClientID].m_MapChunk++;

					CMsgPacker Msg(NETMSG_MAP_DATA, true);
					Msg.AddRaw(&pMapData[Offset], ChunkSize);
					SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);

					if(Config()->m_Debug)
					{
						char aBuf[64];
						str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
						Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
					}
				}
			}
		}
		else if(Unpacker.Type() == NETMSG_READY)
		{
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && (m_aClients[ClientID].m_State == CClient::STATE_CONNECTING || m_aClients[ClientID].m_State == CClient::STATE_CONNECTING_AS_SPEC))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%d addr=%s", ClientID, aAddrStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

				bool ConnectAsSpec = m_aClients[ClientID].m_State == CClient::STATE_CONNECTING_AS_SPEC;
				m_aClients[ClientID].m_State = CClient::STATE_READY;
				IGameServer *pGS = GameServerPlayer(ClientID);
				if(pGS && (!m_aClients[ClientID].m_ChangeWorld || !pGS->IsClientReady(ClientID)))
					pGS->OnClientConnected(ClientID, ConnectAsSpec);
				SendConnectionReady(ClientID);
			}
		}
		else if(Unpacker.Type() == NETMSG_ENTERGAME)
		{
			IGameServer *pGS = GameServerPlayer(ClientID);
			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_READY && pGS && pGS->IsClientReady(ClientID))
			{
				char aAddrStr[NETADDR_MAXSTRSIZE];
				net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "player has entered the game. ClientID=%d world=%d addr=%s", ClientID, m_aClients[ClientID].m_WorldID, aAddrStr);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				m_aClients[ClientID].m_State = CClient::STATE_INGAME;
				m_aClients[ClientID].m_ChangeWorldEnter = m_aClients[ClientID].m_ChangeWorld;
				m_aClients[ClientID].m_ChangeWorld = false;
				SendServerInfo(ClientID);
				pGS->OnClientEnter(ClientID);
			}
		}
		else if(Unpacker.Type() == NETMSG_INPUT)
		{
			CClient::CInput *pInput;
			int64 TagTime;
			int64 Now = time_get();

			m_aClients[ClientID].m_LastAckedSnapshot = Unpacker.GetInt();
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if(Unpacker.Error() || Size / 4 > MAX_INPUT_SIZE)
				return;

			if(m_aClients[ClientID].m_LastAckedSnapshot > 0)
				m_aClients[ClientID].m_SnapRate = CClient::SNAPRATE_FULL;

			// add message to report the input timing
			// skip packets that are old
			if(IntendedTick > m_aClients[ClientID].m_LastInputTick)
			{
				int TimeLeft = ((TickStartTime(IntendedTick) - Now) * 1000) / time_freq();

				CMsgPacker Msg(NETMSG_INPUTTIMING, true);
				Msg.AddInt(IntendedTick);
				Msg.AddInt(TimeLeft);
				SendMsg(&Msg, 0, ClientID);
			}

			m_aClients[ClientID].m_LastInputTick = IntendedTick;

			pInput = &m_aClients[ClientID].m_aInputs[m_aClients[ClientID].m_CurrentInput];

			if(IntendedTick <= Tick())
				IntendedTick = Tick() + 1;

			pInput->m_GameTick = IntendedTick;

			for(int i = 0; i < Size / 4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			int PingCorrection = clamp(Unpacker.GetInt(), 0, 50);
			if(m_aClients[ClientID].m_Snapshots.Get(m_aClients[ClientID].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
			{
				m_aClients[ClientID].m_Latency = (int) (((Now - TagTime) * 1000) / time_freq());
				m_aClients[ClientID].m_Latency = maximum(0, m_aClients[ClientID].m_Latency - PingCorrection);
			}

			mem_copy(m_aClients[ClientID].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE * sizeof(int));

			m_aClients[ClientID].m_CurrentInput++;
			m_aClients[ClientID].m_CurrentInput %= 200;

			// call the mod with the fresh input data
			if(m_aClients[ClientID].m_State == CClient::STATE_INGAME)
				GameServerPlayer(ClientID)->OnClientDirectInput(ClientID, m_aClients[ClientID].m_LatestInput.m_aData);
		}
		else if(Unpacker.Type() == NETMSG_RCON_CMD)
		{
			const char *pCmd = Unpacker.GetString();

			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "ClientID=%d rcon='%s'", ClientID, pCmd);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
				m_RconClientID = ClientID;
				m_RconAuthLevel = m_aClients[ClientID].m_Authed;
				Console()->SetAccessLevel(m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD);
				Console()->ExecuteLineFlag(pCmd, CFGFLAG_SERVER);
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				m_RconClientID = IServer::RCON_CID_SERV;
				m_RconAuthLevel = AUTHED_ADMIN;
			}
		}
		else if(Unpacker.Type() == NETMSG_RCON_AUTH)
		{
			const char *pPw = Unpacker.GetString(CUnpacker::SANITIZE_CC);

			if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0)
			{
				if(Config()->m_SvRconPassword[0] == 0 && Config()->m_SvRconModPassword[0] == 0)
				{
					if(!m_aClients[ClientID].m_NoRconNote)
					{
						SendRconLine(ClientID, "No rcon password set on server. Set sv_rcon_password and/or sv_rcon_mod_password to enable the remote console.");
						m_aClients[ClientID].m_NoRconNote = true;
					}
				}
				else if(Config()->m_SvRconPassword[0] && str_comp(pPw, Config()->m_SvRconPassword) == 0)
				{
					CMsgPacker Msg(NETMSG_RCON_AUTH_ON, true);
					SendMsg(&Msg, MSGFLAG_VITAL, ClientID);

					m_aClients[ClientID].m_Authed = AUTHED_ADMIN;
					m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_ADMIN, CFGFLAG_SERVER);
					if(m_aClients[ClientID].m_Version >= MIN_MAPLIST_CLIENTVERSION)
						m_aClients[ClientID].m_MapListEntryToSend = 0;
					SendRconLine(ClientID, "Admin authentication successful. Full remote console access granted.");
					char aAddrStr[NETADDR_MAXSTRSIZE];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d addr=%s authed (admin)", ClientID, aAddrStr);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(Config()->m_SvRconModPassword[0] && str_comp(pPw, Config()->m_SvRconModPassword) == 0)
				{
					CMsgPacker Msg(NETMSG_RCON_AUTH_ON, true);
					SendMsg(&Msg, MSGFLAG_VITAL, ClientID);

					m_aClients[ClientID].m_Authed = AUTHED_MOD;
					m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_MOD, CFGFLAG_SERVER);
					SendRconLine(ClientID, "Moderator authentication successful. Limited remote console access granted.");
					const IConsole::CCommandInfo *pInfo = Console()->GetCommandInfo("sv_map", CFGFLAG_SERVER, false);
					if(pInfo && pInfo->GetAccessLevel() == IConsole::ACCESS_LEVEL_MOD && m_aClients[ClientID].m_Version >= MIN_MAPLIST_CLIENTVERSION)
						m_aClients[ClientID].m_MapListEntryToSend = 0;
					char aAddrStr[NETADDR_MAXSTRSIZE];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "ClientID=%d addr=%s authed (moderator)", ClientID, aAddrStr);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}
				else if(Config()->m_SvRconMaxTries && m_ServerBan.IsBannable(m_NetServer.ClientAddr(ClientID)))
				{
					m_aClients[ClientID].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientID].m_AuthTries, Config()->m_SvRconMaxTries);
					SendRconLine(ClientID, aBuf);
					if(m_aClients[ClientID].m_AuthTries >= Config()->m_SvRconMaxTries)
					{
						if(!Config()->m_SvRconBantime)
							m_NetServer.Drop(ClientID, "Too many remote console authentication tries");
						else
							m_ServerBan.BanAddr(m_NetServer.ClientAddr(ClientID), Config()->m_SvRconBantime * 60, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientID, "Wrong password.");
				}
			}
		}
		else if(Unpacker.Type() == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY, true);
			SendMsg(&Msg, MSGFLAG_FLUSH, ClientID);
		}
		else
		{
			if(Config()->m_Debug)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "strange message ClientID=%d msg=%d data_size=%d", ClientID, Unpacker.Type(), pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
				str_hex(aBuf, sizeof(aBuf), pPacket->m_pData, minimum(pPacket->m_DataSize, 32));
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State >= CClient::STATE_READY)
			GameServerPlayer(ClientID)->OnMessage(Unpacker.Type(), &Unpacker, ClientID);
	}
}

void CServer::GenerateServerInfo(CPacker *pPacker, int ServerInfoVersion, bool IncludeClientInfo)
{
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CClient::STATE_EMPTY)
			continue;
		if(i >= MAX_HUMAN_CLIENTS || (GameServerPlayer(i) && GameServerPlayer(i)->IsClientBot(i)))
			continue;

		if(GameServerPlayer(i)->IsClientPlayer(i))
			PlayerCount++;

		ClientCount++;
	}

	pPacker->AddString(GameServer()->Version(), 32);
	pPacker->AddString(Config()->m_SvName, 64);
	pPacker->AddString(Config()->m_SvHostname, 128);
	pPacker->AddString(GetMapName(), 32);

	// gametype
	pPacker->AddString(GameServer()->GameType(), 16);

	// flags
	int Flags = 0;
	if(Config()->m_Password[0]) // password set
		Flags |= SERVERINFO_FLAG_PASSWORD;
	if(GameServer()->TimeScore())
		Flags |= SERVERINFO_FLAG_TIMESCORE;
	pPacker->AddInt(Flags);

	pPacker->AddInt(Config()->m_SvSkillLevel); // server skill level
	pPacker->AddInt(ServerInfoVersion == SERVERINFO_VERSION_LEGACY ? PlayerCount : 0); // num players
	pPacker->AddInt(GameServer()->GetMaxPlayerSlots()); // max players
	pPacker->AddInt(ServerInfoVersion == SERVERINFO_VERSION_LEGACY ? ClientCount : 0); // num clients
	pPacker->AddInt(maximum(ClientCount, Config()->m_SvMaxClients)); // max clients

	if(IncludeClientInfo && ServerInfoVersion == SERVERINFO_VERSION_LEGACY)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_aClients[i].m_State == CClient::STATE_EMPTY)
				continue;
			if(i >= MAX_HUMAN_CLIENTS || GameServerPlayer(i)->IsClientBot(i))
				continue;
			{
				pPacker->AddString(ClientName(i), 0); // client name
				pPacker->AddString(ClientClan(i), 0); // client clan
				pPacker->AddInt(m_aClients[i].m_Country); // client country
				pPacker->AddInt(m_aClients[i].m_Score); // client score
				pPacker->AddInt(GameServerPlayer(i)->IsClientPlayer(i) ? 0 : 1); // flag spectator=1, bot=2 (player=0)
			}
		}
	}
}

int CServer::GenerateServerInfoPlayers(CPacker *pPacker, int ServerInfoVersion, int StartClientID)
{
	if(ServerInfoVersion == SERVERINFO_VERSION_LEGACY)
		return -1;
	while(StartClientID < MAX_CLIENTS)
	{
		if(m_aClients[StartClientID].m_State != CClient::STATE_EMPTY &&
			StartClientID < MAX_HUMAN_CLIENTS &&
			!(GameServerPlayer(StartClientID) && GameServerPlayer(StartClientID)->IsClientBot(StartClientID)))
		{
			CPacker InfoPacker;
			InfoPacker.Reset();
			InfoPacker.AddInt(1); // for client to check
			InfoPacker.AddString(ClientName(StartClientID), 0); // client name
			InfoPacker.AddString(ClientClan(StartClientID), 0); // client clan
			InfoPacker.AddInt(m_aClients[StartClientID].m_Country); // client country
			InfoPacker.AddInt(m_aClients[StartClientID].m_Score); // client score
			InfoPacker.AddInt(GameServerPlayer(StartClientID)->IsClientPlayer(StartClientID) ? 0 : 1); // flag spectator=1, bot=2 (player=0)

			if(pPacker->Size() + InfoPacker.Size() + NET_MAX_CHUNKHEADERSIZE > NET_MAX_PAYLOAD)
				break;

			pPacker->AddRaw(InfoPacker.Data(), InfoPacker.Size());
		}
		StartClientID++;
	}

	return StartClientID >= MAX_CLIENTS ? -1 : StartClientID;
}

void CServer::SendServerInfo(int ClientID)
{
	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_aClients[i].m_State != CClient::STATE_EMPTY)
			{
				CMsgPacker Msg(NETMSG_SERVERINFO, true);
				GenerateServerInfo(&Msg, m_aClients[i].m_ServerInfoVersion, false);
				SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, i);
			}
		}
	}
	else if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State != CClient::STATE_EMPTY)
	{
		CMsgPacker Msg(NETMSG_SERVERINFO, true);
		GenerateServerInfo(&Msg, m_aClients[ClientID].m_ServerInfoVersion, false);
		SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
	}
}

void CServer::ExpireServerInfo()
{
	m_ServerInfoNeedsUpdate = true;
}

void CServer::UpdateRegisterServerInfo()
{
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State == CClient::STATE_INGAME)
		{
			if(i < MAX_HUMAN_CLIENTS && GameServerPlayer(i)->IsClientPlayer(i))
				PlayerCount++;
			if(i < MAX_HUMAN_CLIENTS)
				ClientCount++;
		}
	}

	char aMapSha256[SHA256_MAXSTRSIZE];

	sha256_str(m_CurrentMapSha256, aMapSha256, sizeof(aMapSha256));

	static array<char> lServerInfo;
	lServerInfo.clear_size();

	memory_stream Stream(&lServerInfo);
	CJsonWriter JsonWriter(&Stream);

	JsonWriter.BeginObject();
	JsonWriter.WriteAttribute("max_clients");
	JsonWriter.WriteIntValue(m_NetServer.GetMaxClients());

	JsonWriter.WriteAttribute("max_players");
	JsonWriter.WriteIntValue(m_NetServer.GetMaxClients());

	JsonWriter.WriteAttribute("passworded");
	JsonWriter.WriteBoolValue(Config()->m_Password[0]);

	JsonWriter.WriteAttribute("game_type");
	JsonWriter.WriteStrValue(GameServer()->GameType());

	if(Config()->m_SvRegisterCommunityToken[0])
	{
		if(Config()->m_SvFlag != -1)
		{
			JsonWriter.WriteAttribute("country");
			JsonWriter.WriteIntValue(Config()->m_SvFlag); // ISO 3166-1 numeric
		}
	}

	JsonWriter.WriteAttribute("name");
	JsonWriter.WriteStrValue(Config()->m_SvName);

	JsonWriter.WriteAttribute("map");
	JsonWriter.BeginObject();
	JsonWriter.WriteAttribute("name");
	JsonWriter.WriteStrValue(GetMapName());
	JsonWriter.WriteAttribute("sha256");
	JsonWriter.WriteStrValue(aMapSha256);
	JsonWriter.WriteAttribute("size");
	JsonWriter.WriteIntValue(m_CurrentMapSize);
	JsonWriter.EndObject();

	JsonWriter.WriteAttribute("version");
	JsonWriter.WriteStrValue(GameServer()->Version());

	JsonWriter.WriteAttribute("client_score_kind");
	JsonWriter.WriteStrValue("points");

	JsonWriter.WriteAttribute("requires_login");
	JsonWriter.WriteBoolValue(true);

	JsonWriter.WriteAttribute("clients");
	JsonWriter.BeginArray();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aClients[i].m_State != CClient::STATE_INGAME || i >= MAX_HUMAN_CLIENTS)
			continue;
		if(!GameServerPlayer(i)->IsClientPlayer(i))
			continue;
		{
			JsonWriter.BeginObject();

			JsonWriter.WriteAttribute("name");
			JsonWriter.WriteStrValue(ClientName(i));

			JsonWriter.WriteAttribute("clan");
			JsonWriter.WriteStrValue(ClientClan(i));

			JsonWriter.WriteAttribute("country");
			JsonWriter.WriteIntValue(m_aClients[i].m_Country); // ISO 3166-1 numeric

			JsonWriter.WriteAttribute("score");
			JsonWriter.WriteIntValue(m_aClients[i].m_Score);

			JsonWriter.WriteAttribute("is_player");
			JsonWriter.WriteBoolValue(true);

			GameServerPlayer(i)->OnUpdatePlayerServerInfo(&JsonWriter, i);

			JsonWriter.EndObject();
		}
	}

	JsonWriter.EndArray();
	JsonWriter.EndObject();

	Stream.write((unsigned char *) "", 1);

	m_Register.OnNewInfo(lServerInfo.base_ptr());
}

void CServer::PumpNetwork()
{
	CNetChunk Packet;
	TOKEN ResponseToken;

	m_NetServer.Update();

	// process packets
	while(m_NetServer.Recv(&Packet, &ResponseToken))
	{
		if(Packet.m_Flags & NETSENDFLAG_CONNLESS)
		{
			if(ResponseToken == NET_TOKEN_NONE && m_Register.OnPacket(&Packet))
				continue;
			if(Packet.m_DataSize >= int(sizeof(SERVERBROWSE_GETINFO)) &&
				mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
			{
				CUnpacker Unpacker;
				Unpacker.Reset((unsigned char *) Packet.m_pData + sizeof(SERVERBROWSE_GETINFO), Packet.m_DataSize - sizeof(SERVERBROWSE_GETINFO));
				int SrvBrwsToken = Unpacker.GetInt();
				int InfoVersion = Unpacker.GetIntOrDefault(SERVERINFO_VERSION_LEGACY);
				if(Unpacker.Error())
					continue;

				CPacker Packer;
				Packer.Reset();
				Packer.AddRaw(SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO));
				Packer.AddInt(SrvBrwsToken);
				GenerateServerInfo(&Packer, InfoVersion, true);

				CNetChunk Response;
				Response.m_ClientID = -1;
				Response.m_Address = Packet.m_Address;
				Response.m_Flags = NETSENDFLAG_CONNLESS;
				Response.m_pData = Packer.Data();
				Response.m_DataSize = Packer.Size();
				m_NetServer.Send(&Response, ResponseToken);

				if(InfoVersion == SERVERINFO_VERSION_LEGACY)
					continue;

				int Next = 0;
				while(Next != -1)
				{
					Packer.Reset();
					Packer.AddRaw(SERVERBROWSE_PLAYERSINFO, sizeof(SERVERBROWSE_PLAYERSINFO));
					Packer.AddInt(SrvBrwsToken);
					Next = GenerateServerInfoPlayers(&Packer, InfoVersion, Next);

					Response.m_ClientID = -1;
					Response.m_Address = Packet.m_Address;
					Response.m_Flags = NETSENDFLAG_CONNLESS;
					Response.m_pData = Packer.Data();
					Response.m_DataSize = Packer.Size();
					m_NetServer.Send(&Response, ResponseToken);
				}
			}
		}
		else
			ProcessClientPacket(&Packet);
	}

	m_ServerBan.Update();
	m_Econ.Update();
}

const char *CServer::GetMapName() const
{
	if(m_pMultiWorlds && m_pMultiWorlds->IsValid(m_pConfig->m_SvShowWorldWhenConnect))
		return m_pMultiWorlds->GetWorld(m_pConfig->m_SvShowWorldWhenConnect)->GetPath();

	if(!m_pConfig)
		return "";
	const char *pMapShortName = m_pConfig->m_SvMap;
	for(int i = 0; m_pConfig->m_SvMap[i] && m_pConfig->m_SvMap[i + 1]; i++)
	{
		if(m_pConfig->m_SvMap[i] == '/' || m_pConfig->m_SvMap[i] == '\\')
			pMapShortName = &m_pConfig->m_SvMap[i + 1];
	}
	return pMapShortName;
}

void CServer::SyncSvMapWithConnectWorld()
{
	if(!m_pMultiWorlds || !m_pMultiWorlds->IsValid(Config()->m_SvShowWorldWhenConnect))
		return;
	const char *pPath = m_pMultiWorlds->GetWorld(Config()->m_SvShowWorldWhenConnect)->GetPath();
	if(!pPath[0] || str_comp(Config()->m_SvMap, pPath) == 0)
		return;
	str_copy(Config()->m_SvMap, pPath, sizeof(Config()->m_SvMap));
	m_MapReload = str_comp(Config()->m_SvMap, m_aCurrentMap) != 0;
}

void CServer::ChangeMap(const char *pMap)
{
	str_copy(Config()->m_SvMap, pMap, sizeof(Config()->m_SvMap));
	m_MapReload = str_comp(Config()->m_SvMap, m_aCurrentMap) != 0;
}

int CServer::LoadMap(const char *pMapName)
{
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);

	// check for valid standard map
	if(!m_pMapChecker->ReadAndValidateMap(aBuf, IStorage::TYPE_ALL))
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "mapchecker", "invalid standard map");
		return 0;
	}

	if(!m_pMap->Load(aBuf))
		return 0;

	// stop recording when we change map
	if(m_DemoRecorder.IsRecording())
		m_DemoRecorder.Stop();

	// reinit snapshot ids
	if(m_pMultiWorlds)
	{
		for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
			m_aIDPools[w].TimeoutIDs();
	}
	else
		m_aIDPools[0].TimeoutIDs();

	// get the sha256 and crc of the map
	m_CurrentMapSha256 = m_pMap->Sha256();
	m_CurrentMapCrc = m_pMap->Crc();
	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(m_CurrentMapSha256, aSha256, sizeof(aSha256));
	char aBufMsg[256];
	str_format(aBufMsg, sizeof(aBufMsg), "%s sha256 is %s", aBuf, aSha256);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);
	str_format(aBufMsg, sizeof(aBufMsg), "%s crc is %08x", aBuf, m_CurrentMapCrc);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);

	str_copy(m_aCurrentMap, pMapName, sizeof(m_aCurrentMap));

	// load complete map into memory for download
	{
		IOHANDLE File = Storage()->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);
		m_CurrentMapSize = (int) io_length(File);
		if(m_pCurrentMapData)
			mem_free(m_pCurrentMapData);
		m_pCurrentMapData = (unsigned char *) mem_alloc(m_CurrentMapSize);
		io_read(File, m_pCurrentMapData, m_CurrentMapSize);
		io_close(File);
	}
	return 1;
}

void CServer::InitRegister(class IEngine *pEngine, class CConfig *pConfig, class IConsole *pConsole, TOKEN SecurityToken)
{
	m_Register.Init(pEngine, pConfig, pConsole, SecurityToken);
}

void CServer::InitInterfaces(IKernel *pKernel)
{
	m_pConfig = pKernel->RequestInterface<IConfigManager>()->Values();
	m_pConsole = pKernel->RequestInterface<IConsole>();
	m_pStorage = pKernel->RequestInterface<IStorage>();
	m_pMapChecker = pKernel->RequestInterface<IMapChecker>();

	m_pMultiWorlds = new CMultiWorlds();
	if(!m_pMultiWorlds->LoadFromJson(pKernel, m_pStorage, "maps/worlds.json"))
	{
		dbg_msg("server", "FATAL: failed to load maps/worlds.json");
	}
	m_pGameServer = GameServer(0);
	if(m_pMultiWorlds->IsValid(0))
	{
		m_pMap = m_pMultiWorlds->GetWorld(0)->MapDetail()->GetMap();
		SyncLegacyMapFromWorld(0);
	}
}

int CServer::Run()
{
	if(Config()->m_Debug)
	{
		g_UuidManager.DebugDump();
	}
	//
	m_PrintCBIndex = Console()->RegisterPrintCallback(Config()->m_ConsoleOutputLevel, SendRconLineAuthed, this);

	if(!m_pMultiWorlds || m_pMultiWorlds->GetWorldCount() == 0)
	{
		dbg_msg("server", "no worlds loaded from maps/worlds.json");
		Free();
		return -1;
	}
	SyncSvMapWithConnectWorld();
	m_MapChunksPerRequest = Config()->m_SvMapDownloadSpeed;

	// start server
	NETADDR BindAddr;
	if(Config()->m_Bindaddr[0] && net_host_lookup(Config()->m_Bindaddr, &BindAddr, NETTYPE_ALL) == 0)
	{
		// sweet!
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = Config()->m_SvPort;
	}
	else
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = Config()->m_SvPort;
	}

	if(!m_NetServer.Open(BindAddr, Config(), Console(), Kernel()->RequestInterface<IEngine>(), &m_ServerBan,
		   Config()->m_SvMaxClients, Config()->m_SvMaxClientsPerIP, NewClientCallback, DelClientCallback, this))
	{
		dbg_msg("server", "couldn't open socket. port %d might already be in use", Config()->m_SvPort);
		Free();
		return -1;
	}

	InitRegister(Kernel()->RequestInterface<IEngine>(), Config(), Console(), m_NetServer.GetGlobalToken());

	m_Econ.Init(Config(), Console(), &m_ServerBan);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "server name is '%s'", Config()->m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
		GameServer(w)->OnInit();
	
	if(m_pMultiWorlds)
	{
		for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
			GameServer(w)->OnConsoleInit();
	}

	str_format(aBuf, sizeof(aBuf), "netversion %s", GameServer(0)->NetVersion());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	if(str_comp(GameServer(0)->NetVersionHashUsed(), GameServer(0)->NetVersionHashReal()))
	{
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "WARNING: netversion hash differs");
	}
	str_format(aBuf, sizeof(aBuf), "game version %s (%d worlds)", GameServer(0)->Version(), m_pMultiWorlds->GetWorldCount());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// process pending commands
	m_pConsole->StoreCommands(false);

	// ECON cfg uses CFGFLAG_ECON; apply after the command queue is flushed at server start
	Console()->ExecuteFile("econ.cfg");

	if(m_GeneratedRconPassword)
	{
		dbg_msg("server", "+-------------------------+");
		dbg_msg("server", "| rcon password: '%s' |", Config()->m_SvRconPassword);
		dbg_msg("server", "+-------------------------+");
	}

	// start game
	{
		m_GameStartTime = time_get();

		UpdateRegisterServerInfo();
		while(m_RunServer)
		{
			// load new map
			if(m_MapReload || m_CurrentGameTick >= 0x6FFFFFFF) //	force reload to make sure the ticks stay within a valid range
			{
				m_MapReload = false;

				// load map
				if(LoadMap(Config()->m_SvMap))
				{
					bool aSpecs[MAX_CLIENTS];
					bool aReconnect[MAX_CLIENTS];
					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						aSpecs[c] = GameServerPlayer(c) && GameServerPlayer(c)->IsClientSpectator(c);
						aReconnect[c] = false;
						if(m_aClients[c].m_State <= CClient::STATE_AUTH)
							continue;
						if(GameServerPlayer(c) && GameServerPlayer(c)->IsClientBot(c))
						{
							m_aClients[c].m_State = CClient::STATE_EMPTY;
							m_aClients[c].m_aName[0] = 0;
							m_aClients[c].Reset();
							continue;
						}
						aReconnect[c] = true;
					}

					for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
						GameServer(w)->OnShutdown();

					SyncLegacyMapFromWorld(0);

					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						if(!aReconnect[c])
							continue;

						SendMap(c);
						m_aClients[c].Reset();
						m_aClients[c].m_State = aSpecs[c] ? CClient::STATE_CONNECTING_AS_SPEC : CClient::STATE_CONNECTING;
					}

					m_GameStartTime = time_get();
					m_CurrentGameTick = 0;
					for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
						GameServer(w)->OnInit();
				}
				else
				{
					str_format(aBuf, sizeof(aBuf), "failed to load map. mapname='%s'", Config()->m_SvMap);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
					str_copy(Config()->m_SvMap, m_aCurrentMap, sizeof(Config()->m_SvMap));
				}
			}

			int64 Now = time_get();
			bool NewTicks = false;
			bool ShouldSnap = false;
			while(Now > TickStartTime(m_CurrentGameTick + 1))
			{
				m_CurrentGameTick++;
				NewTicks = true;
				if((m_CurrentGameTick % 2) == 0)
					ShouldSnap = true;

				for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
				{
					if(!HasHumanInWorld(w))
						continue;

					for(int c = 0; c < MAX_CLIENTS; c++)
					{
						if(m_aClients[c].m_State == CClient::STATE_EMPTY || m_aClients[c].m_WorldID != w)
							continue;
						for(int i = 0; i < 200; i++)
						{
							if(m_aClients[c].m_aInputs[i].m_GameTick == Tick())
							{
								if(m_aClients[c].m_State == CClient::STATE_INGAME)
									GameServer(w)->OnClientPredictedInput(c, m_aClients[c].m_aInputs[i].m_aData);
								break;
							}
						}
					}

					GameServer(w)->OnTick();
				}
			}

			// snap game
			if(NewTicks)
			{
				if(Config()->m_SvHighBandwidth || ShouldSnap)
				{
					for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
					{
						if(HasHumanInWorld(w))
							DoSnapshot(w);
					}
				}

				UpdateClientRconCommands();

				// master server stuff
				m_Register.RegisterUpdate(m_NetServer.NetType());

				if(m_ServerInfoNeedsUpdate)
				{
					m_ServerInfoNeedsUpdate = false;
					UpdateRegisterServerInfo();
				}
			}

			PumpNetwork();

			// wait for incoming data
			m_NetServer.Wait(clamp(int((TickStartTime(m_CurrentGameTick + 1) - time_get()) * 1000 / time_freq()), 1, 1000 / SERVER_TICK_SPEED / 2));

			if(InterruptSignaled)
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "interrupted");
				break;
			}
		}
	}
	// disconnect all clients on shutdown
	m_NetServer.Close(m_aShutdownReason);
	m_Econ.Shutdown();

	if(m_pMultiWorlds)
	{
		for(int w = 0; w < m_pMultiWorlds->GetWorldCount(); w++)
			GameServer(w)->OnShutdown();
	}
	Free();

	return 0;
}

void CServer::Free()
{
	if(m_pCurrentMapData)
	{
		mem_free(m_pCurrentMapData);
		m_pCurrentMapData = 0;
	}
	delete m_pMultiWorlds;
	m_pMultiWorlds = nullptr;
	m_pMap = nullptr;
}

void CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	if(pResult->NumArguments() > 1)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pResult->GetString(1));
		((CServer *) pUser)->Kick(pResult->GetInteger(0), aBuf);
	}
	else
		((CServer *) pUser)->Kick(pResult->GetInteger(0), "Kicked by console");
}

void CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer *pThis = static_cast<CServer *>(pUser);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pThis->m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			net_addr_str(pThis->m_NetServer.ClientAddr(i), aAddrStr, sizeof(aAddrStr), true);
			if(pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
			{
				const char *pAuthStr = pThis->m_aClients[i].m_Authed == CServer::AUTHED_ADMIN ? "(Admin)" :
						       pThis->m_aClients[i].m_Authed == CServer::AUTHED_MOD   ? "(Mod)" :
														"";
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s client=%x name='%s' score=%d %s", i, aAddrStr,
					pThis->m_aClients[i].m_Version, pThis->m_aClients[i].m_aName, pThis->m_aClients[i].m_Score, pAuthStr);
			}
			else
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s connecting", i, aAddrStr);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		}
	}
}

void CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	CServer *pThis = static_cast<CServer *>(pUser);
	pThis->m_RunServer = false;
	const char *pReason = pResult->GetString(0);
	if(pReason[0])
	{
		str_copy(pThis->m_aShutdownReason, pReason, sizeof(pThis->m_aShutdownReason));
	}
}

void CServer::DemoRecorder_HandleAutoStart()
{
	if(Config()->m_SvAutoDemoRecord)
	{
		if(m_DemoRecorder.IsRecording())
			m_DemoRecorder.Stop();
		char aFilename[128];
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", "auto/autorecord", aDate);
		m_DemoRecorder.Start(aFilename, GameServer()->NetVersion(), m_aCurrentMap, m_CurrentMapSha256, m_CurrentMapCrc, "server");
		if(Config()->m_SvAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/server", "autorecord", ".demo", Config()->m_SvAutoDemoMax);
		}
	}
}

bool CServer::DemoRecorder_IsRecording()
{
	return m_DemoRecorder.IsRecording();
}

void CServer::ConRecord(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *) pUser;
	char aFilename[128];
	if(pResult->NumArguments())
		str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pResult->GetString(0));
	else
	{
		char aDate[20];
		str_timestamp(aDate, sizeof(aDate));
		str_format(aFilename, sizeof(aFilename), "demos/demo_%s.demo", aDate);
	}
	pServer->m_DemoRecorder.Start(aFilename, pServer->GameServer()->NetVersion(), pServer->m_aCurrentMap, pServer->m_CurrentMapSha256, pServer->m_CurrentMapCrc, "server");
}

void CServer::ConStopRecord(IConsole::IResult *pResult, void *pUser)
{
	((CServer *) pUser)->m_DemoRecorder.Stop();
}

void CServer::ConMapReload(IConsole::IResult *pResult, void *pUser)
{
	((CServer *) pUser)->m_MapReload = true;
}

void CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *) pUser;

	if(pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		CMsgPacker Msg(NETMSG_RCON_AUTH_OFF, true);
		pServer->SendMsg(&Msg, MSGFLAG_VITAL, pServer->m_RconClientID);

		pServer->m_aClients[pServer->m_RconClientID].m_Authed = AUTHED_NO;
		pServer->m_aClients[pServer->m_RconClientID].m_AuthTries = 0;
		pServer->m_aClients[pServer->m_RconClientID].m_pRconCmdToSend = 0;
		pServer->m_aClients[pServer->m_RconClientID].m_MapListEntryToSend = -1;
		pServer->SendRconLine(pServer->m_RconClientID, "Logout successful.");
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "ClientID=%d logged out", pServer->m_RconClientID);
		pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CServer *pSelf = (CServer *) pUserData;
	if(pResult->NumArguments())
	{
		str_clean_whitespaces(pSelf->Config()->m_SvName);
		pSelf->SendServerInfo(-1);
	}
}

void CServer::ConchainMaxclientsUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	CServer *pSelf = (CServer *) pUserData;
	if(pResult->NumArguments())
	{
		pSelf->m_NetServer.SetMaxClients(pResult->GetInteger(0));
	}
}

void CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CServer *) pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
}

void CServer::ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if(pResult->NumArguments() == 2)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		const IConsole::CCommandInfo *pInfo = pThis->Console()->GetCommandInfo(pResult->GetString(0), CFGFLAG_SERVER, false);
		int OldAccessLevel = 0;
		if(pInfo)
			OldAccessLevel = pInfo->GetAccessLevel();
		pfnCallback(pResult, pCallbackUserData);
		if(pInfo && OldAccessLevel != pInfo->GetAccessLevel())
		{
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(pThis->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY || pThis->m_aClients[i].m_Authed != CServer::AUTHED_MOD ||
					(pThis->m_aClients[i].m_pRconCmdToSend && str_comp(pResult->GetString(0), pThis->m_aClients[i].m_pRconCmdToSend->m_pName) >= 0))
					continue;

				if(OldAccessLevel == IConsole::ACCESS_LEVEL_ADMIN)
					pThis->SendRconCmdAdd(pInfo, i);
				else
					pThis->SendRconCmdRem(pInfo, i);
			}
		}
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->Console()->SetPrintOutputLevel(pThis->m_PrintCBIndex, pResult->GetInteger(0));
	}
}

void CServer::ConchainRconPasswordSet(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() >= 1)
	{
		static_cast<CServer *>(pUserData)->m_RconPasswordSet = 1;
	}
}

void CServer::ConchainMapUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() >= 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->m_MapReload = str_comp(pThis->Config()->m_SvMap, pThis->m_aCurrentMap) != 0;
	}
}

void CServer::RegisterCommands()
{
	// register console commands
	Console()->Register("kick", "i[id] ?r[reason]", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "", CFGFLAG_SERVER, ConStatus, this, "List players");
	Console()->Register("shutdown", "?r[reason]", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER | CFGFLAG_BASICACCESS, ConLogout, this, "Logout of rcon");

	Console()->Register("record", "?s[file]", CFGFLAG_SERVER | CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients", ConchainMaxclientsUpdate, this);
	Console()->Chain("sv_max_clients", ConchainSpecialInfoupdate, this);
	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);
	Console()->Chain("console_output_level", ConchainConsoleOutputLevelUpdate, this);
	Console()->Chain("sv_rcon_password", ConchainRconPasswordSet, this);
	Console()->Chain("sv_map", ConchainMapUpdate, this);

	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
	m_DemoRecorder.Init(Console(), Storage());
}

int CServer::SnapNewID(int WorldID)
{
	if(WorldID < 0 || WorldID >= ENGINE_MAX_WORLDS)
		WorldID = 0;
	return m_aIDPools[WorldID].NewID();
}

void CServer::SnapFreeID(int ID, int WorldID)
{
	if(WorldID < 0 || WorldID >= ENGINE_MAX_WORLDS)
		WorldID = 0;
	m_aIDPools[WorldID].FreeID(ID);
}

void *CServer::SnapNewItem(int Type, int ID, int Size)
{
	// dbg_assert(Type >= 0 && Type <=0xffff, "incorrect type");
	dbg_assert(ID >= 0 && ID <= 0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

static CServer *CreateServer() { return new CServer(); }

void HandleSigIntTerm(int Param)
{
	InterruptSignaled = 1;

	// Exit the next time a signal is received
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIG_DFL);
}

int main(int argc, const char **argv)
{
	cmdline_fix(&argc, &argv);
#if defined(CONF_FAMILY_WINDOWS)
	for(int i = 1; i < argc; i++)
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0)
		{
			dbg_console_hide();
			break;
		}
	}
#endif

	bool UseDefaultConfig = false;
	for(int i = 1; i < argc; i++)
	{
		if(str_comp("-d", argv[i]) == 0 || str_comp("--default", argv[i]) == 0)
		{
			UseDefaultConfig = true;
			break;
		}
	}

	if(secure_random_init() != 0)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return -1;
	}

	CCurlInit Curl;
	if(Curl.IsFailed())
		return -1;

	HttpThreadInit();

	signal(SIGINT, HandleSigIntTerm);
	signal(SIGTERM, HandleSigIntTerm);

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	int FlagMask = CFGFLAG_SERVER | CFGFLAG_ECON;
	IEngine *pEngine = CreateEngine("Teeworlds_Server");
	IMapChecker *pMapChecker = CreateMapChecker();
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER | CFGFLAG_ECON);
	// IEngineMasterServer *pEngineMasterServer = CreateEngineMasterServer();
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_SERVER, argc, argv);
	IConfigManager *pConfigManager = CreateConfigManager();

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer); // register as both
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pMapChecker);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfigManager);
		// RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IEngineMasterServer *>(pEngineMasterServer)); // register as both
		// RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMasterServer *>(pEngineMasterServer));

		if(RegisterFail)
			return -1;
	}

	pEngine->Init();
	pConfigManager->Init(FlagMask);
	pConsole->Init();
	// pEngineMasterServer->Init();
	// pEngineMasterServer->Load();

	pServer->InitInterfaces(pKernel);
	if(!UseDefaultConfig)
	{
		// register all console commands
		pServer->RegisterCommands();

		// execute autoexec file
		pConsole->ExecuteFile("autoexec.cfg");

		// parse the command line arguments
		if(argc > 1)
			pConsole->ParseArguments(argc - 1, &argv[1]);
	}

	// restore empty config strings to their defaults
	pConfigManager->RestoreStrings();

	pEngine->InitLogfile();

	pServer->InitRconPasswordIfUnset();

	// run the server
	dbg_msg("server", "starting...");
	int Ret = pServer->Run();

	// free (~CRegister may still issue HTTP delete requests)
	delete pServer;
	HttpThreadShutdown();
	delete pEngine;
	delete pKernel;
	delete pMapChecker;
	delete pConsole;
	// delete pEngineMasterServer;
	delete pStorage;
	delete pConfigManager;

	secure_random_uninit();
	cmdline_free(argc, argv);
	return Ret;
}
