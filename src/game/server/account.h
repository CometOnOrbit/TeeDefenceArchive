/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ACCOUNT_H
#define GAME_SERVER_ACCOUNT_H

#include <base/system.h>

#include <engine/console.h>
#include <engine/shared/protocol.h>

#include "item_system.h"
#include "sql_pool.h"

#include <engine/shared/jobs.h>

class CGameContext;
class IEngine;
class CConfig;
class CCommandManager;

class CAccountSystem
{
	enum
	{
		MAX_ACCOUNT_JOBS = 16,
		MAX_AUTH_JOBS = 6,
		FIRST_SAVE_JOB = 6,
	};

	enum
	{
		JOB_NONE = 0,
		JOB_REGISTER,
		JOB_LOGIN,
		JOB_LOGIN_LOAD,
		JOB_SAVE_ACCOUNT,
		JOB_SAVE_ITEMS,
	};

	CSqlConnectionPool m_Pool;
	CGameContext *m_pGame;
	IEngine *m_pEngine;
	CConfig *m_pConfig;

	bool m_Enabled;
	bool m_Pumping;

	struct SJob
	{
		void *m_pSys;
		CJob m_Job;
		bool m_Submitted;
		int m_Type;
		int m_ClientId;
		int64 m_AccountId;
		SAccSyncData m_Sync;
		int m_Error;
	};

	SJob m_aJobs[MAX_ACCOUNT_JOBS];
	int m_aNextItemsSaveTick[MAX_CLIENTS];
	int m_aNextAccountSaveTick[MAX_CLIENTS];
	bool m_aPendingItemsSave[MAX_CLIENTS];
	bool m_aPendingAccountSave[MAX_CLIENTS];
	char m_aPendingAuthUser[MAX_CLIENTS][64];

	static int JobRunner(void *pData);
	static void ClearJobSlot(SJob &Slot);
	static bool JobSlotIdle(SJob &Slot);

	bool StartJob(int Type, int ClientId, const char *pUser, const char *pPass);
	bool StartLoginLoadJob(int ClientId, int64 AccountId, const SAccSyncData *pSync);
	bool StartSaveJob(int ClientId, int UserId, const SAccSyncData *pSync);
	bool StartItemsJob(int ClientId, int UserId, const SAccSyncData *pSync);
	void FlushPendingSaves();
	void ClearSaveThrottle(int ClientId);
	bool QueueItemsSave(int ClientId, bool Force);
	bool QueueAccountSave(int ClientId, bool Force);
	void PumpCompletedJobs();
	void ClearPendingAuth(int ClientId);
	bool AuthClientStillValid(int ClientId, const char *pExpectedUser) const;
	void ApplyLogin(int ClientId, int64 AccountId, const SAccSyncData *pSync);

	static void ComChatRegister(IConsole::IResult *pResult, void *pUser);
	static void ComChatLogin(IConsole::IResult *pResult, void *pUser);
	static void ComAccInfo(IConsole::IResult *pResult, void *pUser);

public:
	CAccountSystem();

	bool Init(CGameContext *pGame, IEngine *pEngine, IConsole *pConsole, CConfig *pConfig);
	void Shutdown();
	void OnGameTick();
	bool IsEnabled() const { return m_Enabled; }

	void OnClientDisconnect(int ClientId);

	/** Queues async tw_Items sync (logged-in players). */
	void RequestSaveItems(int ClientId);

	void RequestSaveAccount(int ClientId);

	void RegisterChatCommands(CCommandManager *pManager, CGameContext *pGame);
	void RegisterConsoleCommands(IConsole *pConsole, CGameContext *pGame);
};

#endif
