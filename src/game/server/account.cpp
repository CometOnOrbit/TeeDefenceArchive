/* (c) TeeDefenceArchive - 2026 */

#include "account.h"

#include "account_crypto.h"

#include <stdio.h>
#include <stdlib.h>

#include <engine/console.h>
#include <engine/engine.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/commands.h>
#include <game/server/core/components/vote/vote_menu_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

static const char *DefaultItemExtraJson()
{
	return "{\"Extra\":{\"Cards\":[],\"Parts\":[]}}";
}

static void ClearItemsInSync(SAccSyncData *pSync)
{
	for(int i = 0; i < NUM_ITEM; i++)
	{
		pSync->m_aItems[i].m_Num = 0;
		pSync->m_aItems[i].m_Capacity = 0;
		str_copy(pSync->m_aItems[i].m_aExtra, DefaultItemExtraJson(), sizeof(pSync->m_aItems[i].m_aExtra));
	}
}

static bool UsernameOk(const char *p)
{
	int l = str_length(p);
	if(l < 3 || l > 63)
		return false;
	for(const char *q = p; *q; q++)
	{
		char c = *q;
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
			continue;
		return false;
	}
	return true;
}

static bool PasswordOk(const char *p)
{
	int l = str_length(p);
	return l >= 6 && l <= 63;
}

static CGameContext *PlayerGameContext(CGameContext *pGame, int ClientId)
{
	if(!pGame || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return nullptr;
	IServer *pSrv = pGame->Server();
	return static_cast<CGameContext *>(pSrv->GameServer(pSrv->GetClientWorldID(ClientId)));
}

static CPlayer *PlayerAt(CGameContext *pGame, int ClientId)
{
	CGameContext *pCtx = PlayerGameContext(pGame, ClientId);
	if(!pCtx)
		return nullptr;
	return pCtx->m_apPlayers[ClientId];
}

static bool IsAccountOnline(CGameContext *pGame, int64 AccountId, int ExcludeClientId)
{
	if(!pGame || AccountId < 0)
		return false;

	IServer *pSrv = pGame->Server();
	const int NumWorlds = pSrv->GetNumWorlds();
	for(int w = 0; w < NumWorlds; w++)
	{
		CGameContext *pCtx = static_cast<CGameContext *>(pSrv->GameServer(w));
		if(!pCtx)
			continue;
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(i == ExcludeClientId)
				continue;
			CPlayer *pP = pCtx->m_apPlayers[i];
			if(!pP || pP->IsDummy() || !pSrv->ClientIngame(i))
				continue;
			if(pSrv->GetClientWorldID(i) != w)
				continue;
			if(pP->GetAccountId() == AccountId)
				return true;
		}
	}
	return false;
}

static bool IsUsernameLoggedInElsewhere(CGameContext *pGame, const char *pUsername, int ExcludeClientId)
{
	if(!pGame || !pUsername || !pUsername[0])
		return false;

	IServer *pSrv = pGame->Server();
	const int NumWorlds = pSrv->GetNumWorlds();
	for(int w = 0; w < NumWorlds; w++)
	{
		CGameContext *pCtx = static_cast<CGameContext *>(pSrv->GameServer(w));
		if(!pCtx)
			continue;
		for(int i = 0; i < MAX_HUMAN_CLIENTS; i++)
		{
			if(i == ExcludeClientId)
				continue;
			CPlayer *pP = pCtx->m_apPlayers[i];
			if(!pP || pP->IsDummy() || !pSrv->ClientIngame(i))
				continue;
			if(pSrv->GetClientWorldID(i) != w)
				continue;
			if(pP->GetAccountId() < 0)
				continue;
			if(str_comp_nocase(pP->m_AccData.m_aUsername, pUsername) == 0)
				return true;
		}
	}
	return false;
}

#ifdef CONF_MYSQL

#include <mysql.h>

static bool SqlExec(MYSQL *pSql, const char *pQuery)
{
	if(mysql_query(pSql, pQuery))
	{
		dbg_msg("mysql", "error: %s | %s", mysql_error(pSql), pQuery);
		return false;
	}
	return true;
}

static bool ColumnExists(MYSQL *pSql, const char *pTable, const char *pColumn)
{
	char aQuery[256];
	str_format(aQuery, sizeof(aQuery), "SHOW COLUMNS FROM `%s` LIKE '%s'", pTable, pColumn);
	if(!SqlExec(pSql, aQuery))
		return false;
	MYSQL_RES *pRes = mysql_store_result(pSql);
	const bool Exists = pRes && mysql_num_rows(pRes) > 0;
	if(pRes)
		mysql_free_result(pRes);
	return Exists;
}

static void SerializeHolding(const int *pHolding, char *pOut, int OutLen)
{
	str_format(pOut, OutLen,
		"{\"pickaxe\":%d,\"axe\":%d,\"sword\":%d,\"turret\":%d}",
		pHolding[ITYPE_PICKAXE], pHolding[ITYPE_AXE], pHolding[ITYPE_SWORD], pHolding[ITYPE_TURRET]);
}

static bool ParseHoldingJson(const char *pJson, int *pHolding)
{
	if(!pJson || !pJson[0])
		return false;
	int aVals[4] = {0};
	const int n = sscanf(pJson, "{\"pickaxe\":%d,\"axe\":%d,\"sword\":%d,\"turret\":%d}", &aVals[0], &aVals[1], &aVals[2], &aVals[3]);
	if(n != 4)
		return false;
	pHolding[ITYPE_PICKAXE] = aVals[0];
	pHolding[ITYPE_AXE] = aVals[1];
	pHolding[ITYPE_SWORD] = aVals[2];
	pHolding[ITYPE_TURRET] = aVals[3];
	return true;
}

