/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <base/tl/array.h>
#include <base/tl/sorted_array.h>

#include <engine/console.h>
#include <engine/server.h>
#include <engine/shared/world_detail.h>

#include <game/commands.h>
#include <game/layers.h>
#include <game/voting.h>

#include "core/components/vote/vote_menu_types.h"
#include "gameworld.h"
#include "item_system.h"
class CAccountSystem;
class TWorldController;

class CBotEngine;

/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/
class CGameContext : public IGameServer
{
	IServer *m_pServer;
	class CConfig *m_pConfig;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;
	CLayers m_Layers;
	CCollision m_Collision;
	CNetObjHandler m_NetObjHandler;
	CTuningParams m_Tuning;

	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTunes(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainSettingUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	static void NewCommandHook(const CCommandManager::CCommand *pCommand, void *pContext);
	static void RemoveCommandHook(const CCommandManager::CCommand *pCommand, void *pContext);

	CGameContext(int Resetting);
	void Construct(int Resetting);

	bool m_Resetting;
	int m_WorldID;

public:
	IServer *Server() const { return m_pServer; }
	IKernel *GetKernel() { return Kernel(); }
	class CConfig *Config() { return m_pConfig; }
	class CConfig *Config() const { return m_pConfig; }
	class IConsole *Console() { return m_pConsole; }
	class IStorage *Storage() { return m_pStorage; }
	CCollision *Collision() { return &m_Collision; }
	CTuningParams *Tuning() { return &m_Tuning; }

	CGameContext();
	~CGameContext();

	void Clear();

	class CPlayer *m_apPlayers[MAX_CLIENTS];

	class CGameController *m_pController;
	CGameWorld m_World;
	CCommandManager m_CommandManager;
	TWorldController *m_pTWorld;
	CItemHelper *m_pItemHelper;
	class CBotEngine *m_pBotEngine;

	// MRPG-style broadcast queuing (see Teeworlds-MRPG-0.6 BroadcastPriority)
	enum BroadcastPriority
	{
		BROADCAST_PRIORITY_LOWER = 0,
		BROADCAST_PRIORITY_GAME_BASIC_STATS,
		BROADCAST_PRIORITY_GAME_INFORMATION,
		BROADCAST_PRIORITY_GAME_PRIORITY,
		BROADCAST_PRIORITY_GAME_WARNING,
		BROADCAST_PRIORITY_GAME_HIGHLIGHT,
		BROADCAST_PRIORITY_GAME_ALERT,
		BROADCAST_PRIORITY_OVERLAY_HIDDEN, // MRPG HiddenBroadcast
		BROADCAST_PRIORITY_MAIN_INFORMATION,
		BROADCAST_PRIORITY_TITLE,
		BROADCAST_PRIORITY_VERY_IMPORTANT,

		// Legacy aliases
		BROADCAST_PRIORITY_LOW = BROADCAST_PRIORITY_LOWER,
		BROADCAST_PRIORITY_NORMAL = BROADCAST_PRIORITY_GAME_INFORMATION,
		BROADCAST_PRIORITY_HIGH = BROADCAST_PRIORITY_GAME_PRIORITY,
		BROADCAST_PRIORITY_CRITICAL = BROADCAST_PRIORITY_VERY_IMPORTANT,
		BROADCAST_PRIORITY_UI = BROADCAST_PRIORITY_MAIN_INFORMATION,
	};

	struct SBroadcastState
	{
		// One-shot message — replaced every tick
		char m_aNextMessage[1024];
		BroadcastPriority m_NextPriority;

		// Timed message — persists until lifespan expires
		char m_aTimedMessage[1024];
		BroadcastPriority m_TimedPriority;
		int m_LifeSpanTick;

		// Previous/final message for dedup & stats integration
		char m_aPrevMessage[1024];
		char m_aCompleteMsg[1024];
		int m_NoChangeUntil;
		bool m_Updated;

		SBroadcastState() :
			m_NextPriority(BROADCAST_PRIORITY_LOWER),
			m_TimedPriority(BROADCAST_PRIORITY_LOWER),
			m_LifeSpanTick(0),
			m_NoChangeUntil(0),
			m_Updated(false)
		{
			m_aNextMessage[0] = '\0';
			m_aTimedMessage[0] = '\0';
			m_aPrevMessage[0] = '\0';
			m_aCompleteMsg[0] = '\0';
		}
	};
	SBroadcastState m_aBroadcastStates[MAX_CLIENTS];

	// Legacy zombie display slot mapping (global, for zombies only)
	int m_aLegacyDisplaySlot[MAX_CLIENTS];
	int m_aLegacyDisplayOwner[MAX_HUMAN_CLIENTS];

	// MRPG-style per-client ID map is stored in Server()->GetIdMap(ClientID),
	// populated by CGameWorld::UpdatePlayerMaps().

	CCommandManager *CommandManager() { return &m_CommandManager; }
	CAccountSystem *Accounts();
	const CAccountSystem *Accounts() const;
	TWorldController *Core() const { return m_pTWorld; }
	TWorldController *TW() { return m_pTWorld; }
	CItemHelper *ItemHelper() { return m_pItemHelper; }
	const CItemHelper *ItemHelper() const { return m_pItemHelper; }
	CBotEngine *BotEngine() { return m_pBotEngine; }

	// helper functions
	class CCharacter *GetPlayerChar(int ClientID);

	int m_LockTeams;

	// voting
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason);
	void EndVote(int Type, bool Force);
	void AbortVoteOnDisconnect(int ClientID);
	void AbortVoteOnTeamChange(int ClientID);

