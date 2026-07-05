/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <base/math.h>

#include <engine/map.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/jsonwriter.h>
#include <engine/shared/memheap.h>
#include <engine/storage.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <game/version.h>
#include <generated/server_data.h>

#include "entities/character.h"
#include "entities/projectile.h"
#include "account.h"
#include "gamecontext.h"
#include "core/components/localization/localization_manager.h"
#include "core/components/vote/vote_menu_manager.h"
#include "worldmodes/defence.h"
#include "worldmodes/hub.h"
#include "worldmodes/pvp.h"
#include "worldmodes/fng.h"
#include "worldmodes/tdm.h"
#include "worldmodes/itdm.h"
#include "worldmodes/ctf.h"
#include "worldmodes/rpg.h"
#include <engine/shared/world_detail.h>
#include "core/tworld_controller.h"
#include "core/components/dialogs/dialog_manager.h"
#include "core/components/skills/skill_manager.h"
#include "core/components/mmo/mmo_manager.h"
#include "data_center.h"
#include "global_state.h"
#include "gamecontroller.h"
#include "player.h"
#include "botengine.h"

static const char *SafeNetSkin(const char *pSkin)
{
	return pSkin && pSkin[0] ? pSkin : "standard";
}

static int ZombieFirstSlot(const CConfig *pCfg)
{
	return minimum((int)MAX_HUMAN_CLIENTS, pCfg->m_SvMaxClients);
}

static bool IsZombieVoteTarget(const CPlayer *pPlayer)
{
	return pPlayer && pPlayer->IsDummy() && pPlayer->GetZomb() != ZOMB_NONE;
}

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_WorldID = INITIALIZER_WORLD_ID;
	m_pServer = 0;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LockTeams = 0;
	m_pItemHelper = nullptr;
	m_pTWorld = nullptr;
	m_pBotEngine = nullptr;
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aLegacyDisplaySlot[i] = -1;
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		m_aLegacyDisplayOwner[i] = -1;

	if(Resetting == NO_RESET)
	{
		m_pVoteOptionHeap = new CHeap();
	}
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	delete m_pBotEngine;
	m_pBotEngine = nullptr;
	delete m_pItemHelper;
	m_pItemHelper = nullptr;
	delete m_pTWorld;
	m_pTWorld = nullptr;
	if(!m_Resetting)
	{
		delete m_pVoteOptionHeap;
	}
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;
	IKernel *pKernel = Kernel();
	int WorldID = m_WorldID;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new(this) CGameContext(RESET);

	RestoreKernel(pKernel);
	m_WorldID = WorldID;

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}

class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

const char *CGameContext::LangOf(int ClientID) const
{
	if(ClientID >= 0 && ClientID < MAX_CLIENTS && m_apPlayers[ClientID])
		return m_apPlayers[ClientID]->GetLanguage();
	if(m_pTWorld && m_pTWorld->LocalizationManager())
		return m_pTWorld->LocalizationManager()->DefaultLang();
	return "zh-cn";
}

const char *CGameContext::Loc(int ClientID, const char *pKey, const char *pDefault) const
{
	if(m_pTWorld && m_pTWorld->LocalizationManager())
		return m_pTWorld->LocalizationManager()->Get(LangOf(ClientID), pKey, pDefault);
	return pDefault ? pDefault : pKey;
}

void CGameContext::LocFormat(char *pBuf, int BufSize, int ClientID, const char *pKey, const char *pDefault, ...) const
{
	const char *pFmt = Loc(ClientID, pKey, pDefault);
	va_list ap;
	va_start(ap, pDefault);
#if defined(CONF_FAMILY_WINDOWS)
	vsnprintf(pBuf, BufSize, pFmt, ap);
#else
	vsnprintf(pBuf, BufSize, pFmt, ap);
#endif
	va_end(ap);
	pBuf[BufSize - 1] = 0;
}

const char *CGameContext::LocItemName(int ClientID, int ID, bool IncludeZero) const
{
	const CItemHelper *pH = ItemHelper();
	if(!pH)
		return Loc(ClientID, "item.name.unknown", "Item");
	if(!IncludeZero && !ID)
		return Loc(ClientID, "item.name.empty", "Empty");
	if(!pH->CheckItemValid(ID))
		return Loc(ClientID, "item.name.hand", "Hand");
	if(!pH->HasItemDefinition(ID))
		return Loc(ClientID, "item.name.unknown", "Item");

	static char aKey[32];
	pH->FormatItemLocKey(ID, aKey, sizeof(aKey));
	return Loc(ClientID, aKey, pH->GetItemName(ID, IncludeZero));
}

const char *CGameContext::LocItemDesc(int ClientID, int ID) const
{
	const CItemHelper *pH = ItemHelper();
	if(!pH || !pH->CheckItemValid(ID) || !pH->HasItemDefinition(ID))
		return "";
	char aKey[40];
	str_format(aKey, sizeof(aKey), "item.id.%d.desc", ID);
	const char *pDefault = pH->GetItemDesc(ID);
	if(!pDefault || !pDefault[0])
		return "";
	return Loc(ClientID, aKey, pDefault);
}

int CGameContext::ResolveItemId(int ClientID, const char *pToken) const
{
	const CItemHelper *pH = ItemHelper();
	if(!pH || !pToken || !pToken[0])
		return -1;

	int ByName = pH->FindItemByName(pToken);
	if(ByName >= 0)
		return ByName;

	for(int i = 0; i < NUM_ITEM; i++)
	{
		if(!pH->HasItemDefinition(i))
			continue;
		if(str_comp(LocItemName(ClientID, i), pToken) == 0)
			return i;
	}

	bool AllDigits = true;
	for(const char *p = pToken; *p; p++)
	{
		if(*p < '0' || *p > '9')
		{
			AllDigits = false;
			break;
		}
	}
	if(AllDigits)
	{
		const int Try = str_toint(pToken);
		if(pH->HasItemDefinition(Try))
			return Try;
	}
	return -1;
}

// ----- send functions -----
void CGameContext::SendChat(int ChatterClientID, int Mode, int To, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
	{
		if(Mode == CHAT_TEAM)
		{
			int TeamID = m_apPlayers[ChatterClientID]->GetTeam();
			str_format(aBuf, sizeof(aBuf), "%d:%d:%d:%s: %s", Mode, TeamID, ChatterClientID, Server()->ClientName(ChatterClientID), pText);
		}
		else
			str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", Mode, ChatterClientID, Server()->ClientName(ChatterClientID), pText);
	}
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);

	const char *pModeStr;
	if(Mode == CHAT_WHISPER || ChatterClientID == -1)
		pModeStr = 0;
	else if(Mode == CHAT_TEAM)
		pModeStr = "teamchat";
	else
		pModeStr = "chat";

	if(pModeStr)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, pModeStr, aBuf);
	}

	CNetMsg_Sv_Chat Msg;
	Msg.m_Mode = Mode;
	Msg.m_ClientID = ChatterClientID;
	Msg.m_pMessage = pText;
	Msg.m_TargetID = -1;

	if(Mode == CHAT_ALL)
	{
		if(ChatterClientID < 0)
		{
			// System message: send to specific client (To) or world-scoped broadcast
			if(To < 0)
			{
				// World-scoped broadcast: only to clients in this world
				const int ThisWorld = GetWorldID();
				for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
				{
					if(!Server()->ClientIngame(i))
						continue;
					if(Server()->GetClientWorldID(i) != ThisWorld)
						continue;
					Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
				}
			}
			else
			{
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
			}
		}
		else if(ChatterClientID < VANILLA_MAX_CLIENTS)
		{
			// Player message: only to players in the same world
			const int SenderWorld = Server()->GetClientWorldID(ChatterClientID);
			for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
			{
				if(!Server()->ClientIngame(i))
					continue;
				if(Server()->GetClientWorldID(i) != SenderWorld)
					continue;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
			}
		}
		else
		{
			// NPC CID >= 64: must map per-recipient, only to same world
			const int SenderWorld = Server()->GetClientWorldID(ChatterClientID);
			for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
			{
				if(!Server()->ClientIngame(i))
					continue;
				if(Server()->GetClientWorldID(i) != SenderWorld)
					continue;
				const int DisplayID = ClientDisplaySlot(i, ChatterClientID);
				if(DisplayID < 0)
					continue;
				Msg.m_ClientID = DisplayID;
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
			}
		}
	}
	else if(Mode == CHAT_TEAM)
	{
		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NOSEND, -1);

		To = m_apPlayers[ChatterClientID]->GetTeam();

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == To)
			{
				if(ChatterClientID >= VANILLA_MAX_CLIENTS && i < MAX_HUMAN_CLIENTS)
				{
					const int DisplayID = ClientDisplaySlot(i, ChatterClientID);
					if(DisplayID < 0)
						continue;
					Msg.m_ClientID = DisplayID;
				}
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
				// Restore original CID for next iteration
				if(ChatterClientID >= VANILLA_MAX_CLIENTS)
					Msg.m_ClientID = ChatterClientID;
			}
		}
	}
	else // Mode == CHAT_WHISPER
	{
		// send to the clients
		Msg.m_TargetID = To;
		if(ChatterClientID != -1)
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ChatterClientID);
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
	}
}

void CGameContext::SendChatTo(int ToClientID, const char *pText)
{
	SendChat(-1, CHAT_ALL, ToClientID, pText);
}

void CGameContext::SendChatLoc(int ToClientID, const char *pKey, const char *pDefault)
{
	char aBuf[512];
	str_copy(aBuf, Loc(ToClientID, pKey, pDefault), sizeof(aBuf));
	SendChatTo(ToClientID, aBuf);
}

