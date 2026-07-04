#ifndef GAME_SERVER_COMPONENT_QUEST_MANAGER_H
#define GAME_SERVER_COMPONENT_QUEST_MANAGER_H

#include <game/server/core/tworld_component.h>
#include "quest_data.h"
#include "quest_board_data.h"
#include "scenario_manager.h"

class CPlayer;
class CCharacter;

class CQuestManager : public TWorldComponent
{
void InitQuestStepDefeats(CQuestDescription* pQuest, int Step, int BotID, int RequiredCount, const char *pZoneName = nullptr);
void InitQuestStepMoveAction(CQuestDescription* pQuest, int Step, vec2 Pos, int WorldID, const char* pTaskName);
void InitQuestStepItems(CQuestDescription* pQuest, int Step, int ItemID, int Value, int Type);
void InitQuestStepDialog(CQuestDescription* pQuest, int Step, const char* pNpcId, vec2 NpcPos, int NpcWorld);
void InitQuestStepScenario(CQuestDescription* pQuest, int Step, const char* pScenarioJson);

CScenarioManager* m_pScenarioManager{};

public:
CQuestManager() = default;

void OnPreInit() override;
void OnInitWorld(const char* pWhereLocalWorld) override;
void OnPlayerLogin(CPlayer* pPlayer) override;
void OnTick() override;
void OnCharacterSpawn(CPlayer* pPlayer) override;

void Init();
void Reset();

// JSON quest definitions (server_content/quests.json)
void LoadQuestDefs();
void InitHardcodedQuests();

void ShowQuestList(CPlayer* pPlayer) const;
void ShowQuestBoardList(CPlayer* pPlayer) const;

void AcceptQuest(CPlayer* pPlayer, int QuestID);
void RefuseQuest(CPlayer* pPlayer, int QuestID);
void RestartQuest(CPlayer* pPlayer, int QuestID);

void TryAcceptNextQuestChain(CPlayer* pPlayer, int BaseQuestID) const;

CPlayerQuest* GetQuest(int ClientID, int QuestID) const;
array<CPlayerQuest*> GetQuests(int ClientID) const;

void AddBoard(CEntityQuestBoard* pBoard);
void RemoveBoard(CEntityQuestBoard* pBoard);

void OnPlayerKill(CPlayer* pPlayer, int VictimID, const char *pZoneName = nullptr);
void RequestPersist(int ClientID);

void RegisterChatCommands(class CCommandManager* pManager);
void RegisterVoteCommands(class CCommandManager* pManager);

void BuildQuestListPage(int ClientID);
void BuildQuestDetailPage(int ClientID, int QuestIdx);
bool OnVoteMenuPage(int ClientID, int Page) override;

void TryTalkNpc(class CPlayer* pPlayer, const char* pNpcId, float NpcPosX = 0, float NpcPosY = 0, int NpcWorld = -1);
bool HasTravelUnlock(class CPlayer* pPlayer, const char* pQuestName);

CScenarioManager* GetScenarioManager() { return m_pScenarioManager; }

static CPlayerQuest* FindPlayerQuest(int ClientID, int QuestID);

array<CEntityQuestBoard*> m_vpBoards{};

private:
void InitQuests();
void InitQuestBoards();

void UpdatePlayerQuests();
void UpdatePlayerObjectives();
};

#endif
