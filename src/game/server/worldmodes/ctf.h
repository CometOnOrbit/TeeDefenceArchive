#ifndef GAME_SERVER_WORLDMODES_CTF_H
#define GAME_SERVER_WORLDMODES_CTF_H

#include "tdm.h"

class CFlag;

class CGameControllerCTF : public CGameControllerTDM
{
	CFlag *m_apFlags[2];

public:
	explicit CGameControllerCTF(CGameContext *pGameServer);

	void OnCharacterSpawn(CCharacter *pChr) override;
	int OnCharacterDeath(CCharacter *pVictim, CPlayer *pKiller, int Weapon) override;
	void OnFlagReturn(CFlag *pFlag) override;
	bool OnEntity(int Index, vec2 Pos) override;
	void Snap(int SnappingClient) override;
	void Tick() override;
};

#endif