void CGameContext::SendChatLocF(int ToClientID, const char *pKey, const char *pDefault, ...)
{
	char aFmt[512];
	str_copy(aFmt, Loc(ToClientID, pKey, pDefault), sizeof(aFmt));
	char aBuf[512];
	va_list ap;
	va_start(ap, pDefault);
	vsnprintf(aBuf, sizeof(aBuf), aFmt, ap);
	va_end(ap);
	aBuf[sizeof(aBuf) - 1] = 0;
	SendChatTo(ToClientID, aBuf);
}

void CGameContext::SendCommunityInfo(int ToClientID)
{
	if(ToClientID < 0 || ToClientID >= MAX_CLIENTS || !m_apPlayers[ToClientID] || m_apPlayers[ToClientID]->IsDummy())
		return;
	if(!Server()->ClientIngame(ToClientID))
		return;

	SendChatLocF(ToClientID, "community.qq_group", "服务器交流 QQ 群：%d", TdQQGroup());
	SendChatLocF(ToClientID, "community.sponsor", "赞助模式 & 服务器请联系作者 QQ：%d", TdQQSponsor());
	SendChatLoc(ToClientID, "community.menu_hint", "按 ESC 打开投票菜单 →「社区与赞助」可再次查看");
}

int CGameContext::TdQQGroup() const
{
	return Config()->m_SvTdQQGroup;
}

int CGameContext::TdQQSponsor() const
{
	return Config()->m_SvTdQQSponsor;
}

bool CGameContext::RequiresLoginToPlay(const CPlayer *pPlayer) const
{
	return Accounts() && Accounts()->IsEnabled() && pPlayer && !pPlayer->IsDummy();
}

void CGameContext::EnforceSpectatorUntilLogin(CPlayer *pPlayer)
{
	if(!RequiresLoginToPlay(pPlayer) || pPlayer->GetAccountId() >= 0 || pPlayer->IsGuest())
		return;

	if(pPlayer->GetCharacter())
		pPlayer->KillCharacter(WEAPON_GAME);

	if(pPlayer->GetTeam() != TEAM_SPECTATORS)
		m_pController->DoTeamChange(pPlayer, TEAM_SPECTATORS, false);
}

void CGameContext::EnterGame(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return;
	CPlayer *pP = m_apPlayers[ClientID];
	if(!RequiresLoginToPlay(pP) || (pP->GetAccountId() < 0 && !pP->IsGuest()))
		return;

	if(pP->GetTeam() != TEAM_RED)
		m_pController->DoTeamChange(pP, TEAM_RED, false);
	pP->Respawn();

	if(IsWorldType(WorldType::PvP))
	{
		SendChatLoc(ClientID, "account.enter_game_pvp", "已加入 PvP 竞技场，祝你好运！");
		SendBroadcastLoc(ClientID, "account.enter_game_pvp_broadcast", "你已加入 PvP — 击败其他玩家！");
	}
	else if(IsWorldType(WorldType::RPG))
	{
		SendChatLoc(ClientID, "account.enter_game_frpg", "欢迎来到 F|RPG 世界——冒险正在等待。");
	}
	else
	{
		SendChatLoc(ClientID, "account.enter_game", "已加入防守方，祝你好运！");
		SendBroadcastLoc(ClientID, "account.enter_game_broadcast", "你已加入游戏 — 守护主塔！");
	}

	vec2 SpawnPos;
	if(Server()->ConsumeChangeWorldSpawnPos(ClientID, &SpawnPos) && pP->GetCharacter())
		pP->GetCharacter()->GetCore()->m_Pos = SpawnPos;
}

void CGameContext::SendChatAllLoc(const char *pKey, const char *pDefault)
{
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!m_apPlayers[i] || !Server()->ClientIngame(i))
			continue;
		SendChatLoc(i, pKey, pDefault);
	}
}

void CGameContext::SendChatAllLocF(const char *pKey, const char *pDefault, ...)
{
	va_list ap;
	va_start(ap, pDefault);
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!m_apPlayers[i] || !Server()->ClientIngame(i))
			continue;
		char aFmt[512];
		str_copy(aFmt, Loc(i, pKey, pDefault), sizeof(aFmt));
		char aBuf[512];
		va_list ap2;
		va_copy(ap2, ap);
		vsnprintf(aBuf, sizeof(aBuf), aFmt, ap2);
		va_end(ap2);
		aBuf[sizeof(aBuf) - 1] = 0;
		SendChatTo(i, aBuf);
	}
	va_end(ap);
}

void CGameContext::OnDaytypeChange(int NewDaytype)
{
	const char *pWorldname = Server()->GetWorldName(m_WorldID);
	switch(NewDaytype)
	{
	case IServer::NIGHT_TYPE:
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Night has fallen in '%s'!", pWorldname);
		SendChat(-1, CHAT_ALL, -1, aBuf);
		break;
	}
	case IServer::MORNING_TYPE:
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "The sun rises over '%s'!", pWorldname);
		SendChat(-1, CHAT_ALL, -1, aBuf);
		break;
	}
	default:
		break;
	}
}

/* #########################################################################
	MRPG-STYLE BROADCAST SYSTEM
	Queued + priority + lifespan, flushes once per tick per client.
######################################################################### */
void CGameContext::AddBroadcast(int ClientID, const char *pText, BroadcastPriority Priority, int LifeSpan)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	SBroadcastState &State = m_aBroadcastStates[ClientID];
	if(LifeSpan > 0)
	{
		if(Priority < State.m_TimedPriority)
			return;
		str_copy(State.m_aTimedMessage, pText, sizeof(State.m_aTimedMessage));
		State.m_TimedPriority = Priority;
		State.m_LifeSpanTick = LifeSpan;
	}
	else
	{
		if(Priority < State.m_NextPriority)
			return;
		str_copy(State.m_aNextMessage, pText, sizeof(State.m_aNextMessage));
		State.m_NextPriority = Priority;
	}
	State.m_Updated = true;
}

void CGameContext::MarkUpdatedBroadcast(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;
	m_aBroadcastStates[ClientID].m_Updated = true;
}

void CGameContext::FlushBroadcastStats(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	SBroadcastState &State = m_aBroadcastStates[ClientID];
	if(!m_apPlayers[ClientID] || !Server()->ClientIngame(ClientID))
		return;

	if(State.m_LifeSpanTick > 0 && State.m_TimedPriority > State.m_NextPriority)
	{
		str_copy(State.m_aNextMessage, State.m_aTimedMessage, sizeof(State.m_aNextMessage));
		State.m_NextPriority = State.m_TimedPriority;
	}

	if(State.m_TimedPriority < BROADCAST_PRIORITY_MAIN_INFORMATION)
	{
		if(m_apPlayers[ClientID])
		{
			m_apPlayers[ClientID]->FormatBroadcastBasicStats(
				State.m_aCompleteMsg,
				sizeof(State.m_aCompleteMsg),
				State.m_aNextMessage);
		}
		else
			str_copy(State.m_aCompleteMsg, State.m_aNextMessage, sizeof(State.m_aCompleteMsg));
	}
	else
		str_copy(State.m_aCompleteMsg, State.m_aNextMessage, sizeof(State.m_aCompleteMsg));

	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = State.m_aCompleteMsg;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

	str_copy(State.m_aPrevMessage, State.m_aNextMessage, sizeof(State.m_aPrevMessage));
	State.m_aCompleteMsg[0] = '\0';
	State.m_Updated = false;
	State.m_NoChangeUntil = Server()->Tick() + Server()->TickSpeed() * 3;
}

void CGameContext::BroadcastTick(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS)
		return;

	SBroadcastState &State = m_aBroadcastStates[ClientID];
	if(!m_apPlayers[ClientID] || !Server()->ClientIngame(ClientID))
	{
		// Not ingame — full reset
		State.m_aPrevMessage[0] = '\0';
		State.m_aNextMessage[0] = '\0';
		State.m_NextPriority = BROADCAST_PRIORITY_LOWER;
		State.m_aTimedMessage[0] = '\0';
		State.m_TimedPriority = BROADCAST_PRIORITY_LOWER;
		State.m_LifeSpanTick = 0;
		State.m_NoChangeUntil = 0;
		State.m_Updated = false;
		return;
	}

	// Timed message overrides one-shot if higher priority
	if(State.m_LifeSpanTick > 0 && State.m_TimedPriority > State.m_NextPriority)
	{
		str_copy(State.m_aNextMessage, State.m_aTimedMessage, sizeof(State.m_aNextMessage));
		State.m_NextPriority = State.m_TimedPriority;
	}

	const bool HasPendingMsg = State.m_NextPriority > BROADCAST_PRIORITY_LOWER;
	if(!State.m_Updated && !HasPendingMsg && Server()->Tick() < State.m_NoChangeUntil)
	{
		if(State.m_LifeSpanTick > 0)
		{
			State.m_LifeSpanTick--;
			if(State.m_LifeSpanTick <= 0)
			{
				State.m_aTimedMessage[0] = '\0';
				State.m_TimedPriority = BROADCAST_PRIORITY_LOWER;
			}
		}
		State.m_aNextMessage[0] = '\0';
		State.m_NextPriority = BROADCAST_PRIORITY_LOWER;
		return;
	}

	// Send only if updated/changed, or every 3 seconds to fight auto-fade (MRPG)
	if(State.m_Updated || str_comp(State.m_aPrevMessage, State.m_aNextMessage) != 0 || Server()->Tick() >= State.m_NoChangeUntil)
	{
		// Below MainInformation: merge HUD stats/weapons with the message line
		if(State.m_TimedPriority < BROADCAST_PRIORITY_MAIN_INFORMATION)
		{
			if(m_apPlayers[ClientID])
			{
				m_apPlayers[ClientID]->FormatBroadcastBasicStats(
					State.m_aCompleteMsg,
					sizeof(State.m_aCompleteMsg),
					State.m_aNextMessage);
			}
			else
				str_copy(State.m_aCompleteMsg, State.m_aNextMessage, sizeof(State.m_aCompleteMsg));
		}
		else
			str_copy(State.m_aCompleteMsg, State.m_aNextMessage, sizeof(State.m_aCompleteMsg));

		CNetMsg_Sv_Broadcast Msg;
		Msg.m_pMessage = State.m_aCompleteMsg;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

		str_copy(State.m_aPrevMessage, State.m_aNextMessage, sizeof(State.m_aPrevMessage));
		State.m_aCompleteMsg[0] = '\0';
		State.m_Updated = false;
		State.m_NoChangeUntil = Server()->Tick() + Server()->TickSpeed() * 3;
	}

	// Decrement timed message lifespan
	if(State.m_LifeSpanTick > 0)
	{
		State.m_LifeSpanTick--;
		if(State.m_LifeSpanTick <= 0)
		{
			State.m_aTimedMessage[0] = '\0';
			State.m_TimedPriority = BROADCAST_PRIORITY_LOWER;
		}
	}

	// Reset one-shot slot for next tick
	State.m_aNextMessage[0] = '\0';
	State.m_NextPriority = BROADCAST_PRIORITY_LOWER;
}

