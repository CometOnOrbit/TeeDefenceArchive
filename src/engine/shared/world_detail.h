#ifndef ENGINE_SHARED_WORLD_DETAIL_H
#define ENGINE_SHARED_WORLD_DETAIL_H

#include <base/system.h>

enum class WorldType
{
	Defence,
	Hub,
	PvP,
};

static inline WorldType WorldTypeFromString(const char *pType)
{
	if(!pType || !pType[0])
		return WorldType::Defence;
	if(str_comp_nocase(pType, "hub") == 0 || str_comp_nocase(pType, "social") == 0)
		return WorldType::Hub;
	if(str_comp_nocase(pType, "pvp") == 0)
		return WorldType::PvP;
	return WorldType::Defence;
}

static inline const char *WorldTypeName(WorldType Type)
{
	switch(Type)
	{
	case WorldType::Hub: return "hub";
	case WorldType::PvP: return "pvp";
	default: return "defence";
	}
}

class CWorldDetail
{
	WorldType m_Type;
	int m_RespawnWorldID;
	int m_JailWorldID;
	int m_RequiredLevel;

public:
	CWorldDetail()
	{
		m_Type = WorldType::Defence;
		m_RespawnWorldID = 0;
		m_JailWorldID = 0;
		m_RequiredLevel = 0;
	}

	CWorldDetail(WorldType Type, int RespawnWorldID, int JailWorldID, int RequiredLevel)
	{
		m_Type = Type;
		m_RespawnWorldID = RespawnWorldID;
		m_JailWorldID = JailWorldID;
		m_RequiredLevel = RequiredLevel;
	}

	WorldType GetType() const { return m_Type; }
	int GetRespawnWorldID() const { return m_RespawnWorldID; }
	int GetJailWorldID() const { return m_JailWorldID; }
	int GetRequiredLevel() const { return m_RequiredLevel; }
};

#endif
