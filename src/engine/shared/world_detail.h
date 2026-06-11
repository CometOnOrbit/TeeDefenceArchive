#ifndef ENGINE_SHARED_WORLD_DETAIL_H
#define ENGINE_SHARED_WORLD_DETAIL_H

class CWorldDetail
{
	int m_RespawnWorldID;
	int m_JailWorldID;
	int m_RequiredLevel;

public:
	CWorldDetail()
	{
		m_RespawnWorldID = 0;
		m_JailWorldID = 0;
		m_RequiredLevel = 0;
	}

	CWorldDetail(int RespawnWorldID, int JailWorldID, int RequiredLevel)
	{
		m_RespawnWorldID = RespawnWorldID;
		m_JailWorldID = JailWorldID;
		m_RequiredLevel = RequiredLevel;
	}

	int GetRespawnWorldID() const { return m_RespawnWorldID; }
	int GetJailWorldID() const { return m_JailWorldID; }
	int GetRequiredLevel() const { return m_RequiredLevel; }
};

#endif
