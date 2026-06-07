#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/shared/protocol.h>

#include <game/collision.h>
#include <game/gamecore.h>
#include <generated/server_data.h>

#include "botengine.h"
#include "entities/character.h"
#include "gamecontext.h"
#include "gamecontroller.h"
#include "gameworld.h"
#include "player.h"
#include "zombie_bot.h"

namespace
{
constexpr float HUMAN_AGGRO_RADIUS = 900.0f;
constexpr float HUMAN_COMBAT_RADIUS = 720.0f;
constexpr float HAMMER_ATTACK_RANGE = 85.0f;
constexpr float ZOOKER_SHOOT_MIN = 160.0f;
constexpr int BOT_HOOK_DIRS = 32;
constexpr float ZOMB_CROWD_RADIUS = 72.0f;
constexpr float ZOMB_SEPARATION_RADIUS = 58.0f;
constexpr float TOWER_SEEK_HOOK_DIST = 180.0f;
} // namespace

CZombieBot::CZombieBot(CBotEngine *pBotEngine, CPlayer *pPlayer, CGameController *pCtrl)
{
	m_pBotEngine = pBotEngine;
	m_pGameServer = pBotEngine->GameServer();
	m_pPlayer = pPlayer;
	m_pCtrl = pCtrl;
	m_pPath = &pBotEngine->m_aPaths[pPlayer->GetCID()];
	m_Flags = 0;
	m_Target = vec2(0.0f, 0.0f);
	m_RealTarget = vec2(0.0f, 0.0f);
	m_LastGoal = vec2(1.0e9f, 1.0e9f);
	m_LastPathTick = 0;
	m_LowSpeedTicks = 0;
	m_McJumpTried = false;
	m_StuckFlipCooldown = 0;
	m_HookCooldown = 0;
	mem_zero(&m_InputData, sizeof(m_InputData));
	m_LastData = m_InputData;
	m_ComputeTarget.m_Type = TARGET_MARCH;
	m_ComputeTarget.m_PlayerCID = -1;
	m_ComputeTarget.m_NeedUpdate = true;
}

CZombieBot::~CZombieBot()
{
	m_pPath->m_Size = 0;
}

CCollision *CZombieBot::Collision() const
{
	return m_pGameServer->Collision();
}

CTuningParams *CZombieBot::Tuning() const
{
	return m_pGameServer->Tuning();
}

int CZombieBot::GetTile(vec2 Pos) const
{
	return m_pBotEngine->GetTile(Pos);
}

int CZombieBot::ZombieFirstSlot() const
{
	return minimum((int)MAX_HUMAN_CLIENTS, m_pGameServer->Config()->m_SvMaxClients);
}

bool CZombieBot::IsGrounded()
{
	return m_pPlayer->GetCharacter()->IsGrounded();
}

vec2 CZombieBot::GetPersonalMarchGoal() const
{
	const vec2 Tower = m_pCtrl->TdGetZombieMarchGoal();
	const int CID = m_pPlayer->GetCID();
	const float Slot = (float)(CID % 24);
	const float Ang = Slot * (2.0f * pi / 24.0f);
	const float Rad = 96.0f + (float)((CID / 24) % 5) * 36.0f;
	vec2 Personal = Tower + vec2(cosf(Ang) * Rad, sinf(Ang) * Rad);

	CGraph *pGraph = m_pBotEngine->GetGraph();
	if(pGraph && pGraph->m_NumVertices > 0 && pGraph->m_pVertices)
	{
		const int V = m_pBotEngine->GetClosestVertex(Personal);
		if(V >= 0 && V < pGraph->m_NumVertices)
			Personal = pGraph->m_pVertices[V].m_Pos;
	}
	return Personal;
}

int CZombieBot::CountNearbyZombies(vec2 Pos, float Radius) const
{
	int Count = 0;
	const int Zombie0 = ZombieFirstSlot();
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(i == m_pPlayer->GetCID())
			continue;
		CPlayer *pZ = m_pGameServer->m_apPlayers[i];
		if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE)
			continue;
		CCharacter *pChr = pZ->GetCharacter();
		if(!pChr || !pChr->IsAlive())
			continue;
		if(distance(Pos, pChr->GetPos()) < Radius)
			Count++;
	}
	return Count;
}

void CZombieBot::ApplyCrowdSteering(vec2 Pos, vec2 *pTargetOff) const
{
	vec2 Sep(0.0f, 0.0f);
	const int Zombie0 = ZombieFirstSlot();
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(i == m_pPlayer->GetCID())
			continue;
		CPlayer *pZ = m_pGameServer->m_apPlayers[i];
		if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE)
			continue;
		CCharacter *pChr = pZ->GetCharacter();
		if(!pChr || !pChr->IsAlive())
			continue;

		vec2 Delta = Pos - pChr->GetPos();
		const float D = length(Delta);
		if(D < 1.0f || D > ZOMB_SEPARATION_RADIUS)
			continue;
		Sep += normalize(Delta) * (ZOMB_SEPARATION_RADIUS - D);
	}

	if(length(Sep) > 1.0f)
		*pTargetOff += Sep * 0.85f;
}

