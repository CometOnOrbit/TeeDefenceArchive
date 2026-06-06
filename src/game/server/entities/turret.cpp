/* (c) TeeDefenceArchive - 2026 */
#include <base/math.h>

#include <engine/shared/config.h>

#include <generated/server_data.h>
#include <generated/protocol.h>

#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>
#include <game/server/turret_ammo.h>

#include <game/collision.h>

#include "character.h"
#include "turret.h"

CTurret::CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int ItemDefId)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_TURRET, 0, Pos, 0)
{
	m_Owner = Owner;
	m_ItemDefId = ItemDefId;
	m_LastShotTick = 0;
	m_LastAmmoWarnTick = 0;
	m_aCenterId = Server()->SnapNewID();
	for(int i = 0; i < NUM_RING_LASERS; i++)
		m_aRingIds[i] = Server()->SnapNewID();
	GameWorld()->InsertEntity(this);
}

CTurret::~CTurret()
{
	Server()->SnapFreeID(m_aCenterId);
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

	const STurretAmmoMix &Mix = pOwner->GetTurretAmmoMix();
	if(!TurretAmmo_CanAfford(pOwner, &Mix, Config()->m_SvTurretAmmoPerShot))
	{
		if(Server()->Tick() - m_LastAmmoWarnTick >= Server()->TickSpeed() * 3)
		{
			GameServer()->SendChatLoc(m_Owner, "turret.ammo.empty", "Not enough materials to fire");
			m_LastAmmoWarnTick = Server()->Tick();
		}
		return;
	}

	const float Range = (float)Config()->m_SvTurretFireRange;
	vec2 From = m_Pos;
	vec2 Dir(0, 0);
	CCharacter *pTarget = nullptr;
	const bool Manual = pH && pExtra && pH->GetCard(pExtra, ITEM_CARD_MANUAL_ID) > 0;
	if(Manual)
	{
		Dir = normalize(vec2((float)pOwnChr->LatestInput().m_TargetX, (float)pOwnChr->LatestInput().m_TargetY));
		if(length(Dir) < 0.01f)
			return;
	}
	else
	{
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
				pTarget = pChr;
			}
		}
		if(!pTarget)
			return;

		Dir = pTarget->GetPos() - From;
		if(length(Dir) < 1.0f)
			return;
		Dir = normalize(Dir);
	}

	if(!TurretAmmo_Consume(pOwner, &Mix, Config()->m_SvTurretAmmoPerShot))
		return;

	STurretShotParams Params;
	TurretAmmo_BuildShotParams(&Mix, pH, pExtra, &Params);
	if(pTarget)
		Params.m_TargetCid = pTarget->GetPlayer()->GetCID();

	TurretAmmo_Fire(GameServer(), GameWorld(), this, pOwner, From, Dir, pTarget, Params, m_ItemDefId);
	m_LastShotTick = Server()->Tick();
}

void CTurret::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const float BaseRadius = (float)Config()->m_SvTurretRadius;
	const float Radius = BaseRadius + (float)(GetVisualLevel() * Config()->m_SvTurretRadius);
	const float AngleStep = 2.0f * pi / (float)NUM_RING_LASERS;
	const int Subtype = GetVisualLevel();

	if(GameServer()->ClientUsesDDNetLaser(SnappingClient))
	{
		CNetObj_DDNetLaser *pCenter = static_cast<CNetObj_DDNetLaser *>(Server()->SnapNewItem(NETOBJTYPE_DDNETLASER, m_aCenterId, sizeof(CNetObj_DDNetLaser)));
		if(pCenter)
		{
			pCenter->m_FromX = (int)m_Pos.x;
			pCenter->m_FromY = (int)m_Pos.y;
			pCenter->m_ToX = (int)m_Pos.x;
			pCenter->m_ToY = (int)m_Pos.y;
			pCenter->m_StartTick = Server()->Tick();
			pCenter->m_Owner = m_Owner;
			pCenter->m_Type = 0;
			pCenter->m_SwitchNumber = -1;
			pCenter->m_Subtype = Subtype;
			pCenter->m_Flags = LASERFLAG_NO_PREDICT;
		}
	}
	else
	{
		CNetObj_Laser *pCenter = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_aCenterId, sizeof(CNetObj_Laser)));
		if(pCenter)
		{
			pCenter->m_X = (int)m_Pos.x;
			pCenter->m_Y = (int)m_Pos.y;
			pCenter->m_FromX = (int)m_Pos.x;
			pCenter->m_FromY = (int)m_Pos.y;
			pCenter->m_StartTick = Server()->Tick();
		}
	}

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

