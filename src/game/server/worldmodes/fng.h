#ifndef GAME_SERVER_WORLDMODES_FNG_H
#define GAME_SERVER_WORLDMODES_FNG_H

#include "arena.h"

// Team FNG: freeze with laser, hammer into spikes for score (ported from ddnet-pvp).
class CGameControllerFNG : public CGameControllerArena
{
	enum
	{
		TILE_SPIKE_GOLD = 7,
		TILE_SPIKE_NORMAL,
		TILE_SPIKE_TEAM_RED,
		TILE_SPIKE_TEAM_BLUE,
		TILE_SPIKE_GREEN = 14,
		TILE_SPIKE_PURPLE,
	};

	int m_HammeredBy[MAX_CLIENTS];
	int m_HookedBy[MAX_CLIENTS];
	bool m_IsSaved[MAX_CLIENTS];
	int m_LastHookedBy[MAX_CLIENTS];

public:
	explicit CGameControllerFNG(CGameContext *pGameServer);

	void PreTick() override;
	void HandleCharacterTiles(CCharacter *pChr, vec2 LastPos, vec2 NewPos) override;
	void OnCharacterSpawn(CCharacter *pChr) override;
	int OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon) override;
	bool OnCharacterTakeDamage(CCharacter *pChr, vec2 &Force, int &Dmg, int From, int Weapon) override;
	bool CanChangeTeam(CPlayer *pPlayer, int JoinTeam) const override;

private:
	bool TrySpikeTile(CCharacter *pChr, int TileIndex);
	void AwardScore(CPlayer *pAttacker, int PlayerDelta, int TeamDelta);
};

#endif