// Legacy API forwards through new system
void CGameContext::SendBroadcast(int ClientID, const char *pText)
{
	if(ClientID < 0)
	{
		// World-scoped broadcast: all players in this world
		const int ThisWorld = GetWorldID();
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(!Server()->ClientIngame(i))
				continue;
			if(Server()->GetClientWorldID(i) != ThisWorld)
				continue;
			AddBroadcast(i, pText, BROADCAST_PRIORITY_GAME_INFORMATION);
		}
		return;
	}
	AddBroadcast(ClientID, pText, BROADCAST_PRIORITY_GAME_INFORMATION);
}

void CGameContext::SendBroadcastLoc(int ClientID, const char *pKey, const char *pDefault)
{
	if(ClientID >= 0 && ClientID < MAX_HUMAN_CLIENTS)
	{
		char aBuf[512];
		str_copy(aBuf, Loc(ClientID, pKey, pDefault), sizeof(aBuf));
		SendBroadcast(ClientID, aBuf);
		return;
	}
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!m_apPlayers[i] || !Server()->ClientIngame(i))
			continue;
		char aBuf[512];
		str_copy(aBuf, Loc(i, pKey, pDefault), sizeof(aBuf));
		SendBroadcast(i, aBuf);
	}
}

void CGameContext::SendBroadcastLocF(int ClientID, const char *pKey, const char *pDefault, ...)
{
	va_list ap;
	va_start(ap, pDefault);
	if(ClientID >= 0 && ClientID < MAX_HUMAN_CLIENTS)
	{
		char aFmt[512];
		str_copy(aFmt, Loc(ClientID, pKey, pDefault), sizeof(aFmt));
		char aBuf[512];
		vsnprintf(aBuf, sizeof(aBuf), aFmt, ap);
		va_end(ap);
		aBuf[sizeof(aBuf) - 1] = 0;
		SendBroadcast(ClientID, aBuf);
		return;
	}
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!m_apPlayers[i] || !Server()->ClientIngame(i))
			continue;
		char aFmt[512];
		str_copy(aFmt, Loc(i, pKey, pDefault), sizeof(aFmt));
		char aBuf[512];
		va_list ap2;
		va_copy(ap2, ap);
		vsnprintf(aBuf, sizeof(aBuf), aFmt, ap2);
		va_end(ap2);
		aBuf[sizeof(aBuf) - 1] = 0;
		SendBroadcast(i, aBuf);
	}
	va_end(ap);
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_Emoticon = Emoticon;
	if(ClientID < VANILLA_MAX_CLIENTS)
	{
		Msg.m_ClientID = ClientID;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(!Server()->ClientIngame(i))
				continue;
			const int DisplayID = ClientDisplaySlot(i, ClientID);
			if(DisplayID < 0)
				continue;
			Msg.m_ClientID = DisplayID;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
		}
	}
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendMotd(int ClientID)
{
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = Config()->m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendSettings(int ClientID)
{
	CNetMsg_Sv_ServerSettings Msg;
	Msg.m_KickVote = Config()->m_SvVoteKick;
	Msg.m_KickMin = Config()->m_SvVoteKickMin;
	Msg.m_SpecVote = Config()->m_SvVoteSpectate;
	Msg.m_TeamLock = m_LockTeams != 0;
	Msg.m_TeamBalance = 0;
	Msg.m_PlayerSlots = GetMaxPlayerSlots();
	Msg.m_AllowSpecVoting = Config()->m_SvAllowSpecVoting;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendSkinChange(int ClientID, int TargetID)
{
	CNetMsg_Sv_SkinChange Msg;
	if(ClientID < VANILLA_MAX_CLIENTS)
	{
		Msg.m_ClientID = ClientID;
	}
	else
	{
		const int DisplayID = ClientDisplaySlot(TargetID, ClientID);
		if(DisplayID < 0)
			return;
		Msg.m_ClientID = DisplayID;
	}
	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		Msg.m_apSkinPartNames[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aaSkinPartNames[p];
		Msg.m_aUseCustomColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aUseCustomColors[p];
		Msg.m_aSkinPartColors[p] = m_apPlayers[ClientID]->m_TeeInfos.m_aSkinPartColors[p];
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH | MSGFLAG_NORECORD, TargetID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
	Msg.AddInt(GameMsgID);
	Msg.AddInt(ParaI1);
	Msg.AddInt(ParaI2);
	Msg.AddInt(ParaI3);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendChatCommand(const CCommandManager::CCommand *pCommand, int ClientID)
{
	CNetMsg_Sv_CommandInfo Msg;
	Msg.m_Name = pCommand->m_aName;
	char aHelp[128];
	if(pCommand->m_aHelpText[0])
		str_copy(aHelp, Loc(ClientID, pCommand->m_aHelpText, pCommand->m_aHelpText), sizeof(aHelp));
	else
		aHelp[0] = 0;
	Msg.m_HelpText = aHelp;
	Msg.m_ArgsFormat = pCommand->m_aArgsFormat;

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendChatCommands(int ClientID)
{
	for(int i = 0; i < CommandManager()->CommandCount(); i++)
	{
		const CCommandManager::CCommand *pCommand = CommandManager()->GetCommand(i);
		if(pCommand && !pCommand->m_VoteOnly)
			SendChatCommand(pCommand, ClientID);
	}
}

void CGameContext::SendRemoveChatCommand(const CCommandManager::CCommand *pCommand, int ClientID)
{
	CNetMsg_Sv_CommandInfoRemove Msg;
	Msg.m_Name = pCommand->m_aName;

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_CHOICE_PASS;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = VOTE_CHOICE_PASS;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq() * VOTE_TIME;
	m_VoteCancelTime = time_get() + time_freq() * VOTE_CANCEL_TIME;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(m_VoteType, -1);
	m_VoteUpdate = true;
}

void CGameContext::EndVote(int Type, bool Force)
{
	m_VoteCloseTime = 0;
	m_VoteCancelTime = 0;
	if(Force)
		m_VoteCreator = -1;
	SendVoteSet(Type, -1);
}

void CGameContext::SendForceVote(int Type, const char *pDescription, const char *pReason)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!m_apPlayers[i])
			continue;
		char aReason[256];
		if(pReason && pReason[0])
			str_copy(aReason, pReason, sizeof(aReason));
		else
			str_copy(aReason, Loc(i, "vote.no_reason", "No reason given"), sizeof(aReason));

		CNetMsg_Sv_VoteSet Msg;
		Msg.m_Type = Type;
		Msg.m_Timeout = 0;
		Msg.m_ClientID = -1;
		Msg.m_pDescription = pDescription;
		Msg.m_pReason = aReason;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::SendVoteSet(int Type, int ToClientID)
{
	auto SendTo = [&](int ClientID) {
		CNetMsg_Sv_VoteSet Msg;
		char aReason[256];
		if(m_VoteCloseTime)
		{
			Msg.m_ClientID = m_VoteCreator;
			Msg.m_Type = Type;
			Msg.m_Timeout = (m_VoteCloseTime - time_get()) / time_freq();
			Msg.m_pDescription = m_aVoteDescription;
			if(m_aVoteReason[0])
				str_copy(aReason, m_aVoteReason, sizeof(aReason));
			else
				str_copy(aReason, Loc(ClientID, "vote.no_reason", "No reason given"), sizeof(aReason));
			Msg.m_pReason = aReason;
		}
		else
		{
			Msg.m_Type = Type;
			Msg.m_Timeout = 0;
			Msg.m_ClientID = m_VoteCreator;
			Msg.m_pDescription = "";
			Msg.m_pReason = "";
		}
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
	};

	if(ToClientID < 0)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i])
				SendTo(i);
		}
	}
	else
		SendTo(ToClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes + No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteClearOptions(int ClientID)
{
	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteOptions(int ClientID)
{
	CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
	while(pCurrent)
	{
		// count options for actual packet
		int NumOptions = 0;
		for(CVoteOptionServer *p = pCurrent; p && NumOptions < MAX_VOTE_OPTION_ADD; p = p->m_pNext, ++NumOptions)
			;

		// pack and send vote list packet
		CMsgPacker Msg(NETMSGTYPE_SV_VOTEOPTIONLISTADD);
		Msg.AddInt(NumOptions);
		while(pCurrent && NumOptions--)
		{
			Msg.AddString(pCurrent->m_aDescription, VOTE_DESC_LENGTH);
			pCurrent = pCurrent->m_pNext;
		}
		Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *) &m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning) / sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendReadyToEnter(CPlayer *pPlayer)
{
	pPlayer->m_IsReadyToEnter = true;
	CNetMsg_Sv_ReadyToEnter m;
	Server()->SendPackMsg(&m, MSGFLAG_VITAL | MSGFLAG_FLUSH, pPlayer->GetCID());
}

void CGameContext::AbortVoteOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && (str_startswith(m_aVoteCommand, "kick ") || str_startswith(m_aVoteCommand, "set_team ") || (str_startswith(m_aVoteCommand, "ban ") && Server()->IsBanned(ClientID))))
		m_VoteCloseTime = -1;
}

void CGameContext::AbortVoteOnTeamChange(int ClientID)
{
	if(m_VoteCloseTime && ClientID == m_VoteClientID && str_startswith(m_aVoteCommand, "set_team "))
		m_VoteCloseTime = -1;
}

void CGameContext::OnTick()
{
	if(m_pTWorld)
		m_pTWorld->OnTick();

	m_pController->PreTick();

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	// if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!m_apPlayers[i])
			continue;
		if(!m_apPlayers[i]->IsDummy() && Server()->GetClientWorldID(i) != m_WorldID)
			continue;
		if(m_apPlayers[i]->PendingChangeWorld())
			continue;
		m_apPlayers[i]->Tick();
		m_apPlayers[i]->PostTick();
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
			EndVote(VOTE_END_ABORT, false);
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || (m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && !Config()->m_SvAllowSpecVoting) || aVoteChecked[i]) // don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i + 1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}
			}

			if(m_VoteEnforce == VOTE_CHOICE_YES || (m_VoteUpdate && Yes >= Total / 2 + 1))
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				if(m_VoteCreator != -1 && m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCallTick = 0;

				EndVote(VOTE_END_PASS, m_VoteEnforce == VOTE_CHOICE_YES);
			}
			else if(m_VoteEnforce == VOTE_CHOICE_NO || (m_VoteUpdate && No >= (Total + 1) / 2) || time_get() > m_VoteCloseTime)
				EndVote(VOTE_END_FAIL, m_VoteEnforce == VOTE_CHOICE_NO);
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

	// Do not synthesize dummy input here: zombie AI drives bots via PreTick + OnPredictedInput.

	// MRPG-style broadcast queue: flush per-client
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(!Server()->ClientIngame(i))
			continue;
		BroadcastTick(i);
	}
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	// Skip dummies — they drive their own input in Tick()
	if(!m_apPlayers[ClientID] || m_apPlayers[ClientID]->IsDummy())
		return;
	int NumFailures = m_NetObjHandler.NumObjFailures();
	if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
	{
		if(Config()->m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT failed on '%s'", m_NetObjHandler.FailedObjOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
	}
	else
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *) pInput);
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	// Skip dummies — they drive their own input in Tick()
	if(!m_apPlayers[ClientID] || m_apPlayers[ClientID]->IsDummy())
		return;
	int NumFailures = m_NetObjHandler.NumObjFailures();
	if(m_NetObjHandler.ValidateObj(NETOBJTYPE_PLAYERINPUT, pInput, sizeof(CNetObj_PlayerInput)) == -1)
	{
		if(Config()->m_Debug && NumFailures != m_NetObjHandler.NumObjFailures())
		{
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "NETOBJTYPE_PLAYERINPUT corrected on '%s'", m_NetObjHandler.FailedObjOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
	}
	else
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *) pInput);
}

