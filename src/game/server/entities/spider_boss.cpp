/* (c) TeeDefenceArchive - 2026 */
/* Giant mech spider: procedural leg locomotion drives the Character core */
#include <base/math.h>

#include <generated/protocol.h>
#include <generated/server_data.h>

#include <game/collision.h>
#include <game/server/entities/character.h>
#include <game/server/entities/projectile.h>
#include <game/server/entities/tower-main.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/worldmodes/defence.h>
#include <game/server/player.h>

#include "spider_boss.h"
#include "../zombie_nav.h"

static const vec2 SPIDER_HIP_LOCAL[NUM_SPIDER_LEGS] = {
	vec2(16.0f, 0.0f),
	vec2(-16.0f, 0.0f),
	vec2(-16.0f, -16.0f),
	vec2(16.0f, -16.0f),
};

static const float SPIDER_KNEE_SIGN[NUM_SPIDER_LEGS] = {-1.0f, 1.0f, 1.0f, -1.0f};
static const float SPIDER_FOOT_OUTWARD = 96.0f;

static const float SPIDER_BODY_H = 128.0f;
static const float SPIDER_FEMUR = 94.0f;
static const float SPIDER_TIBIA = 127.0f;
static const float SPIDER_MIN_REACH = SPIDER_FEMUR - SPIDER_TIBIA + 4.0f;
static const float SPIDER_MAX_REACH = SPIDER_FEMUR + SPIDER_TIBIA - 4.0f;
static const float SPIDER_STEP_DIST = 58.0f;
static const float SPIDER_STEP_LIFT = 42.0f;
static const float SPIDER_STEP_SEC = 0.40f;
static const float SPIDER_FOOT_SKIN = 12.0f;
static const float SPIDER_PROBE = 720.0f;
static const float SPIDER_WALL_PROBE = 110.0f;
static const float SPIDER_SPEED = 8.0f;
static const float SPIDER_CLIMB = 8.0f;
static const float SPIDER_ACCEL = 0.5f;
static const float SPIDER_GRAVITY = 7.5f;
static const float SPIDER_GRENADE_MIN_RANGE = 140.0f;
static const float SPIDER_GRENADE_MAX_RANGE = 1600.0f;
static const float SPIDER_GRENADE_SPAWN_OFF = SPIDER_BODY_H * 0.42f;
static const float SPIDER_HUMAN_AGGRO = 960.0f;
static const float SPIDER_COMBAT_RADIUS = 720.0f;
static const float SPIDER_DETOUR_RADIUS = 620.0f;
static const float SPIDER_BODY_MELEE_RANGE = SPIDER_BODY_H * 0.52f;
static const float SPIDER_FOOT_STOMP_RANGE = 128.0f;
static const float SPIDER_TOWER_MELEE_RANGE = 380.0f;
static const float SPIDER_LEAP_MIN_RANGE = 160.0f;
static const float SPIDER_LEAP_MAX_RANGE = 520.0f;
static const float SPIDER_LEAP_HEIGHT = 88.0f;
static const float SPIDER_WEB_MIN_RANGE = 200.0f;
static const float SPIDER_WEB_MAX_RANGE = 920.0f;
static const int SPIDER_WEB_SHOTS = 5;
static const int SPIDER_FOOT_HIT = 118;
static const int SPIDER_KNEE_HIT = 92;

static bool WeaponCanKnockLeg(int Weapon)
{
	return Weapon == WEAPON_GRENADE || Weapon == WEAPON_GUN || Weapon == WEAPON_HAMMER || Weapon == WEAPON_SHOTGUN;
}

static float EaseSmooth(float t)
{
	t = clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

CSpiderLegPart::CSpiderLegPart(CGameWorld *pGameWorld, CSpiderBoss *pBoss, int Leg, int Seg)
	: CHitableEntity(pGameWorld, CGameWorld::ENTTYPE_SPIDERLEG, 0, vec2(0.0f, 0.0f), Seg == SPIDER_SEG_FOOT ? SPIDER_FOOT_HIT : SPIDER_KNEE_HIT)
{
	m_pBoss = pBoss;
	m_Leg = Leg;
	m_Seg = Seg;
	GameWorld()->InsertEntity(this);
}

bool CSpiderLegPart::TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon)
{
	(void)Force;
	(void)Source;
	(void)Weapon;
	if(!m_pBoss || Dmg <= 0)
		return false;
	if(pFrom && GameWorld()->DamageOwnerFromEntity(pFrom) == m_pBoss->GetOwnerCid())
		return false;
	m_pBoss->DamageLeg(m_Leg, maximum(1, Dmg * 2 / 5), Force, Source, Weapon);
	return true;
}

void CSpiderLegPart::SetSegmentPos(vec2 Pos)
{
	m_Pos = Pos;
}

