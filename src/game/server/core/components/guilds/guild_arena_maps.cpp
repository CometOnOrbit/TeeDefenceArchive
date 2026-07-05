#include "guild_arena_maps.h"

#include "guild_match_mode.h"

#include <base/system.h>
#include <engine/storage.h>

#include <algorithm>
#include <unordered_set>

static const char *MapBasename(const char *pRelPath)
{
	if(!pRelPath)
		return "";
	const char *pBase = pRelPath;
	for(const char *p = pRelPath; *p; p++)
	{
		if(*p == '/' || *p == '\\')
			pBase = p + 1;
	}
	return pBase;
}

static bool IsDeathmatchStyleMode(const char *pMode)
{
	return str_comp_nocase(pMode, "tdm") == 0
		|| str_comp_nocase(pMode, "itdm") == 0
		|| str_comp_nocase(pMode, "idm") == 0
		|| str_comp_nocase(pMode, "dm") == 0;
}

bool IsArenaMapAllowedForMode(const char *pRelPath, const char *pMode)
{
	if(!pRelPath || !pRelPath[0] || !pMode || !pMode[0])
		return false;

	const bool InVanilla = str_startswith(pRelPath, "vanilla/") != nullptr;

	if(str_comp_nocase(pMode, "fng") == 0)
		return str_startswith(pRelPath, "fng/") != nullptr;

	if(str_comp_nocase(pMode, "ctf") == 0)
		return str_startswith_nocase(MapBasename(pRelPath), "ctf") != nullptr;

	if(IsDeathmatchStyleMode(pMode))
		return InVanilla;

	return false;
}

bool ArenaMapFileExists(IStorage *pStorage, const char *pRelPath)
{
	if(!pStorage || !pRelPath || !pRelPath[0])
		return false;

	const char *pSlash = nullptr;
	for(const char *p = pRelPath; *p; p++)
	{
		if(*p == '/')
			pSlash = p;
	}

	char aDir[IO_MAX_PATH_LENGTH] = "maps";
	char aFile[IO_MAX_PATH_LENGTH];
	if(pSlash)
	{
		str_format(aDir, sizeof(aDir), "maps/%.*s", (int)(pSlash - pRelPath), pRelPath);
		str_format(aFile, sizeof(aFile), "%s.map", pSlash + 1);
	}
	else
	{
		str_format(aFile, sizeof(aFile), "%s.map", pRelPath);
	}

	char aFound[IO_MAX_PATH_LENGTH];
	return pStorage->FindFile(aFile, aDir, IStorage::TYPE_ALL, aFound, sizeof(aFound));
}

struct SMapScanCtx
{
	IStorage *m_pStorage;
	char m_aPrefix[IO_MAX_PATH_LENGTH];
	std::vector<std::string> *m_pOut;
};

static int MapListCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	SMapScanCtx *pCtx = (SMapScanCtx *)pUser;
	(void)StorageType;

	if(!pName || !pName[0] || pName[0] == '.')
		return 0;

	if(IsDir)
	{
		char aSavedPrefix[IO_MAX_PATH_LENGTH];
		str_copy(aSavedPrefix, pCtx->m_aPrefix, sizeof(aSavedPrefix));

		if(pCtx->m_aPrefix[0])
			str_format(pCtx->m_aPrefix, sizeof(pCtx->m_aPrefix), "%s/%s", pCtx->m_aPrefix, pName);
		else
			str_copy(pCtx->m_aPrefix, pName, sizeof(pCtx->m_aPrefix));

		char aListPath[IO_MAX_PATH_LENGTH];
		str_format(aListPath, sizeof(aListPath), "maps/%s", pCtx->m_aPrefix);
		pCtx->m_pStorage->ListDirectory(IStorage::TYPE_ALL, aListPath, MapListCallback, pCtx);

		str_copy(pCtx->m_aPrefix, aSavedPrefix, sizeof(pCtx->m_aPrefix));
		return 0;
	}

	if(!str_endswith(pName, ".map"))
		return 0;

	char aRel[IO_MAX_PATH_LENGTH];
	if(pCtx->m_aPrefix[0])
		str_format(aRel, sizeof(aRel), "%s/%s", pCtx->m_aPrefix, pName);
	else
		str_copy(aRel, pName, sizeof(aRel));

	const int Len = str_length(aRel);
	if(Len > 4 && str_comp(aRel + Len - 4, ".map") == 0)
		aRel[Len - 4] = '\0';

	pCtx->m_pOut->push_back(aRel);
	return 0;
}

