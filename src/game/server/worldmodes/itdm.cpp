#include "itdm.h"

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

CGameControllerITDM::CGameControllerITDM(CGameContext *pGameServer)
	: CGameControllerArena(pGameServer)
{
}

void CGameControllerITDM::OnCharacterSpawn(CCharacter *pChr)
{
	if(!pChr || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy())
		return;

	const int aWeapons[] = {WEAPON_LASER};
	const int aAmmo[] = {3};
	StripToWeapons(pChr, aWeapons, 1, aAmmo);
	pChr->SetHealthDirect(1);
	pChr->ReduceArmor(10);
}

void CGameControllerITDM::PreTick()
{
	const int WorldID = GameServer()->GetWorldID();
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(Server()->GetClientWorldID(i) != WorldID)
			continue;
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(!pPlayer || !pPlayer->GetCharacter())
			continue;
		CCharacter *pChr = pPlayer->GetCharacter();
		if(pChr->WeaponAmmo(WEAPON_LASER) < 3)
			pChr->GiveWeapon(WEAPON_LASER, 3);
	}
}

int CGameControllerITDM::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	if(pKiller && pVictim && Weapon != WEAPON_GAME)
	{
		if(pKiller == pVictim->GetPlayer() || pKiller->GetTeam() == pVictim->GetPlayer()->GetTeam())
			pKiller->m_Score--;
		else
			pKiller->m_Score++;
	}

	if(pVictim && pVictim->GetPlayer())
		pVictim->GetPlayer()->m_RespawnTick = maximum(pVictim->GetPlayer()->m_RespawnTick, Server()->Tick() + Server()->TickSpeed() * 2);

	return 0;
}