void CGameContext::SetWorldID(int WorldID)
{
	m_WorldID = WorldID;
}

int CGameContext::GetWorldID() const
{
	return m_WorldID;
}

bool CGameContext::IsWorldType(WorldType Type) const
{
	const CWorldDetail *pDetail = Server()->GetWorldDetail(m_WorldID);
	return pDetail && pDetail->GetType() == Type;
}

void CGameContext::InitWorld()
{
	const CWorldDetail *pDetail = Server()->GetWorldDetail(m_WorldID);
	WorldType Type = WorldType::Defence;
	if(pDetail)
		Type = pDetail->GetType();

	switch(Type)
	{
	case WorldType::RPG:
		m_pController = new CGameControllerRPG(this);
		dbg_msg("world init", "world %d (%s) mode=frpg", m_WorldID, Server()->GetWorldName(m_WorldID));
		break;
	case WorldType::Hub:
		m_pController = new CGameControllerHub(this);
		dbg_msg("world init", "world %d (%s) mode=hub", m_WorldID, Server()->GetWorldName(m_WorldID));
		break;
	case WorldType::PvP:
	{
		const char *pMode = pDetail ? pDetail->GetGameMode() : "";
		if(pMode && pMode[0])
		{
			if(str_comp_nocase(pMode, "fng") == 0)
				m_pController = new CGameControllerFNG(this);
			else if(str_comp_nocase(pMode, "tdm") == 0)
				m_pController = new CGameControllerTDM(this);
			else if(str_comp_nocase(pMode, "itdm") == 0 || str_comp_nocase(pMode, "idm") == 0)
				m_pController = new CGameControllerITDM(this);
			else if(str_comp_nocase(pMode, "ctf") == 0)
				m_pController = new CGameControllerCTF(this);
			else
				m_pController = new CGameControllerPvP(this);
			dbg_msg("world init", "world %d (%s) mode=%s", m_WorldID, Server()->GetWorldName(m_WorldID), pMode);
		}
		else
		{
			m_pController = new CGameControllerPvP(this);
			dbg_msg("world init", "world %d (%s) mode=pvp", m_WorldID, Server()->GetWorldName(m_WorldID));
		}
		break;
	}
	default:
		m_pController = new CGameControllerDefence(this);
		dbg_msg("world init", "world %d (%s) mode=defence", m_WorldID, Server()->GetWorldName(m_WorldID));
		break;
	}
}

void CGameContext::ExportChangeWorldSession(int ClientID)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->IsDummy())
		return;
	Server()->SetChangeWorldSession(ClientID, pPlayer->GetAccountId(), &pPlayer->m_AccData, sizeof(pPlayer->m_AccData));
	Server()->SetChangeWorldWasReady(ClientID, pPlayer->m_IsReadyToEnter);
}

static void RestoreChangeWorldPlayer(CGameContext *pCtx, int ClientID, CPlayer *pPlayer)
{
	int64 AccountId = -1;
	SAccSyncData AccData;
	int AccSize = sizeof(AccData);
	const bool HadSession = pCtx->Server()->PopChangeWorldSession(ClientID, &AccountId, &AccData, &AccSize);
	const bool RestoredSession = HadSession && (AccountId >= 0 || AccountId == -2);
	const bool WasReady = pCtx->Server()->GetChangeWorldWasReady(ClientID);

	if(RestoredSession)
	{
		pPlayer->SetAccountId(AccountId);
		mem_copy(&pPlayer->m_AccData, &AccData, sizeof(AccData));
		pPlayer->m_AccData.m_aPassword[0] = 0;
		if(pPlayer->m_AccData.m_aLanguage[0])
			pPlayer->SetLanguage(pPlayer->m_AccData.m_aLanguage);
		if(AccountId == -2)
			pPlayer->SetGuest(true);
	}
	pPlayer->m_IsReadyToEnter = WasReady || RestoredSession || pPlayer->GetAccountId() >= 0;
}

static void ClientDropNotice(CGameContext *pCtx, int ClientID, const char *pReason, bool SkipSelf)
{
	CPlayer *pPlayer = pCtx->m_apPlayers[ClientID];
	if(!pPlayer)
		return;

	const bool Silent = pCtx->IsClientBot(ClientID) ||
		(pCtx->Config()->m_SvSilentSpectatorMode && pPlayer->GetTeam() == TEAM_SPECTATORS);
	const char *pDropReason = pReason && pReason[0] ? pReason : "disconnected";

	pCtx->RebuildLegacySlotMap(false);
	for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
	{
		if(SkipSelf && i == ClientID)
			continue;
		if(!pCtx->Server()->ClientIngame(i) || !pCtx->m_apPlayers[i])
			continue;

		const int DisplayID = pCtx->ClientDisplaySlot(i, ClientID);
		if(DisplayID < 0)
			continue;

		CNetMsg_Sv_ClientDrop Msg;
		Msg.m_ClientID = DisplayID;
		Msg.m_pReason = pDropReason;
		Msg.m_Silent = Silent;
		pCtx->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, i);
	}
}

static void RemovePlayerFromWorld(CGameContext *pCtx, int ClientID, const char *pDropReason, bool NotifyOthers)
{
	CPlayer *pPlayer = pCtx->m_apPlayers[ClientID];
	if(!pPlayer)
		return;

	pCtx->AbortVoteOnDisconnect(ClientID);
	if(pCtx->m_pTWorld)
		pCtx->m_pTWorld->OnResetClientData(ClientID);
	pCtx->m_pController->OnPlayerDisconnect(pPlayer);

	if(NotifyOthers && (pCtx->Server()->ClientIngame(ClientID) || pCtx->IsClientBot(ClientID)))
		ClientDropNotice(pCtx, ClientID, pDropReason, true);

	for(CGameWorld::TypeRange r = pCtx->m_World.DoTypeRange(CGameWorld::ENTTYPE_PROJECTILE); !r.empty(); r.pop_front())
	{
		CProjectile *p = static_cast<CProjectile *>(r.front());
		if(p->GetOwner() == ClientID)
			p->LoseOwner();
	}

	delete pPlayer;
	pCtx->m_apPlayers[ClientID] = nullptr;
	pCtx->m_VoteUpdate = true;
	pCtx->Server()->ExpireServerInfo();
}