static void ScanAllArenaMaps(IStorage *pStorage, std::vector<std::string> &Out)
{
	Out.clear();
	if(!pStorage)
		return;

	SMapScanCtx Ctx;
	Ctx.m_pStorage = pStorage;
	Ctx.m_aPrefix[0] = '\0';
	Ctx.m_pOut = &Out;
	pStorage->ListDirectory(IStorage::TYPE_ALL, "maps", MapListCallback, &Ctx);

	std::sort(Out.begin(), Out.end());
}

void ListArenaMapsForMode(IStorage *pStorage, const char *pMode, std::vector<std::string> &Out)
{
	Out.clear();
	if(!pStorage || !pMode || !pMode[0])
		return;

	std::vector<std::string> All;
	ScanAllArenaMaps(pStorage, All);
	std::unordered_set<std::string> Seen;
	for(const std::string &Map : All)
	{
		if(!IsArenaMapAllowedForMode(Map.c_str(), pMode))
			continue;
		if(!Seen.insert(Map).second)
			continue;
		Out.push_back(Map);
	}
	std::sort(Out.begin(), Out.end());
}

void GuildWarDefaultArenaMap(IStorage *pStorage, const char *pMode, char *pBuf, int BufSize)
{
	if(!pBuf || BufSize <= 0)
		return;
	pBuf[0] = '\0';
	if(!pMode || !pMode[0])
		return;

	std::vector<std::string> Maps;
	ListArenaMapsForMode(pStorage, pMode, Maps);
	if(!Maps.empty())
	{
		str_copy(pBuf, Maps[0].c_str(), BufSize);
		return;
	}

	const char *pLegacy = GuildWarArenaMapForMode(pMode);
	if(str_comp_nocase(pMode, "fng") == 0)
	{
		char aCandidate[IO_MAX_PATH_LENGTH];
		str_format(aCandidate, sizeof(aCandidate), "fng/%s", pLegacy);
		if(ArenaMapFileExists(pStorage, aCandidate))
		{
			str_copy(pBuf, aCandidate, BufSize);
			return;
		}
	}
	else if(IsDeathmatchStyleMode(pMode))
	{
		char aCandidate[IO_MAX_PATH_LENGTH];
		str_format(aCandidate, sizeof(aCandidate), "vanilla/%s", pLegacy);
		if(ArenaMapFileExists(pStorage, aCandidate))
		{
			str_copy(pBuf, aCandidate, BufSize);
			return;
		}
		str_copy(aCandidate, "vanilla/dm1", sizeof(aCandidate));
		if(ArenaMapFileExists(pStorage, aCandidate))
		{
			str_copy(pBuf, aCandidate, BufSize);
			return;
		}
	}
	else if(str_comp_nocase(pMode, "ctf") == 0)
	{
		char aCandidate[IO_MAX_PATH_LENGTH];
		str_copy(aCandidate, "vanilla/ctf1", sizeof(aCandidate));
		if(ArenaMapFileExists(pStorage, aCandidate))
		{
			str_copy(pBuf, aCandidate, BufSize);
			return;
		}
	}

	if(ArenaMapFileExists(pStorage, pLegacy))
		str_copy(pBuf, pLegacy, BufSize);
	else
		str_copy(pBuf, pLegacy, BufSize);
}