bool CZombieBot::ZombieBlockedAhead(vec2 Pos, int Dir) const
{
	if(Dir == 0)
		return false;

	const vec2 Ahead = Pos + vec2((float)Dir * 34.0f, -4.0f);
	const vec2 Box(CCharacterCore::PHYS_SIZE, CCharacterCore::PHYS_SIZE);
	if(Collision()->TestBox(Ahead, Box))
		return true;

	array<CEntity *> Ents;
	Ents.hint_size(8);
	m_pGameServer->m_World.FindEntities(Ahead, 24.0f, Ents, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Ents.size(); i++)
	{
		CCharacter *pChr = static_cast<CCharacter *>(Ents[i]);
		if(!pChr || !pChr->GetPlayer() || pChr->GetPlayer()->GetCID() == m_pPlayer->GetCID())
			continue;
		if(pChr->GetPlayer()->IsDummy() && pChr->GetPlayer()->GetZomb() != ZOMB_NONE)
			return true;
	}
	return false;
}

void CZombieBot::UpdateZombieTarget()
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr)
		return;

	const vec2 Pos = pChr->GetPos();
	const vec2 MarchGoal = GetPersonalMarchGoal();
	const bool HasTower = m_pCtrl->GetTower() != nullptr;
	const float DistMarch = distance(Pos, MarchGoal);

	int BestCid = -1;
	float BestDist = 1.0e12f;
	vec2 BestPos(0.0f, 0.0f);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == m_pPlayer->GetCID())
			continue;
		CPlayer *pPl = m_pGameServer->m_apPlayers[i];
		if(!pPl || pPl->IsDummy() || pPl->GetTeam() == TEAM_SPECTATORS)
			continue;
		CCharacter *pHuman = pPl->GetCharacter();
		if(!pHuman || !pHuman->IsAlive())
			continue;

		const float D = distance(Pos, pHuman->GetPos());
		if(D < BestDist)
		{
			BestDist = D;
			BestPos = pHuman->GetPos();
			BestCid = i;
		}
	}

	const bool HasHuman = BestCid >= 0;
	const bool HumanLos =
		HasHuman && !Collision()->FastIntersectLine(Pos, BestPos, nullptr, nullptr);
	const float Scale = GetAiScale();
	const bool HumanCombat = HumanLos && BestDist < GetCombatRadius();
	const bool PreferHuman = HumanCombat && (Scale >= 0.55f || BestDist < 300.0f) && (!HasTower || BestDist < DistMarch);

	int NewType = TARGET_MARCH;
	vec2 NewPos = MarchGoal;
	int NewCid = -1;

	if(PreferHuman)
	{
		NewType = TARGET_PLAYER;
		NewPos = BestPos;
		NewCid = BestCid;
	}

	if(m_ComputeTarget.m_Type != NewType || m_ComputeTarget.m_PlayerCID != NewCid ||
		distance(m_ComputeTarget.m_Pos, NewPos) > 48.0f)
	{
		m_ComputeTarget.m_Type = NewType;
		m_ComputeTarget.m_PlayerCID = NewCid;
		m_ComputeTarget.m_Pos = NewPos;
		m_ComputeTarget.m_NeedUpdate = true;
	}
	if(m_LowSpeedTicks > 30)
	{
		m_pPath->m_Size = 0;
		m_ComputeTarget.m_NeedUpdate = true;
		m_LowSpeedTicks = 0;
	}
}

void CZombieBot::UpdateMarchNavigation()
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr)
		return;

	const vec2 Pos = pChr->GetPos();
	vec2 Goal = m_ComputeTarget.m_Pos;
	if(m_ComputeTarget.m_Type == TARGET_MARCH)
		Goal = GetPersonalMarchGoal();

	m_Target = Goal - Pos;
	const int NearZ = CountNearbyZombies(Pos, ZOMB_CROWD_RADIUS);
	const bool Crowded = NearZ >= 1;

	if(length(m_Target) <= 48.0f)
	{
		ApplyCrowdSteering(Pos, &m_Target);
		return;
	}

	if(!Crowded)
	{
		const int Now = m_pGameServer->Server()->Tick();
		const int TickSpeed = m_pGameServer->Server()->TickSpeed();
		const bool NeedPath = m_pPath->m_Size <= 0 || distance(m_LastGoal, Goal) > 48.0f ||
			Now - m_LastPathTick > TickSpeed * 2 || m_ComputeTarget.m_NeedUpdate;

		if(NeedPath && m_pBotEngine->GetGraph()->m_NumVertices > 0)
		{
			m_pBotEngine->GetPath(Pos, Goal, m_pPath);
			m_LastGoal = Goal;
			m_LastPathTick = Now;
			m_ComputeTarget.m_NeedUpdate = false;
		}

		if(m_pPath->m_Size > 0)
		{
			vec2 Way;
			const int EdgeDist = m_pBotEngine->FarestPointOnEdge(m_pPath, Pos, &Way);
			if(EdgeDist >= 0 && distance(Pos, Way) > 48.0f)
				m_Target = Way - Pos;
			else
			{
				const vec2 Next = m_pBotEngine->NextPoint(Pos, Goal);
				if(distance(Pos, Next) > 32.0f)
					m_Target = Next - Pos;
				if(EdgeDist >= 0 && distance(Pos, Way) <= 48.0f)
					m_pPath->m_Size = 0;
			}
		}
	}

	ApplyCrowdSteering(Pos, &m_Target);
	EnsureMarchDrive(Pos, Goal);
}

