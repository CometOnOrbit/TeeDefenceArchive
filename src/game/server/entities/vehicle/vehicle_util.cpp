#include "vehicle_util.h"

#include "vehicle.h"
#include "aircraft.h"

#include <engine/shared/protocol.h>
#include <generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/entities/projectile.h>
#include <game/server/gamecontext.h>
#include <game/server/gameworld.h>
#include <game/server/player.h>

bool VehicleInputPressed(int Prev, int Cur)
{
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	return ((Prev ^ Cur) & Cur) != 0;
}

bool VehicleSpotBlocked(CGameWorld *pWorld, vec2 Pos, float Radius)
{
	for(CGameWorld::TypeRange r = pWorld->DoTypeRange(CGameWorld::ENTTYPE_AIRCRAFT); !r.empty(); r.pop_front())
	{
		CAircraft *pAircraft = static_cast<CAircraft *>(r.front());
		if(!pAircraft->IsDead() && distance(pAircraft->GetPos(), Pos) < Radius)
			return true;
	}
	return false;
}

void VehicleClearOccupant(CCharacter *pChr)
{
	if(!pChr)
		return;

	if(pChr->m_OnVehicle)
		pChr->m_VehicleDismountTick = pChr->Server()->Tick();

	pChr->m_OnVehicle = false;
	pChr->m_VehicleSeat = VEHICLE_SEAT_NONE;
}

void VehicleResetCharacterHook(CCharacter *pChr)
{
	if(!pChr)
		return;

	CCharacterCore *pCore = pChr->GetCore();
	pCore->m_HookState = HOOK_IDLE;
	pCore->m_HookedPlayer = -1;
	pCore->m_HookTick = 0;
	pCore->m_HookPos = pCore->m_Pos;
	pCore->m_HookDir = vec2(0.f, -1.f);
	pCore->m_Input.m_Hook = 0;
}

void VehicleDismount(int &Owner, vec2 &Vel, CCharacter *pChr)
{
	if(Owner < 0 || !pChr)
		return;

	VehicleClearOccupant(pChr);
	pChr->GetCore()->m_Vel = Vel;
	Owner = -1;
	Vel *= 0.5f;
}

void VehicleSyncCharacter(CCharacter *pChr, vec2 Pos, vec2 Vel, float RiderOffsetY)
{
	if(!pChr)
		return;

	vec2 RiderPos = vec2(Pos.x, Pos.y + RiderOffsetY);
	CCharacterCore *pCore = pChr->GetCore();
	pCore->m_Pos = RiderPos;
	pCore->m_Vel = Vel;
	pChr->SetCharacterPos(RiderPos);
	VehicleResetCharacterHook(pChr);
}

void VehicleApplyGravity(CGameContext *pGS, vec2 &Vel)
{
	if(!pGS)
		return;

	Vel.y += pGS->Tuning()->m_Gravity;
}

void VehicleApplyFriction(vec2 &Vel, float Friction)
{
	Vel.x *= Friction;
	Vel.y *= Friction;
}

bool VehicleTryAutoBoard(CGameWorld *pWorld, CEntity *pVehicle, int &Owner, int Team, float BoardRadius)
{
	CEntity *pEnt = pWorld->ClosestEntity(pVehicle->GetPos(), BoardRadius, CGameWorld::ENTTYPE_CHARACTER, pVehicle);
	CCharacter *pChr = pEnt ? static_cast<CCharacter *>(pEnt) : nullptr;
	if(!pChr || pChr->m_OnVehicle || pChr->IsFrozen())
		return false;

	CVehicle *pVehicleCast = static_cast<CVehicle *>(pVehicle);
	if(!pVehicleCast->CanBoard(pChr->GetPlayer()->GetTeam()))
		return false;
	if(Team >= 0 && pChr->GetPlayer()->GetTeam() != Team)
		return false;

	CGameContext *pGS = pWorld->GameServer();
	if(pGS->Server()->Tick() < pChr->m_VehicleDismountTick + pGS->Server()->TickSpeed())
		return false;

	pChr->m_OnVehicle = true;
	pChr->m_VehicleSeat = VEHICLE_SEAT_DRIVER;
	VehicleResetCharacterHook(pChr);
	Owner = pChr->GetPlayer()->GetCID();
	return true;
}

void VehicleOnCharacterDie(CGameContext *pGS, int ClientId)
{
	for(CGameWorld::TypeRange r = pGS->m_World.DoTypeRange(CGameWorld::ENTTYPE_AIRCRAFT); !r.empty(); r.pop_front())
	{
		CAircraft *pAircraft = static_cast<CAircraft *>(r.front());
		if(pAircraft->IsOccupiedBy(ClientId))
			pAircraft->HandleOccupantDismount(ClientId);
	}
}

CVehicle *VehicleFindByOccupant(CGameWorld *pWorld, int ClientId)
{
	for(CGameWorld::TypeRange r = pWorld->DoTypeRange(CGameWorld::ENTTYPE_AIRCRAFT); !r.empty(); r.pop_front())
	{
		CAircraft *pAircraft = static_cast<CAircraft *>(r.front());
		if(!pAircraft->IsDead() && pAircraft->IsOccupiedBy(ClientId))
			return pAircraft;
	}
	return nullptr;
}

void VehicleSpawnShrapnel(CGameContext *pGS, CGameWorld *pWorld, vec2 Pos, int Owner, int Team)
{
	(void)Team;
	const int Pellets = 12;
	for(int i = 0; i < Pellets; i++)
	{
		const float Angle = (float)i / (float)Pellets * 2.f * pi;
		const vec2 Dir = vec2(cosf(Angle), sinf(Angle));
		const float Speed = mix((float)pGS->Tuning()->m_ShotgunSpeeddiff, 1.0f, 0.85f);
		new CProjectile(pWorld, WEAPON_SHOTGUN, Owner, Pos, Dir * Speed,
			(int)(pGS->Server()->TickSpeed() * pGS->Tuning()->m_ShotgunLifetime), 1, 0, 0, -1, WEAPON_SHOTGUN);
	}
	pWorld->CreateSound(Pos, SOUND_SHOTGUN_FIRE);
}
