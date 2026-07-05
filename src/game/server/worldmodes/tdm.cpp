#include "tdm.h"

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CGameControllerTDM::CGameControllerTDM(CGameContext *pGameServer)
	: CGameControllerArena(pGameServer)
{
}

void CGameControllerTDM::OnCharacterSpawn(CCharacter *pChr)
{
	if(!pChr || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy())
		return;

	const int aWeapons[] = {WEAPON_HAMMER, WEAPON_GUN, WEAPON_SHOTGUN, WEAPON_GRENADE, WEAPON_LASER};
	const int aAmmo[] = {-1, 10, 10, 10, 10};
	StripToWeapons(pChr, aWeapons, 5, aAmmo);
	pChr->SetHealthDirect(10);
}

int CGameControllerTDM::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	if(pKiller && pVictim && Weapon != WEAPON_GAME)
	{
		if(pKiller == pVictim->GetPlayer() || pKiller->GetTeam() == pVictim->GetPlayer()->GetTeam())
			pKiller->m_Score--;
		else
			pKiller->m_Score++;
	}

	if(pVictim && pVictim->GetPlayer())
		pVictim->GetPlayer()->m_RespawnTick = maximum(pVictim->GetPlayer()->m_RespawnTick, Server()->Tick() + Server()->TickSpeed() * 3);

	return 0;
}
