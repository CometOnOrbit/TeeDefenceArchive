#ifndef GAME_SERVER_COMPONENT_QUEST_DATA_H
#define GAME_SERVER_COMPONENT_QUEST_DATA_H

#include <base/tl/array.h>
#include <base/tl/hashtable.h>
#include <map>
#include <deque>
#include <optional>
#include <engine/shared/protocol.h>

#include "datafile_progress.h"
#include "quest_step_data.h"

#define PRINT_QUEST_PREFIX "quest_system"

class CGameContext;
class CPlayer;
using QuestIdentifier = int;

enum QuestState
{
	QuestState_NoAccepted,
	QuestState_Accepted,
	QuestState_Finished,
};

enum
{
	QUEST_FLAG_TYPE_MAIN = 1 << 0,
	QUEST_FLAG_TYPE_SIDE = 1 << 1,
	QUEST_FLAG_TYPE_DAILY = 1 << 2,
	QUEST_FLAG_TYPE_WEEKLY = 1 << 3,
	QUEST_FLAG_TYPE_REPEATABLE = 1 << 4,

	QUEST_FLAG_CANT_REFUSE = 1 << 5,
	QUEST_FLAG_NO_ACTIVITY_POINT = 1 << 6,
	QUEST_FLAG_TUTORIAL = 1 << 7,

	QUEST_FLAG_GRANTED_FROM_CHAIN = 1 << 9,
	QUEST_FLAG_GRANTED_FROM_NPC = 1 << 10,
	QUEST_FLAG_GRANTED_FROM_BOARD = 1 << 11,
};

class CQuestDescription
{
public:
	class CReward
	{
		int m_Experience{};
		int m_Gold{};
		int m_Reputation{};

	public:
		struct SRewardItem
		{
			int m_ItemID;
			int m_Count;
		};
		array<SRewardItem> m_RewardItems{};

		CReward() = default;
		void Init(int Experience, int Gold, int Reputation = 0)
		{
			m_Experience = Experience;
			m_Gold = Gold;
			m_Reputation = Reputation;
		}
		int GetExperience() const { return m_Experience; }
		int GetGold() const { return m_Gold; }
		int GetReputation() const { return m_Reputation; }
		void ApplyReward(CGameContext* pGameServer, CPlayer* pPlayer) const;
	};

private:
	QuestIdentifier m_ID{};
	char m_aName[64]{};
	CReward m_Reward{};
	int m_Flags{};
	std::optional<int> m_NextQuestID{};
	std::optional<int> m_PreviousQuestID{};

public:
	CQuestDescription() = default;

	void Init(QuestIdentifier ID, const char* pName, int Gold, int Exp, std::optional<int> NextQuestID, int Reputation = 0)
	{
		m_ID = ID;
		str_copy(m_aName, pName, sizeof(m_aName));
		m_NextQuestID = NextQuestID;
		m_Reward.Init(Exp, Gold, Reputation);
	}

	void InitPrevousQuestID(int QuestID)
	{
		m_PreviousQuestID = QuestID;
	}

	void AddFlag(int Flag) { m_Flags |= Flag; }
	void SetFlags(int Flag) { m_Flags = Flag; }
	int GetFlags() const { return m_Flags; }
	bool HasFlag(int Flag) const { return (m_Flags & Flag) != 0; }

	QuestIdentifier GetID() const { return m_ID; }
	const char* GetName() const { return m_aName; }
	int GetChainLength() const;
	int GetCurrentChainPos() const;
	CQuestDescription* GetNextQuest() const;
	CQuestDescription* GetPreviousQuest() const;
	CReward& Reward() { return m_Reward; }

	bool HasObjectives(int Step);

	void PreparePlayerObjectives(class CGameContext* pGameServer, int Step, int ClientID, std::deque<CQuestStep*>& pElem);
	void ResetPlayerObjectives(std::deque<CQuestStep*>& pElem);

	bool CanBeGrantedByChain() const { return HasFlag(QUEST_FLAG_GRANTED_FROM_CHAIN); }
	bool CanBeGrantedByNPC() const { return HasFlag(QUEST_FLAG_GRANTED_FROM_NPC); }
	bool CanBeGrantedByBoard() const { return HasFlag(QUEST_FLAG_GRANTED_FROM_BOARD); }
	bool CanBeGranted() const { return CanBeGrantedByChain() || CanBeGrantedByNPC() || CanBeGrantedByBoard(); }
	bool CanBeAcceptOrRefuse() const { return CanBeGrantedByBoard(); }

	std::map<int, std::deque<CQuestStepBase>> m_vObjectives;

	static array<CQuestDescription*> ms_aData;
	static CQuestDescription* Find(int ID);
};

class CPlayerQuest
{
	friend class QuestDatafile;
	friend class CQuestManager;

	class CGameContext* m_pGameServer{};
	class CGameContext* GS() const { return m_pGameServer; }
	CPlayer* GetPlayer() const;

	int m_ClientID{};
	QuestIdentifier m_ID{};
	QuestState m_State{};
	int m_Step{};
	std::deque<CQuestStep*> m_vObjectives{};
	QuestDatafile m_Datafile{};

public:
	CPlayerQuest(class CGameContext* pGameServer, QuestIdentifier ID, int ClientID) : m_pGameServer(pGameServer), m_ClientID(ClientID), m_Step(1) { m_ID = ID; }
	~CPlayerQuest();

	void Init(QuestState State)
	{
		m_State = State;
		m_Datafile.Load();
	}

	QuestDatafile& Datafile() { return m_Datafile; }
	CQuestDescription* Info() const;
	QuestIdentifier GetID() const { return m_ID; }
	QuestState GetState() const { return m_State; }
	bool IsCompleted() const { return m_State == QuestState_Finished; }
	bool IsAccepted() const { return m_State == QuestState_Accepted; }
	bool HasUnfinishedObjectives() const;
	int GetStepPos() const { return m_Step; }
	CQuestStep* GetStepByMob(int MobID);

	void SkipObjectivesAndFinish();

	void Update();
	bool Accept(int StartStep = 1);
	bool Restart(int StartStep = 1);
	void Refuse();
	void Reset();

private:
	void UpdateStepProgress();
	void SetNewState(QuestState State);

	static hash_table<QuestIdentifier, CPlayerQuest*, 16> ms_aPlayerQuests[MAX_CLIENTS];
};

#endif