CSpiderBoss::CSpiderBoss(CGameWorld *pGameWorld, CCharacter *pCore, CGameController *pCtrl, int Wave)
	: CEntity(pGameWorld, CGameWorld::ENTTYPE_SPIDERBOSS, 0, pCore ? pCore->GetPos() : vec2(0.0f, 0.0f), 0)
{
	m_pCtrl = pCtrl;
	m_pCore = pCore;
	m_OwnerCid = pCore && pCore->GetPlayer() ? pCore->GetPlayer()->GetCID() : -1;
	m_Wave = maximum(1, Wave);
	m_SpawnCoreHealth = 1;
	m_LastGrenadeTick = 0;
	m_LastStompTick = 0;
	m_LastTowerHitTick = 0;
	m_LastBodySlamTick = 0;
	m_BodyVel = vec2(0.0f, 0.0f);
	m_MoveDir = vec2(1.0f, 0.0f);
	m_StuckTicks = 0;
	m_LastProgressPos = m_Pos;
	m_LastSafePos = m_Pos;
	m_JumpPhase = SPIDER_JUMP_IDLE;
	m_JumpStartTick = 0;
	m_JumpPeakY = 0.0f;
	m_LastLeapTick = 0;
	m_LastWebTick = 0;
	m_ChaseTarget.m_Type = SPIDER_TARGET_TOWER;
	m_ChaseTarget.m_PlayerCid = -1;
	m_ChaseTarget.m_Pos = vec2(0.0f, 0.0f);
	m_ChaseTarget.m_NeedPathUpdate = true;

	const int LegHp = maximum(250, m_Wave * 30);
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
	{
		m_aLegHealth[i] = LegHp;
		m_aLegState[i] = SPIDER_LEG_STANCE;
		m_aKnee[i] = m_Pos;
		m_aFoot[i] = m_Pos;
		m_aPlantedFoot[i] = m_Pos;
		m_aSwingFrom[i] = m_Pos;
		m_aSwingTo[i] = m_Pos;
		m_aSwingT[i] = 1.0f;
		m_aFootKnock[i] = vec2(0.0f, 0.0f);
		for(int s = 0; s < NUM_SPIDER_SEGS; s++)
			m_apSeg[i][s] = new CSpiderLegPart(pGameWorld, this, i, s);
	}

	AddSnappingGroupIds(SNAP_GROUP_SPIDER_LEGS, NUM_SPIDER_LEGS * NUM_SPIDER_SEGS);

	GameWorld()->InsertEntity(this);

	if(m_pCore)
	{
		vec2 Spawn = m_pCore->GetPos();
		if(m_pCtrl)
			Spawn = m_pCtrl->TdSnapSpawnToGround(Spawn, 112.0f);
		InitPose(Spawn);
		m_LastProgressPos = m_Pos;
		m_ChaseTarget.m_Pos = TowerGoal();
		m_SpawnCoreHealth = maximum(1, m_pCore->GetHealth());
		m_pCore->SyncSpiderBody(m_Pos);
	}
}

CSpiderBoss::~CSpiderBoss()
{
	if(m_OwnerCid >= 0 && m_OwnerCid < MAX_CLIENTS)
	{
		CPlayer *pOwner = GameServer()->m_apPlayers[m_OwnerCid];
		if(pOwner)
			ZombieNavClear(pOwner);
	}

	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
	{
		for(int s = 0; s < NUM_SPIDER_SEGS; s++)
		{
			if(m_apSeg[i][s])
			{
				GameWorld()->DestroyEntity(m_apSeg[i][s]);
				m_apSeg[i][s] = nullptr;
			}
		}
	}
}

bool CSpiderBoss::IsCoreAlive() const
{
	return m_pCore && m_pCore->IsAlive();
}

int CSpiderBoss::GetCoreHealth() const
{
	return m_pCore && m_pCore->IsAlive() ? m_pCore->GetHealth() : 0;
}

int CSpiderBoss::GetCoreMaxHealth() const
{
	return maximum(1, m_SpawnCoreHealth);
}

int CSpiderBoss::GetLegsAlive() const
{
	int Count = 0;
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
		if(m_aLegHealth[i] > 0)
			Count++;
	return Count;
}

float CSpiderBoss::TickDt()
{
	const int Ts = Server()->TickSpeed();
	return Ts > 0 ? 1.0f / (float)Ts : 0.02f;
}

vec2 CSpiderBoss::HipLocal(int Leg) const
{
	return Leg >= 0 && Leg < NUM_SPIDER_LEGS ? SPIDER_HIP_LOCAL[Leg] : vec2(0.0f, 0.0f);
}

vec2 CSpiderBoss::HipWorld(int Leg) const
{
	return m_Pos + HipLocal(Leg);
}

bool CSpiderBoss::RayDown(vec2 From, vec2 *pFoot)
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return false;
	vec2 Col, Before;
	if(!pCol->IntersectLine(From, From + vec2(0.0f, SPIDER_PROBE), &Col, &Before))
		return false;
	if(pFoot)
		*pFoot = vec2(Before.x, Before.y - SPIDER_FOOT_SKIN);
	return true;
}

bool CSpiderBoss::RayToWall(int Side, vec2 From, vec2 *pHit)
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return false;
	vec2 Col, Before;
	const vec2 To = From + vec2((float)Side * SPIDER_WALL_PROBE, 0.0f);
	if(!pCol->IntersectLine(From, To, &Col, &Before))
		return false;
	if(pHit)
		*pHit = Before;
	return true;
}

bool CSpiderBoss::RayUp(vec2 From, vec2 *pHit)
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return false;
	vec2 Col, Before;
	if(!pCol->IntersectLine(From, From + vec2(0.0f, -SPIDER_WALL_PROBE), &Col, &Before))
		return false;
	if(pHit)
		*pHit = Before;
	return true;
}

bool CSpiderBoss::IsVerticalWall(int Side, vec2 From, vec2 *pHit)
{
	vec2 Hit;
	if(!RayToWall(Side, From, &Hit))
		return false;

	vec2 Ground;
	if(RayDown(From + vec2(0.0f, 32.0f), &Ground))
	{
		if(Hit.y >= Ground.y - 28.0f)
			return false;
	}

	if(pHit)
		*pHit = Hit;
	return true;
}

vec2 CSpiderBoss::SpiderBodySize() const
{
	return vec2(SPIDER_BODY_H * 0.36f, SPIDER_BODY_H * 0.32f);
}

void CSpiderBoss::ApplySpiderMove(vec2 Vel)
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
	{
		m_Pos += Vel;
		m_BodyVel = Vel;
		return;
	}

	vec2 Pos = m_Pos;
	vec2 MoveVel = Vel;
	const vec2 Size = SpiderBodySize();
	pCol->MoveBox(&Pos, &MoveVel, Size, 0.0f);

	if(absolute(Vel.x) > 0.5f && absolute(Pos.x - m_Pos.x) < maximum(2.0f, absolute(Vel.x) * 0.22f))
	{
		float BestAdvance = absolute(Pos.x - m_Pos.x);
		const float Lifts[] = {14.0f, 28.0f, 42.0f, 56.0f, 72.0f};
		for(unsigned i = 0; i < sizeof(Lifts) / sizeof(Lifts[0]); i++)
		{
			vec2 TryPos = m_Pos;
			vec2 TryVel = Vel;
			TryPos.y -= Lifts[i];
			pCol->MoveBox(&TryPos, &TryVel, Size, 0.0f);
			const float Advance = absolute(TryPos.x - m_Pos.x);
			if(Advance > BestAdvance + 0.5f)
			{
				Pos = TryPos;
				MoveVel = TryVel;
				BestAdvance = Advance;
			}
		}
	}

	m_Pos = Pos;
	m_BodyVel = MoveVel;
}