	int m_VoteCreator;
	int m_VoteType;
	int64 m_VoteCloseTime;
	int64 m_VoteCancelTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_VoteClientID;
	int m_NumVoteOptions;
	int m_VoteEnforce;
	enum
	{
		VOTE_TIME = 25,
		VOTE_CANCEL_TIME = 10,

		MIN_SKINCHANGE_CLIENTVERSION = 0x0703,
		MIN_RACE_CLIENTVERSION = 0x0704,
	};
	class CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	SPlayerVote *GetPlayerVote(int ClientID);

	void AddVote(const char *pDesc, const char *pCmd, int ClientID);
	void AddVote_ListInventory(int ItemType, const char *pCmdPrefix, bool Equip = false);
	void AddVote_ListCraft(int ItemType);
	void AddVote_ListFormula(int ItemID);
	void AddVote_Craft(int ItemID);
	void AddVote_Back();
	void AddVote_Space(int Num = 1);
	void AddVote_Goto(int Page, const char *pDesc);
	void AddVote_TextLine(const char *pText);
	void SetVoteLastPage(int Page);
	void SetVoteBuildClientID(int CID);

	void InitVotes(int ClientID);
	void ClearVotes(int ClientID);
	void CountItemNum(int ClientID);
	bool TryHandleVoteMenuOption(int ClientID, const char *pDescription, int ReasonNumber, const char *pReason);
	void ProcessVoteMenuCommand(int ClientID, const char *pCmdLine, int ReasonNumber, const char *pReason);

	// localization (server_lang via index.json)
	const char *LangOf(int ClientID) const;
	const char *Loc(int ClientID, const char *pKey, const char *pDefault) const;
	void LocFormat(char *pBuf, int BufSize, int ClientID, const char *pKey, const char *pDefault, ...) const;
	const char *LocItemName(int ClientID, int ID, bool IncludeZero = true) const;
	const char *LocItemDesc(int ClientID, int ID) const;
	int ResolveItemId(int ClientID, const char *pToken) const;

	// ----- send functions -----
	void SendChat(int ChatterClientID, int Mode, int To, const char *pText);
	void SendChatTo(int ToClientID, const char *pText);
	void SendChatLoc(int ToClientID, const char *pKey, const char *pDefault);
	void SendChatLocF(int ToClientID, const char *pKey, const char *pDefault, ...);
	void SendCommunityInfo(int ToClientID);
	int TdQQGroup() const;
	int TdQQSponsor() const;
	bool RequiresLoginToPlay(const class CPlayer *pPlayer) const;
	void EnforceSpectatorUntilLogin(class CPlayer *pPlayer);
	void EnterGame(int ClientID);
	void SendChatAllLoc(const char *pKey, const char *pDefault);
	void SendChatAllLocF(const char *pKey, const char *pDefault, ...);
	void SendBroadcast(int ClientID, const char *pText);
	void SendBroadcastLoc(int ClientID, const char *pKey, const char *pDefault);
	void SendBroadcastLocF(int ClientID, const char *pKey, const char *pDefault, ...);
	void AddBroadcast(int ClientID, const char *pText, BroadcastPriority Priority = BROADCAST_PRIORITY_NORMAL, int LifeSpan = 0);
	void MarkUpdatedBroadcast(int ClientID);
	void FlushBroadcastStats(int ClientID);
	void BroadcastTick(int ClientID);

