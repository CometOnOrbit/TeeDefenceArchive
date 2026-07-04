#ifndef GAME_SERVER_ENTITIES_AI_CORE_QUEST_MOB_AI_H
#define GAME_SERVER_ENTITIES_AI_CORE_QUEST_MOB_AI_H

#include "base_ai.h"
#include <game/server/core/components/mmo/mmo_types.h>

class CQuestMobAI final : public CBaseAI
{
	SMMOQuestMobInfo *m_pQuestMobInfo {};

public:
	CQuestMobAI(CCharacterBotAI *pCharacter, SMMOQuestMobInfo *pQuestMobInfo);

	bool CanDamage(CPlayer *pFrom) override;

	void OnSpawn() override;
	void OnRewardPlayer(CPlayer *pPlayer) const override;
	void OnDie(int Killer, int Weapon) override;
	void OnTargetRules(float Radius) override;
	void Process() override;
};

#endif
