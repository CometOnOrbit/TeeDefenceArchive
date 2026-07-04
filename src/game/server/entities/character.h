/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_CHARACTER_H
#define GAME_SERVER_ENTITIES_CHARACTER_H

#include <generated/protocol.h>

#include <game/gamecore.h>
#include <game/server/entity.h>
#include <game/server/core/tools/tiles_handler.h>

// MRPG safety flags for safe zones
enum
{
	SAFEFLAG_DAMAGE_DISABLED = 1 << 0,
	SAFEFLAG_HAMMER_HIT_DISABLED = 1 << 1,
	SAFEFLAG_COLLISION_DISABLED = 1 << 2,
	SAFEFLAG_HOOK_HIT_DISABLED = 1 << 3,
	SAFEFLAG_SUPER = 1 << 4,
};

// Move restrictions for world boundaries and zones
enum
{
	MOVERESTRICTION_PREVENT_LEFT = 1 << 0,
	MOVERESTRICTION_PREVENT_RIGHT = 1 << 1,
	MOVERESTRICTION_PREVENT_UP = 1 << 2,
	MOVERESTRICTION_PREVENT_DOWN = 1 << 3,
};

class CCharacter : public CHitableEntity
{
	MACRO_ALLOC_POOL_ID()

public:
	// character's size
	static const int ms_PhysSize = 28;

	enum
	{
		MIN_KILLMESSAGE_CLIENTVERSION = 0x0704, // todo 0.8: remove me
		MIN_CORRECTTUNING_CLIENTVERSION = 0x0706, // todo 0.8: remove me
	};

	CCharacter(CGameWorld *pWorld);

	virtual void Reset();
	virtual void Destroy();
	virtual void Tick();
	virtual void TickDefered();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);
	virtual void PostSnap();

	bool IsGrounded();

	void SetWeapon(int W);
	void HandleWeaponSwitch();
	void DoWeaponSwitch();

	void HandleWeapons();
	void HandleNinja();

	void OnPredictedInput(CNetObj_PlayerInput *pNewInput);
	void OnDirectInput(CNetObj_PlayerInput *pNewInput);
	void ResetInput();
	void FireWeapon();

	void Die(int Killer, int Weapon);
	bool TakeDamage(vec2 Force, vec2 Source, int Dmg, int From, int Weapon);
	virtual bool TakeHit(vec2 Force, vec2 Source, int Dmg, CEntity *pFrom, int Weapon);

	void ApplyElectronSlow(int CardStacks);

	bool Spawn(class CPlayer *pPlayer, vec2 Pos);

	bool IncreaseHealth(int Amount);
	void AddMaxHealth(int Amount);
	void SetMaxHealth(int Amount) { m_MaxHealth = maximum(1, Amount); }
	int GetMaxHealth() const { return m_MaxHealth; }
	bool IncreaseArmor(int Amount);
	void ReduceArmor(int Amount);
	void SetHealthDirect(int Amount);
	void SetBossHealth(int Amount);
	void SetHitRadius(float Radius);
	void SyncSpiderBody(vec2 Pos);
	void SetInput(const CNetObj_PlayerInput &NewInput) { m_Input = NewInput; }

	bool GiveWeapon(int Weapon, int Ammo);
	void AddWeaponAmmo(int Weapon, int Bonus);
	void SyncMMOWeaponAmmo(int MaxAmmo);
	void GiveNinja();
	void SetNinjaActivationTick(int Tick) { m_Ninja.m_ActivationTick = Tick; }

	void SetEmote(int Emote, int Tick);
	void SetCharacterPos(vec2 NewPos) { m_Core.m_Pos = NewPos; m_Pos = NewPos; }
	// Weapon category: classifies a weapon index (WEAPON_HAMMER etc.) into melee or ranged
	static int WeaponCategoryForWeapon(int Weapon);

	bool IsAlive() const { return m_Alive; }
	int GetHealth() const { return m_Health; }
	int GetArmor() const { return m_Armor; }
	class CPlayer *GetPlayer() { return m_pPlayer; }
	int GetCID();
	vec2 GetVelocity() const { return m_Core.m_Vel; }
	int WeaponAmmo(int Weapon) const;
	int HookState() const { return m_Core.m_HookState; }
	int GetActiveWeapon() const { return m_ActiveWeapon; }
	int GetActiveCategory() const { return m_ActiveCategory; }
	int GetActiveSkillSlot() const { return m_ActiveSkillSlot; }
	int GetActiveMeleeLoadoutIdx() const { return m_ActiveMeleeLoadoutIdx; }
	int GetActiveRangedLoadoutIdx() const { return m_ActiveRangedLoadoutIdx; }
	int GetActiveWeaponItemID() const { return m_ActiveWeaponItemID; }
	bool TryActivateLoadoutIdx(int Category, int LoadoutIdx);
	void CycleLoadoutInCategory(int Direction);
	const CNetObj_PlayerInput &LatestInput() const { return m_LatestInput; }
	const CNetObj_PlayerInput &LatestPrevInput() const { return m_LatestPrevInput; }
	CCharacterCore *GetCore() { return &m_Core; }
	const CCharacterCore *GetCore() const { return &m_Core; }

	// MRPG extensions
	int m_Mana{};
	int m_WaterAir{};
	char m_aZoneName[64]{};
	int m_SafeTickFlags{};
	int m_TuneZoneOverride{};
	int m_MoveRestrictions{};
	vec2 m_PrevPos{};
	CTileHandler *m_pTilesHandler{};

	// Weapon category: melee / ranged / magic (3/4/5 skill bar)
	enum { WEAPONCAT_MELEE = 0, WEAPONCAT_RANGED = 1, WEAPONCAT_MAGIC = 2 };
	enum { WEAPON_VISUAL_NONE = -1 }; // empty hands in CNetObj_Character snap
	enum { NUM_WEAPON_SLOT_CATEGORIES = 2 }; // stored last weapon per melee/ranged slot
	int m_aCategoryLastWeapon[NUM_WEAPON_SLOT_CATEGORIES];
	int m_ActiveCategory;
	int m_ActiveSkillSlot; // 0..2 → keys 3/4/5 when WEAPONCAT_MAGIC
	int m_ActiveMeleeLoadoutIdx;  // 0..3 active melee loadout slot
	int m_ActiveRangedLoadoutIdx; // 0..3 active ranged loadout slot
	int m_ActiveWeaponItemID; // current MMO item for stats/HUD (-1 = default hammer)

	// Skill runtime states (no class system, any player can use any skill)
	int m_RenewTicks;          // renew: remaining regen ticks
	int m_RenewAmount;         // renew: HP per tick
	int m_IronWillTicks;       // iron_will: remaining ticks
	int m_ShadowTicks;         // shadow_step: remaining invisibility ticks
	bool m_ShadowNextCrit;     // shadow_step: next attack deals bonus
	bool m_IsInvisible;        // shadow_step: hidden from enemies

	void SetSafeFlags(int Flags = SAFEFLAG_DAMAGE_DISABLED | SAFEFLAG_COLLISION_DISABLED | SAFEFLAG_HOOK_HIT_DISABLED) { m_SafeTickFlags = Flags; }
	void HandleWater();
	void HandleBuff();
	void HandleTuning();
	void HandleIndependentTuning();
	void HandleSafeFlags();
	void ApplyMoveRestrictions();
	void TickFashionAura();
	bool IncreaseMana(int Amount);
	bool TryUseMana(int Mana);
	int Mana() const { return m_Mana; }

	bool m_InMining;
	int m_MiningTick;
	bool m_LockedCK;
	vec2 m_LockPos;

	int m_RetaliationExpireTick;
	int m_RetaliationStacks;

	// need this hook for gamecontroller to call ninja fire
	void DoNinjaFire(vec2 Direction, int MoveTime);
	// need this hook for gamecontroller to enable weapons, -1 to enable all
	void EnableWeapon(int WeaponID);
	// need this hook for gamecontroller to disable weapons, -1 to disable all, but remember to enable at least one weapon unless you want the server get stuck
	void DisableWeapon(int WeaponID);
	// need this hook for gamecontroller to remove weapon, it's different from the function DisableWeapon.
	void RemoveWeapon(int WeaponID);