void CSpiderBoss::ApplySpiderGroundMove(vec2 WishVel, vec2 WishDir)
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
	{
		m_Pos += WishVel;
		m_BodyVel = WishVel;
		return;
	}

	const vec2 Size = SpiderBodySize();
	const vec2 PosBefore = m_Pos;
	vec2 Pos = m_Pos;
	vec2 Vel = vec2(WishVel.x, 0.0f);
	pCol->MoveBox(&Pos, &Vel, Size, 0.0f);

	if(absolute(WishVel.x) > 0.5f && absolute(Pos.x - PosBefore.x) < maximum(2.0f, absolute(WishVel.x) * 0.22f))
	{
		float BestAdvance = absolute(Pos.x - PosBefore.x);
		const float Lifts[] = {14.0f, 28.0f, 42.0f, 56.0f, 72.0f, 88.0f};
		for(unsigned i = 0; i < sizeof(Lifts) / sizeof(Lifts[0]); i++)
		{
			vec2 TryPos = PosBefore;
			vec2 TryVel = vec2(WishVel.x, 0.0f);
			TryPos.y -= Lifts[i];
			pCol->MoveBox(&TryPos, &TryVel, Size, 0.0f);
			const float Advance = absolute(TryPos.x - PosBefore.x);
			if(Advance > BestAdvance + 0.5f)
			{
				Pos = TryPos;
				Vel = TryVel;
				BestAdvance = Advance;
			}
		}
	}

	const float StandY = FindStandY(WishDir);
	const float RiseCap = maximum(absolute(WishVel.x) * 0.85f, 14.0f);
	const float DropCap = maximum(absolute(WishVel.x) * 0.45f, 10.0f);
	if(StandY < Pos.y - 3.0f)
		Pos.y = maximum(Pos.y - RiseCap, StandY);
	else if(StandY > Pos.y + 3.0f)
		Pos.y = minimum(Pos.y + DropCap, StandY);
	else
		Pos.y = StandY;

	m_Pos = Pos;
	m_BodyVel = vec2(Vel.x, 0.0f);
}

void CSpiderBoss::ClampSpiderToMap()
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return;

	const float Margin = SPIDER_BODY_H * 0.45f;
	const float MaxX = (float)pCol->GetWidth() * 32.0f - Margin;
	const float MaxY = (float)pCol->GetHeight() * 32.0f - Margin;
	m_Pos.x = clamp(m_Pos.x, Margin, MaxX);
	m_Pos.y = clamp(m_Pos.y, Margin, MaxY);
}

bool CSpiderBoss::HasGroundSupport()
{
	const float RayFromY = m_Pos.y - SPIDER_BODY_H * 0.35f;
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return false;
	vec2 Col, Before;
	if(!pCol->IntersectLine(vec2(m_Pos.x, RayFromY), vec2(m_Pos.x, RayFromY + SPIDER_PROBE), &Col, &Before))
		return false;
	const float StandY = (Before.y - SPIDER_FOOT_SKIN) - SPIDER_BODY_H;
	return m_Pos.y >= StandY - 30.0f && m_Pos.y <= StandY + 44.0f;
}

void CSpiderBoss::RecoverIfInvalid()
{
	CCollision *pCol = GameServer()->Collision();
	const vec2 Size = SpiderBodySize();
	const bool Outside = GameLayerClipped(m_Pos);
	const bool InsideSolid = pCol && pCol->TestBox(m_Pos, Size);
	const bool Buried = InsideSolid && !HasGroundSupport();

	if(Outside || Buried)
	{
		m_Pos = m_LastSafePos;
		m_BodyVel = vec2(0.0f, 0.0f);
		m_JumpPhase = SPIDER_JUMP_IDLE;
		InvalidatePath();
		return;
	}

	ClampSpiderToMap();
	if(HasGroundSupport() || !pCol || !InsideSolid)
		m_LastSafePos = m_Pos;
}

float CSpiderBoss::GroundBodyYAt(float X, float RayFromY)
{
	vec2 Foot;
	if(RayDown(vec2(X, RayFromY), &Foot))
		return Foot.y - SPIDER_BODY_H;
	return m_Pos.y;
}

float CSpiderBoss::GroundBodyY(float X)
{
	return GroundBodyYAt(X, m_Pos.y - SPIDER_BODY_H * 0.35f);
}

float CSpiderBoss::FindStandY(vec2 WishDir)
{
	const float RayFromY = m_Pos.y - SPIDER_BODY_H * 0.35f;
	float BestStandY = GroundBodyYAt(m_Pos.x, RayFromY);

	float DirX = 0.0f;
	if(length(WishDir) > 0.01f)
	{
		if(absolute(WishDir.x) > 0.08f)
			DirX = WishDir.x > 0.0f ? 1.0f : -1.0f;
		else if(WishDir.y < -0.08f)
			DirX = 0.0f;
	}

	const float AheadSteps[] = {32.0f, 64.0f, 96.0f, 128.0f};
	for(int i = 0; i < 4; i++)
	{
		const float SampleX = m_Pos.x + DirX * AheadSteps[i];
		const float SampleY = GroundBodyYAt(SampleX, RayFromY);
		if(SampleY < BestStandY)
			BestStandY = SampleY;
	}

	for(int Side = -1; Side <= 1; Side += 2)
	{
		const float SampleX = m_Pos.x + DirX * 56.0f + (float)Side * 32.0f;
		const float SampleY = GroundBodyYAt(SampleX, RayFromY);
		if(SampleY < BestStandY)
			BestStandY = SampleY;
	}

	return clamp(BestStandY, m_Pos.y - 72.0f, m_Pos.y + 36.0f);
}

float CSpiderBoss::LegOutward(int Leg) const
{
	return HipLocal(Leg).x >= 0.0f ? 1.0f : -1.0f;
}

bool CSpiderBoss::ProbeFootSpot(vec2 From, vec2 *pFoot)
{
	const float Offsets[] = {0.0f, 10.0f, -10.0f, 18.0f, -18.0f};
	for(int i = 0; i < (int)(sizeof(Offsets) / sizeof(Offsets[0])); i++)
	{
		if(RayDown(From + vec2(Offsets[i], 0.0f), pFoot))
			return true;
	}
	return false;
}

