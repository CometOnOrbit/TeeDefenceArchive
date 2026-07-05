#include "fng.h"

#include <cmath>

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

CGameControllerFNG::CGameControllerFNG(CGameContext *pGameServer)
	: CGameControllerArena(pGameServer)
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_HammeredBy[i] = -1;
		m_HookedBy[i] = -1;
		m_IsSaved[i] = false;
		m_LastHookedBy[i] = -1;
	}
}

static CCharacter *ActiveCharacterInWorld(CGameContext *pGS, int CID)
{
	if(!pGS || CID < 0 || CID >= MAX_CLIENTS)
		return nullptr;
	if(pGS->Server()->GetClientWorldID(CID) != pGS->GetWorldID())
		return nullptr;
	CPlayer *pPlayer = pGS->m_apPlayers[CID];
	return pPlayer ? pPlayer->GetCharacter() : nullptr;
}

void CGameControllerFNG::PreTick()
{
	const int WorldID = GameServer()->GetWorldID();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(Server()->GetClientWorldID(i) != WorldID)
			continue;

		if(m_HookedBy[i] >= 0)
		{
			CCharacter *pHooking = ActiveCharacterInWorld(GameServer(), m_HookedBy[i]);
			if(!pHooking || pHooking->GetCore()->m_HookedPlayer != i)
			{
				m_LastHookedBy[i] = m_HookedBy[i];
				m_HookedBy[i] = -1;
			}
		}
		m_IsSaved[i] = false;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacter *pChr = ActiveCharacterInWorld(GameServer(), i);
		if(!pChr)
			continue;

		const int HookedCID = pChr->GetCore()->m_HookedPlayer;
		if(HookedCID >= 0 && m_HookedBy[HookedCID] == -1)
			m_HookedBy[HookedCID] = i;

		if(HookedCID >= 0 && IsFriendlyFire(HookedCID, i, 0))
			m_IsSaved[HookedCID] = true;

		if(m_HammeredBy[i] >= 0 && pChr->IsGrounded())
			m_HammeredBy[i] = -1;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CCharacter *pChr = ActiveCharacterInWorld(GameServer(), i);
		if(!pChr)
			continue;

		if(!pChr->IsFrozen())
		{
			m_IsSaved[i] = false;
			m_HookedBy[i] = -1;
			m_LastHookedBy[i] = -1;
			m_HammeredBy[i] = -1;
			continue;
		}

		const int Seconds = (int)std::ceil(pChr->GetFreezeTicks() / (float)Server()->TickSpeed());
		pChr->SetArmorDirect(Seconds);
	}
}

void CGameControllerFNG::OnCharacterSpawn(CCharacter *pChr)
{
	if(!pChr || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy())
	{
		CGameControllerArena::OnCharacterSpawn(pChr);
		return;
	}

	const int aWeapons[] = {WEAPON_HAMMER, WEAPON_LASER};
	const int aAmmo[] = {-1, -1};
	StripToWeapons(pChr, aWeapons, 2, aAmmo);
	pChr->SetHealthDirect(10);
	pChr->SetAllowFrozenWeaponSwitch(true);

	const int CID = pChr->GetPlayer()->GetCID();
	m_HammeredBy[CID] = -1;
	m_HookedBy[CID] = -1;
	m_LastHookedBy[CID] = -1;
	m_IsSaved[CID] = false;
}

bool CGameControllerFNG::OnCharacterTakeDamage(CCharacter *pChr, vec2 &Force, int &Dmg, int From, int Weapon)
{
	if(!pChr || !pChr->GetPlayer())
		return false;

	const int SelfCID = pChr->GetPlayer()->GetCID();

	if(Weapon == WEAPON_HAMMER)
	{
		if(From >= 0 && IsFriendlyFire(SelfCID, From, 0) && pChr->IsFrozen())
		{
			Force.x *= 0.5f;
			Force.y *= 0.5f;
			pChr->ReduceFreeze(3.0f);
			if(!pChr->IsFrozen())
				GameServer()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_ARMOR);
		}
		else
		{
			Force.x *= 3.2f;
			Force.y *= 1.2f;
			if(pChr->IsFrozen())
				m_HammeredBy[SelfCID] = From;
		}
		pChr->GetCore()->m_Vel += Force;
		return true;
	}

	if(Dmg > 0 && !pChr->IsFrozen() && Weapon != WEAPON_WORLD && Weapon != WEAPON_GAME &&
		From >= 0 && From != SelfCID)
	{
		CPlayer *pAttacker = GameServer()->m_apPlayers[From];
		if(pAttacker && pAttacker->GetCharacter())
			pAttacker->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());

		pChr->Freeze(10.0f, true);
		m_LastHookedBy[SelfCID] = From;
		GameServer()->m_World.CreateDeath(pChr->GetPos(), SelfCID);
		AwardScore(pAttacker, 1, 1);
		return true;
	}

	return false;
}

