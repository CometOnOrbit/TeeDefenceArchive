#include "content_types.h"

#include <base/system.h>

EEffectTrigger EffectTriggerFromString(const char *pStr)
{
	if(!pStr)
		return NUM_EFFECT_TRIGGERS;
	if(str_comp_nocase(pStr, "OnWeaponFire") == 0)
		return TRIGGER_WEAPON_FIRE;
	if(str_comp_nocase(pStr, "OnReload") == 0)
		return TRIGGER_RELOAD;
	if(str_comp_nocase(pStr, "OnDealDamage") == 0)
		return TRIGGER_DEAL_DAMAGE;
	if(str_comp_nocase(pStr, "OnTakeDamage") == 0)
		return TRIGGER_TAKE_DAMAGE;
	if(str_comp_nocase(pStr, "OnProjectileHit") == 0)
		return TRIGGER_PROJECTILE_HIT;
	if(str_comp_nocase(pStr, "OnLaserHit") == 0)
		return TRIGGER_LASER_HIT;
	if(str_comp_nocase(pStr, "OnTurretFire") == 0)
		return TRIGGER_TURRET_FIRE;
	if(str_comp_nocase(pStr, "OnMine") == 0)
		return TRIGGER_MINE;
	if(str_comp_nocase(pStr, "OnTick") == 0)
		return TRIGGER_TICK;
	if(str_comp_nocase(pStr, "OnSpawn") == 0)
		return TRIGGER_SPAWN;
	if(str_comp_nocase(pStr, "OnDeath") == 0)
		return TRIGGER_DEATH;
	return NUM_EFFECT_TRIGGERS;
}
