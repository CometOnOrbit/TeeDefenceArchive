/* (c) TeeDefenceArchive - 2026 */
#include <base/math.h>

#include <engine/shared/config.h>

#include <generated/server_data.h>

#include <game/server/entities/character.h>
#include <game/server/entities/electro.h>
#include <game/server/entities/growingexplosion.h>
#include <game/server/entities/laser.h>
#include <game/server/entities/lightning.h>
#include <game/server/entities/plasma.h>
#include <game/server/entities/turret.h>
#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>
#include <game/server/turret_ammo.h>

static const int s_aMatItemIds[NUM_TURRET_AMMO_MATS] = {
	ITEM_LOG, ITEM_COAL, ITEM_COPPER, ITEM_IRON, ITEM_GOLD, ITEM_DIAMOND, ITEM_ENEGRY, ITEM_ZOMBIEHEART,
};

void TurretAmmo_DefaultMix(STurretAmmoMix *pMix)
{
	if(!pMix)
		return;
	mem_zero(pMix->m_aPct, sizeof(pMix->m_aPct));
	pMix->m_aPct[TURRET_AMMO_LOG] = 100;
}

bool TurretAmmo_NormalizeMix(STurretAmmoMix *pMix)
{
	if(!pMix)
		return false;
	int Sum = 0;
	for(int i = 0; i < NUM_TURRET_AMMO_MATS; i++)
	{
		if(pMix->m_aPct[i] < 0)
			pMix->m_aPct[i] = 0;
		Sum += pMix->m_aPct[i];
	}
	if(Sum <= 0)
	{
		TurretAmmo_DefaultMix(pMix);
		return true;
	}
	for(int i = 0; i < NUM_TURRET_AMMO_MATS; i++)
		pMix->m_aPct[i] = (pMix->m_aPct[i] * 100) / Sum;
	return true;
}

int TurretAmmo_MatToItemId(int MatSlot)
{
	if(MatSlot < 0 || MatSlot >= NUM_TURRET_AMMO_MATS)
		return -1;
	return s_aMatItemIds[MatSlot];
}

int TurretAmmo_ItemIdToMat(int ItemId)
{
	for(int i = 0; i < NUM_TURRET_AMMO_MATS; i++)
		if(s_aMatItemIds[i] == ItemId)
			return i;
	return -1;
}

bool TurretAmmo_CanAfford(const CPlayer *pP, const STurretAmmoMix *pMix, int TotalUnits)
{
	if(!pP || !pMix || TotalUnits <= 0)
		return false;
	STurretAmmoMix Norm = *pMix;
	TurretAmmo_NormalizeMix(&Norm);
	for(int i = 0; i < NUM_TURRET_AMMO_MATS; i++)
	{
		if(Norm.m_aPct[i] <= 0)
			continue;
		const int ItemId = s_aMatItemIds[i];
		const int Need = maximum(1, (TotalUnits * Norm.m_aPct[i] + 99) / 100);
		if(pP->m_AccData.m_aItems[ItemId].m_Num < Need)
			return false;
	}
	return true;
}

bool TurretAmmo_Consume(CPlayer *pP, const STurretAmmoMix *pMix, int TotalUnits)
{
	if(!TurretAmmo_CanAfford(pP, pMix, TotalUnits))
		return false;
	STurretAmmoMix Norm = *pMix;
	TurretAmmo_NormalizeMix(&Norm);
	for(int i = 0; i < NUM_TURRET_AMMO_MATS; i++)
	{
		if(Norm.m_aPct[i] <= 0)
			continue;
		const int ItemId = s_aMatItemIds[i];
		const int Need = maximum(1, (TotalUnits * Norm.m_aPct[i] + 99) / 100);
		pP->m_AccData.m_aItems[ItemId].m_Num -= Need;
	}
	return true;
}