vec2 CSpiderBoss::LegHome(int Leg)
{
	const vec2 Pred = m_BodyVel * (SPIDER_STEP_SEC * 0.55f);
	const float Side = LegOutward(Leg);
	vec2 Hint = m_Pos + HipLocal(Leg) + Pred;
	Hint.x += Side * SPIDER_FOOT_OUTWARD;

	vec2 Foot;
	if(ProbeFootSpot(Hint + vec2(0.0f, -72.0f), &Foot))
		return Foot;

	Hint.x += Side * 18.0f;
	if(ProbeFootSpot(Hint + vec2(0.0f, -72.0f), &Foot))
		return Foot;

	return m_aPlantedFoot[Leg];
}

vec2 CSpiderBoss::SolveKnee(int Leg, vec2 Hip, vec2 Foot) const
{
	const float Side = HipLocal(Leg).x >= 0.0f ? 1.0f : -1.0f;
	const float MinOutward = SPIDER_FOOT_OUTWARD * 0.42f;
	if(Side > 0.0f && Foot.x < Hip.x + MinOutward)
		Foot.x = Hip.x + MinOutward;
	else if(Side < 0.0f && Foot.x > Hip.x - MinOutward)
		Foot.x = Hip.x - MinOutward;

	vec2 HF = Foot - Hip;
	float D = length(HF);
	if(D < 0.01f)
		return Hip + vec2(0.0f, SPIDER_FEMUR * 0.5f);

	if(D > SPIDER_MAX_REACH)
	{
		Foot = Hip + HF / D * SPIDER_MAX_REACH;
		HF = Foot - Hip;
		D = SPIDER_MAX_REACH;
	}
	else if(D < SPIDER_MIN_REACH)
	{
		HF = normalize(HF) * SPIDER_MIN_REACH;
		Foot = Hip + HF;
		D = SPIDER_MIN_REACH;
	}

	vec2 N = HF / D;
	const float CosA = clamp((SPIDER_FEMUR * SPIDER_FEMUR + D * D - SPIDER_TIBIA * SPIDER_TIBIA) / (2.0f * SPIDER_FEMUR * D), -1.0f, 1.0f);
	const float SinA = sqrtf(maximum(0.0f, 1.0f - CosA * CosA));
	vec2 Perp(-N.y * SPIDER_KNEE_SIGN[Leg], N.x * SPIDER_KNEE_SIGN[Leg]);
	return Hip + N * (SPIDER_FEMUR * CosA) + Perp * (SPIDER_FEMUR * SinA);
}

int CSpiderBoss::LegGroup(int Leg) const
{
	return Leg % 2;
}

bool CSpiderBoss::GroupIsStepping(int Group) const
{
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
	{
		if(m_aLegHealth[i] <= 0)
			continue;
		if(LegGroup(i) == Group && m_aLegState[i] == SPIDER_LEG_SWING)
			return true;
	}
	return false;
}

bool CSpiderBoss::CanLegStep(int Leg) const
{
	if(m_aLegState[Leg] == SPIDER_LEG_SWING)
		return false;
	return !GroupIsStepping(LegGroup(Leg));
}

void CSpiderBoss::TryStartLegStep(int Leg)
{
	if(!CanLegStep(Leg))
		return;

	const vec2 Home = LegHome(Leg);
	if(distance(m_aPlantedFoot[Leg], Home) < SPIDER_STEP_DIST)
		return;

	m_aLegState[Leg] = SPIDER_LEG_SWING;
	m_aSwingT[Leg] = 0.0f;
	m_aSwingFrom[Leg] = m_aPlantedFoot[Leg];
	m_aSwingTo[Leg] = Home;
}

void CSpiderBoss::PlaceLeg(int Leg)
{
	const vec2 Hip = HipWorld(Leg);
	const float Side = LegOutward(Leg);
	vec2 Foot;
	const float OutSteps[] = {1.0f, 0.82f, 0.65f};
	bool Placed = false;
	for(int i = 0; i < 3; i++)
	{
		if(ProbeFootSpot(Hip + vec2(Side * SPIDER_FOOT_OUTWARD * OutSteps[i], -32.0f), &Foot))
		{
			Placed = true;
			break;
		}
	}
	if(!Placed)
		return;
	if(length(Foot - Hip) > 0.01f)
	{
		m_aPlantedFoot[Leg] = Foot;
		m_aFoot[Leg] = Foot;
		m_aKnee[Leg] = SolveKnee(Leg, Hip, Foot);
		m_aLegState[Leg] = SPIDER_LEG_STANCE;
		m_aSwingT[Leg] = 1.0f;
	}
}

void CSpiderBoss::PlaceAllLegs()
{
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
	{
		if(m_aLegHealth[i] > 0)
			PlaceLeg(i);
	}
}

void CSpiderBoss::InitPose(vec2 Spawn)
{
	m_Pos = vec2(Spawn.x, GroundBodyY(Spawn.x));
	m_LastSafePos = m_Pos;
	m_BodyVel = vec2(0.0f, 0.0f);
	PlaceAllLegs();
}

void CSpiderBoss::UpdateLeg(int Leg)
{
	if(m_aLegHealth[Leg] <= 0)
		return;

	const vec2 Hip = HipWorld(Leg);

	if(m_aLegState[Leg] == SPIDER_LEG_SWING)
	{
		m_aSwingT[Leg] = minimum(1.0f, m_aSwingT[Leg] + TickDt() / SPIDER_STEP_SEC);
		const float E = EaseSmooth(m_aSwingT[Leg]);
		vec2 Foot = m_aSwingFrom[Leg] + (m_aSwingTo[Leg] - m_aSwingFrom[Leg]) * E;
		Foot.y -= sinf(pi * m_aSwingT[Leg]) * SPIDER_STEP_LIFT;

		m_aFoot[Leg] = Foot + m_aFootKnock[Leg];
		m_aKnee[Leg] = SolveKnee(Leg, Hip, m_aFoot[Leg]);

		if(m_aSwingT[Leg] >= 1.0f)
		{
			m_aPlantedFoot[Leg] = m_aSwingTo[Leg];
			m_aFoot[Leg] = m_aPlantedFoot[Leg];
			m_aLegState[Leg] = SPIDER_LEG_STANCE;
			OnLegPlanted(Leg);
		}
		return;
	}

	m_aFoot[Leg] = m_aPlantedFoot[Leg] + m_aFootKnock[Leg];
	m_aKnee[Leg] = SolveKnee(Leg, Hip, m_aFoot[Leg]);

	const float DistHome = distance(m_aPlantedFoot[Leg], LegHome(Leg));
	const float DistHip = distance(Hip, m_aPlantedFoot[Leg]);
	if(DistHome > SPIDER_STEP_DIST || DistHip > SPIDER_MAX_REACH * 0.92f)
		TryStartLegStep(Leg);
}