void CZombieBot::EnsureMarchDrive(vec2 Pos, vec2 Goal)
{
	const vec2 ToGoal = Goal - Pos;
	if(length(ToGoal) < 96.0f)
		return;
	if(absolute(m_Target.x) >= 28.0f)
		return;

	m_Target = ToGoal;
	const int Lane = (m_pPlayer->GetCID() % 7) - 3;
	if(absolute(m_Target.x) < 28.0f)
		m_Target.x = ToGoal.x >= 0.0f ? 72.0f : -72.0f;
	m_Target.y = minimum(m_Target.y, -8.0f) + (float)Lane * 10.0f;
}

void CZombieBot::MakeChoice(bool UseTarget)
{
	(void)UseTarget;

	int Flags = 0;
	CCharacterCore *pMe = m_pPlayer->GetCharacter()->GetCore();
	CCharacterCore TempChar = *pMe;
	TempChar.m_Input = m_InputData;
	const vec2 CurPos = TempChar.m_Pos;

	const int CurTile = GetTile(TempChar.m_Pos);
	const bool Grounded = IsGrounded();

	TempChar.m_Input.m_Direction = (m_Target.x > 28.f) ? 1 : (m_Target.x < -28.f) ? -1 : 0;
	CWorldCore TempWorld;
	TempWorld.m_Tuning = *Tuning();
	TempChar.Init(&TempWorld, Collision());
	TempChar.Tick(true);
	TempChar.Move();
	TempChar.Quantize();

	const int NextTile = GetTile(TempChar.m_Pos);
	const vec2 NextPos = TempChar.m_Pos;

	if(TempChar.m_Input.m_Direction > 0)
		Flags |= BFLAG_RIGHT;
	if(TempChar.m_Input.m_Direction < 0)
		Flags |= BFLAG_LEFT;

	if(m_Target.y < 0)
	{
		if(CurTile & BTILE_SAFE && NextTile & BTILE_HOLE && (Grounded || TempChar.m_Vel.y > 0))
			Flags |= BFLAG_JUMP;
		if(CurTile & BTILE_SAFE && NextTile & BTILE_SAFE)
		{
			if(absolute(CurPos.x - NextPos.x) < 1.0f && TempChar.m_Input.m_Direction)
			{
				if(Grounded)
				{
					Flags |= BFLAG_JUMP;
					m_McJumpTried = true;
				}
				else if(m_McJumpTried && !(TempChar.m_Jumped) && TempChar.m_Vel.y > 0)
					Flags |= BFLAG_JUMP;
				else if(m_McJumpTried && TempChar.m_Jumped & 2 && TempChar.m_Vel.y > 0)
					Flags ^= BFLAG_RIGHT | BFLAG_LEFT;
			}
			else
				m_McJumpTried = false;
		}

		if(!(pMe->m_Jumped))
		{
			const vec2 Vel(pMe->m_Vel.x, minimum(pMe->m_Vel.y, 0.0f));
			if(Collision()->FastIntersectLine(pMe->m_Pos, pMe->m_Pos + Vel * 10.0f, nullptr, nullptr) &&
				!Collision()->FastIntersectLine(pMe->m_Pos,
					pMe->m_Pos + (Vel - vec2(0, TempWorld.m_Tuning.m_AirJumpImpulse)) * 10.0f, nullptr, nullptr))
				Flags |= BFLAG_JUMP;
			if(absolute(m_Target.x) < 28.f && pMe->m_Vel.y > -1.f)
				Flags |= BFLAG_JUMP;
		}
	}

	m_Flags = Flags;
}

