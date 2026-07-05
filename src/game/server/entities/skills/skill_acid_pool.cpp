#include "skill_acid_pool.h"
#include "skill_spawn.h"
#include "skill_vfx_common.h"

#include <base/math.h>

#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <generated/protocol.h>
#include <generated/server_data.h>

#include "../character.h"

static constexpr int ACID_RING_SEGMENTS = 10;

CSkillAcidPool::CSkillAcidPool(CGameWorld *pGameWorld, int OwnerCID, vec2 Pos, float Radius, int PoisonStacks, int DurationTicks, float SlowMul)
	: CChildEntity(pGameWorld, CGameWorld::ENTTYPE_GROWINGEXPLOSION, 0, Pos, (int)maximum(64.f, Radius))
{
	m_Owner = OwnerCID;
	m_Pos = Pos;
	m_Radius = maximum(64.f, Radius);
	m_LifeSpan = maximum(1, DurationTicks);
	m_TickInterval = maximum(1, Server()->TickSpeed() / 2);
	m_NextPulseTick = Server()->Tick() + m_TickInterval;
	m_PoisonStacks = maximum(1, PoisonStacks);
	m_StartTick = Server()->Tick();
	m_SlowMul = clamp(SlowMul, 0.1f, 1.f);

	AddSnappingGroupIds(SNAP_GROUP_RING, ACID_RING_SEGMENTS);
	AddSnappingGroupIds(SNAP_GROUP_CENTER, 1);

	GameWorld()->CreateSound(m_Pos, SOUND_GRENADE_EXPLODE);
	GameWorld()->InsertEntity(this);
}

void CSkillAcidPool::Pulse()
{
	CGameContext *pGS = GameServer();
	if(!pGS || !pGS->Core() || !pGS->Core()->StatusManager())
		return;

	for(int i = 0; i < 3; i++)
	{
		const float A = random_float() * 2.f * pi;
		const vec2 ParticlePos = m_Pos + vec2(cosf(A), sinf(A)) * m_Radius * 0.5f * random_float();
		GameWorld()->CreateDeath(ParticlePos, m_Owner);
	}

	const int SlowTicks = maximum(m_TickInterval + 2, Server()->TickSpeed() / 4);
	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChar = static_cast<CCharacter *>(r.front());
		if(!MMOWeaponTargetValid(pGS, m_Owner, pChar))
			continue;
		if(distance(m_Pos, pChar->GetPos()) > m_Radius)
			continue;
		pGS->Core()->StatusManager()->ApplyStatus(pChar, "poison", m_PoisonStacks, Server()->TickSpeed() * 2);
		pGS->Core()->StatusManager()->ApplyStatus(pChar, "frost", 1, SlowTicks, m_SlowMul);
	}
	SpawnSkillHitBurst(GameWorld(), m_Pos, m_Radius * 0.35f, SKILL_VFX_POISON, Server()->TickSpeed() / 4, 6);
}

void CSkillAcidPool::Tick()
{
	if(!MMOWeaponOwnerChar(GameServer(), m_Owner))
	{
		MarkForDestroy();
		return;
	}

	if(Server()->Tick() >= m_NextPulseTick)
	{
		Pulse();
		m_NextPulseTick = Server()->Tick() + m_TickInterval;
	}

	if(--m_LifeSpan <= 0)
		MarkForDestroy();
}

void CSkillAcidPool::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	const int Elapsed = Server()->Tick() - m_StartTick;
	const float Pulse = 0.85f + 0.15f * sinf((float)Elapsed / (float)maximum(1, Server()->TickSpeed() / 3) * pi);
	const float DrawRadius = maximum(24.f, m_Radius * Pulse);

	const array<int> *pCenterIds = FindSnappingGroupIds(SNAP_GROUP_CENTER);
	if(pCenterIds && pCenterIds->size() > 0)
		SnapLaserDot(Server(), (*pCenterIds)[0], m_Pos, m_StartTick);

	const array<int> *pRingIds = FindSnappingGroupIds(SNAP_GROUP_RING);
	if(!pRingIds || (int)pRingIds->size() < ACID_RING_SEGMENTS)
		return;

	const float AngleStep = 2.f * pi / (float)ACID_RING_SEGMENTS;
	for(int i = 0; i < ACID_RING_SEGMENTS; i++)
	{
		const int NextIndex = (i + 1) % ACID_RING_SEGMENTS;
		const vec2 CurrentPos = m_Pos + vec2(DrawRadius * cosf(AngleStep * i), DrawRadius * sinf(AngleStep * i));
		const vec2 NextPos = m_Pos + vec2(DrawRadius * cosf(AngleStep * NextIndex), DrawRadius * sinf(AngleStep * NextIndex));
		CNetObj_Laser *pRing = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, (*pRingIds)[i], sizeof(CNetObj_Laser)));
		if(pRing)
		{
			pRing->m_X = round_to_int(CurrentPos.x);
			pRing->m_Y = round_to_int(CurrentPos.y);
			pRing->m_FromX = round_to_int(NextPos.x);
			pRing->m_FromY = round_to_int(NextPos.y);
			pRing->m_StartTick = m_StartTick;
		}
	}
}