vec2 CSpiderBoss::TowerGoal()
{
	if(!m_pCtrl) return m_Pos;
	return static_cast<CGameControllerDefence *>(m_pCtrl)->TdGetZombieMarchGoal();
}

bool CSpiderBoss::HasLineOfSight(vec2 Target)
{
	CCollision *pCol = GameServer()->Collision();
	if(!pCol)
		return true;
	return !pCol->FastIntersectLine(m_Pos, Target, nullptr, nullptr);
}

void CSpiderBoss::InvalidatePath()
{
	if(m_OwnerCid >= 0 && m_OwnerCid < MAX_CLIENTS)
	{
		CPlayer *pOwner = GameServer()->m_apPlayers[m_OwnerCid];
		if(pOwner)
			ZombieNavClear(pOwner);
	}
	m_ChaseTarget.m_NeedPathUpdate = true;
}

void CSpiderBoss::UpdateChaseTarget()
{
	const vec2 Tower = TowerGoal();
	const float DistTower = distance(m_Pos, Tower);

	int BestCid = -1;
	float BestDist = 1.0e12f;
	vec2 BestPos(0.0f, 0.0f);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == m_OwnerCid)
			continue;
		CPlayer *pPl = GameServer()->m_apPlayers[i];
		if(!pPl || pPl->IsDummy() || pPl->GetTeam() == TEAM_SPECTATORS)
			continue;
		CCharacter *pHuman = pPl->GetCharacter();
		if(!pHuman || !pHuman->IsAlive())
			continue;

		const float D = distance(m_Pos, pHuman->GetPos());
		if(D < BestDist)
		{
			BestDist = D;
			BestPos = pHuman->GetPos();
			BestCid = i;
		}
	}

	const bool HasHuman = BestCid >= 0;
	const bool HumanLos = HasHuman && HasLineOfSight(BestPos);
	const bool HumanCombat = HumanLos && BestDist < SPIDER_COMBAT_RADIUS;
	const bool HumanAggro = HumanLos && BestDist < SPIDER_HUMAN_AGGRO;
	const bool PreferHuman = HumanCombat &&
		(BestDist < 420.0f || BestDist < DistTower * 0.88f || (HumanAggro && BestDist < SPIDER_DETOUR_RADIUS));

	int NewType = SPIDER_TARGET_TOWER;
	vec2 NewPos = Tower;
	int NewCid = -1;
	if(PreferHuman)
	{
		NewType = SPIDER_TARGET_PLAYER;
		NewPos = BestPos;
		NewCid = BestCid;
	}
	else if(m_ChaseTarget.m_Type == SPIDER_TARGET_PLAYER && m_ChaseTarget.m_PlayerCid >= 0)
	{
		CPlayer *pOld = GameServer()->m_apPlayers[m_ChaseTarget.m_PlayerCid];
		CCharacter *pOldChr = pOld ? pOld->GetCharacter() : nullptr;
		if(pOldChr && pOldChr->IsAlive() && HasLineOfSight(pOldChr->GetPos()) &&
			distance(m_Pos, pOldChr->GetPos()) < SPIDER_DETOUR_RADIUS + 80.0f)
		{
			NewType = SPIDER_TARGET_PLAYER;
			NewPos = pOldChr->GetPos();
			NewCid = m_ChaseTarget.m_PlayerCid;
		}
	}

	if(m_ChaseTarget.m_Type != NewType || m_ChaseTarget.m_PlayerCid != NewCid ||
		distance(m_ChaseTarget.m_Pos, NewPos) > 56.0f)
	{
		m_ChaseTarget.m_Type = NewType;
		m_ChaseTarget.m_PlayerCid = NewCid;
		m_ChaseTarget.m_Pos = NewPos;
		m_ChaseTarget.m_NeedPathUpdate = true;
	}

	if(distance(m_Pos, m_LastProgressPos) < 10.0f && length(m_BodyVel) < 0.35f)
		m_StuckTicks++;
	else
	{
		m_StuckTicks = 0;
		m_LastProgressPos = m_Pos;
	}
	if(m_StuckTicks > 28)
	{
		InvalidatePath();
		m_StuckTicks = 0;
	}
}

vec2 CSpiderBoss::GetChaseGoal()
{
	if(m_ChaseTarget.m_Type == SPIDER_TARGET_PLAYER && m_ChaseTarget.m_PlayerCid >= 0)
	{
		CPlayer *pPl = GameServer()->m_apPlayers[m_ChaseTarget.m_PlayerCid];
		CCharacter *pChr = pPl ? pPl->GetCharacter() : nullptr;
		if(pChr && pChr->IsAlive())
			return pChr->GetPos();
	}
	return m_ChaseTarget.m_Pos;
}

vec2 CSpiderBoss::PathWaypoint(vec2 Goal)
{
	CPlayer *pOwner = m_OwnerCid >= 0 && m_OwnerCid < MAX_CLIENTS ? GameServer()->m_apPlayers[m_OwnerCid] : nullptr;
	if(!pOwner)
		return Goal;

	if(m_ChaseTarget.m_NeedPathUpdate)
	{
		ZombieNavClear(pOwner);
		m_ChaseTarget.m_NeedPathUpdate = false;
	}

	vec2 Follow = Goal;
	vec2 Aim = Goal;
	ZombieNavFollow(GameServer(), pOwner, m_Pos, Goal, Server()->Tick(), Server()->TickSpeed(), &Follow, &Aim);
	if(distance(m_Pos, Follow) > 12.0f)
		return Follow;
	return Goal;
}

