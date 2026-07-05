#include "skill_magic_bolt.h"
#include "skill_spawn.h"

#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/core/components/content/status_manager.h>
#include <generated/server_data.h>

#include "../character.h"
#include "../mmo/mmo_weapon_common.h"

CSkillMagicBolt::CSkillMagicBolt(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, vec2 Dir, int Damage, ESkillBoltEffect Effect, int PreferredTargetCID)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos, 16)
{
	m_Pos = Pos;
	m_Direction = normalize(Dir);
	if(length(m_Direction) < 0.01f)
		m_Direction = vec2(1.f, 0.f);
	m_Owner = OwnerCID;
	m_Damage = maximum(1, Damage);
	m_LifeSpan = Server()->TickSpeed() * 2;
	m_StartTick = Server()->Tick();
	m_TrackedCID = PreferredTargetCID;
	m_Effect = Effect;
	m_Traveled = 0.f;
	m_MaxTravel = (Effect == SKILL_BOLT_SMOKE_HOOK) ? 420.f : 2400.f;
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);
	GameWorld()->InsertEntity(this);
}

void CSkillMagicBolt::Impact(CCharacter *pTarget)
{
	if(!pTarget)
	{
		MarkForDestroy();
		return;
	}

	GameWorld()->CreateSound(pTarget->GetPos(), m_Effect == SKILL_BOLT_WAND ? (int)SOUND_SFX_WEAPON_PULSE : SOUND_LASER_FIRE);

	ESkillVisualStyle HitStyle = SKILL_VFX_ARCANE;
	switch(m_Effect)
	{
	case SKILL_BOLT_ENTANGLE:
		HitStyle = SKILL_VFX_POISON;
		break;
	case SKILL_BOLT_LIFE_DRAIN:
		HitStyle = SKILL_VFX_SHADOW;
		break;
	case SKILL_BOLT_SMOKE_HOOK:
		HitStyle = SKILL_VFX_POISON;
		break;
	case SKILL_BOLT_WAND:
		HitStyle = SKILL_VFX_ARCANE;
		break;
	case SKILL_BOLT_DAMAGE:
	default:
		HitStyle = SKILL_VFX_ARCANE;
		break;
	}
	const float BurstRadius = (m_Effect == SKILL_BOLT_WAND) ? 72.f : 56.f;
	const int BurstDots = (m_Effect == SKILL_BOLT_WAND) ? 14 : 12;
	SpawnSkillHitBurst(GameWorld(), pTarget->GetPos(), BurstRadius, HitStyle, 0, BurstDots);
	if(m_Effect == SKILL_BOLT_WAND)
		GameWorld()->CreateHammerHit(pTarget->GetPos());

	switch(m_Effect)
	{
	case SKILL_BOLT_ENTANGLE:
		if(GameServer()->Core() && GameServer()->Core()->StatusManager())
		{
			const int RootTicks = Server()->TickSpeed() * 2;
			GameServer()->Core()->StatusManager()->ApplyStatus(pTarget, "frost", 1, RootTicks, 0.05f);
		}
		GameWorld()->CreateSound(pTarget->GetPos(), SOUND_HOOK_ATTACH_PLAYER);
		break;
	case SKILL_BOLT_LIFE_DRAIN:
		pTarget->TakeDamage(m_Direction * 4.f, m_Pos, m_Damage, m_Owner, WEAPON_GAME);
		if(CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner))
		{
			pOwner->IncreaseHealth(maximum(1, m_Damage / 2));
			GameWorld()->CreateSound(pOwner->GetPos(), SOUND_PICKUP_HEALTH);
			const int TargetCID = m_TrackedCID >= 0 ? m_TrackedCID : pTarget->GetCID();
			SpawnSkillLinkBeam(GameWorld(), pOwner->GetPos(), pTarget->GetPos(), Server()->TickSpeed(), SKILL_VFX_SHADOW, TargetCID);
		}
		break;
	case SKILL_BOLT_SMOKE_HOOK:
		pTarget->TakeDamage(m_Direction * 6.f, m_Pos, maximum(1, m_Damage / 2), m_Owner, WEAPON_GAME);
		if(CCharacter *pOwner = GameServer()->GetPlayerChar(m_Owner))
		{
			vec2 PullDir = pOwner->GetPos() - pTarget->GetPos();
			if(length(PullDir) > 0.01f)
				pTarget->GetCore()->m_Vel += normalize(PullDir) * 14.f;
		}
		if(GameServer()->Core() && GameServer()->Core()->StatusManager())
		{
			const int DotTicks = Server()->TickSpeed() * 2;
			const int Stacks = maximum(1, m_Damage / 3);
			GameServer()->Core()->StatusManager()->ApplyStatus(pTarget, "poison", Stacks, DotTicks);
			GameServer()->Core()->StatusManager()->ApplyStatus(pTarget, "burn", 1, DotTicks);
		}
		GameWorld()->CreateSound(pTarget->GetPos(), SOUND_HOOK_ATTACH_PLAYER);
		break;
	case SKILL_BOLT_WAND:
		pTarget->TakeDamage(m_Direction * 5.f, m_Pos, m_Damage, m_Owner, WEAPON_LASER);
		GameWorld()->CreateSound(pTarget->GetPos(), SOUND_LASER_BOUNCE);
		break;
	case SKILL_BOLT_DAMAGE:
	default:
		pTarget->TakeDamage(m_Direction * 4.f, m_Pos, m_Damage, m_Owner, WEAPON_LASER);
		break;
	}

	MarkForDestroy();
}

