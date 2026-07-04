#ifndef GAME_SERVER_COMPONENT_QUEST_SCENARIO_MANAGER_H
#define GAME_SERVER_COMPONENT_QUEST_SCENARIO_MANAGER_H

#include "scenario_data.h"
#include "scenario_instance.h"

class CGameContext;
class CPlayer;
class TWorldController;
class CMMOManager;

class CScenarioManager
{
CGameContext* m_pGameServer{};
CGameContext* GS() const { return m_pGameServer; }

array<CScenarioInstance*> m_vInstances{};

public:
CScenarioManager(CGameContext* pGameServer);
~CScenarioManager();

void Init();
void Reset();
void Update();

void LoadFromJson(const char* pJsonString);
void LoadFromFile(const char* pFilename);

CScenarioInstance* CreateInstance(int ScenarioID, CPlayer* pPlayer);
	void RemoveInstance(CScenarioInstance* pInstance);
	CScenarioInstance* GetInstance(int ClientID);
	CScenarioInstance* GetInstance(CPlayer* pPlayer) const;

void OnPlayerKill(CPlayer* pVictim, CPlayer* pKiller, int Weapon);
void OnPlayerMove(CPlayer* pPlayer);
void OnPlayerCollectItem(CPlayer* pPlayer, int ItemID, int Count);

TWorldController *GetCore() const;
CMMOManager *GetMMO() const;

void InitScenarios();
};

#endif
