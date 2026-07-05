#ifndef GAME_SERVER_ENTITIES_AI_CORE_MOB_ABILITY_EXECUTOR_H
#define GAME_SERVER_ENTITIES_AI_CORE_MOB_ABILITY_EXECUTOR_H

struct SMMOMobAbilityDef;
class CCharacterBotAI;

bool ExecuteMobAbility(CCharacterBotAI *pCaster, const SMMOMobAbilityDef &Ability, int MobAttack, int MobLevel);

#endif
