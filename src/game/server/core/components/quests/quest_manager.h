#ifndef GAME_SERVER_CORE_COMPONENTS_QUESTS_QUEST_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_QUESTS_QUEST_MANAGER_H

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/server/core/tworld_component.h>

class CCommandManager;
class CPlayer;

enum
{
	MAX_QUESTS = 32,
	MAX_QUEST_STEPS = 8,
	MAX_QUEST_UNLOCKS = 8,
	MAX_PLAYER_UNLOCKS = 32,
	QUEST_KEY_LEN = 32,
};

struct SQuestStepDef
{
	char m_aType[24];
	char m_aNpc[QUEST_KEY_LEN];
	int m_World;
	float m_X;
	float m_Y;
	float m_Radius;
	int m_Count;
	int m_ItemId;
};

struct SQuestDef
{
	char m_aId[QUEST_KEY_LEN];
	char m_aTitleKey[48];
	bool m_AutoGrant;
	int m_NumSteps;
	SQuestStepDef m_aSteps[MAX_QUEST_STEPS];
	int m_NumUnlocks;
	char m_aaUnlocks[MAX_QUEST_UNLOCKS][QUEST_KEY_LEN];
	int m_RewardItem;
	int m_RewardNum;
	char m_aNextQuest[QUEST_KEY_LEN];
};

struct SPlayerQuestState
{
	bool m_Active;
	bool m_Completed;
	int m_Step;
	int m_SubProgress;
};

class CQuestManager : public TWorldComponent
{
	SQuestDef m_aQuests[MAX_QUESTS];
	int m_NumQuests;
	SPlayerQuestState m_aaState[MAX_CLIENTS][MAX_QUESTS];
	char m_aaaUnlocks[MAX_CLIENTS][MAX_PLAYER_UNLOCKS][QUEST_KEY_LEN];
	int m_aNumUnlocks[MAX_CLIENTS];

public:
	CQuestManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnPlayerLogin(CPlayer *pPlayer) override;
	void OnClientReset(int ClientID) override;
	void OnCharacterSpawn(CPlayer *pPlayer) override;
	void OnTick() override;

	void RegisterChatCommands(CCommandManager *pMgr);
	void RegisterVoteCommands(CCommandManager *pMgr);

	void BuildQuestListPage(int ClientID);
	void BuildQuestDetailPage(int ClientID, int QuestIdx);

	void TryReachProgress(CPlayer *pPlayer, vec2 Pos);
	void TryKillProgress(CPlayer *pPlayer);
	void TryCollectProgress(CPlayer *pPlayer);
	bool TryTalkNpc(CPlayer *pPlayer, const char *pNpcId);
	void RequestPersist(int ClientID);

	bool HasTravelUnlock(CPlayer *pPlayer, const char *pKey) const;
	bool IsQuestCompleted(CPlayer *pPlayer, const char *pQuestId) const;
	void LoadPlayerData(int ClientID, const char *pJson);
	void SavePlayerData(int ClientID, char *pOut, int OutSize) const;

private:
	void LoadQuests();
	int FindQuestIndex(const char *pId) const;
	void GrantAutoQuests(CPlayer *pPlayer);
	void AdvanceStep(CPlayer *pPlayer, int QuestIdx);
	void CompleteQuest(CPlayer *pPlayer, int QuestIdx);
	void AddUnlock(CPlayer *pPlayer, const char *pKey);
	void ActivateQuest(CPlayer *pPlayer, const char *pQuestId);
	bool HasUnlock(int ClientID, const char *pKey) const;
	bool StepMatches(const SQuestStepDef &Step, CPlayer *pPlayer, vec2 Pos, const char *pNpcId, bool Talk, bool Kill, bool Collect) const;
};

#endif