static bool LoadHoldingFromRow(MYSQL_ROW Row, int SwordCol, int PickaxeCol, int AxeCol, int HoldingCol, int *pHolding)
{
	mem_zero(pHolding, sizeof(int) * NUM_ITYPE);
	if(HoldingCol >= 0 && Row[HoldingCol] && Row[HoldingCol][0])
	{
		if(ParseHoldingJson(Row[HoldingCol], pHolding))
			return true;
	}
	if(SwordCol >= 0 || PickaxeCol >= 0 || AxeCol >= 0)
	{
		pHolding[ITYPE_SWORD] = SwordCol >= 0 && Row[SwordCol] ? str_toint(Row[SwordCol]) : 0;
		pHolding[ITYPE_PICKAXE] = PickaxeCol >= 0 && Row[PickaxeCol] ? str_toint(Row[PickaxeCol]) : 0;
		pHolding[ITYPE_AXE] = AxeCol >= 0 && Row[AxeCol] ? str_toint(Row[AxeCol]) : 0;
		return true;
	}
	return false;
}

static bool MigrateAccountsTable(MYSQL *pSql)
{
	if(!ColumnExists(pSql, "tw_Accounts", "Holding"))
	{
		if(!SqlExec(pSql, "ALTER TABLE `tw_Accounts` ADD COLUMN `Holding` JSON DEFAULT NULL"))
			return false;
	}
	if(!SqlExec(pSql, "ALTER TABLE `tw_Accounts` MODIFY COLUMN `Password` varchar(128) NOT NULL"))
		return false;
	if(ColumnExists(pSql, "tw_Accounts", "Sword"))
	{
		if(!SqlExec(pSql,
			"UPDATE `tw_Accounts` SET `Holding`=JSON_OBJECT("
			"'pickaxe', IFNULL(`Pickaxe`,0), 'axe', IFNULL(`Axe`,0), 'sword', IFNULL(`Sword`,0), 'turret', 0) "
			"WHERE `Holding` IS NULL"))
			return false;
	}
	return true;
}

static bool EnsureSchema(MYSQL *pSql)
{
	const char *pAccounts =
		"CREATE TABLE IF NOT EXISTS `tw_Accounts` ("
		"  `UserID` int NOT NULL AUTO_INCREMENT,"
		"  `Username` varchar(64) NOT NULL,"
		"  `Password` varchar(128) NOT NULL,"
		"  `Language` varchar(64) NOT NULL DEFAULT 'zh-cn',"
		"  `Holding` JSON DEFAULT NULL,"
		"  PRIMARY KEY (`UserID`),"
		"  UNIQUE KEY `idx_username` (`Username`)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci";
	if(!SqlExec(pSql, pAccounts))
		return false;
	if(!MigrateAccountsTable(pSql))
		return false;

	const char *pItems =
		"CREATE TABLE IF NOT EXISTS `tw_Items` ("
		"  `UserID` INT NOT NULL,"
		"  `ItemID` INT NOT NULL,"
		"  `Num` INT NOT NULL,"
		"  `Extra` longtext CHARACTER SET utf8mb4 COLLATE utf8mb4_bin DEFAULT NULL CHECK (json_valid(`Extra`)),"
		"  PRIMARY KEY (`UserID`, `ItemID`)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci";
	return SqlExec(pSql, pItems);
}

static void DeleteAccountByUserId(MYSQL *pSql, int64 UserId)
{
	if(UserId <= 0)
		return;
	char aQuery[256];
	str_format(aQuery, sizeof(aQuery), "DELETE FROM tw_Items WHERE UserID=%lld", (long long)UserId);
	SqlExec(pSql, aQuery);
	str_format(aQuery, sizeof(aQuery), "DELETE FROM tw_Accounts WHERE UserID=%lld", (long long)UserId);
	SqlExec(pSql, aQuery);
}

static bool VerifyAccountExists(MYSQL *pSql, int64 UserId)
{
	char aQuery[128];
	str_format(aQuery, sizeof(aQuery), "SELECT UserID FROM tw_Accounts WHERE UserID=%lld LIMIT 1", (long long)UserId);
	if(!SqlExec(pSql, aQuery))
		return false;
	MYSQL_RES *pRes = mysql_store_result(pSql);
	const bool Exists = pRes && mysql_num_rows(pRes) > 0;
	if(pRes)
		mysql_free_result(pRes);
	return Exists;
}

static bool LoadItemsForUser(MYSQL *pSql, int UserId, SAccSyncData *pSync)
{
	ClearItemsInSync(pSync);
	char aQuery[256];
	str_format(aQuery, sizeof(aQuery), "SELECT ItemID, Num, IFNULL(Extra,'') FROM tw_Items WHERE UserID=%d", UserId);
	if(!SqlExec(pSql, aQuery))
		return false;
	MYSQL_RES *pRes = mysql_store_result(pSql);
	if(!pRes)
		return true;
	MYSQL_ROW Row;
	while((Row = mysql_fetch_row(pRes)))
	{
		if(!Row[0] || !Row[1])
			continue;
		int Id = str_toint(Row[0]);
		if(Id < 0 || Id >= NUM_ITEM)
			continue;
		pSync->m_aItems[Id].m_Num = str_toint(Row[1]);
		if(Row[2] && Row[2][0])
			str_copy(pSync->m_aItems[Id].m_aExtra, Row[2], sizeof(pSync->m_aItems[Id].m_aExtra));
		else
			str_copy(pSync->m_aItems[Id].m_aExtra, DefaultItemExtraJson(), sizeof(pSync->m_aItems[Id].m_aExtra));
	}
	mysql_free_result(pRes);
	return true;
}

