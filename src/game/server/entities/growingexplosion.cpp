/* (c) Magnus Auvinen / TeeDefenceArchive */
#include <base/math.h>

#include <engine/shared/config.h>
#include <generated/server_data.h>

#include <game/server/core/components/content/status_manager.h>
#include <game/server/core/tworld_controller.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "growingexplosion.h"

namespace {

constexpr int AvailableForGrow = -1;
constexpr int UnavailableTile = -2;

} // namespace

CGrowingExplosion::CGrowingExplosion(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir, int Owner, int Radius, int ExplosionEffect, bool Fusion,
	int TargetMode, int CustomDamage)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_GROWINGEXPLOSION, CGameWorld::ENTFLAG_CHILD, Pos, 0)
{
	m_MaxGrowing = maximum(1, Radius);
	m_GrowingMap_Length = 2 * m_MaxGrowing + 1;
	m_GrowingMap_Size = m_GrowingMap_Length * m_GrowingMap_Length;
	m_pGrowingMap = new int[m_GrowingMap_Size];
	m_pGrowingMapVec = new vec2[m_GrowingMap_Size];
	m_StartTick = Server()->Tick();
	m_Owner = Owner;
	m_ExplosionEffect = ExplosionEffect;
	m_TargetMode = TargetMode;
	m_CustomDamage = CustomDamage;
	m_Fusion = Fusion;
	m_VisualizedTiles = 0;
	mem_zero(m_Hit, sizeof(m_Hit));

	GameWorld()->InsertEntity(this);

	vec2 ExplosionTile = vec2(16.0f, 16.0f) + vec2((float)((int)round(m_Pos.x) / 32 * 32), (float)((int)round(m_Pos.y) / 32 * 32));
	if(GameServer()->Collision()->CheckPoint(ExplosionTile) && length(Dir) <= 1.1f)
	{
		m_SeedPos = vec2(16.0f, 16.0f) + vec2((float)((int)round(m_Pos.x + 32.0f * Dir.x) / 32 * 32), (float)((int)round(m_Pos.y + 32.0f * Dir.y) / 32 * 32));
	}
	else
		m_SeedPos = ExplosionTile;

	m_SeedX = (int)round(m_SeedPos.x) / 32;
	m_SeedY = (int)round(m_SeedPos.y) / 32;

	for(int j = 0; j < m_GrowingMap_Length; j++)
	{
		for(int i = 0; i < m_GrowingMap_Length; i++)
		{
			vec2 Tile = m_SeedPos + vec2(32.0f * (i - m_MaxGrowing), 32.0f * (j - m_MaxGrowing));
			if(GameServer()->Collision()->CheckPoint(Tile) || distance(Tile, m_SeedPos) > m_MaxGrowing * 32.0f)
				m_pGrowingMap[j * m_GrowingMap_Length + i] = UnavailableTile;
			else
				m_pGrowingMap[j * m_GrowingMap_Length + i] = AvailableForGrow;
			m_pGrowingMapVec[j * m_GrowingMap_Length + i] = vec2(0, 0);
		}
	}
	m_pGrowingMap[m_MaxGrowing * m_GrowingMap_Length + m_MaxGrowing] = Server()->Tick();

	if(m_ExplosionEffect == GROWINGEXPLOSIONEFFECT_ELECTRIC)
	{
		if(Dir.x || Dir.y)
		{
			const int DirX = Dir.x > 0 ? 1 : (Dir.x < 0 ? -1 : 0);
			const int DirY = Dir.y > 0 ? 1 : (Dir.y < 0 ? -1 : 0);
			if(DirX * Dir.x >= DirY * Dir.y)
				m_pGrowingMap[m_MaxGrowing * m_GrowingMap_Length + m_MaxGrowing + DirX] = AvailableForGrow;
			else
				m_pGrowingMap[(m_MaxGrowing + DirY) * m_GrowingMap_Length + m_MaxGrowing] = AvailableForGrow;
		}

		vec2 EndPoint = m_SeedPos + vec2(-16.0f + random_float() * 32.0f, -16.0f + random_float() * 32.0f);
		m_pGrowingMapVec[m_MaxGrowing * m_GrowingMap_Length + m_MaxGrowing] = EndPoint;
	}

	switch(m_ExplosionEffect)
	{
	case GROWINGEXPLOSIONEFFECT_FREEZE:
		if(random_float() < 0.1f)
			GameWorld()->CreateHammerHit(m_SeedPos);
		break;
	case GROWINGEXPLOSIONEFFECT_POISON:
		if(random_float() < 0.1f)
			GameWorld()->CreateDeath(m_SeedPos, m_Owner);
		break;
	default:
		break;
	}
}