	// Varargs convenience wrappers (MRPG-style): format + localize + queue
	template<typename... Ts>
	void BroadcastMsg(int ClientID, BroadcastPriority Priority, int LifeSpan, const char *pText, const Ts&... args)
	{
		if(ClientID >= 0)
		{
			if(m_apPlayers[ClientID])
			{
				char aBuf[1024];
				str_format(aBuf, sizeof(aBuf), pText, args...);
				AddBroadcast(ClientID, aBuf, Priority, LifeSpan);
			}
			return;
		}
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(m_apPlayers[i])
			{
				char aBuf[1024];
				str_format(aBuf, sizeof(aBuf), pText, args...);
				AddBroadcast(i, aBuf, Priority, LifeSpan);
			}
		}
	}

	template<typename... Ts>
	void BroadcastWorldMsg(int WorldID, BroadcastPriority Priority, int LifeSpan, const char *pText, const Ts&... args)
	{
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(!m_apPlayers[i] || !Server()->ClientIngame(i))
				continue;
			if(Server()->GetClientWorldID(i) != WorldID)
				continue;
			char aBuf[1024];
			str_format(aBuf, sizeof(aBuf), pText, args...);
			AddBroadcast(i, aBuf, Priority, LifeSpan);
		}
	}

	// MRPG-compatible names
	template<typename... Ts>
	void Broadcast(int ClientID, BroadcastPriority Priority, int LifeSpan, const char *pText, const Ts&... args)
	{
		BroadcastMsg(ClientID, Priority, LifeSpan, pText, args...);
	}

	template<typename... Ts>
	void BroadcastWorld(int WorldID, BroadcastPriority Priority, int LifeSpan, const char *pText, const Ts&... args)
	{
		BroadcastWorldMsg(WorldID, Priority, LifeSpan, pText, args...);
	}
	void SendEmoticon(int ClientID, int Emoticon);
	void SendWeaponPickup(int ClientID, int Weapon);
	void SendMotd(int ClientID);
	void SendSettings(int ClientID);
	void SendSkinChange(int ClientID, int TargetID);
	void SendTuningParams(int ClientID);
	void SendReadyToEnter(CPlayer *pPlayer);

	void SendGameMsg(int GameMsgID, int ClientID);
	void SendGameMsg(int GameMsgID, int ParaI1, int ClientID);
	void SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID);

	void SendChatCommand(const CCommandManager::CCommand *pCommand, int ClientID);
	void SendChatCommands(int ClientID);
	void SendRemoveChatCommand(const CCommandManager::CCommand *pCommand, int ClientID);

	void SendForceVote(int Type, const char *pDescription, const char *pReason);
	void SendVoteSet(int Type, int ToClientID);
	void SendVoteStatus(int ClientID, int Total, int Yes, int No);
	void SendVoteClearOptions(int ClientID);
	void SendVoteOptions(int ClientID);

	//
	void SwapTeams();

	// engine events
	virtual void OnInit();
	virtual void OnConsoleInit();
	virtual void OnShutdown();

	virtual void OnTick();
	virtual void OnPreSnap();
	virtual void OnSnap(int ClientID);
	virtual void OnPostSnap();

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID);

	virtual void OnClientConnected(int ClientID, bool AsSpec) { OnClientConnected(ClientID, false, AsSpec); }
	void OnClientConnected(int ClientID, bool Dummy, bool AsSpec);
	virtual void OnBotConnected(int ClientID) override;
	void OnClientTeamChange(int ClientID);
	virtual void OnClientEnter(int ClientID);
	virtual void OnClientDrop(int ClientID, const char *pReason);
	virtual void ReleaseClientPlayer(int ClientID) override;
	virtual void OnClientPrepareChangeWorld(int ClientID) override;
	virtual void ExportChangeWorldSession(int ClientID) override;
	virtual void SetWorldID(int WorldID) override;
	virtual int GetWorldID() const override;
	bool IsWorldType(WorldType Type) const;
	void InitWorld();
	virtual void OnClientDirectInput(int ClientID, void *pInput);
	virtual void OnClientPredictedInput(int ClientID, void *pInput);

	virtual bool IsClientBot(int ClientID) const;
	virtual bool IsClientReady(int ClientID) const;
	virtual bool IsClientPlayer(int ClientID) const;
	virtual bool IsClientSpectator(int ClientID) const;

	virtual const char *GameType() const;
	virtual const char *Version() const;
	virtual const char *NetVersion() const;
	virtual const char *NetVersionHashUsed() const;
	virtual const char *NetVersionHashReal() const;

	virtual bool TimeScore() const;
	virtual void OnUpdatePlayerServerInfo(CJsonWriter *pJsonWriter, int ClientID);

	virtual void OnDaytypeChange(int NewDaytype) override;

	virtual int GetMaxPlayerSlots();

	bool ClientUsesExtendedSlots(int ClientID) const;
	bool ClientUsesDDNetLaser(int SnappingClient) const;
	int ClientSnapID(int SnappingClient, int ServerSlot) const;
	int ClientDisplaySlot(int Recipient, int ServerSlot) const;
	void RebuildLegacySlotMap(bool ForceMapUpdate = false);
	void RefreshMappedBotClientInfo(int Recipient);
	void SendClientInfo(int Recipient, int ServerSlot, bool Local, bool Silent);
	void BroadcastClientInfo(int ServerSlot, bool Silent);
};

inline int64 CmaskAll() { return -1; }
inline int64 CmaskOne(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_HUMAN_CLIENTS)
		return 0;
	return (int64)1 << ClientID;
}
inline int64 CmaskAllExceptOne(int ClientID) { return CmaskAll() ^ CmaskOne(ClientID); }
inline bool CmaskIsSet(int64 Mask, int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_HUMAN_CLIENTS)
		return false;
	return (Mask & CmaskOne(ClientID)) != 0;
}
#endif
