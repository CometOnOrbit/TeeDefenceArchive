/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_TURRET_AMMO_H
#define GAME_SERVER_TURRET_AMMO_H

#include <base/system.h>

class CGameContext;
class CGameWorld;
class CPlayer;
class CTurret;
class CCharacter;
class CItemHelper;

enum
{
	TURRET_AMMO_DEBT_SCALE = 1000,
	NUM_TURRET_AMMO_MATS = 8,
	TURRET_AMMO_LOG = 0,
	TURRET_AMMO_COAL,
	TURRET_AMMO_COPPER,
	TURRET_AMMO_IRON,
	TURRET_AMMO_GOLD,
	TURRET_AMMO_DIAMOND,
	TURRET_AMMO_ENEGRY,
	TURRET_AMMO_ZOMBIEHEART,
};

struct STurretAmmoMix
{
	int m_aPct[NUM_TURRET_AMMO_MATS];
};

struct STurretShotParams
{
	int m_Damage;
	int m_Electron;
	float m_HitForce;
	bool m_Explosive;
	bool m_Fusion;
	int m_DominantMat;
	int m_TargetCid;
};

void TurretAmmo_DefaultMix(STurretAmmoMix *pMix);
bool TurretAmmo_NormalizeMix(STurretAmmoMix *pMix);
int TurretAmmo_MatToItemId(int MatSlot);
int TurretAmmo_ItemIdToMat(int ItemId);

void TurretAmmo_ClearDebt(CPlayer *pP);

bool TurretAmmo_Consume(CPlayer *pP, const STurretAmmoMix *pMix, int TotalUnits);
bool TurretAmmo_CanAfford(const CPlayer *pP, const STurretAmmoMix *pMix, int TotalUnits);

int TurretRepair_MaterialCost(CItemHelper *pH, int TurretItemId, int MatId);
bool TurretRepair_CanAfford(CGameContext *pGame, const CPlayer *pP, int TurretItemId);
bool TurretRepair_Consume(CGameContext *pGame, CPlayer *pP, int TurretItemId);
void TurretAmmo_BuildShotParams(const STurretAmmoMix *pMix, CItemHelper *pH, const char *pExtra, STurretShotParams *pOut, CPlayer *pPlayer = nullptr);

void TurretAmmo_Fire(CGameContext *pGame, CGameWorld *pWorld, CTurret *pTurret, CPlayer *pOwner, vec2 From, vec2 Dir, CCharacter *pTarget,
	const STurretShotParams &Params, int ItemDefId);

#endif