static void SaveItems(MYSQL *pSql, int UserId, const SAccSyncData *pSync)
{
	for(int i = 0; i < NUM_ITEM; i++)
	{
		char aQuery[4096];
		str_format(aQuery, sizeof(aQuery), "SELECT Num FROM tw_Items WHERE UserID=%d AND ItemID=%d LIMIT 1", UserId, i);
		if(!SqlExec(pSql, aQuery))
			continue;

		char aEscExtra[2048];
		const char *pRawEx = pSync->m_aItems[i].m_aExtra[0] ? pSync->m_aItems[i].m_aExtra : DefaultItemExtraJson();
		mysql_real_escape_string(pSql, aEscExtra, pRawEx, str_length(pRawEx));

		MYSQL_RES *pRes = mysql_store_result(pSql);
		const bool Exists = pRes && mysql_num_rows(pRes) > 0;
		if(pRes)
			mysql_free_result(pRes);

		if(Exists)
		{
			if(pSync->m_aItems[i].m_Num > 0)
			{
				str_format(aQuery, sizeof(aQuery), "UPDATE tw_Items SET Num=%d, Extra='%s' WHERE UserID=%d AND ItemID=%d",
					pSync->m_aItems[i].m_Num, aEscExtra, UserId, i);
				SqlExec(pSql, aQuery);
			}
			else
			{
				str_format(aQuery, sizeof(aQuery), "DELETE FROM tw_Items WHERE UserID=%d AND ItemID=%d", UserId, i);
				SqlExec(pSql, aQuery);
			}
		}
		else if(pSync->m_aItems[i].m_Num > 0)
		{
			str_format(aQuery, sizeof(aQuery), "INSERT INTO tw_Items(UserID, ItemID, Num, Extra) VALUES (%d,%d,%d,'%s')",
				UserId, i, pSync->m_aItems[i].m_Num, aEscExtra);
			SqlExec(pSql, aQuery);
		}
	}
}

#endif

void CAccountSystem::ClearJobSlot(SJob &Slot)
{
	Slot.m_Submitted = false;
	mem_zero(&Slot.m_Job, sizeof(Slot.m_Job));
}

bool CAccountSystem::JobSlotIdle(SJob &Slot)
{
	return !Slot.m_Submitted || Slot.m_Job.Status() == CJob::STATE_DONE;
}

CAccountSystem::CAccountSystem()
{
	m_pGame = nullptr;
	m_pEngine = nullptr;
	m_pConfig = nullptr;
	m_Enabled = false;
	m_Pumping = false;
	for(auto &Slot : m_aJobs)
		mem_zero(&Slot, sizeof(Slot));
	mem_zero(m_aNextItemsSaveTick, sizeof(m_aNextItemsSaveTick));
	mem_zero(m_aNextAccountSaveTick, sizeof(m_aNextAccountSaveTick));
	mem_zero(m_aPendingItemsSave, sizeof(m_aPendingItemsSave));
	mem_zero(m_aPendingAccountSave, sizeof(m_aPendingAccountSave));
	mem_zero(m_aPendingAuthUser, sizeof(m_aPendingAuthUser));
}

bool CAccountSystem::Init(CGameContext *pGame, IEngine *pEngine, IConsole *pConsole, CConfig *pConfig)
{
	(void)pConsole;
	m_pGame = pGame;
	m_pEngine = pEngine;
	m_pConfig = pConfig;
	m_Enabled = false;

#ifndef CONF_MYSQL
	dbg_msg("acc", "FATAL: server built without MySQL — TeeDefense requires CONF_MYSQL");
	return false;
#else
	if(!pGame || !pEngine || !pConfig)
		return false;

	if(!pConfig->m_SvMysqlEnable)
	{
		dbg_msg("acc", "FATAL: sv_mysql_enable must be 1 (MySQL is required)");
		return false;
	}

	const int DbPort = pConfig->m_SvMysqlPort ? pConfig->m_SvMysqlPort : 3306;
	const int PoolSize = clamp(pConfig->m_SvMysqlPoolSize, 1, 32);
	if(!m_Pool.Init(PoolSize, pConfig->m_SvMysqlHost, DbPort, pConfig->m_SvMysqlUser, pConfig->m_SvMysqlPassword, pConfig->m_SvMysqlDatabase))
	{
		dbg_msg("acc", "MySQL pool init failed");
		return false;
	}

	void *pRaw = m_Pool.Acquire();
	if(!pRaw)
	{
		dbg_msg("acc", "no connection for schema setup");
		m_Pool.Shutdown();
		return false;
	}
	MYSQL *pSql = (MYSQL *)pRaw;
	if(!EnsureSchema(pSql))
	{
		m_Pool.Release(pRaw);
		m_Pool.Shutdown();
		return false;
	}
	m_Pool.Release(pRaw);

	secure_random_init();
	m_Enabled = true;
	dbg_msg("acc", "MySQL account system ready (pool=%d, TeeDef tw_*)", PoolSize);
	return true;
#endif
}

void CAccountSystem::Shutdown()
{
#ifdef CONF_MYSQL
	for(int Wait = 0; Wait < 10000; Wait++)
	{
		bool Busy = false;
		for(auto &Slot : m_aJobs)
		{
			if(Slot.m_Submitted && Slot.m_Job.Status() != CJob::STATE_DONE)
				Busy = true;
		}
		if(!Busy)
			break;
		thread_sleep(1);
	}
	m_Pool.Shutdown();
#endif
	m_Enabled = false;
	m_pEngine = nullptr;
	m_pGame = nullptr;
	m_pConfig = nullptr;
	for(auto &Slot : m_aJobs)
		mem_zero(&Slot, sizeof(Slot));
}

