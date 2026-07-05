#include "skill_smoke_veil.h"

#include <base/math.h>

#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/gamecontext.h>

#include "../character.h"
#include "../mmo/mmo_weapon_common.h"

CSkillSmokeVeil::CSkillSmokeVeil(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int DurationTicks, int SlowTicks, float SlowFactor)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_GROWINGEXPLOSION, 0, Pos, (int)maximum(64.f, Radius))
{
	m_Pos = Pos;
	m_Owner = OwnerCID;
	m_Radius = maximum(96.f, Radius);
	m_MaxLife = maximum(1, DurationTicks);
	m_Life = 0;
	m_SlowTicks = maximum(0, SlowTicks);
	m_SlowFactor = clamp(SlowFactor, 0.f, 1.f);

	GameWorld()->InsertEntity(this);
}

void CSkillSmokeVeil::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	CCollision *pColl = GameServer()->Collision();
	if(pColl)
	{
		if(m_Life % 2 == 0)
		{
			if(pColl->CheckPoint(m_Pos.x + 32, m_Pos.y + 32) == 0)
				GameWorld()->CreateDeath(m_Pos + vec2(32.f, 32.f), -1);
			if(pColl->CheckPoint(m_Pos.x - 32, m_Pos.y - 32) == 0)
				GameWorld()->CreateDeath(m_Pos + vec2(-32.f, -32.f), -1);
		}
		else
		{
			if(pColl->CheckPoint(m_Pos.x - 32, m_Pos.y + 32) == 0)
				GameWorld()->CreateDeath(m_Pos + vec2(-32.f, 32.f), -1);
			if(pColl->CheckPoint(m_Pos.x + 32, m_Pos.y - 32) == 0)
				GameWorld()->CreateDeath(m_Pos + vec2(32.f, -32.f), -1);
		}
	}

	if(m_SlowTicks > 0 && GameServer()->Core() && GameServer()->Core()->StatusManager() && (m_Life % maximum(1, Server()->TickSpeed() / 4)) == 0)
	{
		for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
		{
			CCharacter *pTarget = static_cast<CCharacter *>(r.front());
			if(!MMOWeaponTargetValid(GameServer(), m_Owner, pTarget))
				continue;
			if(distance(m_Pos, pTarget->GetPos()) > m_Radius)
				continue;
			GameServer()->Core()->StatusManager()->ApplyStatus(pTarget, "frost", 1, m_SlowTicks, m_SlowFactor);
		}
	}

	m_Life++;
	if(m_Life > m_MaxLife)
		MarkForDestroy();
}
