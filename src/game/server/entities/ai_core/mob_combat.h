#ifndef GAME_SERVER_ENTITIES_AI_CORE_MOB_COMBAT_H
#define GAME_SERVER_ENTITIES_AI_CORE_MOB_COMBAT_H

struct SMMOMobDef;
class CCharacterBotAI;

float MobPreferredCombatRange(const SMMOMobDef *pDef, int ActiveWeapon);
void ApplyMobCombatLoadout(CCharacterBotAI *pChr, const SMMOMobDef *pDef);

#endif
