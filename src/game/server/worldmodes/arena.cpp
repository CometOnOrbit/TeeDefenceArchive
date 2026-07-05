#include "arena.h"

#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CGameControllerArena::CGameControllerArena(CGameContext *pGameServer)
	: CGameControllerHub(pGameServer)
{
}

bool CGameControllerArena::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	(void)Damage;
	if(ClientID1 == ClientID2)
		return false;

	CPlayer *p1 = GameServer()->m_apPlayers[ClientID1];
	CPlayer *p2 = GameServer()->m_apPlayers[ClientID2];
	if(!p1 || !p2 || p1->IsDummy() || p2->IsDummy())
		return CGameControllerHub::IsFriendlyFire(ClientID1, ClientID2, Damage);

	return p1->GetTeam() == p2->GetTeam() && p1->GetTeam() > TEAM_SPECTATORS;
}

bool CGameControllerArena::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	(void)Damage;
	return Team1 == Team2 && Team1 > TEAM_SPECTATORS;
}

bool CGameControllerArena::OnEntity(int Index, vec2 Pos)
{
	if(Index >= ENTITY_LOG)
		return true;
	return CGameController::OnEntity(Index, Pos);
}

void CGameControllerArena::StripToWeapons(CCharacter *pChr, const int *pWeapons, int NumWeapons, const int *pAmmo) const
{
	if(!pChr)
		return;

	for(int w = 0; w < NUM_WEAPONS; w++)
		pChr->RemoveWeapon(w);

	for(int i = 0; i < NumWeapons; i++)
	{
		const int Weapon = pWeapons[i];
		const int Ammo = pAmmo ? pAmmo[i] : -1;
		pChr->GiveWeapon(Weapon, Ammo);
	}

	if(NumWeapons > 0)
		pChr->SetWeapon(pWeapons[0]);
}
