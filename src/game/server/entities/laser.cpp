/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <generated/server_data.h>

#include "character.h"
#include "laser.h"

CLaser::CLaser(CGameWorld *pGameWorld, vec2 Pos, vec2 Direction, float StartEnergy, int Owner, int Damage, bool Explosive, float HitForce, int ElectronStacks, int CardHostItemId) : CChildEntity(pGameWorld, CGameWorld::ENTTYPE_LASER, 0, Pos)
{
	m_Damage = Damage;
	m_Owner = Owner;
	m_Energy = StartEnergy;
	m_Dir = Direction;
	m_Bounces = 0;
	m_EvalTick = 0;
	m_Explosive = Explosive;
	m_HitForce = HitForce;
	m_ElectronStacks = ElectronStacks;
	m_CardHostItemId = CardHostItemId;
	GameWorld()->InsertEntity(this);
	DoBounce();
}

bool CLaser::HitCharacter(vec2 From, vec2 To)
{
	vec2 At;
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CHitableEntity *pHit = (CHitableEntity *) GameWorld()->IntersectFlagEntity(m_Pos, To, 0.f, At, CGameWorld::ENTFLAG_HITABLE, pOwnerChar);
	if(!pHit)
		return false;
	if(pHit->ObjType() == CGameWorld::ENTTYPE_TURRET && GameWorld()->IsHumanDefenderOwner(m_Owner))
		return false;

	m_From = From;
	m_Pos = At;
	m_Energy = -1;
	if(m_Explosive)
		GameWorld()->CreateExplosion(At, this, WEAPON_LASER, m_Damage);
	else
	{
		vec2 F = normalize(To - From) * maximum(0.001f, m_HitForce);
		pHit->TakeHit(F, normalize(To - From), m_Damage, this, WEAPON_LASER);
		if(m_ElectronStacks > 0 && pHit->ObjType() == CGameWorld::ENTTYPE_CHARACTER)
			static_cast<CCharacter *>(pHit)->ApplyElectronSlow(m_ElectronStacks);
	}
	return true;
}

void CLaser::DoBounce()
{
	m_EvalTick = Server()->Tick();

	if(m_Energy < 0)
	{
		GameWorld()->DestroyEntity(this);
		return;
	}

	vec2 To = m_Pos + m_Dir * m_Energy;

	if(GameServer()->Collision()->IntersectLine(m_Pos, To, 0x0, &To))
	{
		if(!HitCharacter(m_Pos, To))
		{
			// intersected
			m_From = m_Pos;
			m_Pos = To;

			vec2 TempPos = m_Pos;
			vec2 TempDir = m_Dir * 4.0f;

			GameServer()->Collision()->MovePoint(&TempPos, &TempDir, 1.0f, 0);
			m_Pos = TempPos;
			m_Dir = normalize(TempDir);

			m_Energy -= distance(m_From, m_Pos) + GameServer()->Tuning()->m_LaserBounceCost;
			m_Bounces++;

			if(m_Bounces > GameServer()->Tuning()->m_LaserBounceNum)
				m_Energy = -1;

			GameWorld()->CreateSound(m_Pos, SOUND_LASER_BOUNCE);

			CItemHelper *pH = GameServer()->ItemHelper();
			if(pH && m_Owner >= 0 && m_Owner < MAX_CLIENTS)
			{
				CPlayer *pOwner = GameServer()->m_apPlayers[m_Owner];
				if(pOwner)
				{
					const int HostItem = m_CardHostItemId >= 0 ? m_CardHostItemId : pOwner->GetHolding(ITYPE_SWORD);
					if(HostItem > 0)
					{
						const char *pEx = pOwner->GetExtraForItem(HostItem);
						const int Exp = pH->GetCard(pEx, ITEM_CARD_EXPLOSION_ID);
						if(Exp > 0)
						{
							const int Fu = pH->GetCard(pEx, ITEM_CARD_FUSION_ID);
							const int BoomDmg = maximum(1, m_Damage * (2 + Fu) / 2);
							GameWorld()->CreateExplosion(m_Pos, this, WEAPON_LASER, BoomDmg);
						}
					}
				}
			}
		}
	}
	else
	{
		if(!HitCharacter(m_Pos, To))
		{
			m_From = m_Pos;
			m_Pos = To;
			m_Energy = -1;
		}
	}
}

void CLaser::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CLaser::Tick()
{
	if((Server()->Tick() - m_EvalTick) > (Server()->TickSpeed() * GameServer()->Tuning()->m_LaserBounceDelay) / 1000.0f)
		DoBounce();
}

void CLaser::TickPaused()
{
	++m_EvalTick;
}

void CLaser::Snap(int SnappingClient)
{
	if(NetworkClippedLine(SnappingClient, m_From, m_Pos))
		return;

	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = round_to_int(m_Pos.x);
	pObj->m_Y = round_to_int(m_Pos.y);
	pObj->m_FromX = round_to_int(m_From.x);
	pObj->m_FromY = round_to_int(m_From.y);
	pObj->m_StartTick = m_EvalTick;
}