void TurretAmmo_BuildShotParams(const STurretAmmoMix *pMix, CItemHelper *pH, const char *pExtra, STurretShotParams *pOut)
{
	if(!pOut)
		return;
	mem_zero(pOut, sizeof(*pOut));
	STurretAmmoMix Norm = *pMix;
	TurretAmmo_NormalizeMix(&Norm);

	int Dominant = 0;
	int Best = -1;
	for(int i = 0; i < NUM_TURRET_AMMO_MATS; i++)
	{
		if(Norm.m_aPct[i] > Best)
		{
			Best = Norm.m_aPct[i];
			Dominant = i;
		}
	}
	pOut->m_DominantMat = Dominant;

	int DmgMul = 1;
	if(Norm.m_aPct[TURRET_AMMO_IRON] >= 15)
		DmgMul += Norm.m_aPct[TURRET_AMMO_IRON] / 25;
	if(Norm.m_aPct[TURRET_AMMO_GOLD] >= 10)
		DmgMul += Norm.m_aPct[TURRET_AMMO_GOLD] / 20;
	if(Norm.m_aPct[TURRET_AMMO_DIAMOND] >= 10)
		DmgMul += Norm.m_aPct[TURRET_AMMO_DIAMOND] / 10;

	if(pH)
	{
		const int CardExtra = pH->GetCard(pExtra, ITEM_CARD_DAMAGE_ID) * 2;
		pOut->m_Explosive = pH->GetCard(pExtra, ITEM_CARD_EXPLOSION_ID) > 0 || Norm.m_aPct[TURRET_AMMO_COAL] >= 20;
		pOut->m_Fusion = pH->GetCard(pExtra, ITEM_CARD_FUSION_ID) > 0 || Norm.m_aPct[TURRET_AMMO_ENEGRY] >= 25;
		pOut->m_HitForce = 1.f + (float)pH->GetCard(pExtra, ITEM_CARD_FORCE_ID) * 2.f;
		pOut->m_Electron = pH->GetCard(pExtra, ITEM_CARD_ELECTRON_ID);
		const int Base = g_pData->m_Weapons.m_aId[WEAPON_LASER].m_Damage;
		pOut->m_Damage = maximum(1, (Base + CardExtra) * DmgMul);
	}
	else
	{
		pOut->m_Explosive = Norm.m_aPct[TURRET_AMMO_COAL] >= 20;
		pOut->m_Fusion = Norm.m_aPct[TURRET_AMMO_ENEGRY] >= 25;
	}

	if(Norm.m_aPct[TURRET_AMMO_COPPER] >= 20)
		pOut->m_Electron += 1 + Norm.m_aPct[TURRET_AMMO_COPPER] / 20;

	if(!pH)
	{
		const int Base = g_pData->m_Weapons.m_aId[WEAPON_LASER].m_Damage;
		pOut->m_Damage = maximum(1, Base * DmgMul);
	}
}

static vec2 TurretMuzzlePos(CGameContext *pGame, vec2 From, vec2 Dir)
{
	const float BaseR = pGame ? (float)pGame->Config()->m_SvTurretRadius : 32.f;
	const float MuzzleDist = BaseR * 1.5f + 28.f;
	vec2 Pos = From + Dir * MuzzleDist;
	vec2 Push = Dir;
	pGame->Collision()->MovePoint(&Pos, &Push, 6.0f, 0);
	return Pos;
}

void TurretAmmo_Fire(CGameContext *pGame, CGameWorld *pWorld, CTurret *pTurret, CPlayer *pOwner, vec2 From, vec2 Dir, CCharacter *pTarget,
	const STurretShotParams &Params, int ItemDefId)
{
	if(!pGame || !pWorld || !pOwner)
		return;

	const int Owner = pOwner->GetCID();
	const vec2 ShotFrom = TurretMuzzlePos(pGame, From, Dir);

	// Coal-dominant mix fires a directional growing blast; explosion card alone still uses laser/projectile paths.
	if(Params.m_DominantMat == TURRET_AMMO_COAL)
	{
		const int Radius = 2 + (Params.m_Fusion ? 2 : 0);
		new CGrowingExplosion(pWorld, ShotFrom, Dir, Owner, Radius, GROWINGEXPLOSIONEFFECT_BOOM, Params.m_Fusion);
		return;
	}

	if(Params.m_DominantMat == TURRET_AMMO_COPPER || Params.m_Electron > 0)
	{
		vec2 End = ShotFrom + Dir * 160.0f;
		new CElectro(pWorld, ShotFrom, End, Dir * 20.0f, 3);
		if(pTarget)
			pTarget->ApplyElectronSlow(maximum(1, Params.m_Electron));
	}

	if(Params.m_DominantMat == TURRET_AMMO_DIAMOND || Params.m_DominantMat == TURRET_AMMO_ENEGRY)
	{
		new CLightning(pWorld, ShotFrom, Dir, 200.0f, 80.0f, Owner, Params.m_Damage);
		return;
	}

	if(Params.m_DominantMat == TURRET_AMMO_ZOMBIEHEART && pTarget)
	{
		new CPlasma(pWorld, ShotFrom, Owner, pTarget->GetPlayer()->GetCID(), Dir, false, Params.m_Explosive, WEAPON_GRENADE, Params.m_Damage);
		return;
	}

	if(Params.m_DominantMat == TURRET_AMMO_GOLD && pTarget)
	{
		new CPlasma(pWorld, ShotFrom, Owner, pTarget->GetPlayer()->GetCID(), Dir, false, false, WEAPON_GUN, Params.m_Damage);
		return;
	}

	vec2 LaserPos = ShotFrom;
	vec2 LaserDir = Dir;
	pGame->Collision()->MovePoint(&LaserPos, &LaserDir, 4.0f, 0);
	if(length(LaserDir) < 0.001f)
		LaserDir = Dir;
	new CLaser(pWorld, LaserPos, normalize(LaserDir), pGame->Tuning()->m_LaserReach, Owner, Params.m_Damage, Params.m_Explosive, Params.m_HitForce,
		Params.m_Electron, ItemDefId);
}