void CGameContext::OnClientPrepareChangeWorld(int ClientID)
{
	const int DestWorldID = Server()->GetChangeWorldDestID(ClientID);
	const bool Leaving = DestWorldID >= 0 && m_WorldID != DestWorldID;
	const bool Entering = DestWorldID >= 0 && m_WorldID == DestWorldID;

	if(m_apPlayers[ClientID])
		RemovePlayerFromWorld(this, ClientID, "changed world", Leaving);

	if(Leaving || !Entering)
		return;

	const bool ForceSpec = Accounts() && Accounts()->IsEnabled();
	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, false, ForceSpec);
	m_apPlayers[ClientID]->m_IsReadyToEnter = false;
	GetPlayerVote(ClientID)->Reset();
	RestoreChangeWorldPlayer(this, ClientID, m_apPlayers[ClientID]);
	if(ForceSpec && m_apPlayers[ClientID]->m_IsReadyToEnter)
		m_apPlayers[ClientID]->SetTeam(TEAM_SPECTATORS, false);
}

void CGameContext::OnClientEnter(int ClientID)
{
	CPlayer *pPlayer = m_apPlayers[ClientID];
	const bool IsDummy = pPlayer->IsDummy();
	const bool ChangeWorldEnter = Server()->ConsumeChangeWorldEnter(ClientID);

	if(!IsDummy)
		SendChatCommands(ClientID);

	if(ChangeWorldEnter)
		m_pController->SendGameInfo(ClientID);
	else
		m_pController->OnPlayerConnect(pPlayer);

	m_VoteUpdate = true;

	const bool Silent = Config()->m_SvSilentSpectatorMode && pPlayer->GetTeam() == TEAM_SPECTATORS;

	if(IsDummy)
	{
		BroadcastClientInfo(ClientID, true);
	}
	else
	{
		m_World.UpdatePlayerMaps(true);
		MarkUpdatedBroadcast(ClientID);
		AddBroadcast(ClientID, "", BROADCAST_PRIORITY_GAME_BASIC_STATS, Server()->TickSpeed() * 2);

		for(int i = 0; i < MAX_HUMAN_CLIENTS; ++i)
		{
			if(i == ClientID || !m_apPlayers[i] || !Server()->ClientIngame(i))
				continue;

			SendClientInfo(i, ClientID, false, Silent);

			const bool ExistingSilent = Config()->m_SvSilentSpectatorMode && m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS;
			SendClientInfo(ClientID, i, false, ExistingSilent);
		}

		SendClientInfo(ClientID, ClientID, true, Silent);
	}

	if(Server()->DemoRecorder_IsRecording())
	{
		CNetMsg_De_ClientEnter Msg;
		Msg.m_pName = Server()->ClientName(ClientID);
		Msg.m_ClientID = ClientID;
		Msg.m_Team = pPlayer->GetTeam();
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	}

	Server()->ExpireServerInfo();

	if(IsDummy)
		return;

	if(ChangeWorldEnter)
	{
		if(pPlayer->GetAccountId() >= 0 || pPlayer->IsGuest())
			EnterGame(ClientID);
		if(SPlayerVote *pV = GetPlayerVote(ClientID))
			pV->m_Page = PAGE_MENU;
		ClearVotes(ClientID);
		SendChatLocF(ClientID, "travel.to", "Traveling to %s", Server()->GetWorldName(m_WorldID));
	}
	else
	{
		// send active vote
		if(m_VoteCloseTime)
			SendVoteSet(m_VoteType, ClientID);
	}

	// send motd
	SendMotd(ClientID);

	// send settings
	SendSettings(ClientID);
}

void CGameContext::ReleaseClientPlayer(int ClientID)
{
	if(!m_apPlayers[ClientID])
		return;
	const bool WasHuman = !m_apPlayers[ClientID]->IsDummy();
	OnClientDrop(ClientID, "released");
	if(WasHuman && m_pController && Server()->GetNumPlayersInWorld(m_WorldID) == 0)
	{
		auto *pCtrl = dynamic_cast<CGameControllerDefence *>(m_pController);
		if(pCtrl)
			pCtrl->TdPurgeZombieDummies();
	}
}

void CGameContext::OnClientConnected(int ClientID, bool Dummy, bool AsSpec)
{
	Server()->ReleaseClientInOtherWorlds(ClientID, m_WorldID);
	if(m_apPlayers[ClientID])
		return;

	bool ForceSpec = !Dummy && Accounts() && Accounts()->IsEnabled();
	bool RestoredSession = false;
	int64 AccountId = -1;
	SAccSyncData AccData;
	int AccSize = sizeof(AccData);
	if(!Dummy && Server()->PopChangeWorldSession(ClientID, &AccountId, &AccData, &AccSize) && (AccountId >= 0 || AccountId == -2))
		RestoredSession = true;

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, Dummy, AsSpec || (ForceSpec && !RestoredSession));
	m_apPlayers[ClientID]->m_IsReadyToEnter = false;
	GetPlayerVote(ClientID)->Reset();

	if(RestoredSession)
	{
		m_apPlayers[ClientID]->SetAccountId(AccountId);
		mem_copy(&m_apPlayers[ClientID]->m_AccData, &AccData, sizeof(AccData));
		m_apPlayers[ClientID]->m_AccData.m_aPassword[0] = 0;
		if(m_apPlayers[ClientID]->m_AccData.m_aLanguage[0])
			m_apPlayers[ClientID]->SetLanguage(m_apPlayers[ClientID]->m_AccData.m_aLanguage);
		if(AccountId == -2)
			m_apPlayers[ClientID]->SetGuest(true);
		m_apPlayers[ClientID]->m_IsReadyToEnter = true;
	}
	else if(!Dummy && Server()->IsClientChangingWorld(ClientID))
		m_apPlayers[ClientID]->m_IsReadyToEnter = true;

	if(!Dummy)
		m_pController->NotifyPlayerConnected(m_apPlayers[ClientID]);
}

void CGameContext::OnBotConnected(int ClientID)
{
	OnClientConnected(ClientID, true, false);
	m_apPlayers[ClientID]->m_IsReadyToEnter = true;

	m_pController->OnBotPlayerCreated(m_apPlayers[ClientID]);
	OnClientEnter(ClientID);
}

