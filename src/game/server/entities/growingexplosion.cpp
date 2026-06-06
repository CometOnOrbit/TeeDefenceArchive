/* (c) Magnus Auvinen / TeeDefenceArchive */
#include <base/math.h>

#include <engine/shared/config.h>

#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "character.h"
#include "electro.h"
#include "growingexplosion.h"

CGrowingExplosion::CGrowingExplosion(CGameWorld *pGameWorld, vec2 Pos, vec2 Dir, int Owner, int Radius, int ExplosionEffect, bool Fusion)
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
	m_Fusion = Fusion;
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
				m_pGrowingMap[j * m_GrowingMap_Length + i] = -2;
			else
				m_pGrowingMap[j * m_GrowingMap_Length + i] = -1;
			m_pGrowingMapVec[j * m_GrowingMap_Length + i] = vec2(0, 0);
		}
	}
	m_pGrowingMap[m_MaxGrowing * m_GrowingMap_Length + m_MaxGrowing] = Server()->Tick();
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
			if(m_pGrowingMap[Idx] != -1)
				continue;

			const bool FromLeft = i > 0 && m_pGrowingMap[Idx - 1] < TickNow && m_pGrowingMap[Idx - 1] >= 0;
			const bool FromRight = i < m_GrowingMap_Length - 1 && m_pGrowingMap[Idx + 1] < TickNow && m_pGrowingMap[Idx + 1] >= 0;
			const bool FromTop = j > 0 && m_pGrowingMap[Idx - m_GrowingMap_Length] < TickNow && m_pGrowingMap[Idx - m_GrowingMap_Length] >= 0;
			const bool FromBottom = j < m_GrowingMap_Length - 1 && m_pGrowingMap[Idx + m_GrowingMap_Length] < TickNow && m_pGrowingMap[Idx + m_GrowingMap_Length] >= 0;
			if(!(FromLeft || FromRight || FromTop || FromBottom))
				continue;

			m_pGrowingMap[Idx] = TickNow;
			NewTile = true;
			vec2 TileCenter = m_SeedPos + vec2(32.0f * (i - m_MaxGrowing) - 16.0f + random_float() * 32.0f, 32.0f * (j - m_MaxGrowing) - 16.0f + random_float() * 32.0f);
			if(m_ExplosionEffect == GROWINGEXPLOSIONEFFECT_BOOM && random_float() < 0.25f)
			{
				const int Dmg = m_Fusion ? 8 : 4;
				CEntity *pDmgFrom = this;
				if(CCharacter *pOwnerChr = GameServer()->GetPlayerChar(m_Owner))
					pDmgFrom = pOwnerChr;
				GameWorld()->CreateExplosion(TileCenter, pDmgFrom, WEAPON_HAMMER, Dmg);
			}
			if(m_ExplosionEffect == GROWINGEXPLOSIONEFFECT_ELECTRIC)
			{
				vec2 End = TileCenter + vec2(random_float() * 32.0f - 16.0f, random_float() * 32.0f - 16.0f);
				new CElectro(GameWorld(), TileCenter, End, vec2(0, 0), 1);
			}
		}
	}

	if(!NewTile)
		return;

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
		if(m_pGrowingMap[k] < 0)
			continue;
		if(TickNow - m_pGrowingMap[k] >= Server()->TickSpeed() / 4)
			continue;

		if(!p->GetPlayer()->IsDummy() && p->GetPlayer()->GetZomb() <= 0)
			continue;

		const int Dmg = 5 + 20 * (m_MaxGrowing - minimum(TickNow - m_StartTick, m_MaxGrowing)) / maximum(1, m_MaxGrowing);
		p->TakeDamage(normalize(p->GetPos() - m_SeedPos) * 10.0f, m_SeedPos, Dmg, m_Owner, WEAPON_HAMMER);
		m_Hit[Cid] = true;
	}
}

void CGrowingExplosion::TickPaused()
{
	++m_StartTick;
}
