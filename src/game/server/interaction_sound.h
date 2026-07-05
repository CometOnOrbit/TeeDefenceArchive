#ifndef GAME_SERVER_INTERACTION_SOUND_H
#define GAME_SERVER_INTERACTION_SOUND_H

class CGameWorld;
class CPlayer;

void PlayInteractionSound(CGameWorld &World, CPlayer *pPlayer, int Sound);
void PlayUiMenuOpen(CGameWorld &World, int ClientID);
void PlayUiMenuSelect(CGameWorld &World, int ClientID);

#endif
