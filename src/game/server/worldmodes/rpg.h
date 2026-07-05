#ifndef GAME_SERVER_WORLDMODES_RPG_H
#define GAME_SERVER_WORLDMODES_RPG_H

#include "hub.h"
#include <engine/shared/protocol.h>

class CGameControllerRPG : public CGameControllerHub
{
public:
	explicit CGameControllerRPG(CGameContext *pGameServer);

	void Tick() override;
	int OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon) override;
	void OnCharacterSpawn(CCharacter *pChr) override;
	int OnCharacterFireWeapon(CCharacter *pChr, vec2 Direction, int Weapon) override;
	void OnEntitySwitch(int EntityIndex, vec2 Pos, int Flags, int Number) override;
	void HandleCharacterTiles(CCharacter *pChr, vec2 LastPos, vec2 NewPos) override;

	// Training dummy tracking
	struct STrainingDummyTracker
	{
		int m_ClientID = -1;
		int m_TotalDamage = 0;
		int m_LastHitTick = 0;
		int m_StartTick = 0;
	};
	STrainingDummyTracker m_aDummyTrackers[MAX_CLIENTS];
};

#endif
