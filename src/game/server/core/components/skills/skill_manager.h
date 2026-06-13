#ifndef GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_MANAGER_H
#define GAME_SERVER_CORE_COMPONENTS_SKILLS_SKILL_MANAGER_H

#include <engine/shared/protocol.h>

#include <game/server/core/tworld_component.h>

#include "skill_data.h"

class CCommandManager;
class CPlayer;

class CSkillManager : public TWorldComponent
{
	SSkillDescription m_aSkills[MAX_SKILLS];
	int m_NumSkills;
	SSkillInstance m_aaInstances[MAX_CLIENTS][MAX_SKILLS];

public:
	CSkillManager();

	void OnInitWorld(const char *pWhereLocalWorld) override;
	void OnClientReset(int ClientID) override;
	void OnPlayerLogin(CPlayer *pPlayer) override;

	void RegisterChatCommands(CCommandManager *pMgr);
	void RegisterVoteCommands(CCommandManager *pMgr);

	void BuildSkillsListPage(int ClientID);
	void BuildSkillDetailPage(int ClientID, int SkillId);
	void UseSkillsByEmoticon(CPlayer *pPlayer, int EmoticonId);
	int ResolveSkillIdFromArg(const char *pArg) const;

	SSkillInstance *GetInstance(CPlayer *pPlayer, int SkillId);
	const SSkillDescription *FindDescription(int SkillId) const;
	int FindDescriptionIndex(int SkillId) const;
	bool Use(CPlayer *pPlayer, int SkillId);
	bool Learn(CPlayer *pPlayer, int SkillId);
	void CycleEmoticonBind(CPlayer *pPlayer, int SkillId);
	int GetEmoticonBindForClient(int ClientID, int SkillIdx) const;
	void SetEmoticonBindForClient(int ClientID, int SkillIdx, int Bind);
	void RestoreSkillBinds(CPlayer *pPlayer);

private:
	void LoadSkills();
	void ResetClientSkills(int ClientID);
	void AutoLearnForPlayer(CPlayer *pPlayer);
	bool ExecuteSkill(CPlayer *pPlayer, const SSkillDescription &Def);
	void SerializeSkillBindsForSave(int ClientID, char *pOut, int OutLen) const;
};

#endif