bool CZombieBot::AllowTerrainHook(int CurTile, int MoveDir) const
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr)
		return false;
	if(!pChr->IsGrounded())
		return true;
	if(CurTile & BTILE_HOLE)
		return true;
	if(MoveDir != 0 && ZombieBlockedAhead(pChr->GetPos(), MoveDir) && m_Target.y < -24.0f)
		return true;
	if(m_LowSpeedTicks > 12 && absolute(m_Target.x) > 36.0f)
		return true;
	if(m_Target.y < -36.0f)
		return true;
	return false;
}

bool CZombieBot::WantSpeedHook(int CurTile, float DistMarch) const
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || pChr->IsGrounded() || (CurTile & BTILE_HOLE))
		return false;
	return DistMarch > TOWER_SEEK_HOOK_DIST;
}

bool CZombieBot::ShouldTryTerrainHook(int CurTile, int MoveDir, float DistMarch) const
{
	if(m_HookCooldown > 0)
		return false;
	if(!AllowTerrainHook(CurTile, MoveDir) && !WantSpeedHook(CurTile, DistMarch))
		return false;

	if(CurTile & BTILE_HOLE)
		return rand() % 4 == 0;
	if(WantSpeedHook(CurTile, DistMarch))
		return rand() % 5 == 0;
	if(m_Target.y < -32.0f)
		return rand() % 6 == 0;
	if(m_LowSpeedTicks > 10)
		return rand() % 6 == 0;
	if(CountNearbyZombies(m_pPlayer->GetCharacter()->GetPos(), ZOMB_CROWD_RADIUS) >= 2)
		return rand() % 5 == 0;
	if(!m_pPlayer->GetCharacter()->IsGrounded())
		return rand() % 7 == 0;
	return rand() % 9 == 0;
}

void CZombieBot::HandleHook(bool SeeTarget, int MoveDir, float DistMarch)
{
	if(m_pPlayer->GetZomb() != ZOMB_ZOOKER)
	{
		m_InputData.m_Hook = 0;
		return;
	}

	CCharacterCore *pMe = m_pPlayer->GetCharacter()->GetCore();
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pMe || !pChr)
		return;

	const int CurTile = GetTile(pMe->m_Pos);
	if(pMe->m_HookState == HOOK_FLYING)
	{
		m_InputData.m_Hook = 1;
		return;
	}

	if(SeeTarget && m_ComputeTarget.m_PlayerCID >= 0)
	{
		CPlayer *pTarget = m_pGameServer->m_apPlayers[m_ComputeTarget.m_PlayerCID];
		if(pTarget && pTarget->GetCharacter())
		{
			const CCharacterCore *pClosest = pTarget->GetCharacter()->GetCore();
			const float Dist = distance(pClosest->m_Pos, pMe->m_Pos);
			if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == m_ComputeTarget.m_PlayerCID)
				m_InputData.m_Hook = 1;
			else if(!m_InputData.m_Fire)
			{
				if(Dist < Tuning()->m_HookLength * 0.9f)
					m_InputData.m_Hook = m_LastData.m_Hook ^ 1;
				SeeTarget = Dist < Tuning()->m_HookLength * 0.9f;
			}
		}
	}

	if(!SeeTarget)
	{
		if(pMe->m_HookState == HOOK_GRABBED && pMe->m_HookedPlayer == -1)
		{
			const vec2 ToHook = pMe->m_HookPos - pMe->m_Pos;
			if(pChr->IsGrounded() && ToHook.y > 8.0f && m_Target.y > -28.0f)
			{
				m_InputData.m_Hook = 0;
				m_HookCooldown = 14;
				return;
			}

			vec2 HookVel = normalize(ToHook) * Tuning()->m_HookDragAccel;
			if(HookVel.y > 0)
				HookVel.y *= 0.3f;
			const int InputDir = MoveDir != 0 ? MoveDir : pMe->m_Input.m_Direction;
			if((HookVel.x < 0 && InputDir < 0) || (HookVel.x > 0 && InputDir > 0))
				HookVel.x *= 0.95f;
			else
				HookVel.x *= 0.75f;
			HookVel += vec2(0, 1) * Tuning()->m_Gravity;

			const float Ps = dot(m_Target, HookVel);
			if(Ps > 0 ||
				(CurTile & BTILE_HOLE && m_Target.y < 0 && pMe->m_Vel.y > 0.f &&
					pMe->m_HookTick < SERVER_TICK_SPEED + SERVER_TICK_SPEED / 2))
				m_InputData.m_Hook = 1;
			if(pMe->m_HookTick > 4 * SERVER_TICK_SPEED || length(ToHook) < 20.0f)
				m_InputData.m_Hook = 0;
		}
		if(pMe->m_HookState == HOOK_FLYING)
			m_InputData.m_Hook = 1;

		if(GetAiScale() >= 0.4f && !m_InputData.m_Fire && m_LastData.m_Hook == 0 && pMe->m_HookState == HOOK_IDLE &&
			ShouldTryTerrainHook(CurTile, MoveDir, DistMarch))
		{
			const int NumDir = BOT_HOOK_DIRS;
			vec2 HookDir(0.0f, 0.0f);
			float MaxForce = (CurTile & BTILE_HOLE) ? -10000.0f : 0.0f;
			const vec2 Target = m_Target;
			const int InputDir = MoveDir != 0 ? MoveDir : (Target.x > 0.0f ? 1 : (Target.x < 0.0f ? -1 : 0));
			for(int i = 0; i < NumDir; i++)
			{
				const float A = 2 * i * pi / NumDir;
				const vec2 Dir = direction(A);
				vec2 HookPos = pMe->m_Pos + Dir * Tuning()->m_HookLength;

				if(Collision()->FastIntersectLine(pMe->m_Pos, HookPos, &HookPos, nullptr) & CCollision::COLFLAG_SOLID)
				{
					vec2 HookVel = Dir * Tuning()->m_HookDragAccel;
					if(HookVel.y > 0)
						HookVel.y *= 0.3f;
					if((HookVel.x < 0 && InputDir < 0) || (HookVel.x > 0 && InputDir > 0))
						HookVel.x *= 0.95f;
					else
						HookVel.x *= 0.75f;

					HookVel += vec2(0, 1) * Tuning()->m_Gravity;

					const float Ps = dot(Target, HookVel);
					if(Ps > MaxForce)
					{
						MaxForce = Ps;
						HookDir = HookPos - pMe->m_Pos;
					}
				}
			}
			if(length(HookDir) > 32.f)
			{
				m_Target = HookDir;
				m_InputData.m_Hook = 1;
			}
		}
	}

	if(pChr->IsGrounded() && m_Target.y > -20.0f &&
		(pMe->m_HookState == HOOK_FLYING || pMe->m_HookState == HOOK_GRABBED) && m_InputData.m_Hook)
	{
		const vec2 ToHook = pMe->m_HookPos - pMe->m_Pos;
		if(length(ToHook) > 1.0f)
		{
			const vec2 PullDir = normalize(ToHook);
			if(PullDir.y > 0.35f && absolute(PullDir.x) < 0.5f)
			{
				m_InputData.m_Hook = 0;
				m_HookCooldown = 12;
			}
		}
	}
}

