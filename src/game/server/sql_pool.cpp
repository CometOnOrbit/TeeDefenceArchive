/* (c) TeeDefenceArchive - 2026 */
#include "sql_pool.h"

#ifdef CONF_MYSQL

#include <mysql.h>

#include <base/tl/array.h>

struct CSqlConnectionPool::SPoolInner
{
	LOCK m_Lock;
	array<MYSQL *> m_Idle;
	bool m_Initialized;
};

CSqlConnectionPool::CSqlConnectionPool()
{
	m_pInner = new SPoolInner;
	m_pInner->m_Lock = lock_create();
	m_pInner->m_Initialized = false;
}

CSqlConnectionPool::~CSqlConnectionPool()
{
	Shutdown();
	delete m_pInner;
	m_pInner = nullptr;
}

bool CSqlConnectionPool::IsInitialized() const
{
	return m_pInner && m_pInner->m_Initialized;
}

bool CSqlConnectionPool::Init(int PoolSize, const char *pHost, int Port, const char *pUser, const char *pPassword, const char *pDatabase)
{
	if(!m_pInner || PoolSize <= 0 || !pHost || !pUser || !pDatabase)
		return false;
	Shutdown();
	for(int i = 0; i < PoolSize; i++)
	{
		MYSQL *pConn = mysql_init(nullptr);
		if(!pConn)
			goto fail;
		const unsigned int Timeout = 8;
		mysql_options(pConn, MYSQL_OPT_CONNECT_TIMEOUT, &Timeout);
		if(!mysql_real_connect(pConn, pHost, pUser, pPassword ? pPassword : "", pDatabase, Port, nullptr, CLIENT_MULTI_STATEMENTS))
		{
			dbg_msg("mysql", "connect failed: %s", mysql_error(pConn));
			mysql_close(pConn);
			goto fail; // THIS IS FUUUUUUUUUUUUUUUUCKING UGLY BUT USEFUL AHHHHHHH
		}
		bool Reconnect = true;
		mysql_options(pConn, MYSQL_OPT_RECONNECT, &Reconnect);
		m_pInner->m_Idle.add(pConn);
	}
	m_pInner->m_Initialized = true;
	dbg_msg("mysql", "pool initialized (%d connections)", PoolSize);
	return true;

// TuT I promised never to use this shit, but...
fail:
	for(int j = 0; j < m_pInner->m_Idle.size(); j++)
	{
		mysql_close(m_pInner->m_Idle[j]);
	}
	m_pInner->m_Idle.clear();
	return false;
}

void CSqlConnectionPool::Shutdown()
{
	if(!m_pInner)
		return;
	lock_wait(m_pInner->m_Lock);
	for(int i = 0; i < m_pInner->m_Idle.size(); i++)
	{
		mysql_close(m_pInner->m_Idle[i]);
	}
	m_pInner->m_Idle.clear();
	m_pInner->m_Initialized = false;
	lock_unlock(m_pInner->m_Lock);
}

void *CSqlConnectionPool::Acquire()
{
	if(!m_pInner || !m_pInner->m_Initialized)
		return nullptr;

	for(int Attempt = 0; Attempt < 300; Attempt++)
	{
		lock_wait(m_pInner->m_Lock);
		MYSQL *pConn = nullptr;
		if(m_pInner->m_Idle.size() > 0)
		{
			const int Last = m_pInner->m_Idle.size() - 1;
			pConn = m_pInner->m_Idle[Last];
			m_pInner->m_Idle.remove_index_fast(Last);
		}
		lock_unlock(m_pInner->m_Lock);
		if(pConn)
			return pConn;
		thread_sleep(10);
	}
	return nullptr;
}

void CSqlConnectionPool::Release(void *pConn)
{
	if(!pConn || !m_pInner || !m_pInner->m_Initialized)
		return;
	lock_wait(m_pInner->m_Lock);
	m_pInner->m_Idle.add((MYSQL *)pConn);
	lock_unlock(m_pInner->m_Lock);
}

#else // !CONF_MYSQL

CSqlConnectionPool::CSqlConnectionPool()
{
	m_pInner = nullptr;
}

CSqlConnectionPool::~CSqlConnectionPool()
{
}

bool CSqlConnectionPool::IsInitialized() const
{
	return false;
}

bool CSqlConnectionPool::Init(int PoolSize, const char *pHost, int Port, const char *pUser, const char *pPassword, const char *pDatabase)
{
	(void)PoolSize;
	(void)pHost;
	(void)Port;
	(void)pUser;
	(void)pPassword;
	(void)pDatabase;
	return false;
}

void CSqlConnectionPool::Shutdown()
{
}

void *CSqlConnectionPool::Acquire()
{
	return nullptr;
}

void CSqlConnectionPool::Release(void *pConn)
{
	(void)pConn;
}

#endif