vec2 CSpiderBoss::ComputeMoveWish(vec2 Goal, vec2 Waypoint)
{
	vec2 To = Goal - m_Pos;
	const float DistGoal = length(To);
	if(DistGoal < 24.0f)
		return vec2(0.0f, 0.0f);

	vec2 Wish = Waypoint - m_Pos;
	if(length(Wish) < 36.0f)
		Wish = To;

	if(length(Wish) < 20.0f)
		return vec2(0.0f, 0.0f);

	const bool PathUp = Waypoint.y < m_Pos.y - 28.0f || To.y < m_Pos.y - 28.0f;
	if(DistGoal > 120.0f && absolute(Wish.x) < 40.0f && !PathUp)
	{
		Wish.x = To.x >= 0.0f ? 88.0f : -88.0f;
		Wish.y = minimum(Wish.y, -8.0f);
	}

	return normalize(Wish);
}

void CSpiderBoss::UpdateLeapStomp()
{
	const int Now = Server()->Tick();
	const float StandY = GroundBodyY(m_Pos.x);

	if(m_JumpPhase == SPIDER_JUMP_RISE)
	{
		vec2 Vel = vec2(m_BodyVel.x * 0.82f, -18.0f - 0.4f * (float)minimum(m_Wave, 20));
		ApplySpiderMove(Vel);
		if(m_Pos.y <= m_JumpPeakY - SPIDER_LEAP_HEIGHT || Now - m_JumpStartTick > Server()->TickSpeed())
			m_JumpPhase = SPIDER_JUMP_FALL;
	}
	else if(m_JumpPhase == SPIDER_JUMP_FALL)
	{
		vec2 Vel = vec2(m_BodyVel.x * 0.65f, 24.0f + 0.5f * (float)minimum(m_Wave, 20));
		ApplySpiderMove(Vel);
		if(m_Pos.y >= StandY - 6.0f)
		{
			m_Pos.y = StandY;
			m_BodyVel = vec2(0.0f, 0.0f);
			GameWorld()->CreateExplosion(m_Pos, m_pCore, WEAPON_HAMMER, SpiderAttackDamage(9));
			for(int i = 0; i < NUM_SPIDER_LEGS; i++)
			{
				if(m_aLegHealth[i] <= 0)
					continue;
				GameWorld()->CreateExplosion(m_aFoot[i], m_pCore, WEAPON_HAMMER, SpiderAttackDamage(6));
			}
			m_JumpPhase = SPIDER_JUMP_IDLE;
		}
	}

	RecoverIfInvalid();
}

void CSpiderBoss::TryLeapStomp()
{
	if(m_JumpPhase != SPIDER_JUMP_IDLE || Server()->Tick() - m_LastLeapTick < Server()->TickSpeed() * 5)
		return;

	const float StandY = GroundBodyY(m_Pos.x);
	if(m_Pos.y < StandY - 14.0f)
		return;

	CCharacter *pTarget = nullptr;
	if(m_ChaseTarget.m_Type == SPIDER_TARGET_PLAYER && m_ChaseTarget.m_PlayerCid >= 0)
	{
		CPlayer *pPl = GameServer()->m_apPlayers[m_ChaseTarget.m_PlayerCid];
		pTarget = pPl ? pPl->GetCharacter() : nullptr;
	}
	if(!IsHumanTarget(pTarget))
		pTarget = NearestVisibleHuman(SPIDER_LEAP_MAX_RANGE);
	if(!IsHumanTarget(pTarget))
		return;

	const float Dist = distance(pTarget->GetPos(), m_Pos);
	if(Dist < SPIDER_LEAP_MIN_RANGE || Dist > SPIDER_LEAP_MAX_RANGE)
		return;

	m_JumpPhase = SPIDER_JUMP_RISE;
	m_JumpStartTick = Server()->Tick();
	m_JumpPeakY = m_Pos.y;
	m_LastLeapTick = Server()->Tick();
	m_BodyVel = vec2(0.0f, 0.0f);
}

void CSpiderBoss::TrySpitWeb()
{
	if(!m_pCore || Server()->Tick() - m_LastWebTick < Server()->TickSpeed() * 4)
		return;

	CCharacter *pTarget = NearestVisibleHuman(SPIDER_WEB_MAX_RANGE);
	if(!IsHumanTarget(pTarget))
		return;

	const float Dist = distance(pTarget->GetPos(), m_Pos);
	if(Dist < SPIDER_WEB_MIN_RANGE || Dist > SPIDER_WEB_MAX_RANGE)
		return;

	vec2 BaseDir = pTarget->GetPos() - m_Pos;
	if(length(BaseDir) < 1.0f)
		return;
	BaseDir = normalize(BaseDir);
	const float BaseAngle = angle(BaseDir);
	const float Spread = 0.22f;

	for(int i = 0; i < SPIDER_WEB_SHOTS; i++)
	{
		const float T = (SPIDER_WEB_SHOTS <= 1) ? 0.0f : (float)i / (float)(SPIDER_WEB_SHOTS - 1) - 0.5f;
		const float A = BaseAngle + T * Spread;
		const vec2 Dir(cosf(A), sinf(A));
		new CProjectile(GameWorld(), WEAPON_SHOTGUN, m_OwnerCid, m_Pos + Dir * SPIDER_GRENADE_SPAWN_OFF, Dir, 2,
			SpiderAttackDamage(2), false, 2.0f, SOUND_GUN_FIRE, WEAPON_SHOTGUN);
	}

	m_LastWebTick = Server()->Tick();
}

