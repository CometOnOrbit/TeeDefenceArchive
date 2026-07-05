#ifndef GAME_SERVER_ENTITIES_SKILLS_SKILL_DETONATION_BEAM_H
#define GAME_SERVER_ENTITIES_SKILLS_SKILL_DETONATION_BEAM_H

#include <base/vmath.h>

class CGameWorld;

void SpawnSkillDetonationBeam(CGameWorld *pWorld, int OwnerCID, vec2 From, vec2 To, int ExplosionRadiusTiles, int ExplosionDamage, bool MiniPull);

#endif