bool CAccountSystem::StartJob(int Type, int ClientId, const char *pUser, const char *pPass)
{
	if(!m_Enabled || !m_pEngine)
		return false;

	PumpCompletedJobs();

	for(int i = 0; i < MAX_AUTH_JOBS; i++)
	{
		SJob &Slot = m_aJobs[i];
		if(!JobSlotIdle(Slot))
			continue;

		mem_zero(&Slot, sizeof(Slot));
		Slot.m_pSys = this;
		Slot.m_Submitted = true;
		Slot.m_Type = Type;
		Slot.m_ClientId = ClientId;
		Slot.m_AccountId = 0;
		str_copy(Slot.m_Sync.m_aUsername, pUser, sizeof(Slot.m_Sync.m_aUsername));
		str_copy(Slot.m_Sync.m_aPassword, pPass, sizeof(Slot.m_Sync.m_aPassword));
		str_copy(Slot.m_Sync.m_aLanguage, "zh-cn", sizeof(Slot.m_Sync.m_aLanguage));
		mem_zero(Slot.m_Sync.m_Holding, sizeof(Slot.m_Sync.m_Holding));
		mem_zero(Slot.m_Sync.m_ItemCount, sizeof(Slot.m_Sync.m_ItemCount));
		ClearItemsInSync(&Slot.m_Sync);
		Slot.m_Error = -1;

		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
			str_copy(m_aPendingAuthUser[ClientId], pUser, sizeof(m_aPendingAuthUser[ClientId]));

		m_pEngine->AddJob(&Slot.m_Job, JobRunner, &Slot);
		return true;
	}
	return false;
}

bool CAccountSystem::StartLoginLoadJob(int ClientId, int64 AccountId, const SAccSyncData *pSync)
{
	if(!m_Enabled || !m_pEngine || AccountId <= 0 || !pSync)
		return false;

	for(int i = 0; i < MAX_AUTH_JOBS; i++)
	{
		SJob &Slot = m_aJobs[i];
		if(!JobSlotIdle(Slot))
			continue;

		mem_zero(&Slot, sizeof(Slot));
		Slot.m_pSys = this;
		Slot.m_Submitted = true;
		Slot.m_Type = JOB_LOGIN_LOAD;
		Slot.m_ClientId = ClientId;
		Slot.m_AccountId = AccountId;
		mem_copy(&Slot.m_Sync, pSync, sizeof(Slot.m_Sync));
		Slot.m_Sync.m_aPassword[0] = 0;
		Slot.m_Error = -1;

		m_pEngine->AddJob(&Slot.m_Job, JobRunner, &Slot);
		return true;
	}
	return false;
}

bool CAccountSystem::StartSaveJob(int ClientId, int UserId, const SAccSyncData *pSync)
{
	if(!m_Enabled || !m_pEngine || UserId <= 0 || !pSync)
		return false;

	for(int i = FIRST_SAVE_JOB; i < MAX_ACCOUNT_JOBS; i++)
	{
		SJob &Slot = m_aJobs[i];
		if(!JobSlotIdle(Slot))
			continue;

		mem_zero(&Slot, sizeof(Slot));
		Slot.m_pSys = this;
		Slot.m_Submitted = true;
		Slot.m_Type = JOB_SAVE_ACCOUNT;
		Slot.m_ClientId = ClientId;
		Slot.m_AccountId = UserId;
		mem_copy(&Slot.m_Sync, pSync, sizeof(Slot.m_Sync));
		Slot.m_Error = -1;

		m_pEngine->AddJob(&Slot.m_Job, JobRunner, &Slot);
		return true;
	}
	return false;
}

bool CAccountSystem::StartItemsJob(int ClientId, int UserId, const SAccSyncData *pSync)
{
	if(!m_Enabled || !m_pEngine || UserId <= 0 || !pSync)
		return false;

	for(int i = FIRST_SAVE_JOB; i < MAX_ACCOUNT_JOBS; i++)
	{
		SJob &Slot = m_aJobs[i];
		if(!JobSlotIdle(Slot))
			continue;

		mem_zero(&Slot, sizeof(Slot));
		Slot.m_pSys = this;
		Slot.m_Submitted = true;
		Slot.m_Type = JOB_SAVE_ITEMS;
		Slot.m_ClientId = ClientId;
		Slot.m_AccountId = UserId;
		mem_copy(&Slot.m_Sync, pSync, sizeof(Slot.m_Sync));
		Slot.m_Error = -1;

		m_pEngine->AddJob(&Slot.m_Job, JobRunner, &Slot);
		return true;
	}
	return false;
}

#ifdef CONF_MYSQL