void CSpiderBoss::UpdateBody()
{
	UpdateChaseTarget();
	const vec2 Goal = GetChaseGoal();
	const vec2 Waypoint = PathWaypoint(Goal);
	vec2 WishDir = ComputeMoveWish(Goal, Waypoint);
	m_MoveDir = WishDir;

	if(m_JumpPhase != SPIDER_JUMP_IDLE)
	{
		UpdateLeapStomp();
		int BestLeg = -1;
		float BestDist = 0.0f;
		for(int i = 0; i < NUM_SPIDER_LEGS; i++)
		{
			if(m_aLegHealth[i] <= 0 || m_aLegState[i] == SPIDER_LEG_SWING)
				continue;
			const float D = distance(m_aPlantedFoot[i], LegHome(i));
			if(D > BestDist)
			{
				BestDist = D;
				BestLeg = i;
			}
		}
		if(BestLeg >= 0 && BestDist > SPIDER_STEP_DIST * 1.4f)
			TryStartLegStep(BestLeg);
		return;
	}

	const float MaxSpd = SPIDER_SPEED + 0.12f * (float)minimum(m_Wave, 20);
	const float StandY = FindStandY(WishDir);
	const bool NearGround = m_Pos.y >= StandY - 18.0f;

	if(!NearGround)
	{
		vec2 WishVel = vec2(WishDir.x, 0.0f) * MaxSpd * 0.55f;
		m_BodyVel.x += (WishVel.x - m_BodyVel.x) * SPIDER_ACCEL;
		m_BodyVel.y += SPIDER_GRAVITY;
		ApplySpiderMove(m_BodyVel);
		const float LandY = FindStandY(WishDir);
		if(m_Pos.y >= LandY - 6.0f)
		{
			m_Pos.y = LandY;
			m_BodyVel = vec2(0.0f, 0.0f);
		}
	}
	else
	{
		vec2 WishVel = vec2(WishDir.x, 0.0f) * MaxSpd;
		if(length(WishDir) < 0.01f)
			m_BodyVel.x *= 0.8f;
		else if(absolute(WishDir.x) < 0.12f && distance(Goal, m_Pos) > 96.0f)
			WishVel.x = (Goal.x >= m_Pos.x ? 1.0f : -1.0f) * MaxSpd;

		m_BodyVel += (WishVel - m_BodyVel) * SPIDER_ACCEL;
		ApplySpiderGroundMove(m_BodyVel, WishDir);
	}

	RecoverIfInvalid();

	int BestLeg = -1;
	float BestDist = 0.0f;
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
	{
		if(m_aLegHealth[i] <= 0 || m_aLegState[i] == SPIDER_LEG_SWING)
			continue;
		const float D = distance(m_aPlantedFoot[i], LegHome(i));
		if(D > BestDist)
		{
			BestDist = D;
			BestLeg = i;
		}
	}
	if(BestLeg >= 0 && BestDist > SPIDER_STEP_DIST && length(m_BodyVel) > 0.15f)
		TryStartLegStep(BestLeg);
}

void CSpiderBoss::UpdateLegHitboxes()
{
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
	{
		if(m_aLegHealth[i] <= 0)
			continue;
		if(m_apSeg[i][SPIDER_SEG_KNEE])
			m_apSeg[i][SPIDER_SEG_KNEE]->SetSegmentPos(m_aKnee[i]);
		if(m_apSeg[i][SPIDER_SEG_FOOT])
			m_apSeg[i][SPIDER_SEG_FOOT]->SetSegmentPos(m_aFoot[i]);
	}
}

void CSpiderBoss::ApplyFootKnockback()
{
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
		m_aFootKnock[i] *= 0.82f;
}

void CSpiderBoss::DamageLeg(int Leg, int Dmg, vec2 Force, vec2 Source, int Weapon)
{
	if(Leg < 0 || Leg >= NUM_SPIDER_LEGS || m_aLegHealth[Leg] <= 0)
		return;
	m_aLegHealth[Leg] -= Dmg;
	if(WeaponCanKnockLeg(Weapon) && length(Source) > 1.0f)
	{
		vec2 Push = normalize(m_aFoot[Leg] - Source);
		if(length(Push) < 0.01f && length(Force) > 0.01f)
			Push = normalize(Force);
		m_aFootKnock[Leg] += Push * 20.0f;
	}
	if(m_aLegHealth[Leg] <= 0)
		GameServer()->SendChatAllLocF("boss.spider.leg_break", "蜘蛛 Boss：第 %d 条腿受损！", Leg + 1);
}

CCharacter *CSpiderBoss::NearestHuman(float MaxDist)
{
	CCharacter *pBest = nullptr;
	float Best = MaxDist * MaxDist;
	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChr = static_cast<CCharacter *>(r.front());
		if(!pChr || !pChr->IsAlive() || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy())
			continue;
		if(pChr->GetPlayer()->GetTeam() != TEAM_RED)
			continue;
		const float D2 = (pChr->GetPos() - m_Pos).x * (pChr->GetPos() - m_Pos).x + (pChr->GetPos() - m_Pos).y * (pChr->GetPos() - m_Pos).y;
		if(D2 < Best)
		{
			Best = D2;
			pBest = pChr;
		}
	}
	return pBest;
}

CCharacter *CSpiderBoss::NearestVisibleHuman(float MaxDist)
{
	CCharacter *pBest = nullptr;
	float Best = MaxDist * MaxDist;
	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChr = static_cast<CCharacter *>(r.front());
		if(!pChr || !pChr->IsAlive() || !pChr->GetPlayer() || pChr->GetPlayer()->IsDummy())
			continue;
		if(pChr->GetPlayer()->GetTeam() != TEAM_RED)
			continue;
		const vec2 Pos = pChr->GetPos();
		const float D2 = (Pos - m_Pos).x * (Pos - m_Pos).x + (Pos - m_Pos).y * (Pos - m_Pos).y;
		if(D2 >= Best || !HasLineOfSight(Pos))
			continue;
		Best = D2;
		pBest = pChr;
	}
	return pBest;
}

bool CSpiderBoss::IsHumanTarget(CCharacter *pChr) const
{
	return pChr && pChr->IsAlive() && pChr->GetPlayer() && !pChr->GetPlayer()->IsDummy() &&
		pChr->GetPlayer()->GetTeam() == TEAM_RED;
}

int CSpiderBoss::SpiderAttackDamage(int Base) const
{
	return maximum(Base, Base + m_Wave / 2);
}

void CSpiderBoss::OnLegPlanted(int Leg)
{
	if(Leg < 0 || Leg >= NUM_SPIDER_LEGS || m_aLegHealth[Leg] <= 0)
		return;
	if(Server()->Tick() - m_LastStompTick < Server()->TickSpeed())
		return;

	const vec2 Foot = m_aPlantedFoot[Leg];
	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChr = static_cast<CCharacter *>(r.front());
		if(!IsHumanTarget(pChr))
			continue;
		if(distance(pChr->GetPos(), Foot) > SPIDER_FOOT_STOMP_RANGE)
			continue;
		GameWorld()->CreateExplosion(Foot, m_pCore, WEAPON_HAMMER, SpiderAttackDamage(5));
		m_LastStompTick = Server()->Tick();
		return;
	}
}

