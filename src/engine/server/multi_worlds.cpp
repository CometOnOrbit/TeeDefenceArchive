#include "multi_worlds.h"

#include <base/hash.h>
#include <engine/kernel.h>
#include <engine/map.h>
#include <engine/server.h>
#include <engine/storage.h>
#include <engine/shared/jsonparser.h>

CMapDetail::CMapDetail(CWorld *pWorld)
{
	m_pWorld = pWorld;
	m_pMap = nullptr;
	m_Crc = 0;
	m_pData = nullptr;
	m_Size = 0;
}

CMapDetail::~CMapDetail()
{
	Unload();
}

static void MapStoragePath(char *pBuf, int BufSize, const char *pMap)
{
	if(!pBuf || BufSize <= 0 || !pMap || !pMap[0])
	{
		if(BufSize > 0)
			pBuf[0] = 0;
		return;
	}
	if(str_endswith(pMap, ".map"))
		str_format(pBuf, BufSize, "maps/%s", pMap);
	else
		str_format(pBuf, BufSize, "maps/%s.map", pMap);
}

bool CMapDetail::Load(IStorage *pStorage)
{
	if(!m_pWorld || !pStorage)
		return false;

	char aBuf[IO_MAX_PATH_LENGTH];
	MapStoragePath(aBuf, sizeof(aBuf), m_pWorld->GetPath());

	if(!m_pMap)
		m_pMap = CreateEngineMap();
	if(!m_pMap->Load(aBuf))
		return false;

	if(m_pData)
	{
		free(m_pData);
		m_pData = nullptr;
	}
	m_Size = 0;
	m_aSha256 = m_pMap->Sha256();
	m_Crc = m_pMap->Crc();

	void *pData = nullptr;
	unsigned DataSize = 0;
	if(pStorage->ReadFile(aBuf, IStorage::TYPE_ALL, &pData, &DataSize) && pData)
	{
		m_pData = (unsigned char *)pData;
		m_Size = DataSize;
	}
	return m_pMap->IsLoaded() && m_pData != nullptr;
}

void CMapDetail::Unload()
{
	if(m_pMap && m_pMap->IsLoaded())
		m_pMap->Unload();
	m_Size = 0;
	m_Crc = 0;
	m_aSha256 = SHA256_ZEROED;
	if(m_pData)
	{
		free(m_pData);
		m_pData = nullptr;
	}
}

CWorld::CWorld(int ID, const char *pName, const char *pPath, const CWorldDetail &Detail)
{
	m_ID = ID;
	m_Detail = Detail;
	str_copy(m_aName, pName, sizeof(m_aName));
	str_copy(m_aPath, pPath, sizeof(m_aPath));
	m_pGameServer = nullptr;
	m_pMapDetail = new CMapDetail(this);
}

CWorld::~CWorld()
{
	delete m_pGameServer;
	delete m_pMapDetail;
}

CMultiWorlds::CMultiWorlds()
{
	m_NumInitialized = 0;
	m_NextIsReloading = false;
	for(int i = 0; i < ENGINE_MAX_WORLDS; i++)
		m_apWorlds[i] = nullptr;
}

CMultiWorlds::~CMultiWorlds()
{
	Clear(true);
}

bool CMultiWorlds::Init(CWorld *pNewWorld, IKernel *pKernel)
{
	if(!pNewWorld || !pKernel)
		return false;

	pNewWorld->m_pGameServer = CreateGameServer();
	pNewWorld->m_pGameServer->SetWorldID(pNewWorld->m_ID);

	bool RegisterFail = false;
	if(m_NextIsReloading)
	{
		RegisterFail = RegisterFail || !pKernel->ReregisterInterface(pNewWorld->m_pMapDetail->m_pMap, pNewWorld->m_ID);
		RegisterFail = RegisterFail || !pKernel->ReregisterInterface(static_cast<IMap *>(pNewWorld->m_pMapDetail->m_pMap), pNewWorld->m_ID);
		RegisterFail = RegisterFail || !pKernel->ReregisterInterface(pNewWorld->m_pGameServer, pNewWorld->m_ID);
	}
	else
	{
		pNewWorld->m_pMapDetail->m_pMap = CreateEngineMap();
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pNewWorld->m_pMapDetail->m_pMap, pNewWorld->m_ID);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap *>(pNewWorld->m_pMapDetail->m_pMap), pNewWorld->m_ID);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pNewWorld->m_pGameServer, pNewWorld->m_ID);
	}

	if(RegisterFail)
		return false;

	m_NumInitialized++;
	return true;
}

