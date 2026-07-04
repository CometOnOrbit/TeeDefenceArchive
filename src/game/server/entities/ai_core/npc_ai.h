#ifndef GAME_SERVER_ENTITIES_AI_CORE_NPC_AI_H
#define GAME_SERVER_ENTITIES_AI_CORE_NPC_AI_H

#include "base_ai.h"
#include <game/server/core/components/mmo/mmo_types.h>

class CNpcAI final : public CBaseAI
{
	SMMONpcInfo m_NpcInfo{};

	int m_DefaultMoveDirection {};
	int m_DefaultMoveNextTick {};

public:
	CNpcAI(CCharacterBotAI *pCharacter, const SMMONpcInfo &NpcInfo);
	CNpcAI(CCharacterBotAI *pCharacter, EMMONpcFunction Function);

	bool CanDamage(CPlayer *pFrom) override;

	void OnSpawn() override;
	void OnTakeDamage(int Dmg, int From, int Weapon) override;
	void OnTargetRules(float Radius) override;
	void Process() override;
	bool IsConversational() override;

private:
	void ProcessGuardianNPC();
	void ProcessDefaultNPC();
	void UpdateDefaultMovementDirection(bool HasPlayerNearby, bool HasGroundAhead);
};

#endif
