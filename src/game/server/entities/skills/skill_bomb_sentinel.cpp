#include "skill_bomb_sentinel.h"
#include "skill_spawn.h"

#include <game/server/entities/growingexplosion.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <generated/protocol.h>
#include <generated/server_data.h>

#include "../character.h"

CSentinelBombProjectile::CSentinelBombProjectile(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, 0, Pos, 12)
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_Dir = normalize(Dir);
	if(length(m_Dir) < 0.01f)
		m_Dir = vec2(1.f, 0.f);
	m_Damage = maximum(1, Damage);
	m_LifeSpan = maximum(1, (int)(Server()->TickSpeed() * 1.2f));
	m_StartTick = Server()->Tick();
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);
	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
	GameWorld()->InsertEntity(this);
}

void CSentinelBombProjectile::Explode()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	SpawnSkillHitBurst(GameWorld(), m_Pos, 128.f, SKILL_VFX_FIRE, 0, 10);
	new CGrowingExplosion(GameWorld(), m_Pos, m_Dir, m_Owner, 4, GROWINGEXPLOSIONEFFECT_BOOM, false, GE_TARGET_MMO_HOSTILE, m_Damage);
	MarkForDestroy();
}

void CSentinelBombProjectile::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	const vec2 PrevPos = m_Pos;
	const float Speed = 32.f;
	m_Pos += m_Dir * Speed;

	if(GameServer()->Collision()->CheckPoint(m_Pos) || GameLayerClipped(m_Pos))
	{
		Explode();
		return;
	}

	vec2 HitPos = m_Pos;
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	CEntity *pHitEnt = GameWorld()->IntersectFlagEntity(PrevPos, m_Pos, 8.f, HitPos, CGameWorld::ENTFLAG_HITABLE, pOwnerChar);
	if(pHitEnt && pHitEnt->ObjType() == CGameWorld::ENTTYPE_CHARACTER)
	{
		CCharacter *pTarget = static_cast<CCharacter *>(pHitEnt);
		if(MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
		{
			Explode();
			return;
		}
	}

	if(--m_LifeSpan <= 0)
		Explode();
}

void CSentinelBombProjectile::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(!pProj)
		return;

	pProj->m_X = round_to_int(m_Pos.x);
	pProj->m_Y = round_to_int(m_Pos.y);
	pProj->m_VelX = round_to_int(m_Dir.x * 100.f);
	pProj->m_VelY = round_to_int(m_Dir.y * 100.f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = WEAPON_GRENADE;
}

CSoldierBombSentinel::CSoldierBombSentinel(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, int MaxShots, int Damage, float Range, float IntervalSec)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 48)
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_MaxShots = maximum(1, MaxShots);
	m_ShotsLeft = m_MaxShots;
	m_Damage = maximum(1, Damage);
	m_Range = maximum(120.f, Range);
	m_IntervalTicks = maximum(1, (int)(IntervalSec * Server()->TickSpeed()));
	m_NextFireTick = Server()->Tick() + m_IntervalTicks;
	m_StartTick = Server()->Tick();
	m_OrbitAngle = 0.f;

	AddSnappingGroupIds(SNAP_GROUP_RING, m_MaxShots);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
	GameWorld()->InsertEntity(this);
}

void CSoldierBombSentinel::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	m_OrbitAngle = fmodf((Server()->Tick() - m_StartTick) / (float)Server()->TickSpeed() * pi * 0.5f, 2.f * pi);

	if(m_ShotsLeft <= 0)
	{
		MarkForDestroy();
		return;
	}

	if(Server()->Tick() < m_NextFireTick)
		return;

	m_NextFireTick = Server()->Tick() + m_IntervalTicks;

	CCharacter *pNearest = nullptr;
	float BestDist = m_Range;
	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
			continue;
		const float Dist = distance(m_Pos, pChar->GetPos());
		if(Dist <= BestDist)
		{
			BestDist = Dist;
			pNearest = pChar;
		}
	}

	if(!pNearest)
		return;

	vec2 Dir = normalize(pNearest->GetPos() - m_Pos);
	if(length(Dir) < 0.01f)
		Dir = vec2(1.f, 0.f);

	new CSentinelBombProjectile(GameWorld(), m_Owner, m_Pos, Dir, m_Damage);
	m_ShotsLeft--;
	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
}

void CSoldierBombSentinel::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const array<int> *pCenterIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pCenterIds && pCenterIds->size() > 0)
	{
		const vec2 CoreFrom = m_Pos + vec2(0.f, -10.f);
		CNetObj_Laser *pCore = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pCenterIds)[0], sizeof(CNetObj_Laser)));
		if(pCore)
		{
			pCore->m_X = round_to_int(m_Pos.x);
			pCore->m_Y = round_to_int(m_Pos.y);
			pCore->m_FromX = round_to_int(CoreFrom.x);
			pCore->m_FromY = round_to_int(CoreFrom.y);
			pCore->m_StartTick = m_StartTick;
		}
	}

	const array<int> *pOrbitIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pOrbitIds)
		return;

	const float OrbitRadius = 40.f;
	const int OrbitCount = minimum(m_ShotsLeft, (int)pOrbitIds->size());
	for(int i = 0; i < OrbitCount; i++)
	{
		const float Angle = m_OrbitAngle + 2.f * pi * (float)i / (float)maximum(1, m_MaxShots);
		const vec2 OrbitPos = m_Pos + vec2(cosf(Angle), sinf(Angle)) * OrbitRadius;
		CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, (*pOrbitIds)[i], sizeof(CNetObj_Projectile)));
		if(!pProj)
			continue;
		pProj->m_X = round_to_int(OrbitPos.x);
		pProj->m_Y = round_to_int(OrbitPos.y);
		pProj->m_VelX = 0;
		pProj->m_VelY = 0;
		pProj->m_StartTick = Server()->Tick();
		pProj->m_Type = WEAPON_GRENADE;
	}
}