void CGameContext::OnClientTeamChange(int ClientID)
{
	if(m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS)
		AbortVoteOnTeamChange(ClientID);

	// mark client's projectile has team projectile
	for(CGameWorld::TypeRange r = m_World.DoTypeRange(CGameWorld::ENTTYPE_PROJECTILE); !r.empty(); r.pop_front())
	{
		CProjectile *p = static_cast<CProjectile *>(r.front());
		if(p->GetOwner() == ClientID)
			p->LoseOwner();
	}

	Server()->ExpireServerInfo();
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	if(!m_apPlayers[ClientID])
		return;

	if(Server()->ClientIngame(ClientID) || IsClientBot(ClientID))
	{
		if(Server()->DemoRecorder_IsRecording())
		{
			CNetMsg_De_ClientLeave Msg;
			Msg.m_ClientID = ClientID;
			Msg.m_pName = Server()->ClientName(ClientID);
			Msg.m_pReason = pReason;
			Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
		}

		ClientDropNotice(this, ClientID, pReason, false);
	}

	// Notify friends that this player is offline
	// (friend notification handled in CMMOManager::OnClientReset via TWorldController)

	RemovePlayerFromWorld(this, ClientID, nullptr, false);
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		if(Config()->m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(Config()->m_SvSpamprotection && pPlayer->m_LastChatTeamTick && pPlayer->m_LastChatTeamTick + Server()->TickSpeed() > Server()->Tick())
				return;

			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *) pRawMsg;

			// trim right and set maximum length to 128 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(!str_utf8_is_whitespace(Code))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 127)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 20 characters per second)
			if(Length == 0 || (Config()->m_SvSpamprotection && pPlayer->m_LastChatTeamTick && pPlayer->m_LastChatTeamTick + Server()->TickSpeed() * (Length / 20) > Server()->Tick()))
				return;

			pPlayer->m_LastChatTeamTick = Server()->Tick();

			auto TryChatCommand = [&](const char *pCommandStr) -> bool {
				if(!pCommandStr || !pCommandStr[0])
					return false;
				char aCommand[16];
				str_format(aCommand, sizeof(aCommand), "%.*s", str_span(pCommandStr, " "), pCommandStr);
				const CCommandManager::CCommand *pCommand = m_CommandManager.GetCommand(aCommand);
				if(!pCommand || pCommand->m_VoteOnly)
					return false;
				CommandManager()->OnCommand(pCommand->m_aName, str_skip_whitespaces_const(str_skip_to_whitespace_const(pCommandStr)), ClientID);
				return true;
			};

			if(pMsg->m_pMessage[0] == '/')
			{
				if(!TryChatCommand(pMsg->m_pMessage + 1))
					return;
			}
			else if(TryChatCommand(pMsg->m_pMessage))
			{
				// bare command name without leading slash
			}
			else if(pMsg->m_Mode != CHAT_NONE)
			{
				SendChat(ClientID, pMsg->m_Mode, pMsg->m_Target, pMsg->m_pMessage);
			}
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;

			// MRPG: inject input events + dispatch business logic separately
			if(pMsg->m_Vote == 1)
				Server()->Input()->AppendEventKeyClick(ClientID, KEY_EVENT_VOTE_YES);
			else if(pMsg->m_Vote == -1 || pMsg->m_Vote == 0)
				Server()->Input()->AppendEventKeyClick(ClientID, KEY_EVENT_VOTE_NO);

			if(pPlayer)
				pPlayer->ParseVoteOptionResult(pMsg->m_Vote);
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *) pRawMsg;
			int64 Now = Server()->Tick();

			const bool IsOptionVote = str_comp_nocase(pMsg->m_Type, "option") == 0;

			if(pMsg->m_Force)
			{
				if(!Server()->IsAuthed(ClientID))
					return;
			}
			else if(!IsOptionVote)
			{
				if((Config()->m_SvSpamprotection && ((pPlayer->m_LastVoteTryTick && pPlayer->m_LastVoteTryTick + Server()->TickSpeed() * 3 > Now) ||
									    (pPlayer->m_LastVoteCallTick && pPlayer->m_LastVoteCallTick + Server()->TickSpeed() * VOTE_COOLDOWN > Now))) ||
					(pPlayer->GetTeam() == TEAM_SPECTATORS && !Config()->m_SvAllowSpecVoting) || m_VoteCloseTime)
					return;

				pPlayer->m_LastVoteTryTick = Now;
			}

			m_VoteType = VOTE_UNKNOWN;
			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "";

			if(IsOptionVote)
			{
				// ---- If a MotdMenu is active, dispatch F3/F4 clicks to it ----
				CPlayer *pPlayer = m_apPlayers[ClientID];
				if(pPlayer && pPlayer->m_pMotdMenu)
				{
					// The MotdMenu handles input via snapshot edge detection in Tick().
					// F3/F4 vote messages are not used for menu navigation.
					return;
				}

				const int ReasonNumber = clamp(str_toint(pReason), 0, 1000000000);

				if(TryHandleVoteMenuOption(ClientID, pMsg->m_Value, ReasonNumber, pReason))
					return;

				if(Core() && Core()->VoteMenuManager())
				{
					SPlayerVote *pMenuVote = Core()->VoteMenuManager()->GetPlayerVote(ClientID);
					if(pMenuVote && pMenuVote->m_aVoteOptions.size() > 0)
						return;
				}

				if(!pMsg->m_Force)
				{
					if((Config()->m_SvSpamprotection && ((pPlayer->m_LastVoteTryTick && pPlayer->m_LastVoteTryTick + Server()->TickSpeed() * 3 > Now) ||
										    (pPlayer->m_LastVoteCallTick && pPlayer->m_LastVoteCallTick + Server()->TickSpeed() * VOTE_COOLDOWN > Now))) ||
						(pPlayer->GetTeam() == TEAM_SPECTATORS && !Config()->m_SvAllowSpecVoting) || m_VoteCloseTime)
						return;

					pPlayer->m_LastVoteTryTick = Now;
				}

				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
					{
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						char aBuf[128];
						str_format(aBuf, sizeof(aBuf),
							"'%d:%s' voted %s '%s' reason='%s' cmd='%s' force=%d",
							ClientID, Server()->ClientName(ClientID), pMsg->m_Type,
							aDesc, pReason, aCmd, pMsg->m_Force);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
						if(pMsg->m_Force)
						{
							Server()->SetRconCID(ClientID);
							Console()->ExecuteLine(aCmd);
							Server()->SetRconCID(IServer::RCON_CID_SERV);
							SendForceVote(VOTE_START_OP, aDesc, pReason);
							return;
						}
						m_VoteType = VOTE_START_OP;
						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
					return;
			}
			else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				if(!Config()->m_SvVoteKick || m_pController->GetRealPlayerNum() < Config()->m_SvVoteKickMin)
					return;

				int KickID = str_toint(pMsg->m_Value);
				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID] || KickID == ClientID || Server()->IsAuthed(KickID))
					return;
				if(IsZombieVoteTarget(m_apPlayers[KickID]))
				{
					if(!pMsg->m_Force)
						SendChatLoc(ClientID, "vote.target_zombie", "不能对僵尸发起投票。");
					return;
				}

				str_format(aDesc, sizeof(aDesc), "%2d: %s", KickID, Server()->ClientName(KickID));
				if(!Config()->m_SvVoteKickBantime)
				{
					const char *pKickReason = Loc(KickID, "vote.kick.reason", "Kicked by vote");
					str_format(aCmd, sizeof(aCmd), "kick %d %s", KickID, pKickReason);
				}
				else
				{
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
					const char *pBanReason = Loc(KickID, "vote.ban.reason", "Banned by vote");
					str_format(aCmd, sizeof(aCmd), "ban %s %d %s", aAddrStr, Config()->m_SvVoteKickBantime, pBanReason);
				}
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf),
					"'%d:%s' voted %s '%d:%s' reason='%s' cmd='%s' force=%d",
					ClientID, Server()->ClientName(ClientID), pMsg->m_Type,
					KickID, Server()->ClientName(KickID), pReason, aCmd, pMsg->m_Force);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				if(pMsg->m_Force)
				{
					Server()->SetRconCID(ClientID);
					Console()->ExecuteLine(aCmd);
					Server()->SetRconCID(IServer::RCON_CID_SERV);
					return;
				}
				m_VoteType = VOTE_START_KICK;
				m_VoteClientID = KickID;
			}
			else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
			{
				if(!Config()->m_SvVoteSpectate)
					return;

				int SpectateID = str_toint(pMsg->m_Value);
				if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS || SpectateID == ClientID)
					return;
				if(IsZombieVoteTarget(m_apPlayers[SpectateID]))
				{
					if(!pMsg->m_Force)
						SendChatLoc(ClientID, "vote.target_zombie", "不能对僵尸发起投票。");
					return;
				}

				str_format(aDesc, sizeof(aDesc), "%2d: %s", SpectateID, Server()->ClientName(SpectateID));
				str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, Config()->m_SvVoteSpectateRejoindelay);
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf),
					"'%d:%s' voted %s '%d:%s' reason='%s' cmd='%s' force=%d",
					ClientID, Server()->ClientName(ClientID), pMsg->m_Type,
					SpectateID, Server()->ClientName(SpectateID), pReason, aCmd, pMsg->m_Force);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				if(pMsg->m_Force)
				{
					Server()->SetRconCID(ClientID);
					Console()->ExecuteLine(aCmd);
					Server()->SetRconCID(IServer::RCON_CID_SERV);
					SendForceVote(VOTE_START_SPEC, aDesc, pReason);
					return;
				}
				m_VoteType = VOTE_START_SPEC;
				m_VoteClientID = SpectateID;
			}

			if(m_VoteType != VOTE_UNKNOWN)
			{
				m_VoteCreator = ClientID;
				StartVote(aDesc, aCmd, pReason);
				pPlayer->m_Vote = VOTE_CHOICE_YES;
				pPlayer->m_VotePos = m_VotePos = 1;
				pPlayer->m_LastVoteCallTick = Now;
			}
		}
		else if(MsgID == NETMSGTYPE_CL_SETTEAM)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *) pRawMsg;

			if(pPlayer->GetTeam() == pMsg->m_Team ||
				(Config()->m_SvSpamprotection && pPlayer->m_LastSetTeamTick && pPlayer->m_LastSetTeamTick + Server()->TickSpeed() * 3 > Server()->Tick()) ||
				(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams) || pPlayer->m_TeamChangeTick > Server()->Tick())
				return;

			pPlayer->m_LastSetTeamTick = Server()->Tick();

			// Switch team on given client and kill/respawn him
			if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID) && m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
			{
				if(pPlayer->GetTeam() == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
					m_VoteUpdate = true;
				pPlayer->m_TeamChangeTick = Server()->Tick() + Server()->TickSpeed() * 3;
				m_pController->DoTeamChange(pPlayer, pMsg->m_Team);
			}
			else if(RequiresLoginToPlay(pPlayer) && pPlayer->GetAccountId() < 0 && !pPlayer->IsGuest() && pMsg->m_Team == TEAM_RED)
			{
				SendChatLoc(ClientID, "login.hint", "本服务器需要 MySQL 账号 — 使用 /register 或 /login");
				SendBroadcastLoc(ClientID, "login.broadcast", "旁观者模式 — 输入 /register 用户名 密码 或 /login 用户名 密码 加入游戏");
			}
		}
		else if(MsgID == NETMSGTYPE_CL_SETSPECTATORMODE)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *) pRawMsg;

			if(Config()->m_SvSpamprotection && pPlayer->m_LastSetSpectatorModeTick && pPlayer->m_LastSetSpectatorModeTick + Server()->TickSpeed() > Server()->Tick())
				return;

			pPlayer->m_LastSetSpectatorModeTick = Server()->Tick();
			if(!pPlayer->SetSpectatorID(pMsg->m_SpecMode, pMsg->m_SpectatorID))
				SendGameMsg(GAMEMSG_SPEC_INVALID_ID, ClientID);
		}
		else if(MsgID == NETMSGTYPE_CL_EMOTICON)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *) pRawMsg;

			if(Config()->m_SvSpamprotection && pPlayer->m_LastEmoteTick && pPlayer->m_LastEmoteTick + (Server()->TickSpeed() / 2) > Server()->Tick())
				return;

			pPlayer->m_LastEmoteTick = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
			// Phase 3: emoticon key → item quick-slot use (replaces old skill-emoticon binding)
			if(Core() && Core()->GetMMOManager())
				Core()->GetMMOManager()->UseItemByEmoticon(pPlayer, pMsg->m_Emoticon);
		}
		else if(MsgID == NETMSGTYPE_CL_KILL)
		{
			if(pPlayer->m_LastKillTick && pPlayer->m_LastKillTick + Server()->TickSpeed() * 3 > Server()->Tick())
				return;

			pPlayer->m_LastKillTick = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
		else if(MsgID == NETMSGTYPE_CL_READYCHANGE)
		{
			if(pPlayer->m_LastReadyChangeTick && pPlayer->m_LastReadyChangeTick + Server()->TickSpeed() * 1 > Server()->Tick())
				return;

			pPlayer->m_LastReadyChangeTick = Server()->Tick();
			m_pController->OnPlayerReadyChange(pPlayer);
		}
		else if(MsgID == NETMSGTYPE_CL_SKINCHANGE)
		{
			if(pPlayer->m_LastChangeInfoTick && pPlayer->m_LastChangeInfoTick + Server()->TickSpeed() * 5 > Server()->Tick())
				return;

			pPlayer->m_LastChangeInfoTick = Server()->Tick();
			CNetMsg_Cl_SkinChange *pMsg = (CNetMsg_Cl_SkinChange *) pRawMsg;

			for(int p = 0; p < NUM_SKINPARTS; p++)
			{
				str_utf8_copy_num(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]), MAX_SKIN_LENGTH);
				pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			/*
			// update all clients
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(!m_apPlayers[i] || (!Server()->ClientIngame(i) && !m_apPlayers[i]->IsDummy()) || Server()->GetClientVersion(i) < MIN_SKINCHANGE_CLIENTVERSION)
					continue;

				SendSkinChange(pPlayer->GetCID(), i);
			}
			*/

			m_pController->OnPlayerInfoChange(pPlayer);
			// update all clients
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(!m_apPlayers[i] || (!Server()->ClientIngame(i) && !m_apPlayers[i]->IsDummy()) || Server()->GetClientVersion(i) < MIN_SKINCHANGE_CLIENTVERSION)
					continue;

				SendSkinChange(pPlayer->GetCID(), i);
			}
		}
		else if(MsgID == NETMSGTYPE_CL_COMMAND)
		{
			CNetMsg_Cl_Command *pMsg = (CNetMsg_Cl_Command *) pRawMsg;
			CommandManager()->OnCommand(pMsg->m_Name, pMsg->m_Arguments, ClientID);
		}
	}
	else
	{
		if(MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *) pRawMsg;
			pPlayer->m_LastChangeInfoTick = Server()->Tick();

			// set start infos
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);

			for(int p = 0; p < NUM_SKINPARTS; p++)
			{
				str_utf8_copy_num(pPlayer->m_TeeInfos.m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], sizeof(pPlayer->m_TeeInfos.m_aaSkinPartNames[p]), MAX_SKIN_LENGTH);
				pPlayer->m_TeeInfos.m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->m_TeeInfos.m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			m_pController->OnPlayerInfoChange(pPlayer);

			SendVoteClearOptions(ClientID);
			SendVoteOptions(ClientID);
			SendTuningParams(ClientID);
			SendReadyToEnter(pPlayer);
		}
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	const char *pParamName = pResult->GetString(0);

	char aBuf[256];
	if(pResult->NumArguments() == 2)
	{
		float NewValue = pResult->GetFloat(1);
		if(pSelf->Tuning()->Set(pParamName, NewValue) && pSelf->Tuning()->Get(pParamName, &NewValue))
		{
			str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
			pSelf->SendTuningParams(-1);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "No such tuning parameter: %s", pParamName);
		}
	}
	else
	{
		float Value;
		if(pSelf->Tuning()->Get(pParamName, &Value))
		{
			str_format(aBuf, sizeof(aBuf), "%s %.2f", pParamName, Value);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "No such tuning parameter: %s", pParamName);
		}
	}
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	CTuningParams TuningParams;

	if(pResult->NumArguments())
	{
		const char *pParamName = pResult->GetString(0);
		float DefaultValue = 0.0f;
		char aBuf[256];
		if(TuningParams.Get(pParamName, &DefaultValue) && pSelf->Tuning()->Set(pParamName, DefaultValue))
		{
			str_format(aBuf, sizeof(aBuf), "%s reset to %.2f", pParamName, DefaultValue);
			pSelf->SendTuningParams(-1);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "No such tuning parameter: %s", pParamName);
		}
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
	else
	{
		*pSelf->Tuning() = TuningParams;
		pSelf->SendTuningParams(-1);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
	}
}

