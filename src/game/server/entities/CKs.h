/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_ENTITIES_CKS_H
#define GAME_SERVER_ENTITIES_CKS_H

#include <game/server/entity.h>

static const int CKsPhysSize = 14;

class CKs : public CEntity
{
	static const int ms_PhysSize = 14;

public:
	CKs(CGameWorld *pGameWorld, int Type, vec2 Pos);

	void Reset() override;
	void Tick() override;
	void TickPaused() override;
	void Snap(int SnappingClient) override;
	void Picking(int Time, class CPlayer *pPlayer);
	void RewardIfDestroyed(class CPlayer *pPlayer);

	int m_Health;

	int GetMaxHealth();
	void HandleLock(class CCharacter *pChr);

private:
	int m_Type;
	int m_LockedPlayer;
};

#endif
