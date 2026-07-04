#ifndef GAME_SERVER_COMPONENT_QUEST_STEP_DATA_INFO_H
#define GAME_SERVER_COMPONENT_QUEST_STEP_DATA_INFO_H

#include <base/tl/array.h>
#include <base/vmath.h>
#include <unordered_map>
#include <string>
#include <deque>

class CPlayer;

struct SQuestBotInfo
{
	int m_ID{};
	int m_QuestID{};
	int m_BotID{};
	int m_StepPos{};
	vec2 m_Position{};
	int m_WorldID{};
	struct SRequiredDefeat
	{
		int m_BotID{};
		int m_RequiredCount{};
		char m_ZoneName[64] = {}; // optional zone constraint
	};
	array<SRequiredDefeat> m_vRequiredDefeats{};

	struct SRequiredItem
	{
		int m_ItemID{};
		int m_Value{};
		enum { TYPE_SHOW, TYPE_GIVE } m_Type{};
	};
	array<SRequiredItem> m_vRequiredItems{};

	struct SMoveAction
	{
		int m_Step{};
		char m_TaskName[64]{};
		vec2 m_Position{};
		int m_WorldID{};
		unsigned m_TypeFlags{};
		int m_Cooldown{};
	};
	array<SMoveAction> m_vRequiredMoveAction{};

	struct SRequiredDialog
	{
		char m_aNpcId[64]{};
	};
	array<SRequiredDialog> m_vRequiredDialogs{};

	array<int> m_RewardItems{};

	bool HasAction() const
	{
		return m_vRequiredDefeats.size() > 0
			|| m_vRequiredItems.size() > 0
			|| m_vRequiredMoveAction.size() > 0
			|| m_vRequiredDialogs.size() > 0;
	}
	char m_ScenarioJson[512]{};

	const char* GetName() const { return ""; }
};

class CQuestStepBase
{
public:
	CQuestStepBase() = default;
	virtual ~CQuestStepBase() = default;

	SQuestBotInfo m_Bot{};
	virtual void UpdateBot() const;

private:
	bool IsActiveStep() const;
};

class CEntity;
class CEntityDirNavigator;
class CGameContext;
class CQuestStep : public CQuestStepBase
{
	class CGameContext* m_pGameServer{};
	class CGameContext* GS() const { return m_pGameServer; }
	class CPlayer* GetPlayer() const;

	CEntityDirNavigator* m_pEntNavigator{};
	struct SMobProgressStatus
	{
		int m_Count{};
		bool m_Complete{};
		bool m_DialogDone{};
	};

public:
	CQuestStep(class CGameContext* pGameServer, int ClientID, const SQuestBotInfo& Bot) : m_pGameServer(pGameServer), m_ClientID(ClientID)
	{
		m_Bot = Bot;
	}
	~CQuestStep() override;

	std::unordered_map<int, SMobProgressStatus> m_aMobProgress{};
	std::unordered_map<std::string, SMobProgressStatus> m_aDialogProgress{};
	std::deque<bool> m_aMoveActionProgress{};

	int m_ClientID{};
	bool m_StepComplete{};
	bool m_MarkedForDestroy{};
	bool m_TaskListReceived{};

	bool IsComplete();
	bool Finish(bool Force = false);
	void PostFinish();
	bool TryAutoFinish();

	void AppendDefeatProgress(int DefeatedBotID, const char *pZoneName = nullptr);
	void FormatStringTasks(char* aBufQuestTask, int Size);

	void UpdateNavigator();
	void UpdateObjectives();
	void Update();

	void MarkForDestroy();
	void ClearObjectives();

	void StartScenario();
	void CompleteDialog(const char* pNpcId);

	int GetMoveActionCurrentStepPos() const;

	array<CEntity*> m_vpEntitiesMoveAction{};
	array<CEntityDirNavigator*> m_vpEntitiesDefeatBotNavigator{};
};

#endif
