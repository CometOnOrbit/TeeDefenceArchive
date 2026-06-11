#include "pvp.h"

#include <engine/shared/config.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CGameControllerPvP::CGameControllerPvP(CGameContext *pGameServer)
	: CGameControllerHub(pGameServer)
{
}

void CGameControllerPvP::OnCharacterSpawn(CCharacter *pChr)
{
	if(!pChr || pChr->GetPlayer()->IsDummy())
	{
		CGameControllerHub::OnCharacterSpawn(pChr);
		return;
	}

	pChr->SetHealthDirect(GameServer()->Config()->m_SvPlayerMaxHealth);
	pChr->GiveWeapon(WEAPON_HAMMER, -1);
	pChr->GiveWeapon(WEAPON_GUN, 10);
	pChr->GiveWeapon(WEAPON_SHOTGUN, 10);
	pChr->GiveWeapon(WEAPON_GRENADE, 10);
	pChr->GiveWeapon(WEAPON_LASER, 10);
}

bool CGameControllerPvP::IsFriendlyFire(int ClientID1, int ClientID2, int Damage) const
{
	(void)Damage;
	if(ClientID1 == ClientID2)
		return false;

	CPlayer *p1 = GameServer()->m_apPlayers[ClientID1];
	CPlayer *p2 = GameServer()->m_apPlayers[ClientID2];
	if(!p1 || !p2)
		return false;

	if(p1->IsDummy() || p2->IsDummy())
		return CGameControllerHub::IsFriendlyFire(ClientID1, ClientID2, Damage);

	return false;
}

bool CGameControllerPvP::IsFriendlyTeamFire(int Team1, int Team2, int Damage) const
{
	(void)Damage;
	(void)Team1;
	(void)Team2;
	return false;
}

bool CGameControllerPvP::OnEntity(int Index, vec2 Pos)
{
	if(Index >= ENTITY_LOG)
		return true;
	return CGameControllerDefence::OnEntity(Index, Pos);
}