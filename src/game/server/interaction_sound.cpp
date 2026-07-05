#include "interaction_sound.h"

#include <generated/server_data.h>

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

void PlayInteractionSound(CGameWorld &World, CPlayer *pPlayer, int Sound)
{
	if(!pPlayer || Sound < 0)
		return;
	if(CCharacter *pChr = pPlayer->GetCharacter())
		World.CreateSound(pChr->GetPos(), Sound, CmaskOne(pPlayer->GetCID()));
	else
		World.CreatePlayerSound(pPlayer->GetCID(), Sound);
}

void PlayUiMenuOpen(CGameWorld &World, int ClientID)
{
	World.CreatePlayerSound(ClientID, SOUND_UI_MENU_CLICK);
}

void PlayUiMenuSelect(CGameWorld &World, int ClientID)
{
	World.CreatePlayerSound(ClientID, SOUND_UI_MENU_ITEM_CLICK);
}
