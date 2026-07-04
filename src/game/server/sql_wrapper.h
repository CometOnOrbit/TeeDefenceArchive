/* (c) TeeDefenceArchive - 2026
 * MRPG-style SQL wrapper around TDA's existing MySQL C API.
 * Provides DB::SELECT / INSERT / UPDATE / REMOVE convenience,
 * CSqlString escaping, and ResultPtr result accessors.
 */
#ifndef GAME_SERVER_SQL_WRAPPER_H
#define GAME_SERVER_SQL_WRAPPER_H

#include <base/system.h>
#include <engine/shared/config.h>

#include <mysql.h>

#include <cstring>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class CConfig;

// ================================================================
// DB enum (MRPG-style)
// ================================================================
enum class DB
{
	SELECT = 0,
	INSERT,
	UPDATE,
	REMOVE,
	OTHER,
};

// ================================================================
// CSqlString – embedded escaping (MRPG-style)
// ================================================================
template<int MAX_LEN>
class CSqlString
{
	char m_aBuf[MAX_LEN * 2 + 1]; // worst-case: every byte doubled + null

public:
	CSqlString() { m_aBuf[0] = 0; }
	CSqlString(const char *pStr)
	{
		if(!pStr) { m_aBuf[0] = 0; return; }
		mysql_real_escape_string(nullptr, m_aBuf, pStr, str_length(pStr));
	}

	const char *cstr() const { return m_aBuf; }
};

// ================================================================
// WrapperResultSet – owns MYSQL_RES + MYSQL_ROW cursor
// ================================================================
class WrapperResultSet
{
	MYSQL_RES *m_pRes;
	MYSQL_ROW m_pRow;
	my_ulonglong m_NumRows;
	int m_NumFields;

public:
	WrapperResultSet()
	{}


	explicit WrapperResultSet(MYSQL_RES *pRes) :
		m_pRes(pRes), m_pRow(nullptr), m_NumRows(0), m_NumFields(0)
	{
		if(m_pRes)
		{
			m_NumRows = mysql_num_rows(m_pRes);
			m_NumFields = mysql_num_fields(m_pRes);
		}
	}

	~WrapperResultSet()
	{
		if(m_pRes)
			mysql_free_result(m_pRes);
	}

	WrapperResultSet(const WrapperResultSet &) = delete;
	WrapperResultSet &operator=(const WrapperResultSet &) = delete;
	WrapperResultSet(WrapperResultSet &&other) noexcept :
		m_pRes(other.m_pRes), m_pRow(other.m_pRow),
		m_NumRows(other.m_NumRows), m_NumFields(other.m_NumFields)
	{
		other.m_pRes = nullptr;
		other.m_pRow = nullptr;
		other.m_NumRows = 0;
		other.m_NumFields = 0;
	}
	WrapperResultSet &operator=(WrapperResultSet &&other) noexcept
	{
		if(this != &other)
		{
			if(m_pRes) mysql_free_result(m_pRes);
			m_pRes = other.m_pRes; m_pRow = other.m_pRow;
			m_NumRows = other.m_NumRows; m_NumFields = other.m_NumFields;
			other.m_pRes = nullptr; other.m_pRow = nullptr;
			other.m_NumRows = 0; other.m_NumFields = 0;
		}
		return *this;
	}

	explicit operator bool() const { return m_pRes != nullptr; }

	bool next()
	{
		if(!m_pRes) return false;
		m_pRow = mysql_fetch_row(m_pRes);
		return m_pRow != nullptr;
	}

	size_t rowsCount() const { return (size_t)m_NumRows; }

	int getInt(const char *pCol) const
	{
		if(!m_pRow) return 0;
		int idx = FieldIndex(pCol);
		if(idx < 0 || !m_pRow[idx]) return 0;
		return str_toint(m_pRow[idx]);
	}

	int64_t getInt64(const char *pCol) const
	{
		if(!m_pRow) return 0;
		int idx = FieldIndex(pCol);
		if(idx < 0 || !m_pRow[idx]) return 0;
		return (int64_t)atoll(m_pRow[idx]);
	}

	bool getBoolean(const char *pCol) const
	{
		if(!m_pRow) return false;
		int idx = FieldIndex(pCol);
		if(idx < 0 || !m_pRow[idx]) return false;
		return m_pRow[idx][0] == '1';
	}

	std::string getString(const char *pCol) const
	{
		if(!m_pRow) return "";
		int idx = FieldIndex(pCol);
		if(idx < 0 || !m_pRow[idx]) return "";
		return std::string(m_pRow[idx]);
	}

	double getDouble(const char *pCol) const
	{
		if(!m_pRow) return 0.0;
		int idx = FieldIndex(pCol);
		if(idx < 0 || !m_pRow[idx]) return 0.0;
		return str_tofloat(m_pRow[idx]);
	}

private:
	int FieldIndex(const char *pName) const
	{
		if(!m_pRes) return -1;
		MYSQL_FIELD *pFields = mysql_fetch_fields(m_pRes);
		if(!pFields) return -1;
		for(int i = 0; i < m_NumFields; i++)
		{
			if(str_comp(pFields[i].name, pName) == 0)
				return i;
		}
		return -1;
	}
};

using ResultPtr = std::shared_ptr<WrapperResultSet>;
using CallbackUpdatePtr = std::function<void(bool)>;

// ================================================================
// dbg_assert-compatible fmt_default (simple string format)
// ================================================================
// Use str_format instead of MRPG's fmt_default

// ================================================================
// Convenience functions: Execute* wrappers using TDA's SqlExecQuery
// ================================================================

