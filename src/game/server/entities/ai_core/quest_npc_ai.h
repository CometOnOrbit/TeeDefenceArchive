#ifndef GAME_SERVER_ENTITIES_AI_CORE_QUEST_NPC_AI_H
#define GAME_SERVER_ENTITIES_AI_CORE_QUEST_NPC_AI_H

#include "base_ai.h"
#include <game/server/core/components/mmo/mmo_types.h>

class CQuestNpcAI final : public CBaseAI
{
	SMMOQuestNpcInfo *m_pQuestNpcInfo {};

public:
	CQuestNpcAI(CCharacterBotAI *pCharacter, SMMOQuestNpcInfo *pQuestNpcInfo);

	bool CanDamage(CPlayer *pFrom) override;

	void OnSpawn() override;
	void Process() override;

	bool IsConversational() override;
};

#endif
