#ifndef GAME_SERVER_CORE_COMPONENTS_QUESTS_DATAFILE_PROGRESS_H
#define GAME_SERVER_CORE_COMPONENTS_QUESTS_DATAFILE_PROGRESS_H

class CPlayerQuest;
class QuestDatafile
{
	CPlayerQuest* m_pQuest{};

public:
	void Init(CPlayerQuest* pQuest) { m_pQuest = pQuest; }
	void Create() const;
	void Load() const;
	bool Save() const;
	void Delete() const;
	const char* GetFilename(char* pBuf, int BufSize) const;
};

#endif