#ifndef GAME_SERVER_ENTITIES_VEHICLE_VEHICLE_H
#define GAME_SERVER_ENTITIES_VEHICLE_VEHICLE_H

#include <game/server/entity.h>
#include "vehicle_util.h"

class CCharacter;

class CVehicle : public CEntity
{
protected:
	int m_Health;
	int m_MaxHealth;
	int m_Team;
	int m_Driver;
	int m_OwnerClientID;
	vec2 m_Vel;
	vec2 m_SpawnPos;
	bool m_Dead;
	int m_aHealthBarIds[3];

	CVehicle(CGameWorld *pGameWorld, int ObjType, vec2 Pos, int Team, int Health, int OwnerClientID = -1);

	CCharacter *DriverChar();
	void ApplyHorizontalInput(CCharacter *pDriver, int MaxSpeed, int Accel, float IdleDecay);
	void MoveBox(vec2 Size);
	void Explode();
	void SpawnShrapnel();
	void SnapHealthBar(int SnappingClient);
	void ClearOwnerDeployRef();

	virtual float DriverOffsetY() const { return VehicleScale(-8.f); }
	virtual float BoardRadius() const { return VehicleScale(32.f); }
	virtual vec2 CollisionSize() const { return vec2(VehicleScale(48.f), VehicleScale(24.f)); }
	virtual void TickDriver(CCharacter *pDriver) = 0;
	virtual void TickIdle() {}
	virtual void OnDriverBoarded(CCharacter *pDriver);
	virtual void OnBeforeDestroy() {}

public:
	virtual ~CVehicle();

	virtual void Tick();
	void Reset();

	int GetDriver() const { return m_Driver; }
	int GetOwnerClientID() const { return m_OwnerClientID; }
	int GetTeam() const { return m_Team; }
	int GetHealth() const { return m_Health; }
	int GetMaxHealth() const { return m_MaxHealth; }
	vec2 GetCollisionSize() const { return CollisionSize(); }
	bool IsDead() const { return m_Dead; }

	void TakeDamage(int Amount, int From);
	bool CanBoard(int Team) const;
	void ForceDriverLeave();
	bool IsOccupiedBy(int ClientId) const;
	void HandleOccupantDismount(int ClientId);
	void BoardDriver(CCharacter *pDriver);

	static void ForEachAircraft(CGameWorld *pWorld, class CAircraft **ppOut, int Max, int *pNum);
};

#endif