float CZombieBot::GetAiScale() const
{
	const int Wave = maximum(1, m_pCtrl->GetTdWave());
	return minimum(1.0f, 0.30f + (Wave - 1) * 0.078f) * m_pCtrl->TdDifficultyAiMul();
}

float CZombieBot::GetCombatRadius() const
{
	return HUMAN_COMBAT_RADIUS * (0.35f + 0.65f * GetAiScale());
}

float CZombieBot::GetAggroRadius() const
{
	return HUMAN_AGGRO_RADIUS * (0.35f + 0.65f * GetAiScale());
}

int CZombieBot::GetActiveZombType() const
{
	const int Z = m_pPlayer->GetZomb();
	if(Z != ZOMB_ZEATER)
		return Z;
	for(int i = 0; i < NUM_ZOMB_SUB; i++)
	{
		const int Sub = m_pPlayer->GetZombSub(i);
		if(Sub != ZOMB_NONE)
			return Sub;
	}
	return Z;
}

float CZombieBot::GetTypeAttackRange(int ZombType) const
{
	switch(ZombType)
	{
	case ZOMB_ZUNNER:
	case ZOMB_FLOMBIE:
		return 4000.0f;
	case ZOMB_ZENADE:
		return 800.0f;
	case ZOMB_ZOOMER:
		return Tuning()->m_LaserReach;
	case ZOMB_ZOOKER:
		return Tuning()->m_HookLength;
	case ZOMB_ZOTTER:
		return 500.0f;
	default:
		return HAMMER_ATTACK_RANGE;
	}
}

bool CZombieBot::TryZamerDetonate(float DistTower, float DistHuman, bool InSight)
{
	if(m_pPlayer->GetZomb() != ZOMB_ZAMER)
		return false;

	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return false;

	const float Trigger = 70.0f + 15.0f * GetAiScale();
	const bool NearTower = DistTower < Trigger;
	const bool NearHuman = InSight && DistHuman < Trigger;
	if(!NearTower && !NearHuman)
		return false;

	const vec2 Pos = pChr->GetPos();
	const int Wave = maximum(1, m_pCtrl->GetTdWave());
	const int Dmg = maximum(2, 3 + Wave / 2);
	const vec2 Offsets[4] = {vec2(5.0f, 5.0f), vec2(-5.0f, 5.0f), vec2(-5.0f, -5.0f), vec2(5.0f, -5.0f)};
	CGameContext *pGS = m_pGameServer;
	m_pPlayer->m_ZamerDetonating = true;
	for(int i = 0; i < 4; i++)
		pGS->m_World.CreateExplosion(Pos + Offsets[i], pChr, WEAPON_GRENADE, Dmg);
	m_pPlayer->m_ZamerDetonating = false;
	if(pChr->IsAlive())
		pChr->Die(m_pPlayer->GetCID(), WEAPON_SELF);
	return true;
}