void CMultiWorlds::Clear(bool Shutdown)
{
	for(int i = 0; i < ENGINE_MAX_WORLDS; i++)
	{
		if(!m_apWorlds[i])
			continue;
		if(Shutdown)
		{
			delete m_apWorlds[i];
			m_apWorlds[i] = nullptr;
			continue;
		}
		m_apWorlds[i]->m_pMapDetail->Unload();
		delete m_apWorlds[i]->m_pGameServer;
		m_apWorlds[i]->m_pGameServer = nullptr;
	}
	if(Shutdown)
		m_NumInitialized = 0;
}

bool CMultiWorlds::LoadFromJson(IKernel *pKernel, IStorage *pStorage, const char *pJsonPath)
{
	if(!pKernel || !pStorage || !pJsonPath)
		return false;

	Clear(false);
	m_NumInitialized = 0;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile(pJsonPath, pStorage);
	if(!pRoot || pRoot->type != json_object)
	{
		dbg_msg("multiworld", "failed to parse %s: %s", pJsonPath, Parser.Error());
		return false;
	}

	const json_value &Arr = (*pRoot)["worlds"];
	if(Arr.type != json_array || Arr.u.array.length == 0)
	{
		dbg_msg("multiworld", "%s: missing worlds array", pJsonPath);
		return false;
	}

	const unsigned Count = Arr.u.array.length < (unsigned)ENGINE_MAX_WORLDS ? (unsigned)Arr.u.array.length : (unsigned)ENGINE_MAX_WORLDS;
	for(unsigned i = 0; i < Count; i++)
	{
		const json_value &El = Arr[(int)i];
		if(El.type != json_object)
			continue;

		char aMap[128] = {0};
		char aTitle[128] = {0};
		if(El["map"].type == json_string)
			str_copy(aMap, El["map"].u.string.ptr, sizeof(aMap));
		if(!aMap[0])
			continue;
		if(El["title"].type == json_string)
			str_copy(aTitle, El["title"].u.string.ptr, sizeof(aTitle));
		if(!aTitle[0])
			str_copy(aTitle, aMap, sizeof(aTitle));

		const char *pMode = "defence";
		if(El["mode"].type == json_string)
			pMode = El["mode"].u.string.ptr;

		bool TravelLocked = false;
		if(El["travel_locked"].type == json_boolean)
			TravelLocked = El["travel_locked"].u.boolean != 0;
		else if(WorldTypeFromString(pMode) != WorldType::Story)
			TravelLocked = true;

		char aRequiredQuest[32] = {0};
		if(El["required_quest"].type == json_string)
			str_copy(aRequiredQuest, El["required_quest"].u.string.ptr, sizeof(aRequiredQuest));

		const int WorldID = (int)i;
		const CWorldDetail Detail(WorldTypeFromString(pMode), 0, 0, 0, TravelLocked, aRequiredQuest);
		m_apWorlds[WorldID] = new CWorld(WorldID, aTitle, aMap, Detail);
		if(!Init(m_apWorlds[WorldID], pKernel))
		{
			dbg_msg("multiworld", "failed to init world %d (%s)", WorldID, aTitle);
			Clear(true);
			return false;
		}
		if(!m_apWorlds[WorldID]->MapDetail()->Load(pStorage))
		{
			dbg_msg("multiworld", "failed to load map maps/%s for world %d", aMap, WorldID);
			Clear(true);
			return false;
		}
		dbg_msg("multiworld", "world %d: %s mode=%s (maps/%s)", WorldID, aTitle, WorldTypeName(Detail.GetType()), aMap);
	}

	m_NextIsReloading = true;
	return m_NumInitialized > 0;
}

const char *CMultiWorlds::GetWorldName(int WorldID) const
{
	CWorld *pW = GetWorld(WorldID);
	return pW ? pW->GetName() : "";
}