int CAccountSystem::JobRunner(void *pData)
{
	SJob *pSlot = (SJob *)pData;
	CAccountSystem *pSys = (CAccountSystem *)pSlot->m_pSys;
	MYSQL *pSql = (MYSQL *)pSys->m_Pool.Acquire();
	if(!pSql)
	{
		pSlot->m_Error = 100;
		return -1;
	}

	if(pSlot->m_Type == JOB_REGISTER)
	{
		char aEscUser[128];
		char aHash[128];
		char aEscHash[256];
		mysql_real_escape_string(pSql, aEscUser, pSlot->m_Sync.m_aUsername, str_length(pSlot->m_Sync.m_aUsername));
		if(!AccountPasswordHash(pSlot->m_Sync.m_aPassword, aHash, sizeof(aHash)))
		{
			pSlot->m_Error = 102;
			pSys->m_Pool.Release(pSql);
			return -1;
		}
		mysql_real_escape_string(pSql, aEscHash, aHash, str_length(aHash));

		char aQuery[512];
		str_format(aQuery, sizeof(aQuery), "SELECT UserID FROM tw_Accounts WHERE Username='%s' LIMIT 1", aEscUser);
		if(!SqlExec(pSql, aQuery))
		{
			pSlot->m_Error = 102;
			pSys->m_Pool.Release(pSql);
			return -1;
		}

		MYSQL_RES *pRes = mysql_store_result(pSql);
		if(pRes && mysql_num_rows(pRes) > 0)
		{
			mysql_free_result(pRes);
			pSlot->m_Error = 1;
			pSys->m_Pool.Release(pSql);
			return -1;
		}
		if(pRes)
			mysql_free_result(pRes);

		str_format(aQuery, sizeof(aQuery), "INSERT INTO tw_Accounts(Username, Password) VALUES ('%s','%s')", aEscUser, aEscHash);
		if(!SqlExec(pSql, aQuery))
		{
			pSlot->m_Error = 102;
			pSys->m_Pool.Release(pSql);
			return -1;
		}
		pSlot->m_AccountId = (int64)mysql_insert_id(pSql);
		if(pSlot->m_AccountId <= 0 || !VerifyAccountExists(pSql, pSlot->m_AccountId))
		{
			if(pSlot->m_AccountId > 0)
				DeleteAccountByUserId(pSql, pSlot->m_AccountId);
			pSlot->m_AccountId = 0;
			pSlot->m_Error = 102;
			pSys->m_Pool.Release(pSql);
			return -1;
		}
		pSlot->m_Error = 0;
	}
	else if(pSlot->m_Type == JOB_LOGIN)
	{
		char aEscUser[128];
		mysql_real_escape_string(pSql, aEscUser, pSlot->m_Sync.m_aUsername, str_length(pSlot->m_Sync.m_aUsername));

		const bool HasLegacyCols = ColumnExists(pSql, "tw_Accounts", "Sword");
		char aQuery[768];
		if(HasLegacyCols)
			str_format(aQuery, sizeof(aQuery),
				"SELECT UserID,Username,Password,Language,IFNULL(Holding,''),Sword,Pickaxe,Axe FROM tw_Accounts WHERE Username='%s' LIMIT 1",
				aEscUser);
		else
			str_format(aQuery, sizeof(aQuery),
				"SELECT UserID,Username,Password,Language,IFNULL(Holding,'') FROM tw_Accounts WHERE Username='%s' LIMIT 1",
				aEscUser);

		if(!SqlExec(pSql, aQuery))
		{
			pSlot->m_Error = 103;
			pSys->m_Pool.Release(pSql);
			return -1;
		}

		MYSQL_RES *pRes = mysql_store_result(pSql);
		if(!pRes || mysql_num_rows(pRes) == 0)
		{
			if(pRes)
				mysql_free_result(pRes);
			pSlot->m_Error = 2;
			pSys->m_Pool.Release(pSql);
			return -1;
		}

		MYSQL_ROW Row = mysql_fetch_row(pRes);
		if(!Row || !Row[0] || !Row[1] || !Row[2])
		{
			mysql_free_result(pRes);
			pSlot->m_Error = 103;
			pSys->m_Pool.Release(pSql);
			return -1;
		}

		const char *pStoredPass = Row[2];
		char aUpgradeHash[128];
		aUpgradeHash[0] = 0;
		if(!AccountPasswordVerify(pSlot->m_Sync.m_aPassword, pStoredPass, aUpgradeHash, sizeof(aUpgradeHash)))
		{
			mysql_free_result(pRes);
			pSlot->m_Error = 3;
			pSys->m_Pool.Release(pSql);
			return -1;
		}

		pSlot->m_AccountId = (int64)atoll(Row[0]);
		str_copy(pSlot->m_Sync.m_aUsername, Row[1], sizeof(pSlot->m_Sync.m_aUsername));
		pSlot->m_Sync.m_aPassword[0] = 0;
		str_copy(pSlot->m_Sync.m_aLanguage, Row[3] ? Row[3] : "zh-cn", sizeof(pSlot->m_Sync.m_aLanguage));

		if(HasLegacyCols)
			LoadHoldingFromRow(Row, 5, 6, 7, 4, pSlot->m_Sync.m_Holding);
		else
			LoadHoldingFromRow(Row, -1, -1, -1, 4, pSlot->m_Sync.m_Holding);

		mysql_free_result(pRes);

		if(aUpgradeHash[0])
		{
			char aEscHash[256];
			mysql_real_escape_string(pSql, aEscHash, aUpgradeHash, str_length(aUpgradeHash));
			str_format(aQuery, sizeof(aQuery), "UPDATE tw_Accounts SET Password='%s' WHERE UserID=%lld", aEscHash, (long long)pSlot->m_AccountId);
			SqlExec(pSql, aQuery);
		}

		char aHoldingJson[128];
		SerializeHolding(pSlot->m_Sync.m_Holding, aHoldingJson, sizeof(aHoldingJson));
		char aEscHolding[256];
		mysql_real_escape_string(pSql, aEscHolding, aHoldingJson, str_length(aHoldingJson));
		str_format(aQuery, sizeof(aQuery), "UPDATE tw_Accounts SET Holding='%s' WHERE UserID=%lld AND Holding IS NULL", aEscHolding, (long long)pSlot->m_AccountId);
		SqlExec(pSql, aQuery);

		pSlot->m_Error = 0;
	}
	else if(pSlot->m_Type == JOB_LOGIN_LOAD)
	{
		if(!LoadItemsForUser(pSql, (int)pSlot->m_AccountId, &pSlot->m_Sync))
			pSlot->m_Error = 104;
		else
			pSlot->m_Error = 0;
	}
	else if(pSlot->m_Type == JOB_SAVE_ACCOUNT)
	{
		char aEscUser[128];
		char aEscLang[128];
		char aHoldingJson[128];
		char aEscHolding[256];
		mysql_real_escape_string(pSql, aEscUser, pSlot->m_Sync.m_aUsername, str_length(pSlot->m_Sync.m_aUsername));
		mysql_real_escape_string(pSql, aEscLang, pSlot->m_Sync.m_aLanguage, str_length(pSlot->m_Sync.m_aLanguage));
		SerializeHolding(pSlot->m_Sync.m_Holding, aHoldingJson, sizeof(aHoldingJson));
		mysql_real_escape_string(pSql, aEscHolding, aHoldingJson, str_length(aHoldingJson));

		char aQuery[1024];
		str_format(aQuery, sizeof(aQuery),
			"UPDATE tw_Accounts SET Username='%s',Language='%s',Holding='%s' WHERE UserID=%lld",
			aEscUser, aEscLang, aEscHolding, (long long)pSlot->m_AccountId);
		if(!SqlExec(pSql, aQuery))
			pSlot->m_Error = 105;
		else
		{
			SaveItems(pSql, (int)pSlot->m_AccountId, &pSlot->m_Sync);
			pSlot->m_Error = 0;
		}
	}
	else if(pSlot->m_Type == JOB_SAVE_ITEMS)
	{
		SaveItems(pSql, (int)pSlot->m_AccountId, &pSlot->m_Sync);
		pSlot->m_Error = 0;
	}

	pSys->m_Pool.Release(pSql);
	return 0;
}

