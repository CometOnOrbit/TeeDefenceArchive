#include <game/server/gamecontext.h>
#include <game/server/entities/character.h>

#include "aircraft.h"
#include "vehicle_util.h"
#include "vehicle_visual.h"

CAircraft::CAircraft(CGameWorld *pGameWorld, vec2 Pos, int Team, int OwnerClientID)
: CVehicle(pGameWorld, CGameWorld::ENTTYPE_AIRCRAFT, Pos, Team, 50, OwnerClientID)
{
	m_PrevDriverJump = 0;
	const int WorldId = GameServer()->GetWorldID();
	for(int i = 0; i < AIRCRAFT_PART_IDS; i++)
		m_PartIds[i] = Server()->SnapNewID(WorldId);
	GameWorld()->InsertEntity(this);
}

CAircraft::~CAircraft()
{
	const int WorldId = GameServer()->GetWorldID();
	for(int i = 0; i < AIRCRAFT_PART_IDS; i++)
		Server()->SnapFreeID(m_PartIds[i], WorldId);
}

void CAircraft::TickDriver(CCharacter *pDriver)
{
	ApplyHorizontalInput(pDriver, AIRCRAFT_MAX_SPEED, AIRCRAFT_ACCEL, 0.92f);
	if(pDriver)
	{
		const CNetObj_PlayerInput &Input = pDriver->m_Input;
		if(Input.m_Hook & 1)
		{
			if(Input.m_TargetY < 0)
				m_Vel.y = SaturatedAdd(-(float)AIRCRAFT_MAX_SPEED, (float)AIRCRAFT_MAX_SPEED, m_Vel.y, -(float)AIRCRAFT_ACCEL);
			else
				m_Vel.y = SaturatedAdd(-(float)AIRCRAFT_MAX_SPEED, (float)AIRCRAFT_MAX_SPEED, m_Vel.y, (float)AIRCRAFT_ACCEL);
		}

		pDriver->TickVehicleWeapon();

		if(VehicleInputPressed(m_PrevDriverJump, Input.m_Jump))
			ForceDriverLeave();
		m_PrevDriverJump = Input.m_Jump & INPUT_STATE_MASK;
	}
	VehicleApplyFriction(m_Vel, 0.96f);
	MoveBox(CollisionSize());
}

void CAircraft::TickIdle()
{
	VehicleApplyFriction(m_Vel, 0.95f);
	MoveBox(CollisionSize());
}

void CAircraft::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || m_Dead)
		return;

	VehicleVisual::SnapAircraft(Server(), GameServer()->GetWorldID(), GetID(), m_PartIds, AIRCRAFT_PART_IDS, m_Pos);
	SnapHealthBar(SnappingClient);
}
