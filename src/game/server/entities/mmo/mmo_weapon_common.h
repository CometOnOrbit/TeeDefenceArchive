#ifndef GAME_SERVER_ENTITIES_MMO_MMO_WEAPON_COMMON_H
#define GAME_SERVER_ENTITIES_MMO_MMO_WEAPON_COMMON_H

class CGameContext;
class CCharacter;

bool MMOWeaponTargetValid(CGameContext *pGS, int OwnerCID, CCharacter *pTarget);
CCharacter *MMOWeaponOwnerChar(CGameContext *pGS, int OwnerCID);

#endif