void CGameContext::ConTunes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float Value;
		pSelf->Tuning()->Get(i, &Value);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->GetName(i), Value);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	pSelf->SendChat(-1, CHAT_ALL, -1, pResult->GetString(0));
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	pSelf->SendBroadcast(-1, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int) MAX_CLIENTS - 1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments() > 2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID] || !pSelf->m_pController->CanJoinTeam(Team, ClientID))
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick() + pSelf->Server()->TickSpeed() * Delay * 60;
	pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[ClientID], Team);
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	pSelf->SendGameMsg(GAMEMSG_TEAM_ALL, Team, -1);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i] && pSelf->m_pController->CanJoinTeam(Team, i))
			pSelf->m_pController->DoTeamChange(pSelf->m_apPlayers[i], Team, false);
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	pDescription = str_skip_whitespaces_const(pDescription);
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	for(CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst; pOption; pOption = pOption->m_pNext)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	CVoteOptionServer *pOption = (CVoteOptionServer *) pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len, alignof(CVoteOptionServer));
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len + 1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *) pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len, alignof(CVoteOptionServer));
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len + 1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	pSelf->SendVoteClearOptions(-1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *) pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = VOTE_CHOICE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = VOTE_CHOICE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *) pUserData;
		pSelf->SendMotd(-1);
	}
}

void CGameContext::ConchainSettingUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGameContext *pSelf = (CGameContext *) pUserData;
		pSelf->SendSettings(-1);
	}
}

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	Console()->Register("tune", "s[tuning] ?i[value]", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value or show current value");
	Console()->Register("tune_reset", "?s[tuning]", CFGFLAG_SERVER, ConTuneReset, this, "Reset all or one tuning variable to default");
	Console()->Register("tunes", "", CFGFLAG_SERVER, ConTunes, this, "List all tuning variables and their values");

	Console()->Register("say", "r[text]", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("broadcast", "r[text]", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("set_team", "i[id] i[team] ?i[delay]", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i[team]", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");

	Console()->Register("add_vote", "s[option] r[command]", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s[option]", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r['yes'|'no']", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");

	if(auto *pCtrl = dynamic_cast<CGameControllerDefence *>(m_pController))
		pCtrl->RegisterTeeDefenseConsoleCommands(this);

	if(m_pTWorld)
		m_pTWorld->OnConsoleInit(m_pConsole);
}

void CGameContext::NewCommandHook(const CCommandManager::CCommand *pCommand, void *pContext)
{
	CGameContext *pSelf = (CGameContext *) pContext;
	pSelf->SendChatCommand(pCommand, -1);
}

void CGameContext::RemoveCommandHook(const CCommandManager::CCommand *pCommand, void *pContext)
{
	CGameContext *pSelf = (CGameContext *) pContext;
	pSelf->SendRemoveChatCommand(pCommand, -1);
}

void CGameContext::OnInit()
{
	// init everything
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_World.SetGameServer(this);
	m_CommandManager.Init(m_pConsole, this, NewCommandHook, RemoveCommandHook);

	// HACK: only set static size for items, which were available in the first 0.7 release
	// so new items don't break the snapshot delta
	static const int OLD_NUM_NETOBJTYPES = 23;
	for(int i = 0; i < OLD_NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	IMap *pMap = Kernel()->RequestInterface<IMap>(m_WorldID);
	m_Layers.Init(Kernel(), pMap);
	m_Collision.Init(&m_Layers);
	// Initialize global data center before any world initialization
	CDataCenter::Init(Storage());
	CGlobalState::Init();

	InitWorld();

	m_pItemHelper = new CItemHelper(this);
	m_pItemHelper->LoadDefinitions(Storage());
	m_pTWorld = new TWorldController(this);
	IEngine *pEngine = Kernel()->RequestInterface<IEngine>();
	m_pTWorld->OnInit(m_pServer, m_pConsole, m_pStorage, pEngine);

	if(!Accounts() || !Accounts()->IsEnabled())
	{
		if(Config()->m_SvMysqlEnable)
		{
			dbg_msg("server", "FATAL: MySQL account system failed (sv_mysql_enable=1 but DB unreachable)");
			exit(1);
		}
		dbg_msg("server", "Running without account system (sv_mysql_enable=0, testing mode)");
	}

	m_pController->RegisterChatCommands(CommandManager());

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *) pMap->GetData(pTileMap->m_Data);
	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y * pTileMap->m_Width + x].m_Index;

			if(Index > TILE_UNHOOKABLE && Index < ENTITY_OFFSET)
			{
				vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				m_pController->OnExtraTile(Index, Pos);
			}
			if(Index >= ENTITY_OFFSET)
			{
				vec2 Pos(x * 32.0f + 16.0f, y * 32.0f + 16.0f);
				m_pController->OnEntity(Index - ENTITY_OFFSET, Pos);
			}
		}
	}

	Collision()->InitSwitchEntities([](int EntityIndex, vec2 Pos, int Flags, int Number, void *pUser) {
		CGameContext *pSelf = static_cast<CGameContext *>(pUser);
		if(pSelf && pSelf->m_pController)
			pSelf->m_pController->OnEntitySwitch(EntityIndex, Pos, Flags, Number);
	}, this);

	m_pBotEngine = new CBotEngine(this);
	m_pBotEngine->Init(Collision());

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

	Console()->Chain("sv_vote_kick", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_kick_min", ConchainSettingUpdate, this);
	Console()->Chain("sv_vote_spectate", ConchainSettingUpdate, this);
	Console()->Chain("sv_max_clients", ConchainSettingUpdate, this);
	Console()->Chain("sv_allow_spec_voting", ConchainSettingUpdate, this);
}

void CGameContext::OnShutdown()
{
	if(m_pTWorld)
		m_pTWorld->OnShutdown();
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CNetObj_De_TuneParams *pTuneParams = static_cast<CNetObj_De_TuneParams *>(Server()->SnapNewItem(NETOBJTYPE_DE_TUNEPARAMS, 0, sizeof(CNetObj_De_TuneParams)));
		if(!pTuneParams)
			return;

		mem_copy(pTuneParams->m_aTuneParams, &m_Tuning, sizeof(pTuneParams->m_aTuneParams));
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}

	// Show players from all worlds in the scoreboard.
	const int NumWorlds = Server()->GetNumWorlds();
	for(int w = 0; w < NumWorlds; w++)
	{
		if(w == m_WorldID)
			continue;
		CGameContext *pOtherCtx = static_cast<CGameContext *>(Server()->GameServer(w));
		if(!pOtherCtx)
			continue;
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			CPlayer *pOther = pOtherCtx->m_apPlayers[i];
			if(!pOther || pOther->IsDummy() || !Server()->ClientIngame(i))
				continue;
			pOther->SnapPlayerInfoOnly(ClientID, this);
		}
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_World.PostSnap();
}

