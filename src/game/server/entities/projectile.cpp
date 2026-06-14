/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>

#include <game/server/gamecontext.h>
#include <game/server/item_system.h>
#include <game/server/player.h>

#include "character.h"
#include "growingexplosion.h"
#include "lightning.h"
#include <engine/shared/config.h>

#include <game/server/core/components/content/content_types.h>
#include <game/server/core/components/content/effect_registry.h>
#include <game/server/core/tworld_controller.h>

#include "projectile.h"

CProjectile::CProjectile(CGameWorld *pGameWorld, int Type, int Owner, vec2 Pos, vec2 Dir, int Span,
	int Damage, bool Explosive, float Force, int SoundImpact, int Weapon) : CChildEntity(pGameWorld, CGameWorld::ENTTYPE_PROJECTILE, 0, vec2(round_to_int(Pos.x), round_to_int(Pos.y)))
{
	m_Type = Type;
	m_Direction.x = round_to_int(Dir.x * 100.0f) / 100.0f;
	m_Direction.y = round_to_int(Dir.y * 100.0f) / 100.0f;
	m_LifeSpan = Span;
	m_Owner = Owner;
	m_OwnerTeam = GameServer()->m_apPlayers[Owner]->GetTeam();
	m_Force = Force;
	m_Damage = Damage;
	m_SoundImpact = SoundImpact;
	m_Weapon = Weapon;
	m_StartTick = Server()->Tick();
	m_Explosive = Explosive;

	GameWorld()->InsertEntity(this);
}

void CProjectile::Reset()
{
	GameWorld()->DestroyEntity(this);
}

void CProjectile::LoseOwner()
{
	if(m_OwnerTeam == TEAM_BLUE)
		m_Owner = PLAYER_TEAM_BLUE;
	else
		m_Owner = PLAYER_TEAM_RED;
}

vec2 CProjectile::GetPos(float Time)
{
	float Curvature = 0;
	float Speed = 0;

	switch(m_Type)
	{
		case WEAPON_GRENADE:
			Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
			Speed = GameServer()->Tuning()->m_GrenadeSpeed;
			break;

		case WEAPON_SHOTGUN:
			Curvature = GameServer()->Tuning()->m_ShotgunCurvature;
			Speed = GameServer()->Tuning()->m_ShotgunSpeed;
			break;

		case WEAPON_GUN:
			Curvature = GameServer()->Tuning()->m_GunCurvature;
			Speed = GameServer()->Tuning()->m_GunSpeed;
			break;
	}

	return CalcPos(m_Pos, m_Direction, Curvature, Speed, Time);
}

