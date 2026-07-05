#ifndef GAME_SERVER_ENTITIES_MMO_MMO_WEAPON_COMMON_H
#define GAME_SERVER_ENTITIES_MMO_MMO_WEAPON_COMMON_H

#include <base/vmath.h>

class CGameContext;
class CCharacter;
struct SMMOWeaponProfile;

bool MMOWeaponTargetValid(CGameContext *pGS, int OwnerCID, CCharacter *pTarget);
CCharacter *MMOWeaponOwnerChar(CGameContext *pGS, int OwnerCID);

void FireMMOLightningBolts(CGameContext *pGS, vec2 Pos, vec2 Dir, int OwnerCID, int Damage, const SMMOWeaponProfile *pProf);
void FireMMOElectroArc(CGameContext *pGS, CCharacter *pOwner, vec2 Start, vec2 Dir, int Damage, const SMMOWeaponProfile *pProf);

#endif
