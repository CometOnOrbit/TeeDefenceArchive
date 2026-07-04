/* (c) TeeDefenceArchive - 2026 */
#include <base/math.h>

#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/worldmodes/defence.h>
#include <game/server/player.h>

#include "character.h"
#include "projectile.h"
#include "tower-main.h"

CTowerMain::CTowerMain(CGameWorld *pGameWorld, vec2 StandPos)
	: CHitableEntity(pGameWorld, CGameWorld::ENTTYPE_TOWERMAIN, 0, StandPos, pGameWorld && pGameWorld->Config() ? pGameWorld->Config()->m_SvTdTowerHitRadius : 200)
{
	m_Pos = StandPos;

	AddSnappingGroupIds(SNAP_GROUP_TOWER_BODY, 9);
	AddSnappingGroupIds(SNAP_GROUP_TOWER_SIDE, s_TowerNumSide);

	GameWorld()->InsertEntity(this);

	m_Health = GetMaxHealth();
}

int CTowerMain::GetMaxHealth()
{
	CGameContext *pCtx = GameWorld()->GameServer();
	if(pCtx && pCtx->m_pController)
		return static_cast<CGameControllerDefence *>(pCtx->m_pController)->TdGetDifficultyTowerMaxHealth();
	return GameWorld()->Config()->m_SvMaxTowerHealth;
}

void CTowerMain::Tick()
{
	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChr = (CCharacter *)r.front();
		if(!pChr->IsAlive())
			continue;

		float Len = distance(pChr->GetPos(), m_Pos);
		if(Len < pChr->GetProximityRadius() + s_TowerSize)
		{
			int ClientID = pChr->GetPlayer()->GetCID();
			if(pChr->GetPlayer()->IsDummy())
			{
				if(GameWorld()->Config()->m_SvTdTowerTouchDamage > 0)
					TakeDamage(GameWorld()->Config()->m_SvTdTowerTouchDamage);
				if(pChr->IsAlive())
					pChr->Die(ClientID, WEAPON_GAME);
				continue;
			}

			if(Server()->Tick() % Server()->TickSpeed() == 0)
			{
				pChr->IncreaseHealth(3);
				GameServer()->SendBroadcastLocF(ClientID, "tower.heal_broadcast", "Tower: %d / %d", m_Health, GetMaxHealth());
			}
		}
	}
}

void CTowerMain::Reset()
{
	m_Health = GetMaxHealth();
}

void CTowerMain::SetHealth(int Health)
{
	m_Health = clamp(Health, 0, GetMaxHealth());
}

void CTowerMain::Snap(int SnappingClient)
{
	(void)SnappingClient;

	const array<int> *pBodyIds = FindSnappingGroupIds(SNAP_GROUP_TOWER_BODY);
	const array<int> *pSideIds = FindSnappingGroupIds(SNAP_GROUP_TOWER_SIDE);
	if(!pBodyIds || !pSideIds)
		return;

	int aSize = pBodyIds->size();
	const float kPi = 3.14159265358979323846f;

	for(int i = 0; i < aSize; i++)
	{
		CNetObj_Projectile *pEff = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, (*pBodyIds)[i], sizeof(CNetObj_Projectile)));
		if(!pEff)
			continue;

		pEff->m_X = (int)(cosf(kPi / 9.0f * i * 4.0f) * (16.0f + 5.0f / 110.0f) + m_Pos.x);
		pEff->m_Y = (int)(sinf(kPi / 9.0f * i * 4.0f) * (16.0f + 5.0f / 110.0f) + m_Pos.y);

		pEff->m_StartTick = Server()->Tick() - 2;
		pEff->m_Type = WEAPON_SHOTGUN;
	}

	float AngleStep = 2.0f * kPi / s_TowerNumSide;

	for(int i = 0; i < s_TowerNumSide; i++)
	{
		if(i >= pSideIds->size())
			break;
		vec2 PartPosStart = m_Pos + vec2(s_TowerSize * cosf(AngleStep * i), s_TowerSize * sinf(AngleStep * i));
		vec2 PartPosEnd = m_Pos + vec2(s_TowerSize * cosf(AngleStep * (i + 1)), s_TowerSize * sinf(AngleStep * (i + 1)));

		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pSideIds)[i], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;

		pObj->m_X = (int)PartPosStart.x;
		pObj->m_Y = (int)PartPosStart.y;
		pObj->m_FromX = (int)PartPosEnd.x;
		pObj->m_FromY = (int)PartPosEnd.y;
		pObj->m_StartTick = Server()->Tick();
	}
}

void CTowerMain::TakeDamage(int Dmg)
{
	if(m_Health <= 0)
		return;
	m_Health -= Dmg;
}

static bool IsZombieDamageSource(CGameContext *pGame, int Owner, CEntity *pFrom)
{
	(void)pFrom;
	if(Owner == PLAYER_TEAM_BLUE)
		return true;
	if(Owner < 0 || Owner >= MAX_CLIENTS || !pGame->m_apPlayers[Owner])
		return false;
	CPlayer *pP = pGame->m_apPlayers[Owner];
	return pP->IsDummy() || pP->GetTeam() == TEAM_BLUE;
}

bool CTowerMain::TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon)
{
	(void)Force;
	(void)Source;
	(void)Weapon;

	const int Owner = GameWorld()->DamageOwnerFromEntity(pFrom);

	if(!IsZombieDamageSource(GameServer(), Owner, pFrom))
		return false;

	TakeDamage(maximum(1, Dmg));
	return true;
}
