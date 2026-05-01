/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <base/tl/array.h>
#include <base/tl/sorted_array.h>

#include <engine/console.h>
#include <engine/server.h>

#include <game/commands.h>
#include <game/layers.h>
#include <game/voting.h>

#include "account.h"
#include "gameworld.h"
#include "item_system.h"

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

public:
	IServer *Server() const { return m_pServer; }
	class CConfig *Config() { return m_pConfig; }
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
	CAccountSystem m_Accounts;
	CItemHelper *m_pItemHelper;
	class CBotEngine *m_pBotEngine;

	CCommandManager *CommandManager() { return &m_CommandManager; }
	CAccountSystem *Accounts() { return &m_Accounts; }
	CItemHelper *ItemHelper() { return m_pItemHelper; }
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

	enum EVoteMenuPage
	{
		PAGE_MENU = 0,
		PAGE_INVENTORY,
		PAGE_CHECK_ITEM,
		PAGE_CRAFT,
		PAGE_CRAFT_SELECTED,
		PAGE_EQUIPMENT,
		PAGE_TURRET,
	};

	struct SPlayerVote
	{
		enum EVoteSelect
		{
			ITEMLIST = 0,
			ITEM,
			EQUIPMENT,
			NUM_SELECT,
		};

		struct SVoteOptions
		{
			char m_aDescription[VOTE_DESC_LENGTH];
			char m_aCommand[VOTE_CMD_LENGTH];
		};

		array<SVoteOptions> m_aVoteOptions;
		int m_LastPage;
		int m_Page;
		int m_Select[NUM_SELECT];
		bool m_Confirm;
		char m_aExtraText[VOTE_DESC_LENGTH];

		void Reset()
		{
			m_aVoteOptions.clear();
			m_LastPage = 0;
			m_Page = 0;
			m_Confirm = false;
			for(int i = 0; i < NUM_SELECT; i++)
				m_Select[i] = 0;
			m_aExtraText[0] = 0;
		}
	};

	SPlayerVote m_aPlayerVotes[MAX_CLIENTS];
	int m_VoteBuildClientID;

	SPlayerVote *GetPlayerVote(int ClientID) { return &m_aPlayerVotes[ClientID]; }

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
	void SetVoteBuildClientID(int CID) { m_VoteBuildClientID = CID; }

	void InitVotes(int ClientID);
	void ClearVotes(int ClientID);
	void CountItemNum(int ClientID);
	bool TryHandleVoteMenuOption(int ClientID, const char *pDescription);
	void ProcessVoteMenuCommand(int ClientID, const char *pCmdLine);

	// ----- send functions -----
	void SendChat(int ChatterClientID, int Mode, int To, const char *pText);
	void SendBroadcast(int ClientID, const char *pText);
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

	virtual int GetMaxPlayerSlots();
};

inline int64 CmaskAll() { return -1; }
inline int64 CmaskOne(int ClientID) { return (int64) 1 << ClientID; }
inline int64 CmaskAllExceptOne(int ClientID) { return CmaskAll() ^ CmaskOne(ClientID); }
inline bool CmaskIsSet(int64 Mask, int ClientID) { return (Mask & CmaskOne(ClientID)) != 0; }
#endif