#else

int CAccountSystem::JobRunner(void *pData)
{
	(void)pData;
	return 0;
}

#endif

void CAccountSystem::ClearPendingAuth(int ClientId)
{
	if(ClientId >= 0 && ClientId < MAX_CLIENTS)
		m_aPendingAuthUser[ClientId][0] = 0;
}

bool CAccountSystem::AuthClientStillValid(int ClientId, const char *pExpectedUser) const
{
	if(!m_pGame || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(!m_pGame->Server()->ClientIngame(ClientId))
		return false;
	CPlayer *pP = PlayerAt(m_pGame, ClientId);
	if(!pP || pP->IsDummy())
		return false;
	if(pP->GetAccountId() >= 0)
		return false;
	if(!m_aPendingAuthUser[ClientId][0])
		return false;
	if(pExpectedUser && pExpectedUser[0] && str_comp_nocase(m_aPendingAuthUser[ClientId], pExpectedUser) != 0)
		return false;
	return true;
}

void CAccountSystem::ApplyLogin(int ClientId, int64 AccountId, const SAccSyncData *pSync)
{
	if(!m_pGame || ClientId < 0 || ClientId >= MAX_CLIENTS || !pSync)
		return;
	CGameContext *pCtx = PlayerGameContext(m_pGame, ClientId);
	CPlayer *pP = pCtx ? pCtx->m_apPlayers[ClientId] : nullptr;
	if(!pP || pP->IsDummy())
		return;

	pP->SetAccountId(AccountId);
	mem_copy(&pP->m_AccData, pSync, sizeof(pP->m_AccData));
	pP->m_AccData.m_aPassword[0] = 0;
	pP->SetLanguage(pP->m_AccData.m_aLanguage[0] ? pP->m_AccData.m_aLanguage : "zh-cn");
	pCtx->SendChatLoc(ClientId, "account.login.ok", u8"登录成功。");
	pCtx->SendCommunityInfo(ClientId);
	pCtx->EnterGame(ClientId);
	if(SPlayerVote *pV = pCtx->GetPlayerVote(ClientId))
		pV->m_Page = PAGE_MENU;
	pCtx->ClearVotes(ClientId);
}

void CAccountSystem::PumpCompletedJobs()
{
	if(!m_Enabled || !m_pGame || m_Pumping)
		return;

	m_Pumping = true;

	for(auto &Slot : m_aJobs)
	{
		if(!Slot.m_Submitted)
			continue;
		if(Slot.m_Job.Status() != CJob::STATE_DONE)
			continue;

		const int ClientId = Slot.m_ClientId;
		CPlayer *pP = (ClientId >= 0 && ClientId < MAX_CLIENTS) ? PlayerAt(m_pGame, ClientId) : nullptr;

		char aAutoLoginUser[64] = {0};
		char aAutoLoginPass[128] = {0};
		bool AutoLogin = false;
		bool SlotCleared = false;

		if(Slot.m_Type == JOB_REGISTER)
		{
			if(Slot.m_Error == 0)
			{
				if(pP && !pP->IsDummy())
					m_pGame->SendChatLoc(ClientId, "account.register.ok", u8"注册成功。");
				str_copy(aAutoLoginUser, Slot.m_Sync.m_aUsername, sizeof(aAutoLoginUser));
				str_copy(aAutoLoginPass, Slot.m_Sync.m_aPassword, sizeof(aAutoLoginPass));
				AutoLogin = true;
			}
			else if(Slot.m_Error == 1)
			{
				if(pP && !pP->IsDummy())
					m_pGame->SendChatLoc(ClientId, "account.register.taken", u8"用户名已被占用。");
			}
			else
			{
				if(pP && !pP->IsDummy())
					m_pGame->SendChatLoc(ClientId, "account.register.fail", u8"注册失败（服务器）。");
			}
		}
		else if(Slot.m_Type == JOB_LOGIN)
		{
			if(Slot.m_Error == 0)
			{
				if(AuthClientStillValid(ClientId, Slot.m_Sync.m_aUsername))
				{
					if(IsAccountOnline(m_pGame, Slot.m_AccountId, ClientId))
					{
						if(pP && !pP->IsDummy())
							m_pGame->SendChatLoc(ClientId, "account.login.already_online", u8"该账号已在其他客户端登录。");
						ClearPendingAuth(ClientId);
					}
					else
					{
						const int64 AccountId = Slot.m_AccountId;
						SAccSyncData Sync;
						mem_copy(&Sync, &Slot.m_Sync, sizeof(Sync));
						ClearJobSlot(Slot);
						SlotCleared = true;
						if(!StartLoginLoadJob(ClientId, AccountId, &Sync))
						{
							if(pP && !pP->IsDummy())
								m_pGame->SendChatLoc(ClientId, "account.login.fail", u8"登录失败（服务器）。");
							ClearPendingAuth(ClientId);
						}
					}
				}
				else
				{
					ClearPendingAuth(ClientId);
				}
			}
			else
			{
				if(Slot.m_Error == 2)
				{
					if(pP && !pP->IsDummy())
						m_pGame->SendChatLoc(ClientId, "account.login.not_found", u8"用户不存在。");
				}
				else if(Slot.m_Error == 3)
				{
					if(pP && !pP->IsDummy())
						m_pGame->SendChatLoc(ClientId, "account.login.wrong_pass", u8"密码错误。");
				}
				else
				{
					if(pP && !pP->IsDummy())
						m_pGame->SendChatLoc(ClientId, "account.login.fail", u8"登录失败（服务器）。");
				}
				ClearPendingAuth(ClientId);
			}
		}
		else if(Slot.m_Type == JOB_LOGIN_LOAD)
		{
			if(Slot.m_Error == 0 && AuthClientStillValid(ClientId, Slot.m_Sync.m_aUsername))
			{
				if(IsAccountOnline(m_pGame, Slot.m_AccountId, ClientId))
				{
					if(pP && !pP->IsDummy())
						m_pGame->SendChatLoc(ClientId, "account.login.already_online", u8"该账号已在其他客户端登录。");
				}
				else
				{
					ApplyLogin(ClientId, Slot.m_AccountId, &Slot.m_Sync);
				}
			}
			else if(Slot.m_Error != 0 && pP && !pP->IsDummy())
			{
				m_pGame->SendChatLoc(ClientId, "account.login.fail", u8"登录失败（服务器）。");
			}
			ClearPendingAuth(ClientId);
		}

		if(!SlotCleared)
			ClearJobSlot(Slot);

		if(AutoLogin && !StartJob(JOB_LOGIN, ClientId, aAutoLoginUser, aAutoLoginPass))
		{
			if(pP && !pP->IsDummy())
				m_pGame->SendChatLoc(ClientId, "account.register.autologin_fail", u8"自动登录排队失败，请使用 /login。");
		}
	}

	m_Pumping = false;
}

void CAccountSystem::ClearSaveThrottle(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aNextItemsSaveTick[ClientId] = 0;
	m_aNextAccountSaveTick[ClientId] = 0;
	m_aPendingItemsSave[ClientId] = false;
	m_aPendingAccountSave[ClientId] = false;
}

bool CAccountSystem::QueueItemsSave(int ClientId, bool Force)
{
	if(!m_Enabled || !m_pGame || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	CPlayer *pP = PlayerAt(m_pGame, ClientId);
	if(!pP || pP->GetAccountId() < 0)
		return false;

	const int Now = m_pGame->Server()->Tick();
	const int Interval = maximum(1, m_pConfig->m_SvAccSaveItemsSec) * m_pGame->Server()->TickSpeed();
	if(!Force && Now < m_aNextItemsSaveTick[ClientId])
	{
		m_aPendingItemsSave[ClientId] = true;
		return false;
	}
	if(!StartItemsJob(ClientId, (int)pP->GetAccountId(), &pP->m_AccData))
	{
		m_aPendingItemsSave[ClientId] = true;
		return false;
	}

	m_aNextItemsSaveTick[ClientId] = Now + Interval;
	m_aPendingItemsSave[ClientId] = false;
	return true;
}

bool CAccountSystem::QueueAccountSave(int ClientId, bool Force)
{
	if(!m_Enabled || !m_pGame || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	CPlayer *pP = PlayerAt(m_pGame, ClientId);
	if(!pP || pP->GetAccountId() < 0)
		return false;

	const int Now = m_pGame->Server()->Tick();
	const int Interval = maximum(1, m_pConfig->m_SvAccSaveAccountSec) * m_pGame->Server()->TickSpeed();
	if(!Force && Now < m_aNextAccountSaveTick[ClientId])
	{
		m_aPendingAccountSave[ClientId] = true;
		return false;
	}
	if(!StartSaveJob(ClientId, (int)pP->GetAccountId(), &pP->m_AccData))
	{
		m_aPendingAccountSave[ClientId] = true;
		return false;
	}

	m_aNextAccountSaveTick[ClientId] = Now + Interval;
	m_aPendingAccountSave[ClientId] = false;
	return true;
}

void CAccountSystem::FlushPendingSaves()
{
	if(!m_Enabled || !m_pGame)
		return;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pSaveP = PlayerAt(m_pGame, i);
		if(!pSaveP || pSaveP->GetAccountId() < 0)
			continue;
		if(m_aPendingAccountSave[i])
			QueueAccountSave(i, false);
		if(m_aPendingItemsSave[i])
			QueueItemsSave(i, false);
	}
}

void CAccountSystem::OnGameTick()
{
	PumpCompletedJobs();
	FlushPendingSaves();
}

void CAccountSystem::OnClientDisconnect(int ClientId)
{
	ClearPendingAuth(ClientId);
	if(!m_Enabled || !m_pGame)
		return;
	CPlayer *pP = PlayerAt(m_pGame, ClientId);
	if(!pP || pP->GetAccountId() < 0)
	{
		ClearSaveThrottle(ClientId);
		return;
	}

	const int UserId = (int)pP->GetAccountId();
	SAccSyncData Sync;
	mem_copy(&Sync, &pP->m_AccData, sizeof(Sync));
	pP->ClearAccount();
	StartSaveJob(ClientId, UserId, &Sync);
	ClearSaveThrottle(ClientId);
}

void CAccountSystem::RequestSaveItems(int ClientId)
{
	QueueItemsSave(ClientId, false);
}

void CAccountSystem::RequestSaveAccount(int ClientId)
{
	QueueAccountSave(ClientId, false);
}

void CAccountSystem::ComChatRegister(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CAccountSystem *pAcc = pGame->Accounts();

	const char *pU = pResult->GetString(0);
	const char *pPw = pResult->GetString(1);

	if(!pAcc || !pAcc->IsEnabled())
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "account.disabled", u8"MySQL 账号系统不可用，请联系管理员。");
		return;
	}
	if(!UsernameOk(pU) || !PasswordOk(pPw))
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "account.register.usage", u8"用户名 3–63（字母数字下划线），密码 6–63 位。");
		return;
	}
	if(!pAcc->StartJob(JOB_REGISTER, pCtx->m_ClientID, pU, pPw))
		pGame->SendChatLoc(pCtx->m_ClientID, "account.busy", u8"服务器忙，请稍后再试。");
	else
		pGame->SendChatLoc(pCtx->m_ClientID, "account.register.pending", u8"正在注册…");
}

