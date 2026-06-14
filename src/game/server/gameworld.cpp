/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <generated/server_data.h>

#include "entities/character.h"
#include "entities/growingexplosion.h"
#include "entities/plasma.h"
#include "entity.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "gameworld.h"
#include "player.h"

#include <algorithm>

//////////////////////////////////////////////////
// game world
//////////////////////////////////////////////////
CGameWorld::CGameWorld()
{
	m_pGameServer = 0x0;
	m_pConfig = 0x0;
	m_pServer = 0x0;

	for(int i = 0; i < NUM_ENTTYPES; i++)
	{
		m_alpEntityLists[i].hint_size(16);
	}
	m_lpFlagEntityList.hint_size(8);
}

CGameWorld::~CGameWorld()
{
	// delete all entities
	for(int i = 0; i < NUM_ENTTYPES; i++)
		while(m_alpEntityLists[i].size())
			delete m_alpEntityLists[i][0];
}

void CGameWorld::SetGameServer(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();
	m_Events.SetGameServer(pGameServer);
}

CGameWorld::TypeRange CGameWorld::DoTypeRange(int Type)
{
	dbg_assert(Type >= 0 && Type < NUM_ENTTYPES, "out of range");
	return m_alpEntityLists[Type].all();
}

CGameWorld::FlagRange CGameWorld::DoFlagRange(int Flag)
{
	return FlagRange(m_lpFlagEntityList.all(), CFlagCheck(Flag));
}

int CGameWorld::FindEntities(vec2 Pos, float Radius, array<CEntity *> &lpEnts, int Type)
{
	if(Type < 0 || Type >= NUM_ENTTYPES)
		return 0;

	int Num = 0;
	for(auto &pEnt : m_alpEntityLists[Type])
	{
		if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
		{
			lpEnts.add(pEnt);
			Num++;
		}
	}

	return Num;
}

int CGameWorld::FindFlagEntities(vec2 Pos, float Radius, array<CEntity *> &lpEnts, int Flag)
{
	CFlagCheck Check(Flag);
	int Num = 0;
	for(auto &pEnt : m_lpFlagEntityList)
	{
		if(!Check(pEnt)) continue;
		if(distance(pEnt->m_Pos, Pos) < Radius + pEnt->m_ProximityRadius)
		{
			lpEnts.add(pEnt);
			Num++;
		}
	}

	return Num;
}

void CGameWorld::InsertEntity(CEntity *pEnt)
{
	m_alpEntityLists[pEnt->m_ObjType].add(pEnt);
	if(pEnt->ObjFlag() != 0)
		m_lpFlagEntityList.add(pEnt);
}

void CGameWorld::DestroyEntity(CEntity *pEnt)
{
	pEnt->MarkForDestroy();
}

void CGameWorld::RemoveEntity(CEntity *pEnt)
{
	m_alpEntityLists[pEnt->m_ObjType].remove_fast(pEnt);
	m_lpFlagEntityList.remove_fast(pEnt);

	if(m_alpEntityLists[pEnt->m_ObjType].size() > 32 && m_alpEntityLists[pEnt->m_ObjType].used_memory() < m_alpEntityLists[pEnt->m_ObjType].memusage() / 3) // lower than 1/3
	{
		m_alpEntityLists[pEnt->m_ObjType].optimize();
	}
	if(m_lpFlagEntityList.size() > 32 && m_lpFlagEntityList.used_memory() < m_lpFlagEntityList.memusage() / 3) // lower than 1/3
	{
		m_lpFlagEntityList.optimize();
	}
}

//
void CGameWorld::Snap(int SnappingClient)
{
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(auto &pEnt : m_alpEntityLists[i])
			if(pEnt)
				pEnt->Snap(SnappingClient);
	m_Events.Snap(SnappingClient);
}

void CGameWorld::PostSnap()
{
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(auto &pEnt : m_alpEntityLists[i])
			if(pEnt)
				pEnt->PostSnap();
	m_Events.Clear();
}

void CGameWorld::RemoveEntities()
{
	// destroy objects marked for destruction
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(int j = 0; j < m_alpEntityLists[i].size(); j++)
		{
			if(m_alpEntityLists[i][j]->IsMarkedForDestroy())
			{
				m_alpEntityLists[i][j]->Destroy();
				j--;
			}
		}
}

void CGameWorld::Tick()
{
	// update all objects (index loop: Tick() may InsertEntity into any list)
	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(int j = 0; j < m_alpEntityLists[i].size(); j++)
			if(m_alpEntityLists[i][j])
				m_alpEntityLists[i][j]->Tick();

	for(int i = 0; i < NUM_ENTTYPES; i++)
		for(int j = 0; j < m_alpEntityLists[i].size(); j++)
			if(m_alpEntityLists[i][j])
				m_alpEntityLists[i][j]->TickDefered();

	RemoveEntities();
}