void CProjectile::Tick()
{
	float Pt = (Server()->Tick() - m_StartTick - 1) / (float) Server()->TickSpeed();
	float Ct = (Server()->Tick() - m_StartTick) / (float) Server()->TickSpeed();
	vec2 PrevPos = GetPos(Pt);
	vec2 CurPos = GetPos(Ct);
	int Collide = GameServer()->Collision()->IntersectLine(PrevPos, CurPos, &CurPos, 0);
	CCharacter *pOwnerChar = GameServer()->GetPlayerChar(m_Owner);
	CEntity *pIntersect = GameWorld()->IsHumanDefenderOwner(m_Owner)
		? GameWorld()->IntersectFlagEntitySkippingTurrets(PrevPos, CurPos, 6.0f, CurPos, CGameWorld::ENTFLAG_HITABLE, pOwnerChar)
		: GameWorld()->IntersectFlagEntity(PrevPos, CurPos, 6.0f, CurPos, CGameWorld::ENTFLAG_HITABLE, pOwnerChar);
	CHitableEntity *pTargetEnt = static_cast<CHitableEntity *>(pIntersect);

	if(pOwnerChar && pOwnerChar->GetPlayer() && ((Server()->Tick() - m_StartTick) % 25 == 0 || Server()->Tick() - m_StartTick < 3))
	{
		CItemHelper *pH = GameServer()->ItemHelper();
		const char *pEx = pOwnerChar->GetPlayer()->GetExtraForItem(pOwnerChar->GetPlayer()->GetHolding(ITYPE_SWORD));
		int ElectronCheck = 0;
		if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
		{
			CEffectContext Ctx = {};
			Ctx.m_pPlayer = pOwnerChar->GetPlayer();
			Ctx.m_pExtraJson = pEx;
			GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_PROJECTILE_HIT, Ctx);
			ElectronCheck = Ctx.m_ElectronStacks;
		}
		if(Config()->m_SvContentLegacyCards || !Config()->m_SvContentFramework)
			ElectronCheck = pH ? pH->GetCard(pEx, ITEM_CARD_ELECTRON_ID) : 0;
		if(pH && ElectronCheck > 0)
		{
			const int LightningDmg = maximum(1, m_Damage / 2);
			vec2 FlyDir = CurPos - PrevPos;
			if(length(FlyDir) < 0.001f)
				FlyDir = m_Direction;
			float a = angle(normalize(FlyDir));
			if(m_Type == WEAPON_SHOTGUN)
			{
				new CLightning(GameWorld(), CurPos, vec2(cosf(a), sinf(a)), 100.f, 50.f, m_Owner, LightningDmg);
			}
			else
			{
				const float Spreading[] = {-0.185f, -0.130f, -0.050f, 0.050f, 0.130f, 0.185f};
				for(int i = 0; i < 3; i++)
				{
					float SpreadA = a + Spreading[i + 3];
					new CLightning(GameWorld(), CurPos, vec2(cosf(SpreadA), sinf(SpreadA)), 200.f, 100.f, m_Owner, LightningDmg);
				}
			}
		}
	}

	m_LifeSpan--;

	if(pTargetEnt || Collide || m_LifeSpan < 0 || GameLayerClipped(CurPos))
	{
		if(m_LifeSpan >= 0 || m_Weapon == WEAPON_GRENADE)
			GameWorld()->CreateSound(CurPos, m_SoundImpact);

		int FusionStacks = 0;
		bool ExplosionCard = false;
		int ElectronStacks = 0;
		int ChainLightningStacks = 0;
		if(pOwnerChar && pOwnerChar->GetPlayer())
		{
			if(CItemHelper *pH = GameServer()->ItemHelper())
			{
				const char *pEx = pOwnerChar->GetPlayer()->GetExtraForItem(pOwnerChar->GetPlayer()->GetHolding(ITYPE_SWORD));
				if(Config()->m_SvContentFramework && GameServer()->Core() && GameServer()->Core()->EffectRegistry())
				{
					CEffectContext Ctx = {};
					Ctx.m_pPlayer = pOwnerChar->GetPlayer();
					Ctx.m_pExtraJson = pEx;
					GameServer()->Core()->EffectRegistry()->Apply(TRIGGER_PROJECTILE_HIT, Ctx);
					FusionStacks = Ctx.m_FusionStacks;
					ExplosionCard = (Ctx.m_Flags & EFFECT_FLAG_EXPLOSIVE) != 0;
					ElectronStacks = Ctx.m_ElectronStacks;
					ChainLightningStacks = Ctx.m_ChainLightningStacks;
				}
				if(Config()->m_SvContentLegacyCards || !Config()->m_SvContentFramework)
				{
					FusionStacks = pH->GetCard(pEx, ITEM_CARD_FUSION_ID);
					ExplosionCard = pH->GetCard(pEx, ITEM_CARD_EXPLOSION_ID) > 0;
					ElectronStacks = pH->GetCard(pEx, ITEM_CARD_ELECTRON_ID);
				}
			}
		}

		if(FusionStacks > 0 && m_Type != WEAPON_SHOTGUN)
			new CGrowingExplosion(GameWorld(), CurPos, vec2(0.f, 0.f), m_Owner, 24 * FusionStacks, GROWINGEXPLOSIONEFFECT_BOOM, true);

		if(m_Explosive || ExplosionCard)
		{
			if(ElectronStacks > 0)
			{
				float ElRadius = (float)ElectronStacks;
				if(FusionStacks > 0)
					ElRadius = 0.5f;
				new CGrowingExplosion(GameWorld(), CurPos, vec2(0.f, 0.f), m_Owner, maximum(1, (int)(5.f * ElRadius)), GROWINGEXPLOSIONEFFECT_ELECTRIC, FusionStacks > 0);
			}
			GameWorld()->CreateExplosion(CurPos, this, m_Weapon, maximum(1, m_Damage));
		}
		else if(pTargetEnt)
		{
			pTargetEnt->TakeHit(m_Direction * maximum(0.001f, m_Force), m_Direction * -1, m_Damage, this, m_Weapon);
			if(ChainLightningStacks > 0)
			{
				const int LightningDmg = maximum(1, m_Damage / 2);
				const float Spreading[] = {-0.185f, -0.130f, -0.050f, 0.050f, 0.130f, 0.185f};
				float a = angle(m_Direction);
				const int NumBolts = minimum(ChainLightningStacks, 5);
				for(int i = 0; i < NumBolts; i++)
				{
					float SpreadA = a + Spreading[i + 1];
					new CLightning(GameWorld(), CurPos, vec2(cosf(SpreadA), sinf(SpreadA)), 200.f, 100.f, m_Owner, LightningDmg);
				}
			}
			if(pTargetEnt->ObjType() == CGameWorld::ENTTYPE_CHARACTER && pOwnerChar && pOwnerChar->GetPlayer() &&
				pOwnerChar->GetPlayer()->GetZomb() == ZOMB_SPIDER_BOSS && m_Weapon == WEAPON_SHOTGUN)
			{
				static_cast<CCharacter *>(pTargetEnt)->ApplyElectronSlow(4);
			}
		}

		GameWorld()->DestroyEntity(this);
	}
}

void CProjectile::TickPaused()
{
	++m_StartTick;
}

void CProjectile::FillInfo(CNetObj_Projectile *pProj)
{
	pProj->m_X = round_to_int(m_Pos.x);
	pProj->m_Y = round_to_int(m_Pos.y);
	pProj->m_VelX = round_to_int(m_Direction.x * 100.0f);
	pProj->m_VelY = round_to_int(m_Direction.y * 100.0f);
	pProj->m_StartTick = m_StartTick;
	pProj->m_Type = m_Type;
}

void CProjectile::Snap(int SnappingClient)
{
	float Ct = (Server()->Tick() - m_StartTick) / (float) Server()->TickSpeed();

	if(NetworkClipped(SnappingClient, GetPos(Ct)))
		return;

	CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, GetID(), sizeof(CNetObj_Projectile)));
	if(pProj)
		FillInfo(pProj);
}
