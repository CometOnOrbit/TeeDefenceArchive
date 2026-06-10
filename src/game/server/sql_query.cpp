/* (c) TeeDefenceArchive - 2026 */
#include <base/math.h>

#include "sql_query.h"

#ifdef CONF_MYSQL

#include <mysql.h>

#include <engine/shared/config.h>

static LOCK s_SqlLogLock = nullptr;
static IOHANDLE s_SqlLogFile = nullptr;
static char s_aSqlLogFile[128] = {0};

static void SqlLogEnsureFile(CConfig *pConfig)
{
	if(!pConfig || !pConfig->m_SvSqlFailedLogFile[0])
	{
		if(s_SqlLogFile)
		{
			io_close(s_SqlLogFile);
			s_SqlLogFile = nullptr;
		}
		s_aSqlLogFile[0] = 0;
		return;
	}

	if(s_SqlLogFile && str_comp(s_aSqlLogFile, pConfig->m_SvSqlFailedLogFile) == 0)
		return;

	if(s_SqlLogFile)
	{
		io_close(s_SqlLogFile);
		s_SqlLogFile = nullptr;
	}

	str_copy(s_aSqlLogFile, pConfig->m_SvSqlFailedLogFile, sizeof(s_aSqlLogFile));
	s_SqlLogFile = io_open(s_aSqlLogFile, IOFLAG_APPEND);
}

bool SqlConnectionLost(int Err)
{
	return Err == 2006 || Err == 2013 || Err == 2014;
}

void SqlLogFailedQuery(CConfig *pConfig, const char *pReason, const char *pQuery)
{
	if(!pConfig || !pConfig->m_SvSqlFailedLogFile[0] || !pQuery)
		return;

	if(!s_SqlLogLock)
		s_SqlLogLock = lock_create();

	lock_wait(s_SqlLogLock);
	SqlLogEnsureFile(pConfig);
	if(s_SqlLogFile)
	{
		char aTimestamp[64];
		str_timestamp(aTimestamp, sizeof(aTimestamp));
		char aLine[2048];
		str_format(aLine, sizeof(aLine), "[%s] %s DML: %s", aTimestamp, pReason ? pReason : "failed", pQuery);
		io_write(s_SqlLogFile, aLine, str_length(aLine));
		io_write_newline(s_SqlLogFile);
		io_flush(s_SqlLogFile);
	}
	lock_unlock(s_SqlLogLock);
}

bool SqlExecQuery(void *pSql, CConfig *pConfig, const char *pQuery)
{
	MYSQL *pConn = (MYSQL *)pSql;
	if(!pConn || !pQuery)
		return false;

	const int MaxRetries = pConfig ? maximum(1, pConfig->m_SvSqlDmlMaxRetries) : 3;
	for(int Attempt = 0; Attempt < MaxRetries; Attempt++)
	{
		if(mysql_query(pConn, pQuery) == 0)
			return true;

		const int Err = mysql_errno(pConn);
		dbg_msg("mysql", "error (%d): %s | %s", Err, mysql_error(pConn), pQuery);
		if(SqlConnectionLost(Err) && Attempt + 1 < MaxRetries)
		{
			mysql_ping(pConn);
			thread_sleep(50);
			continue;
		}

		SqlLogFailedQuery(pConfig, "query failed", pQuery);
		return false;
	}
	return false;
}

#endif