CEntity *CGameWorld::IntersectEntity(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, int Type, CEntity *pNotThis)
{
	// Find other entities
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_alpEntityLists[Type])
	{
		if(pEnt == pNotThis)
			continue;

		vec2 IntersectPos = closest_point_on_line(Pos0, Pos1, pEnt->m_Pos);
		float Len = distance(pEnt->m_Pos, IntersectPos);
		if(Len < pEnt->GetProximityRadius() + Radius)
		{
			Len = distance(Pos0, IntersectPos);
			if(Len < ClosestLen)
			{
				NewPos = IntersectPos;
				ClosestLen = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

CCharacter *CGameWorld::IntersectCharacter(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, CCharacter *pNotThis)
{
	vec2 At;
	CEntity *pHit = IntersectEntity(Pos0, Pos1, Radius, At, ENTTYPE_CHARACTER, pNotThis);
	if(!pHit)
		return nullptr;
	NewPos = At;
	return static_cast<CCharacter *>(pHit);
}

CEntity *CGameWorld::IntersectFlagEntity(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, int Flag, CEntity *pNotThis)
{
	CFlagCheck Check(Flag);
	// Find other entities
	float ClosestLen = distance(Pos0, Pos1) * 100.0f;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_lpFlagEntityList)
	{
		if(pEnt == pNotThis || !Check(pEnt))
			continue;

		vec2 IntersectPos = closest_point_on_line(Pos0, Pos1, pEnt->m_Pos);
		float Len = distance(pEnt->m_Pos, IntersectPos);
		if(Len < pEnt->GetProximityRadius() + Radius)
		{
			Len = distance(Pos0, IntersectPos);
			if(Len < ClosestLen)
			{
				NewPos = IntersectPos;
				ClosestLen = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

CEntity *CGameWorld::IntersectFlagEntitySkippingTurrets(vec2 Pos0, vec2 Pos1, float Radius, vec2 &NewPos, int Flag, CEntity *pNotThis)
{
	const float MaxDist = distance(Pos0, Pos1);
	if(MaxDist < 0.001f)
		return IntersectFlagEntity(Pos0, Pos1, Radius, NewPos, Flag, pNotThis);

	const vec2 Dir = normalize(Pos1 - Pos0);
	vec2 Start = Pos0;
	float Traveled = 0.f;

	while(Traveled < MaxDist)
	{
		vec2 At;
		CEntity *pHit = IntersectFlagEntity(Start, Pos1, Radius, At, Flag, pNotThis);
		if(!pHit)
			return nullptr;
		if(pHit->ObjType() != ENTTYPE_TURRET)
		{
			NewPos = At;
			return pHit;
		}

		const float Step = distance(Start, At) + pHit->GetProximityRadius() + Radius + 2.0f;
		if(Step < 0.5f)
			return nullptr;
		Traveled += Step;
		Start = Pos0 + Dir * minimum(Traveled, MaxDist);
	}
	return nullptr;
}

CEntity *CGameWorld::ClosestEntity(vec2 Pos, float Radius, int Type, CEntity *pNotThis)
{
	// Find other entities
	float ClosestRange = Radius * 2;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_alpEntityLists[Type])
	{
		if(pEnt == pNotThis)
			continue;

		float Len = distance(Pos, pEnt->m_Pos);
		if(Len < pEnt->m_ProximityRadius + Radius)
		{
			if(Len < ClosestRange)
			{
				ClosestRange = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

CEntity *CGameWorld::ClosestFlagEntity(vec2 Pos, float Radius, int Flag, CEntity *pNotThis)
{
	CFlagCheck Check(Flag);
	// Find other entities
	float ClosestRange = Radius * 2;
	CEntity *pClosest = 0;

	for(auto &pEnt : m_lpFlagEntityList)
	{
		if(pEnt == pNotThis || !Check(pEnt))
			continue;

		float Len = distance(Pos, pEnt->m_Pos);
		if(Len < pEnt->m_ProximityRadius + Radius)
		{
			if(Len < ClosestRange)
			{
				ClosestRange = Len;
				pClosest = pEnt;
			}
		}
	}

	return pClosest;
}

bool CGameWorld::CFlagCheck::operator()(CEntity *&pEntity) const { return pEntity->ObjFlag() & m_ConditionFlag; }

void CGameWorld::CreateDamage(vec2 Pos, int Id, vec2 Source, int HealthAmount, int ArmorAmount, bool Self)
{
	float f = angle(Source);
	CNetEvent_Damage *pEvent = (CNetEvent_Damage *) m_Events.Create(NETEVENTTYPE_DAMAGE, sizeof(CNetEvent_Damage));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_ClientID = Id;
		pEvent->m_Angle = (int) (f * 256.0f);
		pEvent->m_HealthAmount = HealthAmount;
		pEvent->m_ArmorAmount = ArmorAmount;
		pEvent->m_Self = Self;
	}
}

void CGameWorld::CreateFloatingAmount(vec2 Pos, int ClientID, int Amount, int64 Mask)
{
	if(Amount <= 0)
		return;

	int HealthAmount = 0;
	int ArmorAmount = 0;
	if(Amount < 10)
		HealthAmount = Amount;
	else
	{
		HealthAmount = minimum(9, Amount / 10);
		ArmorAmount = Amount % 10;
	}

	float f = angle(vec2(0.f, -1.f));
	CNetEvent_Damage *pEvent = (CNetEvent_Damage *)m_Events.Create(NETEVENTTYPE_DAMAGE, sizeof(CNetEvent_Damage), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
		pEvent->m_Angle = (int)(f * 256.0f);
		pEvent->m_HealthAmount = HealthAmount;
		pEvent->m_ArmorAmount = ArmorAmount;
		pEvent->m_Self = false;
	}
}

void CGameWorld::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *) m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
	}
}

bool CGameWorld::IsHumanDefenderOwner(int OwnerCid)
{
	if(OwnerCid < 0 || OwnerCid >= MAX_CLIENTS || !GameServer())
		return false;
	CPlayer *pP = GameServer()->m_apPlayers[OwnerCid];
	return pP && !pP->IsDummy() && pP->GetTeam() != TEAM_BLUE;
}

int CGameWorld::DamageOwnerFromEntity(CEntity *pFrom) const
{
	if(!pFrom)
		return -1;
	if(pFrom->ObjType() == ENTTYPE_CHARACTER)
		return static_cast<CCharacter *>(pFrom)->GetCID();
	if(pFrom->ObjType() == ENTTYPE_GROWINGEXPLOSION)
		return static_cast<CGrowingExplosion *>(pFrom)->GetOwner();
	if(pFrom->ObjType() == ENTTYPE_PLASMA)
		return static_cast<CPlasma *>(pFrom)->GetOwner();
	if(pFrom->ObjFlag() & ENTFLAG_CHILD)
		return static_cast<const CChildEntity *>(pFrom)->GetOwner();
	return -1;
}

void CGameWorld::CreateExplosion(vec2 Pos, CEntity *pOwner, int Weapon, int MaxDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *) m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
	}

	const int OwnerCid = DamageOwnerFromEntity(pOwner);
	const bool HumanDefender = IsHumanDefenderOwner(OwnerCid);

	// deal damage
	array<CEntity *> lpEnts;
	lpEnts.hint_size(8);
	float Radius = g_pData->m_Explosion.m_Radius;
	float InnerRadius = 48.0f;
	float MaxForce = g_pData->m_Explosion.m_MaxForce;
	const int Num = FindFlagEntities(Pos, Radius, lpEnts, CGameWorld::ENTFLAG_HITABLE);
	for(int i = 0; i < Num; i++)
	{
		CEntity *pEnt = lpEnts[i];
		if(!pEnt || pEnt->IsMarkedForDestroy())
			continue;
		const int Type = pEnt->ObjType();
		if(Type != ENTTYPE_CHARACTER && Type != ENTTYPE_TOWERMAIN && Type != ENTTYPE_TURRET && Type != ENTTYPE_SPIDERLEG)
			continue;
		if(HumanDefender && (Type == ENTTYPE_TOWERMAIN || Type == ENTTYPE_TURRET))
			continue;

		vec2 Diff = pEnt->GetPos() - Pos;
		vec2 Force(0, MaxForce);
		float l = length(Diff);
		if(l)
			Force = normalize(Diff) * MaxForce;
		float Factor = 1 - clamp((l - InnerRadius) / (Radius - InnerRadius), 0.0f, 1.0f);
		if((int) (Factor * MaxDamage))
			static_cast<CHitableEntity *>(pEnt)->TakeHit(Force * Factor, Diff * -1, (int) (Factor * MaxDamage), pOwner, Weapon);
	}
}

void CGameWorld::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *) m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int) Pos.x;
		ev->m_Y = (int) Pos.y;
	}
}

void CGameWorld::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *) m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameWorld::CreateSound(vec2 Pos, int Sound, int64 Mask)
{
	if(Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *) m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int) Pos.x;
		pEvent->m_Y = (int) Pos.y;
		pEvent->m_SoundID = Sound;
	}
}