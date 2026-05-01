/* (c) TeeDefenceArchive - 2026 */
#include <base/math.h>

#include <engine/shared/config.h>

#include <generated/server_data.h>

#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include "character.h"
#include "laser.h"
#include "turret.h"

#include <generated/protocol.h>

CTurret::CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int ItemDefId)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_TURRET, 0, Pos, 0)
{
	m_Owner = Owner;
	m_ItemDefId = ItemDefId;
	m_LastShotTick = 0;
	for(int i = 0; i < NUM_RING_LASERS; i++)
		m_aRingIds[i] = Server()->SnapNewID();
	GameWorld()->InsertEntity(this);
}

CTurret::~CTurret()
{
	for(int i = 0; i < NUM_RING_LASERS; i++)
		Server()->SnapFreeID(m_aRingIds[i]);
}

int CTurret::GetVisualLevel() const
{
	if(m_ItemDefId < ITEM_TURRET_BEGINNER || m_ItemDefId > ITEM_TURRET_ADVANCED)
		return 0;
	return m_ItemDefId - ITEM_TURRET_BEGINNER;
}

void CTurret::Tick()
{
	if(!GameServer()->m_apPlayers[m_Owner])
	{
		MarkForDestroy();
		return;
	}

	CPlayer *pOwner = GameServer()->m_apPlayers[m_Owner];
	CCharacter *pOwnChr = pOwner->GetCharacter();
	if(!pOwnChr)
		return;

	CItemHelper *pH = GameServer()->ItemHelper();
	const char *pExtra = pOwner->GetExtraForItem(m_ItemDefId);
	int Cd = maximum(1, Config()->m_SvTurretFireCooldown);
	if(pH && pExtra)
	{
		const int QF = pH->GetCard(pExtra, ITEM_CARD_QUICKLY_FIRE_ID);
		const int PC = pH->GetPart(pExtra, ITEM_PART_COOLING);
		Cd = maximum(1, Cd - QF - PC * 2);
		const bool ManualAim = pH->GetCard(pExtra, ITEM_CARD_MANUAL_ID) > 0;
		if(ManualAim && QF > 0)
			Cd = maximum(1, Cd - 2);
	}
	if(Server()->Tick() - m_LastShotTick < Cd)
		return;

	const float Range = (float)Config()->m_SvTurretFireRange;
	vec2 From = m_Pos;
	vec2 Dir(0, 0);
	const bool Manual = pH && pExtra && pH->GetCard(pExtra, ITEM_CARD_MANUAL_ID) > 0;
	if(Manual)
	{
		Dir = normalize(vec2((float)pOwnChr->LatestInput().m_TargetX, (float)pOwnChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			return;
	}
	else
	{
		CCharacter *pBest = nullptr;
		float BestD = Range * Range;

		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pChr = static_cast<CCharacter *>(r.front());
			if(!pChr || !pChr->IsAlive() || !pChr->GetPlayer())
				continue;
			if(pChr->GetPlayer()->GetCID() == m_Owner)
				continue;
			if(!pChr->GetPlayer()->IsDummy())
				continue;
			const float Dx = pChr->GetPos().x - From.x, Dy = pChr->GetPos().y - From.y;
			const float D2 = Dx * Dx + Dy * Dy;
			if(D2 < BestD)
			{
				BestD = D2;
				pBest = pChr;
			}
		}
		if(!pBest)
			return;

		Dir = pBest->GetPos() - From;
		if(length(Dir) < 1.0f)
			return;
		Dir = normalize(Dir);
	}

	const int CardDmg = pH ? maximum(1, pH->GetCard(pExtra, ITEM_CARD_DAMAGE_ID)) : 1;
	const bool Exp = pH && pH->GetCard(pExtra, ITEM_CARD_EXPLOSION_ID) > 0;
	const float HitForce = pH ? (float)pH->GetCard(pExtra, ITEM_CARD_FORCE_ID) * 2.5f : 0.f;
	int El = pH ? pH->GetCard(pExtra, ITEM_CARD_ELECTRON_ID) : 0;
	const int FrcN = pH ? pH->GetCard(pExtra, ITEM_CARD_FORCE_ID) : 0;
	if(El > 0 && FrcN > 0)
		El += minimum(El, FrcN);
	const int Base = g_pData->m_Weapons.m_aId[WEAPON_LASER].m_Damage;
	const int Dmg = maximum(1, Base * CardDmg);

	new CLaser(GameWorld(), From + Dir * 24.0f, Dir, GameServer()->Tuning()->m_LaserReach, m_Owner, Dmg, Exp, HitForce, El, m_ItemDefId);
	m_LastShotTick = Server()->Tick();
}

void CTurret::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const float BaseRadius = (float)Config()->m_SvTurretRadius;
	const float Radius = BaseRadius + (float)(GetVisualLevel() * Config()->m_SvTurretRadius);
	const float AngleStep = 2.0f * pi / (float)NUM_RING_LASERS;

	for(int i = 0; i < NUM_RING_LASERS; i++)
	{
		vec2 PartPosStart = m_Pos + vec2(Radius * cosf(AngleStep * (float)i), Radius * sinf(AngleStep * (float)i));
		vec2 PartPosEnd = m_Pos + vec2(Radius * cosf(AngleStep * (float)(i + 1)), Radius * sinf(AngleStep * (float)(i + 1)));
		CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_aRingIds[i], sizeof(CNetObj_Laser)));
		if(!pObj)
			return;
		pObj->m_X = (int)PartPosStart.x;
		pObj->m_Y = (int)PartPosStart.y;
		pObj->m_FromX = (int)PartPosEnd.x;
		pObj->m_FromY = (int)PartPosEnd.y;
		pObj->m_StartTick = Server()->Tick();
	}
}