CGrowingExplosion::~CGrowingExplosion()
{
	delete[] m_pGrowingMap;
	delete[] m_pGrowingMapVec;
}

void CGrowingExplosion::Reset()
{
	GameWorld()->DestroyEntity(this);
}

int CGrowingExplosion::GetActualDamage()
{
	if(m_CustomDamage >= 0)
		return m_CustomDamage;

	return 5 + 20 * (m_MaxGrowing - minimum(Server()->Tick() - m_StartTick, m_MaxGrowing)) / maximum(1, m_MaxGrowing);
}

bool CGrowingExplosion::IsHostileTarget(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive() || !pChr->GetPlayer())
		return false;

	if(m_TargetMode == GE_TARGET_TD)
	{
		CPlayer *pPl = pChr->GetPlayer();
		return pPl->IsDummy() || pPl->GetZomb() > 0;
	}

	if(m_TargetMode == GE_TARGET_MMO_HOSTILE)
		return MMOWeaponTargetValid(GameServer(), m_Owner, pChr);

	return false;
}

bool CGrowingExplosion::IsHealTarget(CCharacter *pChr)
{
	if(!pChr || !pChr->IsAlive() || !pChr->GetPlayer() || m_TargetMode != GE_TARGET_MMO_ALLY)
		return false;

	CPlayer *pPl = pChr->GetPlayer();
	if(pPl->IsDummy() || pPl->m_pMMOBotData || pPl->GetTeam() == TEAM_SPECTATORS)
		return false;

	CPlayer *pOwner = GameServer()->m_apPlayers[m_Owner];
	if(!pOwner)
		return false;

	return pPl->GetTeam() == pOwner->GetTeam();
}

void CGrowingExplosion::ProcessShockwaveHit(CCharacter *pCharacter)
{
	if(!pCharacter)
		return;

	const float Power = m_MaxGrowing / 16.0f;
	float InnerRadius = 96.0f;
	const float OuterRadius = m_MaxGrowing * 32.0f;
	if(InnerRadius >= OuterRadius)
		InnerRadius = OuterRadius * 0.9f;

	vec2 Diff = pCharacter->GetPos() - m_SeedPos;
	vec2 ForceDir(0, 1);
	float l = length(Diff);
	if(l)
		ForceDir = normalize(Diff);

	const float Ratio = (l - InnerRadius) / (OuterRadius - InnerRadius);
	l = 1.f - clamp(Ratio, 0.f, 1.f);
	float Dmg = 10.f * l * Power;
	if(m_CustomDamage >= 0)
		Dmg = (float)m_CustomDamage * l;

	int DamageFrom = m_Owner;
	if(pCharacter->GetCID() == m_Owner)
	{
		Dmg *= 0.5f;
	}

	if(Dmg > 0.f)
		pCharacter->TakeDamage(ForceDir * Dmg * 2.f, m_SeedPos, (int)Dmg, DamageFrom, WEAPON_GRENADE);

	m_Hit[pCharacter->GetCID()] = true;
}