void CSkillMagicBolt::Tick()
{
	CCharacter *pOwnerChar = MMOWeaponOwnerChar(GameServer(), m_Owner);
	if(!pOwnerChar)
	{
		MarkForDestroy();
		return;
	}

	const float Speed = (m_Effect == SKILL_BOLT_SMOKE_HOOK) ? 28.f : (m_Effect == SKILL_BOLT_WAND ? 26.f : 22.f);

	if(m_Effect == SKILL_BOLT_SMOKE_HOOK)
	{
		m_Pos += m_Direction * Speed;
		m_Traveled += Speed;

		if(GameServer()->Collision()->CheckPoint(m_Pos) || m_Traveled >= m_MaxTravel)
		{
			MarkForDestroy();
			return;
		}

		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pChar = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
				continue;
			if(distance(m_Pos, pChar->GetPos()) < 28.f)
			{
				Impact(pChar);
				return;
			}
		}

		if(--m_LifeSpan <= 0)
			MarkForDestroy();
		return;
	}

	CCharacter *pTarget = nullptr;
	if(m_TrackedCID >= 0 && m_TrackedCID < MAX_CLIENTS)
	{
		CPlayer *pPl = GameServer()->m_apPlayers[m_TrackedCID];
		if(pPl && pPl->GetCharacter() && MMOWeaponTargetValid(GameServer(), m_Owner, pPl->GetCharacter()))
			pTarget = pPl->GetCharacter();
	}

	if(!pTarget)
	{
		float BestDist = 2400.f;
		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pChar = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(GameServer(), m_Owner, pChar))
				continue;
			const float Dist = distance(m_Pos, pChar->GetPos());
			if(Dist < BestDist)
			{
				BestDist = Dist;
				pTarget = pChar;
				m_TrackedCID = pChar->GetCID();
			}
		}
	}

	if(pTarget)
		m_Direction = normalize(pTarget->GetPos() - m_Pos);

	m_Pos += m_Direction * Speed;

	if(GameServer()->Collision()->CheckPoint(m_Pos))
	{
		MarkForDestroy();
		return;
	}

	if(pTarget && distance(m_Pos, pTarget->GetPos()) < 24.f)
		Impact(pTarget);

	if(--m_LifeSpan <= 0)
		MarkForDestroy();
}

void CSkillMagicBolt::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	vec2 From = m_Pos - m_Direction * (m_Effect == SKILL_BOLT_WAND ? 32.f : 24.f);
	CNetObj_Laser *pLaser = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pLaser)
		return;

	pLaser->m_X = round_to_int(m_Pos.x);
	pLaser->m_Y = round_to_int(m_Pos.y);
	pLaser->m_FromX = round_to_int(From.x);
	pLaser->m_FromY = round_to_int(From.y);
	pLaser->m_StartTick = m_StartTick;

	const array<int> *pCoreIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pCoreIds && pCoreIds->size() > 0)
	{
		const vec2 CoreTo = m_Pos + vec2(0.f, -6.f);
		CNetObj_Laser *pCore = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pCoreIds)[0], sizeof(CNetObj_Laser)));
		if(pCore)
		{
			pCore->m_X = round_to_int(m_Pos.x);
			pCore->m_Y = round_to_int(m_Pos.y);
			pCore->m_FromX = round_to_int(CoreTo.x);
			pCore->m_FromY = round_to_int(CoreTo.y);
			pCore->m_StartTick = m_StartTick;
		}
	}
}
