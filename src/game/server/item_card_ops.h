/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ITEM_CARD_OPS_H
#define GAME_SERVER_ITEM_CARD_OPS_H

class CGameContext;
class CPlayer;
struct SAccSyncData;

/** @param pType "Cards" or "Parts". @return true on success. */
bool ItemCardOps_Place(CGameContext *pGame, CPlayer *pP, int HostItemId, const char *pType, int CardId);

/** Remove one card/part stack entry (decrements num or removes row). */
bool ItemCardOps_Separate(CGameContext *pGame, CPlayer *pP, int HostItemId, const char *pType, int CardId);

#endif