// Simple synchronous SELECT → ResultPtr
inline ResultPtr SqlSelect(const char *pQuery, void *pSql, CConfig *pConfig)
{
	if(!pSql || !pQuery) return nullptr;
	MYSQL *pMysql = (MYSQL *)pSql;
	if(!SqlExecQuery(pSql, pConfig, pQuery))
		return nullptr;
	MYSQL_RES *pRes = mysql_store_result(pMysql);
	if(!pRes) return nullptr;
	return std::make_shared<WrapperResultSet>(pRes);
}

// Simple synchronous update (INSERT/UPDATE/REMOVE) → bool
inline bool SqlUpdate(const char *pQuery, void *pSql, CConfig *pConfig)
{
	if(!pSql || !pQuery) return false;
	if(!SqlExecQuery(pSql, pConfig, pQuery))
		return false;
	return true;
}

// ================================================================
// DB::Execute convenience (synchronous, no thread pool)
// ================================================================

// SELECT: returns ResultPtr
template<DB T>
inline std::enable_if_t<T == DB::SELECT, ResultPtr>
DB_Execute(const char *pSelect, const char *pTable, const char *pWhere, void *pSql, CConfig *pConfig)
{
	char aQuery[4096];
	if(pWhere && pWhere[0])
		str_format(aQuery, sizeof(aQuery), "SELECT %s FROM %s %s", pSelect, pTable, pWhere);
	else
		str_format(aQuery, sizeof(aQuery), "SELECT %s FROM %s", pSelect, pTable);
	return SqlSelect(aQuery, pSql, pConfig);
}

template<DB T, typename... Ts>
inline std::enable_if_t<T == DB::SELECT, ResultPtr>
DB_Execute(const char *pSelect, const char *pTable, const char *pFmt, void *pSql, CConfig *pConfig, Ts&&... args)
{
	char aWhere[2048];
	str_format(aWhere, sizeof(aWhere), pFmt, std::forward<Ts>(args)...);
	return DB_Execute<DB::SELECT>(pSelect, pTable, aWhere, pSql, pConfig);
}

// INSERT: returns bool
template<DB T>
inline std::enable_if_t<(T == DB::INSERT), bool>
DB_Execute(const char *pTable, const char *pValues, void *pSql, CConfig *pConfig)
{
	char aQuery[4096];
	str_format(aQuery, sizeof(aQuery), "INSERT INTO %s %s", pTable, pValues);
	return SqlUpdate(aQuery, pSql, pConfig);
}

template<DB T, typename... Ts>
inline std::enable_if_t<(T == DB::INSERT), bool>
DB_Execute(const char *pTable, const char *pFmt, void *pSql, CConfig *pConfig, Ts&&... args)
{
	char aValues[2048];
	str_format(aValues, sizeof(aValues), pFmt, std::forward<Ts>(args)...);
	return DB_Execute<DB::INSERT>(pTable, aValues, pSql, pConfig);
}

// UPDATE: returns bool
template<DB T>
inline std::enable_if_t<(T == DB::UPDATE), bool>
DB_Execute(const char *pTable, const char *pSet, void *pSql, CConfig *pConfig)
{
	char aQuery[4096];
	str_format(aQuery, sizeof(aQuery), "UPDATE %s SET %s", pTable, pSet);
	return SqlUpdate(aQuery, pSql, pConfig);
}

template<DB T, typename... Ts>
inline std::enable_if_t<(T == DB::UPDATE), bool>
DB_Execute(const char *pTable, const char *pFmt, void *pSql, CConfig *pConfig, Ts&&... args)
{
	char aSet[2048];
	str_format(aSet, sizeof(aSet), pFmt, std::forward<Ts>(args)...);
	return DB_Execute<DB::UPDATE>(pTable, aSet, pSql, pConfig);
}

// REMOVE: returns bool
template<DB T>
inline std::enable_if_t<(T == DB::REMOVE), bool>
DB_Execute(const char *pTable, const char *pWhere, void *pSql, CConfig *pConfig)
{
	char aQuery[4096];
	str_format(aQuery, sizeof(aQuery), "DELETE FROM %s %s", pTable, pWhere);
	return SqlUpdate(aQuery, pSql, pConfig);
}

template<DB T, typename... Ts>
inline std::enable_if_t<(T == DB::REMOVE), bool>
DB_Execute(const char *pTable, const char *pFmt, void *pSql, CConfig *pConfig, Ts&&... args)
{
	char aWhere[2048];
	str_format(aWhere, sizeof(aWhere), pFmt, std::forward<Ts>(args)...);
	return DB_Execute<DB::REMOVE>(pTable, aWhere, pSql, pConfig);
}

// Custom SQL (OTHER)
template<DB T>
inline std::enable_if_t<(T == DB::OTHER), bool>
DB_Execute(const char *pCustomSql, void *pSql, CConfig *pConfig)
{
	return SqlUpdate(pCustomSql, pSql, pConfig);
}

// ================================================================
// DBSet - simple string set helper (like MRPG's DBSet for comma-separated IDs)
// ================================================================
class DBSet
{
	std::vector<std::string> m_vItems;
public:
	DBSet() = default;
	DBSet(const std::string &str)
	{
		// Parse comma-separated values
		size_t start = 0, end;
		while((end = str.find(',', start)) != std::string::npos)
		{
			m_vItems.push_back(str.substr(start, end - start));
			start = end + 1;
		}
		if(start < str.length())
			m_vItems.push_back(str.substr(start));
	}

	const std::vector<std::string> &getItems() const { return m_vItems; }
	size_t size() const { return m_vItems.size(); }
};

#endif // GAME_SERVER_SQL_WRAPPER_H