void CZombieBot::ApplySpecialMovement(bool InSight, float DistHuman)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive() || m_pPlayer->GetZomb() != ZOMB_FLOMBIE)
		return;

	if(!InSight)
		return;

	CCharacterCore *pCore = pChr->GetCore();
	const float TargetY = m_RealTarget.y;
	if(TargetY < pCore->m_Pos.y)
		pCore->m_Vel.y -= 0.25f + Tuning()->m_Gravity;
	else if(TargetY > pCore->m_Pos.y)
		pCore->m_Vel.y += 0.25f;
}

void CZombieBot::TryZeleTeleport(bool InSight, float DistHuman)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive() || m_pPlayer->GetZomb() != ZOMB_ZELE || !InSight)
		return;

	if(DistHuman <= 200.0f || DistHuman > 500.0f)
		return;

	vec2 Dest = m_RealTarget;
	if(!Collision()->CheckPoint(Dest + vec2(0.0f, 32.0f)))
		Dest += vec2(0.0f, 32.0f);
	else if(!Collision()->CheckPoint(Dest - vec2(0.0f, 32.0f)))
		Dest -= vec2(0.0f, 32.0f);
	else
		return;

	CCharacterCore *pCore = pChr->GetCore();
	pCore->m_Pos = Dest;
	pCore->m_Vel.y = -0.1f;
}

void CZombieBot::TryZeaterConsume()
{
	if(m_pPlayer->GetZomb() != ZOMB_ZEATER)
		return;

	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr || !pChr->IsAlive())
		return;

	const vec2 Pos = pChr->GetPos();
	const int Zombie0 = ZombieFirstSlot();
	for(int i = Zombie0; i < MAX_CLIENTS; i++)
	{
		if(i == m_pPlayer->GetCID())
			continue;
		CPlayer *pZ = m_pGameServer->m_apPlayers[i];
		if(!pZ || !pZ->IsDummy() || pZ->GetZomb() == ZOMB_NONE || pZ->GetZomb() == ZOMB_ZEATER)
			continue;
		CCharacter *pVictim = pZ->GetCharacter();
		if(!pVictim || !pVictim->IsAlive())
			continue;
		if(distance(Pos, pVictim->GetPos()) > 65.0f)
			continue;

		int Slot = -1;
		for(int s = 0; s < NUM_ZOMB_SUB; s++)
		{
			if(m_pPlayer->GetZombSub(s) == ZOMB_NONE)
			{
				Slot = s;
				break;
			}
		}
		if(Slot < 0)
			return;

		const int VictimType = pZ->GetZomb();
		m_pPlayer->SetZombSub(Slot, VictimType);
		if(VictimType == ZOMB_ZASTER)
			pChr->SetHealthDirect(100);
		else
			pChr->IncreaseHealth(10);
		pVictim->Die(m_pPlayer->GetCID(), WEAPON_GAME);
		return;
	}
}

void CZombieBot::UpdateZinvisState(bool HumanCombat, bool HumanAggro)
{
	if(m_pPlayer->GetZomb() != ZOMB_ZINVIS)
		return;
	m_pPlayer->SetZombVisible(HumanCombat || HumanAggro);
}

