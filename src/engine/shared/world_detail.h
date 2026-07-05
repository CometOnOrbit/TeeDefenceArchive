#ifndef ENGINE_SHARED_WORLD_DETAIL_H
#define ENGINE_SHARED_WORLD_DETAIL_H

#include <base/system.h>
#include <cstdint>

enum
{
	WORLD_FLAG_NO_PREPARE_MAP = 1 << 0,
};

enum class WorldType
{
	Defence,
	Hub,
	PvP,
	RPG,
	Story,
};

static inline WorldType WorldTypeFromString(const char *pType)
{
	if(!pType || !pType[0])
		return WorldType::Defence;
	if(str_comp_nocase(pType, "hub") == 0 || str_comp_nocase(pType, "social") == 0)
		return WorldType::Hub;
	if(str_comp_nocase(pType, "pvp") == 0 || str_comp_nocase(pType, "fng") == 0 ||
		str_comp_nocase(pType, "tdm") == 0 || str_comp_nocase(pType, "itdm") == 0 ||
		str_comp_nocase(pType, "idm") == 0 || str_comp_nocase(pType, "ctf") == 0)
		return WorldType::PvP;
	if(str_comp_nocase(pType, "rpg") == 0 || str_comp_nocase(pType, "frpg") == 0 ||
		str_comp_nocase(pType, "f|rpg") == 0)
		return WorldType::RPG;
	if(str_comp_nocase(pType, "story") == 0)
		return WorldType::Story;
	return WorldType::Defence;
}

static inline const char *WorldTypeName(WorldType Type)
{
	switch(Type)
	{
	case WorldType::Hub: return "hub";
	case WorldType::PvP: return "pvp";
	case WorldType::RPG: return "frpg";
	case WorldType::Story: return "story";
	default: return "defence";
	}
}

class CWorldDetail
{
	WorldType m_Type;
	int64_t m_Flags;
	int m_RespawnWorldID;
	int m_JailWorldID;
	int m_RequiredLevel;
	bool m_TravelLocked;
	bool m_NoDaytime;
	char m_aRequiredQuest[32];
	char m_aGameMode[16];

public:
	CWorldDetail()
	{
		m_Type = WorldType::Defence;
		m_Flags = 0;
		m_RespawnWorldID = 0;
		m_JailWorldID = 0;
		m_RequiredLevel = 0;
		m_TravelLocked = false;
		m_NoDaytime = false;
		m_aRequiredQuest[0] = 0;
		m_aGameMode[0] = 0;
	}

	CWorldDetail(WorldType Type, int RespawnWorldID, int JailWorldID, int RequiredLevel, bool TravelLocked = false, const char *pRequiredQuest = nullptr, int64_t Flags = 0)
	{
		m_Type = Type;
		m_Flags = Flags;
		m_RespawnWorldID = RespawnWorldID;
		m_JailWorldID = JailWorldID;
		m_RequiredLevel = RequiredLevel;
		m_TravelLocked = TravelLocked;
		m_NoDaytime = false;
		m_aRequiredQuest[0] = 0;
		m_aGameMode[0] = 0;
		if(pRequiredQuest && pRequiredQuest[0])
			str_copy(m_aRequiredQuest, pRequiredQuest, sizeof(m_aRequiredQuest));
	}

	void SetGameMode(const char *pMode)
	{
		if(!pMode || !pMode[0])
		{
			m_aGameMode[0] = 0;
			return;
		}
		str_copy(m_aGameMode, pMode, sizeof(m_aGameMode));
	}

	const char *GetGameMode() const { return m_aGameMode; }

	WorldType GetType() const { return m_Type; }
	int GetRespawnWorldID() const { return m_RespawnWorldID; }
	int GetJailWorldID() const { return m_JailWorldID; }
	int GetRequiredLevel() const { return m_RequiredLevel; }
	bool GetTravelLocked() const { return m_TravelLocked; }
	bool GetNoDaytime() const { return m_NoDaytime; }
	const char *GetRequiredQuest() const { return m_aRequiredQuest; }

	bool HasFlag(int64_t Flag) const { return (m_Flags & Flag) != 0; }
	void SetFlag(int64_t Flag) { m_Flags |= Flag; }
};

#endif
