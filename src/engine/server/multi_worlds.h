#ifndef ENGINE_SERVER_MULTI_WORLDS_H
#define ENGINE_SERVER_MULTI_WORLDS_H

#include <base/hash.h>
#include <engine/shared/world_detail.h>
#include <engine/shared/protocol.h>

class IKernel;
class IEngineMap;
class IStorage;
class IGameServer;
class CWorld;

class CMapDetail
{
	friend class CMultiWorlds;

	CWorld *m_pWorld;
	IEngineMap *m_pMap;
	SHA256_DIGEST m_aSha256;
	unsigned m_Crc;
	unsigned char *m_pData;
	unsigned m_Size;

public:
	CMapDetail(CWorld *pWorld);
	~CMapDetail();

	bool Load(IStorage *pStorage);
	void Unload();

	IEngineMap *GetMap() const { return m_pMap; }
	bool IsLoaded() const { return m_pMap != nullptr; }
	unsigned GetCrc() const { return m_Crc; }
	const SHA256_DIGEST &GetSha256() const { return m_aSha256; }
	unsigned char *GetData() const { return m_pData; }
	unsigned GetSize() const { return m_Size; }
};

class CWorld
{
	friend class CMultiWorlds;

	int m_ID;
	char m_aName[64];
	char m_aPath[128];
	IGameServer *m_pGameServer;
	CMapDetail *m_pMapDetail;
	CWorldDetail m_Detail;

public:
	CWorld(int ID, const char *pName, const char *pPath, const CWorldDetail &Detail);
	~CWorld();

	IGameServer *GameServer() const { return m_pGameServer; }
	CMapDetail *MapDetail() const { return m_pMapDetail; }
	const char *GetName() const { return m_aName; }
	const char *GetPath() const { return m_aPath; }
	int GetID() const { return m_ID; }
	CWorldDetail *GetDetail() { return &m_Detail; }
	const CWorldDetail *GetDetail() const { return &m_Detail; }
};

class CMultiWorlds
{
	int m_NumInitialized;
	bool m_NextIsReloading;
	CWorld *m_apWorlds[ENGINE_MAX_WORLDS];

	bool Init(CWorld *pNewWorld, IKernel *pKernel);
	void Clear(bool Shutdown);

public:
	CMultiWorlds();
	~CMultiWorlds();

	bool LoadFromJson(IKernel *pKernel, IStorage *pStorage, const char *pJsonPath);

	CWorld *GetWorld(int WorldID) const
	{
		if(WorldID < 0 || WorldID >= ENGINE_MAX_WORLDS)
			return nullptr;
		return m_apWorlds[WorldID];
	}

	bool IsValid(int WorldID) const
	{
		return WorldID >= 0 && WorldID < ENGINE_MAX_WORLDS && m_apWorlds[WorldID] && m_apWorlds[WorldID]->GameServer();
	}

	int GetSizeInitialized() const { return m_NumInitialized; }
	int GetWorldCount() const { return m_NumInitialized; }
	const char *GetWorldName(int WorldID) const;
};

#endif