void CZombieBot::HandleZombieWeapon(bool InSight, bool HumanCombat, bool HumanAggro, bool HasTower,
	float DistTower, float DistHuman)
{
	CCharacter *pChr = m_pPlayer->GetCharacter();
	if(!pChr)
		return;

	const int Z = GetActiveZombType();
	const float Scale = GetAiScale();
	const float AttackRange = GetTypeAttackRange(Z);
	const vec2 Pos = pChr->GetPos();
	const vec2 TowerPos = m_pCtrl->TdGetZombieMarchGoal();
	vec2 AimOff = m_Target;

	if(Z == ZOMB_ZINJA && HumanCombat && DistHuman < HAMMER_ATTACK_RANGE && pChr->GetActiveWeapon() == WEAPON_HAMMER)
		pChr->GiveNinja();

	switch(Z)
	{
	case ZOMB_ZUNNER:
	case ZOMB_FLOMBIE:
		if((HumanCombat || HumanAggro) && InSight)
			pChr->SetWeapon(WEAPON_GUN);
		else
			pChr->SetWeapon(WEAPON_HAMMER);
		break;
	case ZOMB_ZOOKER:
		pChr->SetWeapon(WEAPON_HAMMER);
		break;
	case ZOMB_ZOTTER:
		if(InSight && (HumanAggro || (HasTower && DistTower < AttackRange)))
			pChr->SetWeapon(WEAPON_SHOTGUN);
		else
			pChr->SetWeapon(WEAPON_HAMMER);
		break;
	case ZOMB_ZENADE:
		if(InSight && (DistHuman > 100.0f || DistTower > 100.0f) &&
			(HumanAggro || (HasTower && DistTower < AttackRange)))
			pChr->SetWeapon(WEAPON_GRENADE);
		else
			pChr->SetWeapon(WEAPON_HAMMER);
		break;
	case ZOMB_ZOOMER:
		if(InSight && (HumanAggro || (HasTower && DistTower < AttackRange)))
			pChr->SetWeapon(WEAPON_LASER);
		else
			pChr->SetWeapon(WEAPON_HAMMER);
		break;
	case ZOMB_ZAMER:
	case ZOMB_ZABY:
	case ZOMB_ZASTER:
	case ZOMB_ZINJA:
	case ZOMB_ZELE:
	case ZOMB_ZINVIS:
	case ZOMB_ZEATER:
	default:
		pChr->SetWeapon(pChr->GetActiveWeapon() == WEAPON_NINJA ? WEAPON_NINJA : WEAPON_HAMMER);
		break;
	}

	m_InputData.m_WantedWeapon = pChr->GetActiveWeapon() + 1;

	if(HumanCombat && InSight)
		AimOff = m_RealTarget - Pos;
	else if(HasTower)
		AimOff = TowerPos - Pos;

	int FireDiv = 3;
	switch(Z)
	{
	case ZOMB_ZAMER:
		FireDiv = 2;
		break;
	case ZOMB_ZOOKER:
		FireDiv = 4;
		break;
	case ZOMB_ZUNNER:
	case ZOMB_FLOMBIE:
		FireDiv = 3;
		break;
	case ZOMB_ZASTER:
		FireDiv = 5;
		break;
	case ZOMB_ZOTTER:
		FireDiv = 4;
		break;
	case ZOMB_ZENADE:
		FireDiv = 6;
		break;
	case ZOMB_ZOOMER:
		FireDiv = 4;
		break;
	case ZOMB_ZINJA:
		FireDiv = 3;
		break;
	default:
		FireDiv = 3;
		break;
	}
	FireDiv = maximum(1, (int)(FireDiv / (0.45f + 0.55f * Scale)));

	const int FirePeriod = maximum(1, m_pGameServer->Server()->TickSpeed() / maximum(1, FireDiv));
	const int ActiveWeapon = pChr->GetActiveWeapon();
	const bool Hammering = ActiveWeapon == WEAPON_HAMMER;
	const bool Ninjaing = ActiveWeapon == WEAPON_NINJA;
	const bool ShootGun = ActiveWeapon == WEAPON_GUN && pChr->WeaponAmmo(WEAPON_GUN) > 0;
	const bool ShootShotgun = ActiveWeapon == WEAPON_SHOTGUN;
	const bool ShootGrenade = ActiveWeapon == WEAPON_GRENADE;
	const bool ShootLaser = ActiveWeapon == WEAPON_LASER;

	if(Ninjaing && HumanCombat && DistHuman < 500.0f)
		m_InputData.m_Fire = (m_pGameServer->Server()->Tick() % (FirePeriod * 2)) < FirePeriod ? 1 : 0;
	else if(Hammering &&
		((HumanCombat && DistHuman < HAMMER_ATTACK_RANGE) || (HasTower && DistTower < HAMMER_ATTACK_RANGE)))
		m_InputData.m_Fire = (m_pGameServer->Server()->Tick() % (FirePeriod * 2)) < FirePeriod ? 1 : 0;
	else if(ShootGun && (HumanAggro || (HasTower && DistTower < AttackRange)))
		m_InputData.m_Fire = (m_pGameServer->Server()->Tick() % (FirePeriod * 2)) < FirePeriod ? 1 : 0;
	else if(ShootShotgun && InSight && (HumanAggro || (HasTower && DistTower < AttackRange)))
		m_InputData.m_Fire = (m_pGameServer->Server()->Tick() % (FirePeriod * 2)) < FirePeriod ? 1 : 0;
	else if(ShootGrenade && InSight && (HumanAggro || (HasTower && DistTower < AttackRange)))
		m_InputData.m_Fire = (m_pGameServer->Server()->Tick() % (FirePeriod * 3)) < FirePeriod ? 1 : 0;
	else if(ShootLaser && InSight && (HumanAggro || (HasTower && DistTower < AttackRange)))
		m_InputData.m_Fire = (m_pGameServer->Server()->Tick() % (FirePeriod * 2)) < FirePeriod ? 1 : 0;

	if(m_InputData.m_Fire)
	{
		m_Target = AimOff;
		const int Spread = (int)((1.0f - Scale) * 96.0f + 8.0f);
		const float Angle = angle(m_Target) + (float)(rand() % (Spread * 2 + 1) - Spread) * pi / 1024.0f;
		m_Target = direction(Angle) * length(m_Target);
	}
}