void CSpiderBoss::TryMeleeAttacks()
{
	if(Server()->Tick() - m_LastBodySlamTick < Server()->TickSpeed())
		return;

	for(CGameWorld::TypeRange r = GameWorld()->DoTypeRange(CGameWorld::ENTTYPE_CHARACTER); !r.empty(); r.pop_front())
	{
		CCharacter *pChr = static_cast<CCharacter *>(r.front());
		if(!IsHumanTarget(pChr))
			continue;
		if(distance(pChr->GetPos(), m_Pos) > SPIDER_BODY_MELEE_RANGE)
			continue;
		GameWorld()->CreateExplosion(m_Pos, m_pCore, WEAPON_HAMMER, SpiderAttackDamage(6));
		m_LastBodySlamTick = Server()->Tick();
		return;
	}
}

void CSpiderBoss::TryAttackTower()
{
	if(!m_pCtrl || Server()->Tick() - m_LastTowerHitTick < Server()->TickSpeed() * 2)
		return;
	CGameControllerDefence *pCtrl = static_cast<CGameControllerDefence *>(m_pCtrl);
	CTowerMain *pTower = pCtrl->GetTower();
	if(!pTower)
		return;

	float DistTower = distance(m_Pos, pTower->GetPos());
	for(int i = 0; i < NUM_SPIDER_LEGS && DistTower > SPIDER_TOWER_MELEE_RANGE; i++)
	{
		if(m_aLegHealth[i] <= 0)
			continue;
		DistTower = minimum(DistTower, distance(m_aFoot[i], pTower->GetPos()));
	}
	if(DistTower > SPIDER_TOWER_MELEE_RANGE)
		return;

	pTower->TakeHit(vec2(0.0f, -4.0f), m_Pos - pTower->GetPos(), SpiderAttackDamage(10), m_pCore, WEAPON_HAMMER);
	m_LastTowerHitTick = Server()->Tick();
}

void CSpiderBoss::TryGrenade()
{
	if(!m_pCore || Server()->Tick() - m_LastGrenadeTick < Server()->TickSpeed() * 3)
		return;

	vec2 TargetPos(0.0f, 0.0f);
	bool HasTarget = false;

	if(m_ChaseTarget.m_Type == SPIDER_TARGET_PLAYER && m_ChaseTarget.m_PlayerCid >= 0)
	{
		CPlayer *pPl = GameServer()->m_apPlayers[m_ChaseTarget.m_PlayerCid];
		CCharacter *pChr = pPl ? pPl->GetCharacter() : nullptr;
		if(IsHumanTarget(pChr) && HasLineOfSight(pChr->GetPos()))
		{
			const float Dist = distance(pChr->GetPos(), m_Pos);
			if(Dist >= SPIDER_GRENADE_MIN_RANGE && Dist <= SPIDER_GRENADE_MAX_RANGE)
			{
				TargetPos = pChr->GetPos();
				HasTarget = true;
			}
		}
	}

	if(!HasTarget)
	{
		CCharacter *pHuman = NearestVisibleHuman(SPIDER_GRENADE_MAX_RANGE);
		if(pHuman)
		{
			const float DistHuman = distance(pHuman->GetPos(), m_Pos);
			if(DistHuman >= SPIDER_GRENADE_MIN_RANGE && DistHuman <= SPIDER_GRENADE_MAX_RANGE)
			{
				TargetPos = pHuman->GetPos();
				HasTarget = true;
			}
		}
	}

	if(!HasTarget && m_pCtrl)
	{
		CGameControllerDefence *pCtrl = static_cast<CGameControllerDefence *>(m_pCtrl);
		if(pCtrl->GetTower())
		{
			TargetPos = pCtrl->GetTower()->GetPos();
			const float DistTower = distance(TargetPos, m_Pos);
			if(DistTower >= SPIDER_GRENADE_MIN_RANGE && DistTower <= SPIDER_GRENADE_MAX_RANGE)
				HasTarget = true;
		}
	}

	if(!HasTarget)
		return;

	vec2 Dir = TargetPos - m_Pos;
	if(length(Dir) < 1.0f)
		return;
	Dir = normalize(Dir);
	new CProjectile(GameWorld(), WEAPON_GRENADE, m_OwnerCid, m_Pos + Dir * SPIDER_GRENADE_SPAWN_OFF, Dir, 1,
		SpiderAttackDamage(6), true, 0.0f, SOUND_GRENADE_FIRE, WEAPON_GRENADE);
	m_LastGrenadeTick = Server()->Tick();
}

void CSpiderBoss::Tick()
{
}

void CSpiderBoss::TickDefered()
{
	if(!IsCoreAlive() || !m_pCore)
		return;

	ApplyFootKnockback();
	UpdateBody();
	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
		UpdateLeg(i);
	UpdateLegHitboxes();
	m_pCore->SyncSpiderBody(m_Pos);

	TryLeapStomp();
	TrySpitWeb();
	TryMeleeAttacks();
	TryAttackTower();
	TryGrenade();
}

void CSpiderBoss::Snap(int SnappingClient)
{
	if(!m_pCore || NetworkClipped(SnappingClient))
		return;

	for(int i = 0; i < NUM_SPIDER_LEGS; i++)
	{
		if(m_aLegHealth[i] <= 0)
			continue;
		const vec2 Hip = HipWorld(i);
		const vec2 Seg[2][2] = {{Hip, m_aKnee[i]}, {m_aKnee[i], m_aFoot[i]}};
		for(int s = 0; s < NUM_SPIDER_SEGS; s++)
		{
			CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, SnapGroupId(SNAP_GROUP_SPIDER_LEGS, i * NUM_SPIDER_SEGS + s), sizeof(CNetObj_Laser)));
			if(!pObj)
				return;
			pObj->m_X = (int)Seg[s][0].x;
			pObj->m_Y = (int)Seg[s][0].y;
			pObj->m_FromX = (int)Seg[s][1].x;
			pObj->m_FromY = (int)Seg[s][1].y;
			pObj->m_StartTick = Server()->Tick();
		}
	}
}