void CAccountSystem::ComChatLogin(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	CAccountSystem *pAcc = pGame->Accounts();

	const char *pU = pResult->GetString(0);
	const char *pPw = pResult->GetString(1);

	if(!pAcc || !pAcc->IsEnabled())
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "account.disabled", u8"MySQL 账号系统不可用，请联系管理员。");
		return;
	}
	if(!UsernameOk(pU) || !PasswordOk(pPw))
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "account.login.usage", u8"用户名或密码格式无效。");
		return;
	}
	CPlayer *pP = pGame->m_apPlayers[pCtx->m_ClientID];
	if(pP && pP->GetAccountId() >= 0)
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "account.login.already", u8"你已经登录。");
		return;
	}
	if(IsUsernameLoggedInElsewhere(pGame, pU, pCtx->m_ClientID))
	{
		pGame->SendChatLoc(pCtx->m_ClientID, "account.login.already_online", u8"该账号已在其他客户端登录。");
		return;
	}
	if(!pAcc->StartJob(JOB_LOGIN, pCtx->m_ClientID, pU, pPw))
		pGame->SendChatLoc(pCtx->m_ClientID, "account.busy", u8"服务器忙，请稍后再试。");
	else
		pGame->SendChatLoc(pCtx->m_ClientID, "account.login.pending", u8"正在登录…");
}