void CGameControllerFNG::AwardScore(CPlayer *pAttacker, int PlayerDelta, int TeamDelta)
{
	(void)TeamDelta;
	if(!pAttacker)
		return;
	pAttacker->m_Score += PlayerDelta;
}

bool CGameControllerFNG::TrySpikeTile(CCharacter *pChr, int TileIndex)
{
	int PlayerScore = 0;

	switch(TileIndex)
	{
	case TILE_SPIKE_GOLD: PlayerScore = 8; break;
	case TILE_SPIKE_GREEN: PlayerScore = 2; break;
	case TILE_SPIKE_PURPLE: PlayerScore = 3; break;
	case TILE_SPIKE_NORMAL: PlayerScore = 3; break;
	case TILE_SPIKE_TEAM_RED:
	case TILE_SPIKE_TEAM_BLUE: PlayerScore = 5; break;
	default: return false;
	}

	const int Victim = pChr->GetPlayer()->GetCID();
	int Attacker = Victim;
	if(m_IsSaved[Victim] || !pChr->IsFrozen())
		Attacker = Victim;
	else if(m_HammeredBy[Victim] >= 0)
		Attacker = m_HammeredBy[Victim];
	else if(m_HookedBy[Victim] >= 0)
		Attacker = m_HookedBy[Victim];
	else
		Attacker = m_LastHookedBy[Victim];

	if(Attacker >= 0 && IsFriendlyFire(Victim, Attacker, 0))
		Attacker = Victim;

	const bool IsKill = Attacker != Victim && Attacker >= 0;

	if(TileIndex == TILE_SPIKE_TEAM_RED || TileIndex == TILE_SPIKE_TEAM_BLUE)
	{
		CPlayer *pAttacker = Attacker >= 0 ? GameServer()->m_apPlayers[Attacker] : nullptr;
		if(pAttacker)
		{
			const int SpikeTeam = TileIndex == TILE_SPIKE_TEAM_RED ? TEAM_RED : TEAM_BLUE;
			if(pAttacker->GetTeam() != SpikeTeam && IsKill)
			{
				if(pAttacker->GetCharacter())
				{
					pAttacker->GetCharacter()->SetEmote(EMOTE_PAIN, Server()->Tick() + Server()->TickSpeed());
					pAttacker->GetCharacter()->Freeze(5.0f, true);
				}
				pAttacker->m_Score -= 5;
				pChr->Die(Victim, WEAPON_SELF);
				return true;
			}
		}
	}

	pChr->Die(IsKill ? Attacker : Victim, IsKill ? WEAPON_NINJA : WEAPON_SELF);

	if(IsKill)
	{
		CPlayer *pAttacker = GameServer()->m_apPlayers[Attacker];
		if(pAttacker && pAttacker->GetCharacter())
			pAttacker->GetCharacter()->SetEmote(EMOTE_HAPPY, Server()->Tick() + Server()->TickSpeed());
		AwardScore(pAttacker, PlayerScore, PlayerScore);
		GameServer()->m_World.CreateSound(pChr->GetPos(), SOUND_PICKUP_HEALTH);
	}

	return true;
}

void CGameControllerFNG::HandleCharacterTiles(CCharacter *pChr, vec2 LastPos, vec2 NewPos)
{
	(void)LastPos;
	(void)NewPos;
	if(!pChr)
		return;

	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return;

	const vec2 Pos = pChr->GetPos();
	const float R = CCharacter::ms_PhysSize / 3.0f;
	const vec2 aOffsets[] = {
		vec2(R, -R), vec2(R, R), vec2(-R, -R), vec2(-R, R), vec2(0, 0),
	};

	for(const vec2 &Off : aOffsets)
	{
		const int Index = pCol->GetMapIndex(Pos + Off);
		if(Index < 0)
			continue;
		if(TrySpikeTile(pChr, pCol->GetMainTileIndex(Index)))
			return;
		if(TrySpikeTile(pChr, pCol->GetFrontTileIndex(Index)))
			return;
	}
}

int CGameControllerFNG::OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon)
{
	(void)pKiller;
	(void)Weapon;
	if(pVictim && pVictim->GetPlayer())
		pVictim->GetPlayer()->m_RespawnTick = maximum(pVictim->GetPlayer()->m_RespawnTick, Server()->Tick() + Server()->TickSpeed() * 2);
	return 0;
}

bool CGameControllerFNG::CanChangeTeam(CPlayer *pPlayer, int JoinTeam) const
{
	(void)JoinTeam;
	if(pPlayer && pPlayer->GetCharacter() && pPlayer->GetCharacter()->IsFrozen())
		return false;
	return CGameController::CanChangeTeam(pPlayer, JoinTeam);
}
