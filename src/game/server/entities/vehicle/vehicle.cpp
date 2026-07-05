#include "vehicle.h"
#include "aircraft.h"

#include "vehicle_util.h"
#include "vehicle_visual.h"

#include <generated/protocol.h>
#include <game/server/entities/character.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

CVehicle::CVehicle(CGameWorld *pGameWorld, int ObjType, vec2 Pos, int Team, int Health, int OwnerClientID)
: CEntity(pGameWorld, ObjType, 0, Pos, (int)VehicleScale(32.f))
{
	m_Health = Health;
	m_MaxHealth = Health;
	m_Team = Team;
	m_Driver = -1;
	m_OwnerClientID = OwnerClientID;
	m_Vel = vec2(0.f, 0.f);
	m_SpawnPos = Pos;
	m_Dead = false;
	const int WorldId = GameServer()->GetWorldID();
	for(int i = 0; i < 3; i++)
		m_aHealthBarIds[i] = Server()->SnapNewID(WorldId);
}

CVehicle::~CVehicle()
{
	const int WorldId = GameServer()->GetWorldID();
	for(int i = 0; i < 3; i++)
		Server()->SnapFreeID(m_aHealthBarIds[i], WorldId);
	ClearOwnerDeployRef();
	ForceDriverLeave();
}

CCharacter *CVehicle::DriverChar()
{
	if(m_Driver < 0)
		return nullptr;
	return GameServer()->GetPlayerChar(m_Driver);
}

void CVehicle::ApplyHorizontalInput(CCharacter *pDriver, int MaxSpeed, int Accel, float IdleDecay)
{
	if(!pDriver)
		return;

	if(pDriver->m_Input.m_Direction < 0)
		m_Vel.x = SaturatedAdd(-(float)MaxSpeed, (float)MaxSpeed, m_Vel.x, -(float)Accel);
	else if(pDriver->m_Input.m_Direction > 0)
		m_Vel.x = SaturatedAdd(-(float)MaxSpeed, (float)MaxSpeed, m_Vel.x, (float)Accel);
	else
		m_Vel.x *= IdleDecay;
}

void CVehicle::MoveBox(vec2 Size)
{
	vec2 NewPos = m_Pos;
	GameServer()->Collision()->MoveBox(&NewPos, &m_Vel, Size, 0.f);
	m_Pos = NewPos;
}

bool CVehicle::CanBoard(int Team) const
{
	(void)Team;
	return !m_Dead;
}

void CVehicle::TakeDamage(int Amount, int From)
{
	(void)From;
	if(Amount <= 0 || m_Dead)
		return;

	m_Health -= Amount;
	if(m_Health <= 0)
		Explode();
}

void CVehicle::SpawnShrapnel()
{
	VehicleSpawnShrapnel(GameServer(), GameWorld(), m_Pos, m_Driver >= 0 ? m_Driver : -1, m_Team);
}

void CVehicle::SnapHealthBar(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || m_Dead)
		return;

	const int Segments = 3;
	const int Filled = maximum(0, minimum(Segments, (m_Health * Segments + m_MaxHealth - 1) / maximum(1, m_MaxHealth)));
	const int WorldId = GameServer()->GetWorldID();
	for(int i = 0; i < Segments; i++)
	{
		if(i >= Filled)
			break;
		VehicleVisual::SnapPickup(Server(), WorldId, m_aHealthBarIds[i],
			vec2(m_Pos.x + (i - 1) * VehicleScale(10.f), m_Pos.y - VehicleScale(38.f)), PICKUP_HEALTH);
	}
}

void CVehicle::ClearOwnerDeployRef()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		CPlayer *pPlayer = GameServer()->m_apPlayers[i];
		if(pPlayer && pPlayer->m_pDeployedVehicle == this)
			pPlayer->m_pDeployedVehicle = nullptr;
	}
}

void CVehicle::Explode()
{
	if(m_Dead)
		return;

	const vec2 ExplosionPos = m_Pos;
	CCharacter *pDriver = DriverChar();

	m_Dead = true;
	m_Health = 0;
	ForceDriverLeave();
	GameWorld()->CreateExplosion(ExplosionPos, pDriver, WEAPON_GRENADE, 0);
	SpawnShrapnel();
	OnBeforeDestroy();
	ClearOwnerDeployRef();
	GameWorld()->DestroyEntity(this);
}

void CVehicle::OnDriverBoarded(CCharacter *pDriver)
{
	(void)pDriver;
}

void CVehicle::BoardDriver(CCharacter *pDriver)
{
	if(!pDriver || m_Dead || m_Driver >= 0)
		return;

	m_Driver = pDriver->GetPlayer()->GetCID();
	pDriver->m_OnVehicle = true;
	pDriver->m_VehicleSeat = VEHICLE_SEAT_DRIVER;
	VehicleResetCharacterHook(pDriver);
	OnDriverBoarded(pDriver);
}

void CVehicle::Tick()
{
	if(m_Dead)
		return;

	if(m_Driver >= 0)
	{
		CCharacter *pDriver = DriverChar();
		if(!pDriver || pDriver->IsFrozen())
		{
			ForceDriverLeave();
			return;
		}

		TickDriver(pDriver);
		VehicleSyncCharacter(pDriver, m_Pos, m_Vel, DriverOffsetY());
		pDriver->m_VehicleSeat = VEHICLE_SEAT_DRIVER;
	}
	else
	{
		TickIdle();
		if(VehicleTryAutoBoard(GameWorld(), this, m_Driver, m_Team, BoardRadius()))
		{
			if(CCharacter *pDriver = DriverChar())
			{
				pDriver->m_VehicleSeat = VEHICLE_SEAT_DRIVER;
				OnDriverBoarded(pDriver);
			}
		}
	}
}

void CVehicle::Reset()
{
	m_Dead = true;
	ClearOwnerDeployRef();
	GameWorld()->DestroyEntity(this);
}

void CVehicle::ForceDriverLeave()
{
	if(m_Driver < 0)
		return;

	if(CCharacter *pDriver = DriverChar())
		VehicleDismount(m_Driver, m_Vel, pDriver);
	else
		m_Driver = -1;
}

bool CVehicle::IsOccupiedBy(int ClientId) const
{
	return m_Driver == ClientId;
}

void CVehicle::HandleOccupantDismount(int ClientId)
{
	if(m_Driver == ClientId)
		ForceDriverLeave();
}

void CVehicle::ForEachAircraft(CGameWorld *pWorld, CAircraft **ppOut, int Max, int *pNum)
{
	int Num = 0;
	for(CGameWorld::TypeRange r = pWorld->DoTypeRange(CGameWorld::ENTTYPE_AIRCRAFT); !r.empty(); r.pop_front())
	{
		CAircraft *pAircraft = static_cast<CAircraft *>(r.front());
		if(Num < Max)
			ppOut[Num++] = pAircraft;
	}
	if(pNum)
		*pNum = Num;
}
