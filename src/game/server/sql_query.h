/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_SQL_QUERY_H
#define GAME_SERVER_SQL_QUERY_H

class CConfig;

#ifdef CONF_MYSQL
bool SqlConnectionLost(int Err);
bool SqlExecQuery(void *pSql, CConfig *pConfig, const char *pQuery);
void SqlLogFailedQuery(CConfig *pConfig, const char *pReason, const char *pQuery);
#endif

#endif
