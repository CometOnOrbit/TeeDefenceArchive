#include "skill_meteor.h"
#include "skill_spawn.h"

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/content/status_manager.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <generated/server_data.h>

#include "../character.h"
#include <generated/protocol.h>

CSkillMeteor::CSkillMeteor(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, int Damage, float Radius, int WarningTicks)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, (int)maximum(64.f, Radius))
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_Damage = maximum(1, Damage);
	m_Radius = maximum(64.f, Radius);
	m_WarningTicks = maximum(1, WarningTicks);
	m_StartTick = Server()->Tick();
	m_Impacted = false;

	AddSnappingGroupIds(SNAP_GROUP_RING, 12);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	SpawnSkillCastRing(GameWorld(), m_Pos, m_Radius, m_WarningTicks, SKILL_VFX_FIRE, SKILL_RING_STEADY, m_WarningTicks / 2);
	GameWorld()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
	GameWorld()->InsertEntity(this);
}

bool CSkillMeteor::IsHostileTarget(CCharacter *pOwnerChar, CCharacter *pTarget)
{
	if(!pOwnerChar || !pTarget || pTarget == pOwnerChar)
		return false;
	return MMOWeaponTargetValid(GameServer(), m_Owner, pTarget);
}

void CSkillMeteor::Impact()
{
	if(m_Impacted)
		return;
	m_Impacted = true;

	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	SpawnSkillHitBurst(GameWorld(), m_Pos, m_Radius, SKILL_VFX_FIRE, 0, 18);
	SpawnSkillCastRing(GameWorld(), m_Pos, m_Radius * 1.2f, Server()->TickSpeed() / 2, SKILL_VFX_FIRE, SKILL_RING_EXPAND_BURST, Server()->TickSpeed() / 5);

	if(pOwnerChar)
		GameWorld()->CreateExplosion(m_Pos, pOwnerChar, WEAPON_GRENADE, maximum(2, m_Damage / 2));

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pTarget = static_cast<CCharacter *>(r.front());
		if(!IsHostileTarget(pOwnerChar, pTarget))
			continue;
		if(distance(m_Pos, pTarget->GetPos()) > m_Radius)
			continue;

		vec2 Dir = normalize(pTarget->GetPos() - m_Pos);
		if(length(Dir) < 0.01f)
			Dir = vec2(0.f, -1.f);
		pTarget->TakeDamage(Dir * 12.f, m_Pos, m_Damage, m_Owner, WEAPON_GRENADE);
		if(GameServer()->Core() && GameServer()->Core()->StatusManager())
			GameServer()->Core()->StatusManager()->ApplyStatus(pTarget, "burn", 2, Server()->TickSpeed() * 4);
	}

	MarkForDestroy();
}

void CSkillMeteor::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	const int Elapsed = Server()->Tick() - m_StartTick;
	if(!m_Impacted && Elapsed >= m_WarningTicks)
		Impact();
	else if(!m_Impacted && (Elapsed % maximum(1, Server()->TickSpeed() / 4)) == 0)
		GameWorld()->CreateHammerHit(m_Pos + vec2(0.f, -48.f - (float)(Elapsed % 32)));
}

void CSkillMeteor::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || m_Impacted)
		return;

	const int Elapsed = Server()->Tick() - m_StartTick;
	const float Pulse = 0.7f + 0.3f * sinf((float)Elapsed / (float)maximum(1, Server()->TickSpeed() / 3) * pi);
	const float R = m_Radius * Pulse;
	const array<int> *pDotIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pDotIds || pDotIds->size() < 12)
		return;

	const float AngleStep = 2.f * pi / 12.f;
	for(int i = 0; i < 12; i++)
	{
		const float Angle = AngleStep * (float)i;
		const vec2 DotPos = m_Pos + vec2(R * cosf(Angle), R * sinf(Angle));
		CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pDotIds)[i], sizeof(CNetObj_Laser)));
		if(!pLaser)
			continue;
		pLaser->m_X = round_to_int(DotPos.x);
		pLaser->m_Y = round_to_int(DotPos.y);
		pLaser->m_FromX = round_to_int(m_Pos.x);
		pLaser->m_FromY = round_to_int(m_Pos.y - 64.f);
		pLaser->m_StartTick = m_StartTick;
	}
}