bool CGameContext::IsClientBot(int ClientID) const
{
	return m_apPlayers[ClientID] && (m_apPlayers[ClientID]->IsDummy() || m_apPlayers[ClientID]->IsQuestNpc());
}

bool CGameContext::IsClientReady(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReadyToEnter;
}

bool CGameContext::IsClientPlayer(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() != TEAM_SPECTATORS;
}

bool CGameContext::IsClientSpectator(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS;
}

const char *CGameContext::GameType() const { return MOD_NAME; }
const char *CGameContext::Version() const { return GAME_VERSION; }
const char *CGameContext::NetVersion() const { return GAME_NETVERSION; }
const char *CGameContext::NetVersionHashUsed() const { return GAME_NETVERSION_HASH_FORCED; }
const char *CGameContext::NetVersionHashReal() const { return GAME_NETVERSION_HASH; }
bool CGameContext::TimeScore() const { return false; }

void CGameContext::OnUpdatePlayerServerInfo(CJsonWriter *pJsonWriter, int ClientID)
{
	if(!m_apPlayers[ClientID])
		return;

	CTeeInfos &TeeInfo = m_apPlayers[ClientID]->m_TeeInfos;

	pJsonWriter->WriteAttribute("skin");
	pJsonWriter->BeginObject();

	const char *apPartNames[NUM_SKINPARTS] = {"body", "marking", "decoration", "hands", "feet", "eyes"};

	for(int i = 0; i < NUM_SKINPARTS; ++i)
	{
		pJsonWriter->WriteAttribute(apPartNames[i]);
		pJsonWriter->BeginObject();

		pJsonWriter->WriteAttribute("name");
		pJsonWriter->WriteStrValue(TeeInfo.m_aaSkinPartNames[i]);

		if(TeeInfo.m_aUseCustomColors[i])
		{
			pJsonWriter->WriteAttribute("color");
			pJsonWriter->WriteIntValue(TeeInfo.m_aSkinPartColors[i]);
		}

		pJsonWriter->EndObject();
	}

	pJsonWriter->EndObject();

	pJsonWriter->WriteAttribute("afk");
	pJsonWriter->WriteBoolValue(false);

	pJsonWriter->WriteAttribute("team");
	pJsonWriter->WriteIntValue(m_apPlayers[ClientID]->GetTeam());
}

int CGameContext::GetMaxPlayerSlots()
{
	return minimum(Config()->m_SvMaxClients, (int)MAX_HUMAN_CLIENTS);
}

bool CGameContext::ClientUsesExtendedSlots(int ClientID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !Server()->ClientIngame(ClientID))
		return true;
	// Teeworlds Archive 0.7.6 sends CLIENT_VERSION (0x0706); DDNet 0.7 mode sends PREV_CLIENT_VERSION (0x0705) and supports 128 slots.
	return Server()->GetClientVersion(ClientID) < CLIENT_VERSION;
}

bool CGameContext::ClientUsesDDNetLaser(int SnappingClient) const
{
	if(SnappingClient == -1)
		return true;
	return ClientUsesExtendedSlots(SnappingClient);
}

int CGameContext::ClientDisplaySlot(int Recipient, int ServerSlot) const
{
	if(ServerSlot < 0 || ServerSlot >= MAX_CLIENTS)
		return -1;
	if(Recipient < 0 || ServerSlot < MAX_HUMAN_CLIENTS)
		return ServerSlot;

	int Target = ServerSlot;
	if(!Server()->Translate(Target, Recipient))
		return -1;
	return Target;
}

int CGameContext::ClientSnapID(int SnappingClient, int ServerSlot) const
{
	if(SnappingClient < 0)
		return ServerSlot;
	int Target = ServerSlot;
	if(!Server()->Translate(Target, SnappingClient))
		return -1;
	return Target;
}

void CGameContext::RefreshMappedBotClientInfo(int Recipient)
{
	if(Recipient < 0 || Recipient >= MAX_HUMAN_CLIENTS || !Server()->ClientIngame(Recipient))
		return;
	if(Server()->GetClientWorldID(Recipient) != m_WorldID)
		return;

	int *pMap = Server()->GetIdMap(Recipient);
	for(int i = MAX_HUMAN_CLIENTS; i < VANILLA_MAX_CLIENTS - 1; i++)
	{
		const int ServerSlot = pMap[i];
		if(ServerSlot >= 0 && m_apPlayers[ServerSlot])
			SendClientInfo(Recipient, ServerSlot, false, true);
	}
}

void CGameContext::RebuildLegacySlotMap(bool ForceMapUpdate)
{
	(void)ForceMapUpdate;
}

void CGameContext::SendClientInfo(int Recipient, int ServerSlot, bool Local, bool Silent)
{
	if(Recipient < 0 || Recipient >= MAX_CLIENTS || !m_apPlayers[ServerSlot])
		return;

	const int DisplayID = ClientDisplaySlot(Recipient, ServerSlot);
	if(DisplayID < 0)
		return;

	CNetMsg_Sv_ClientInfo Msg;
	Msg.m_ClientID = DisplayID;
	Msg.m_Local = Local ? 1 : 0;
	Msg.m_Team = m_apPlayers[ServerSlot]->GetTeam();
	Msg.m_pName = Server()->ClientName(ServerSlot);
	Msg.m_pClan = Server()->ClientClan(ServerSlot);
	Msg.m_Country = Server()->ClientCountry(ServerSlot);
	Msg.m_Silent = Silent;
	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		Msg.m_apSkinPartNames[p] = SafeNetSkin(m_apPlayers[ServerSlot]->m_TeeInfos.m_aaSkinPartNames[p]);
		Msg.m_aUseCustomColors[p] = m_apPlayers[ServerSlot]->m_TeeInfos.m_aUseCustomColors[p];
		Msg.m_aSkinPartColors[p] = m_apPlayers[ServerSlot]->m_TeeInfos.m_aSkinPartColors[p];
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, Recipient);
}

void CGameContext::BroadcastClientInfo(int ServerSlot, bool Silent)
{
	(void)ServerSlot;
	(void)Silent;
	m_World.UpdatePlayerMaps(true);
}

CAccountSystem *CGameContext::Accounts()
{
	if(m_WorldID == INITIALIZER_WORLD_ID)
		return m_pTWorld ? m_pTWorld->Account() : nullptr;
	CGameContext *pBase = static_cast<CGameContext *>(Server()->GameServer(INITIALIZER_WORLD_ID));
	return pBase && pBase->m_pTWorld ? pBase->m_pTWorld->Account() : nullptr;
}

const CAccountSystem *CGameContext::Accounts() const
{
	if(m_WorldID == INITIALIZER_WORLD_ID)
		return m_pTWorld ? m_pTWorld->Account() : nullptr;
	const CGameContext *pBase = static_cast<const CGameContext *>(Server()->GameServer(INITIALIZER_WORLD_ID));
	return pBase && pBase->m_pTWorld ? pBase->m_pTWorld->Account() : nullptr;
}

static CVoteMenuManager *VoteMgr(CGameContext *pCtx)
{
	return pCtx && pCtx->Core() ? pCtx->Core()->VoteMenuManager() : nullptr;
}

SPlayerVote *CGameContext::GetPlayerVote(int ClientID)
{
	CVoteMenuManager *pV = VoteMgr(this);
	return pV ? pV->GetPlayerVote(ClientID) : nullptr;
}

void CGameContext::AddVote(const char *pDesc, const char *pCmd, int ClientID)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote(pDesc, pCmd, ClientID);
}

void CGameContext::AddVote_ListInventory(int ItemType, const char *pCmdPrefix, bool Equip)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_ListInventory(ItemType, pCmdPrefix, Equip);
}

void CGameContext::AddVote_ListCraft(int ItemType)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_ListCraft(ItemType);
}

void CGameContext::AddVote_ListFormula(int ItemID)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_ListFormula(ItemID);
}

void CGameContext::AddVote_Craft(int ItemID)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_Craft(ItemID);
}

void CGameContext::AddVote_Back()
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_Back();
}

void CGameContext::AddVote_Space(int Num)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_Space(Num);
}

void CGameContext::AddVote_Goto(int Page, const char *pDesc)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_Goto(Page, pDesc);
}

void CGameContext::AddVote_TextLine(const char *pText)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->AddVote_TextLine(pText);
}

void CGameContext::SetVoteLastPage(int Page)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->SetVoteLastPage(Page);
}

void CGameContext::SetVoteBuildClientID(int CID)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->SetVoteBuildClientID(CID);
}

void CGameContext::InitVotes(int ClientID)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->InitVotes(ClientID);
}

void CGameContext::ClearVotes(int ClientID)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->ClearVotes(ClientID);
}

void CGameContext::CountItemNum(int ClientID)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->CountItemNum(ClientID);
}

bool CGameContext::TryHandleVoteMenuOption(int ClientID, const char *pDescription, int ReasonNumber, const char *pReason)
{
	CVoteMenuManager *pV = VoteMgr(this);
	return pV ? pV->TryHandleVoteMenuOption(ClientID, pDescription, ReasonNumber, pReason) : false;
}

void CGameContext::ProcessVoteMenuCommand(int ClientID, const char *pCmdLine, int ReasonNumber, const char *pReason)
{
	if(CVoteMenuManager *pV = VoteMgr(this))
		pV->ProcessVoteMenuCommand(ClientID, pCmdLine, ReasonNumber, pReason);
}

IGameServer *CreateGameServer() { return new CGameContext; }