void CZombieBot::Tick()
{
	if(!m_pPlayer->GetCharacter())
		return;

	const CCharacterCore *pMe = m_pPlayer->GetCharacter()->GetCore();

	if(m_StuckFlipCooldown > 0)
		m_StuckFlipCooldown--;
	if(m_HookCooldown > 0)
		m_HookCooldown--;

	UpdateZombieTarget();

	mem_zero(&m_InputData, sizeof(m_InputData));
	m_InputData.m_WantedWeapon = m_LastData.m_WantedWeapon;

	const vec2 Pos = pMe->m_Pos;
	const bool HasTower = m_pCtrl->GetTower() != nullptr;
	const vec2 TowerPos = m_pCtrl->TdGetZombieMarchGoal();
	const float DistTower = distance(Pos, TowerPos);

	bool InSight = false;
	float DistHuman = 1.0e12f;
	bool HumanCombat = false;
	bool HumanAggro = false;

	if(m_ComputeTarget.m_Type == TARGET_PLAYER && m_ComputeTarget.m_PlayerCID >= 0)
	{
		CPlayer *pTarget = m_pGameServer->m_apPlayers[m_ComputeTarget.m_PlayerCID];
		if(pTarget && pTarget->GetCharacter())
		{
			const CCharacterCore *pClosest = pTarget->GetCharacter()->GetCore();
			InSight = !Collision()->FastIntersectLine(Pos, pClosest->m_Pos, nullptr, nullptr);
			m_RealTarget = pClosest->m_Pos;
			DistHuman = distance(Pos, pClosest->m_Pos);
			HumanCombat = InSight && DistHuman < GetCombatRadius();
			HumanAggro = InSight && DistHuman < GetAggroRadius();
			if(InSight)
				m_Target = pClosest->m_Pos - Pos;
			else
				UpdateMarchNavigation();
		}
		else
			m_ComputeTarget.m_NeedUpdate = true;
	}

	if(m_ComputeTarget.m_Type != TARGET_PLAYER)
		UpdateMarchNavigation();
	else if(!InSight)
	{
		ApplyCrowdSteering(Pos, &m_Target);
		EnsureMarchDrive(Pos, m_ComputeTarget.m_Pos);
	}

	m_RealTarget = m_Target + Pos;
	MakeChoice(InSight);

	int MoveDir = 0;
	if(m_Flags & BFLAG_RIGHT)
		MoveDir = 1;
	else if(m_Flags & BFLAG_LEFT)
		MoveDir = -1;

	if(MoveDir != 0 && ZombieBlockedAhead(Pos, MoveDir))
	{
		m_Flags |= BFLAG_JUMP;
		if(m_StuckFlipCooldown <= 0 && m_LowSpeedTicks > 12)
		{
			m_Flags ^= BFLAG_LEFT | BFLAG_RIGHT;
			m_StuckFlipCooldown = 20;
		}
	}

	if(IsGrounded() && !(m_Flags & BFLAG_JUMP) && absolute(pMe->m_Vel.x) < 0.4f && absolute(m_Target.x) > 40.0f)
		m_Flags |= BFLAG_JUMP;
	if(IsGrounded() && !(m_Flags & BFLAG_JUMP) && m_Target.y < -48.0f)
		m_Flags |= BFLAG_JUMP;

	if(absolute(pMe->m_Vel.x) < 0.5f)
		m_LowSpeedTicks++;
	else
		m_LowSpeedTicks = 0;

	TryZeaterConsume();
	UpdateZinvisState(HumanCombat, HumanAggro);
	TryZeleTeleport(InSight, DistHuman);
	ApplySpecialMovement(InSight, DistHuman);

	if(TryZamerDetonate(DistTower, DistHuman, InSight))
		return;

	HandleZombieWeapon(InSight, HumanCombat, HumanAggro, HasTower, DistTower, DistHuman);
	HandleHook(InSight, MoveDir, DistTower);

	if(m_Flags & BFLAG_LEFT)
		m_InputData.m_Direction = -1;
	if(m_Flags & BFLAG_RIGHT)
		m_InputData.m_Direction = 1;
	if(m_Flags & BFLAG_JUMP)
		m_InputData.m_Jump = 1;

	m_InputData.m_TargetX = m_LastData.m_TargetX;
	m_InputData.m_TargetY = m_LastData.m_TargetY;
	if(m_InputData.m_Hook || m_InputData.m_Fire)
	{
		m_InputData.m_TargetX = (int)m_Target.x;
		m_InputData.m_TargetY = (int)m_Target.y;
	}
	if(m_InputData.m_TargetX == 0 && m_InputData.m_TargetY == 0)
		m_InputData.m_TargetY = -1;

	m_LastData = m_InputData;
}