private:
	friend class CCharacterBotAI;
	friend class CBaseAI;
	friend class CMobAI;
	friend class CNpcAI;
	friend class CQuestMobAI;
	friend class CQuestNpcAI;
	friend class CTargetAI;
	friend class CWorldBossManager;

	// player controlling this character
	class CPlayer *m_pPlayer;

	bool m_Alive;

	// weapon info
	array<CEntity *> m_lpHitObjects;

	struct WeaponStat
	{
		int m_AmmoRegenStart;
		int m_Ammo;
		bool m_Got;
		bool m_Valid;
	} m_aWeapons[NUM_WEAPONS];

	int m_ActiveWeapon;
	int m_LastWeapon;
	int m_QueuedWeapon;

	int m_ReloadTimer;
	int m_AttackTick;

	int m_EmoteType;
	int m_EmoteStop;

	// last tick that the player took any action ie some input
	int m_LastAction;
	int m_LastNoAmmoSound;

	// these are non-heldback inputs
	CNetObj_PlayerInput m_LatestPrevInput;
	CNetObj_PlayerInput m_LatestInput;

	// input
	CNetObj_PlayerInput m_Input;
	int m_NumInputs;
	int m_Jumped;

	int m_Health;
	int m_Armor;

	int m_CardElectronTicks;

	int m_MaxHealth;

	int m_TriggeredEvents;

	// ninja
	struct
	{
		vec2 m_ActivationDir;
		int m_ActivationTick;
		int m_CurrentMoveTime;
		int m_OldVelAmount;
	} m_Ninja;

	// the player core for the physics
	CCharacterCore m_Core;

	// info for dead reckoning
	int m_ReckoningTick; // tick that we are performing dead reckoning From
	CCharacterCore m_SendCore; // core that we should send
	CCharacterCore m_ReckoningCore; // the dead reckoning core
};

#endif