void CGrowingExplosion::Tick()
{
	if(IsMarkedForDestroy())
		return;

	const int TickNow = Server()->Tick();
	if(TickNow - m_StartTick > m_MaxGrowing)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	bool NewTile = false;

	for(int j = 0; j < m_GrowingMap_Length; j++)
	{
		for(int i = 0; i < m_GrowingMap_Length; i++)
		{
			const int Idx = j * m_GrowingMap_Length + i;
			if(m_pGrowingMap[Idx] != AvailableForGrow)
				continue;

			const bool FromLeft = i > 0 && m_pGrowingMap[Idx - 1] < TickNow && m_pGrowingMap[Idx - 1] >= 0;
			const bool FromRight = i < m_GrowingMap_Length - 1 && m_pGrowingMap[Idx + 1] < TickNow && m_pGrowingMap[Idx + 1] >= 0;
			const bool FromTop = j > 0 && m_pGrowingMap[Idx - m_GrowingMap_Length] < TickNow && m_pGrowingMap[Idx - m_GrowingMap_Length] >= 0;
			const bool FromBottom = j < m_GrowingMap_Length - 1 && m_pGrowingMap[Idx + m_GrowingMap_Length] < TickNow && m_pGrowingMap[Idx + m_GrowingMap_Length] >= 0;
			if(!(FromLeft || FromRight || FromTop || FromBottom))
				continue;

			m_pGrowingMap[Idx] = TickNow;
			NewTile = true;
			m_VisualizedTiles++;

			vec2 TileCenter = m_SeedPos + vec2(32.0f * (i - m_MaxGrowing) - 16.0f + random_float() * 32.0f, 32.0f * (j - m_MaxGrowing) - 16.0f + random_float() * 32.0f);

			switch(m_ExplosionEffect)
			{
			case GROWINGEXPLOSIONEFFECT_FREEZE:
				if(random_float() < 0.1f)
					GameWorld()->CreateHammerHit(TileCenter);
				break;
			case GROWINGEXPLOSIONEFFECT_POISON:
				if(random_float() < 0.1f)
					GameWorld()->CreateDeath(TileCenter, m_Owner);
				break;
			case GROWINGEXPLOSIONEFFECT_HEAL:
				if(m_VisualizedTiles % 8 == 0)
					GameWorld()->CreateDeath(TileCenter, m_Owner);
				break;
			case GROWINGEXPLOSIONEFFECT_BOOM:
				if(random_float() < (m_TargetMode == GE_TARGET_TD ? 0.25f : 0.2f))
				{
					CEntity *pDmgFrom = this;
					if(CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner))
						pDmgFrom = pOwnerChr;
					const int Dmg = m_TargetMode == GE_TARGET_TD ? (m_Fusion ? 8 : 4) : maximum(1, GetActualDamage() / 2);
					GameWorld()->CreateExplosion(TileCenter, pDmgFrom, WEAPON_HAMMER, Dmg);
				}
				break;
			case GROWINGEXPLOSIONEFFECT_ELECTRIC:
			{
				vec2 EndPoint = m_SeedPos + vec2(32.0f * (i - m_MaxGrowing) - 16.0f + random_float() * 32.0f, 32.0f * (j - m_MaxGrowing) - 16.0f + random_float() * 32.0f);
				m_pGrowingMapVec[Idx] = EndPoint;

				vec2 aPossibleStartPoints[4];
				int NumStartPoints = 0;
				if(FromLeft)
					aPossibleStartPoints[NumStartPoints++] = m_pGrowingMapVec[Idx - 1];
				if(FromRight)
					aPossibleStartPoints[NumStartPoints++] = m_pGrowingMapVec[Idx + 1];
				if(FromTop)
					aPossibleStartPoints[NumStartPoints++] = m_pGrowingMapVec[Idx - m_GrowingMap_Length];
				if(FromBottom)
					aPossibleStartPoints[NumStartPoints++] = m_pGrowingMapVec[Idx + m_GrowingMap_Length];

				if(NumStartPoints > 0)
				{
					const vec2 StartPoint = aPossibleStartPoints[random_int() % NumStartPoints];
					GameWorld()->CreateLaserDot(StartPoint, EndPoint, Server()->TickSpeed() / 6);
				}

				if(random_float() < 0.1f)
					GameWorld()->CreateSound(EndPoint, SOUND_LASER_BOUNCE);
			}
			break;
			default:
				break;
			}
		}
	}

	if(NewTile && m_ExplosionEffect == GROWINGEXPLOSIONEFFECT_POISON && random_float() < 0.1f)
		GameWorld()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	if(!NewTile)
		return;

	CStatusManager *pStatusMgr = GameServer()->Core() ? GameServer()->Core()->StatusManager() : nullptr;
	const int HitWindow = Server()->TickSpeed() / 4;

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *p = static_cast<CCharacter *>(r.front());
		if(!p || !p->IsAlive() || !p->GetPlayer())
			continue;

		const int Cid = p->GetPlayer()->GetCID();
		if(m_Hit[Cid])
			continue;

		const int tileX = m_MaxGrowing + (int)round(p->GetPos().x) / 32 - m_SeedX;
		const int tileY = m_MaxGrowing + (int)round(p->GetPos().y) / 32 - m_SeedY;
		if(tileX < 0 || tileX >= m_GrowingMap_Length || tileY < 0 || tileY >= m_GrowingMap_Length)
			continue;

		const int k = tileY * m_GrowingMap_Length + tileX;
		if(m_pGrowingMap[k] < 0 || TickNow - m_pGrowingMap[k] >= HitWindow)
			continue;

		if(m_ExplosionEffect == GROWINGEXPLOSIONEFFECT_HEAL)
		{
			if(!IsHealTarget(p))
				continue;
			const int Heal = maximum(1, m_CustomDamage >= 0 ? m_CustomDamage : 2);
			p->IncreaseHealth(Heal);
			m_Hit[Cid] = true;
			continue;
		}

		if(!IsHostileTarget(p))
			continue;

		switch(m_ExplosionEffect)
		{
		case GROWINGEXPLOSIONEFFECT_BOOM:
			if(m_TargetMode == GE_TARGET_MMO_HOSTILE)
				ProcessShockwaveHit(p);
			else
			{
				const int Dmg = GetActualDamage();
				p->TakeDamage(normalize(p->GetPos() - m_SeedPos) * 10.0f, m_SeedPos, Dmg, m_Owner, WEAPON_HAMMER);
				m_Hit[Cid] = true;
			}
			break;
		case GROWINGEXPLOSIONEFFECT_ELECTRIC:
		{
			const int Dmg = GetActualDamage();
			if(Dmg)
				p->TakeDamage(normalize(p->GetPos() - m_SeedPos) * 4.0f, m_SeedPos, Dmg, m_Owner, WEAPON_LASER);
			m_Hit[Cid] = true;
			break;
		}
		case GROWINGEXPLOSIONEFFECT_FREEZE:
			if(pStatusMgr)
			{
				const int SlowTicks = Server()->TickSpeed() * 3;
				pStatusMgr->ApplyStatus(p, "frost", 1, SlowTicks, 0.08f);
			}
			m_Hit[Cid] = true;
			break;
		case GROWINGEXPLOSIONEFFECT_POISON:
			if(pStatusMgr)
			{
				const int PoisonTicks = Server()->TickSpeed() * 5;
				const int Stacks = maximum(1, m_CustomDamage >= 0 ? m_CustomDamage : 2);
				pStatusMgr->ApplyStatus(p, "poison", Stacks, PoisonTicks, 0.86f);
			}
			m_Hit[Cid] = true;
			break;
		default:
			break;
		}
	}
}

void CGrowingExplosion::TickPaused()
{
	++m_StartTick;
}