CTurretPreview::CTurretPreview(CGameWorld *pGameWorld, vec2 Pos, int Owner, int ItemDefId, bool Valid)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 0)
{
	m_Owner = Owner;
	m_ItemDefId = ItemDefId;
	m_Valid = Valid;
	m_aCenterId = Server()->SnapNewID();
	for(int i = 0; i < NUM_RING_LASERS; i++)
		m_aRingIds[i] = Server()->SnapNewID();
	GameWorld()->InsertEntity(this);
}

CTurretPreview::~CTurretPreview()
{
	Server()->SnapFreeID(m_aCenterId);
	for(int i = 0; i < NUM_RING_LASERS; i++)
		Server()->SnapFreeID(m_aRingIds[i]);
}

void CTurretPreview::SetPreviewPos(vec2 Pos)
{
	m_Pos = Pos;
}

void CTurretPreview::SetValid(bool Valid)
{
	m_Valid = Valid;
}

int CTurretPreview::GetVisualLevel() const
{
	if(m_ItemDefId < ITEM_TURRET_BEGINNER || m_ItemDefId > ITEM_TURRET_ADVANCED)
		return 0;
	return m_ItemDefId - ITEM_TURRET_BEGINNER;
}

void CTurretPreview::Snap(int SnappingClient)
{
	if(SnappingClient != -1 && SnappingClient != m_Owner)
		return;
	if(NetworkClipped(SnappingClient))
		return;

	const float BaseRadius = (float)Config()->m_SvTurretRadius;
	const float Radius = BaseRadius + (float)(GetVisualLevel() * Config()->m_SvTurretRadius);
	const float AngleStep = 2.0f * pi / (float)NUM_RING_LASERS;
	const int Subtype = GetVisualLevel() + (m_Valid ? 0 : 8);

	if(GameServer()->ClientUsesDDNetLaser(SnappingClient))
	{
		CNetObj_DDNetLaser *pCenter = static_cast<CNetObj_DDNetLaser *>(Server()->SnapNewItem(NETOBJTYPE_DDNETLASER, m_aCenterId, sizeof(CNetObj_DDNetLaser)));
		if(pCenter)
		{
			pCenter->m_FromX = (int)m_Pos.x;
			pCenter->m_FromY = (int)m_Pos.y;
			pCenter->m_ToX = (int)m_Pos.x;
			pCenter->m_ToY = (int)m_Pos.y;
			pCenter->m_StartTick = Server()->Tick();
			pCenter->m_Owner = m_Owner;
			pCenter->m_Type = 0;
			pCenter->m_SwitchNumber = -1;
			pCenter->m_Subtype = Subtype;
			pCenter->m_Flags = LASERFLAG_NO_PREDICT;
		}
	}
	else
	{
		CNetObj_Laser *pCenter = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_aCenterId, sizeof(CNetObj_Laser)));
		if(pCenter)
		{
			pCenter->m_X = (int)m_Pos.x;
			pCenter->m_Y = (int)m_Pos.y;
			pCenter->m_FromX = (int)m_Pos.x;
			pCenter->m_FromY = (int)m_Pos.y;
			pCenter->m_StartTick = Server()->Tick();
		}
	}

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
