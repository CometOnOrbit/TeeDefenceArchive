#include "skill_detonation_beam.h"
#include "skill_gravity_well.h"
#include "skill_spawn.h"
#include "skill_link_beam.h"

#include <game/server/entities/electro.h>
#include <game/server/entities/growingexplosion.h>
#include <game/server/entities/mmo/mmo_weapon_common.h>
#include <game/server/gamecontext.h>
#include <generated/server_data.h>

void SpawnSkillDetonationBeam(CGameWorld *pWorld, int OwnerCID, vec2 From, vec2 To, int ExplosionRadiusTiles, int ExplosionDamage, bool MiniPull)
{
	if(!pWorld)
		return;

	vec2 Delta = To - From;
	if(length(Delta) < 1.f)
		Delta = vec2(64.f, 0.f);
	vec2 Offset(-Delta.y * 0.15f, Delta.x * 0.15f);
	new CElectro(pWorld, From, To, Offset, 2);

	const int BeamTicks = maximum(1, pWorld->GameServer()->Server()->TickSpeed() / 2);
	SpawnSkillLinkBeam(pWorld, From, To, BeamTicks, SKILL_VFX_FIRE);

	pWorld->CreateSound(To, SOUND_LASER_BOUNCE);
	SpawnSkillHitBurst(pWorld, To, (float)ExplosionRadiusTiles * 32.f, SKILL_VFX_FIRE, 0, 12);
	new CGrowingExplosion(pWorld, To, normalize(Delta), OwnerCID, maximum(2, ExplosionRadiusTiles), GROWINGEXPLOSIONEFFECT_BOOM, false, GE_TARGET_MMO_HOSTILE, maximum(1, ExplosionDamage));

	if(MiniPull)
	{
		const int PullTicks = maximum(1, pWorld->GameServer()->Server()->TickSpeed() / 2);
		const float PullRadius = (float)maximum(64, ExplosionRadiusTiles * 24);
		new CSkillGravityWell(pWorld, OwnerCID, To, PullRadius, PullTicks, 0, 0, false);
	}
}