void CAccountSystem::ComAccInfo(IConsole::IResult *pResult, void *pUser)
{
	(void)pResult;
	CGameContext *pGame = (CGameContext *)pUser;
	CAccountSystem *pAcc = pGame->Accounts();
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "accounts: enabled=%d mysql_pool=%d",
		pAcc && pAcc->m_Enabled ? 1 : 0,
#ifdef CONF_MYSQL
		pAcc && pAcc->m_Pool.IsInitialized() ? 1 : 0
#else
		0
#endif
	);
	pGame->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "acc", aBuf);
}

static void ComChatLanguage(IConsole::IResult *pResult, void *pUser)
{
	CCommandManager::SCommandContext *pCtx = (CCommandManager::SCommandContext *)pUser;
	CGameContext *pGame = (CGameContext *)pCtx->m_pContext;
	const int CID = pCtx->m_ClientID;
	if(CID < 0 || !pGame->m_apPlayers[CID])
		return;
	CPlayer *pP = pGame->m_apPlayers[CID];
	if(pResult->NumArguments() < 1)
	{
		pGame->SendChatLocF(CID, "language.status", "Language: %s (use /language zh-cn or /language en)", pP->GetLanguage());
		return;
	}
	pP->SetLanguage(pResult->GetString(0));
	pGame->SendChatCommands(CID);
	if(pGame->Core() && pGame->Core()->VoteMenuManager())
		pGame->Core()->VoteMenuManager()->ClearVotes(CID);
	pGame->SendChatLocF(CID, "language.changed", "%s: %s", pGame->Loc(CID, "menu.title", "Player menu"), pP->GetLanguage());
}

void CAccountSystem::RegisterChatCommands(CCommandManager *pManager, CGameContext *pGame)
{
	pManager->AddCommand("register", "cmd.register.help", "sr", ComChatRegister, pGame);
	pManager->AddCommand("login", "cmd.login.help", "sr", ComChatLogin, pGame);
	if(pManager->AddCommand("language", "cmd.language.help", "?s", ComChatLanguage, pGame) != 0)
		dbg_msg("server", "failed to register /language chat command");
}

void CAccountSystem::RegisterConsoleCommands(IConsole *pConsole, CGameContext *pGame)
{
	pConsole->Register("acc_info", "", CFGFLAG_SERVER, ComAccInfo, pGame, "Print MySQL account subsystem status");
}
