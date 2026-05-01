/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_SQL_POOL_H
#define GAME_SERVER_SQL_POOL_H

#include <base/system.h>

class CSqlConnectionPool
{
	struct SPoolInner;
	SPoolInner *m_pInner;

public:
	CSqlConnectionPool();
	~CSqlConnectionPool();

	bool IsInitialized() const;

	// Creates PoolSize TCP connections. Main thread only before workers use the pool.
	bool Init(int PoolSize, const char *pHost, int Port, const char *pUser, const char *pPassword, const char *pDatabase);

	// Closes all connections. Wait until no job holds a connection.
	void Shutdown();

	// For job threads: borrow a connection (blocking lock). Returns nullptr if pool is down / empty.
	void *Acquire();

	// Return a connection acquired with Acquire().
	void Release(void *pConn);
};

#endif
