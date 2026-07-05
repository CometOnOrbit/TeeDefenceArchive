#include "guild_match_mode.h"

#include <base/system.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>
#include <game/server/entities/character.h>

static const SGuildWarModeDef s_aGuildWarModes[] = {
	{"fng", "FNG", "激光冻结 + 锤击入刺得分（需 FNG 刺图）", "AliveFNG", 30, true, false},
	{"ctf", "CTF", "团队夺旗，插旗得分", "ctf1", 50, true, false},
	{"tdm", "TDM", "团队死斗，标准武器", "dm1", 30, true, false},
	{"itdm", "iTDM", "团队激光一击必杀，弹药自动补充", "dm1", 20, true, true},
	{"idm", "iDM", "激光一击必杀（兼容旧命令，同 iTDM）", "dm1", 20, true, true},
};

static EGuildWarMode FindGuildWarModeById(const char *pMode)
{
	if(!pMode || !pMode[0])
		return GUILDWAR_MODE_INVALID;
	for(int i = 0; i < NUM_GUILDWAR_MODES; i++)
	{
		if(str_comp_nocase(pMode, s_aGuildWarModes[i].m_pId) == 0)
			return (EGuildWarMode)i;
	}
	return GUILDWAR_MODE_INVALID;
}

EGuildWarMode ParseGuildWarMode(const char *pMode)
{
	return FindGuildWarModeById(pMode);
}

const char *GuildWarModeToString(EGuildWarMode Mode)
{
	if(Mode < 0 || Mode >= NUM_GUILDWAR_MODES)
		return "";
	return s_aGuildWarModes[Mode].m_pId;
}

const SGuildWarModeDef *GetGuildWarModeDef(EGuildWarMode Mode)
{
	if(Mode < 0 || Mode >= NUM_GUILDWAR_MODES)
		return nullptr;
	return &s_aGuildWarModes[Mode];
}

const char *GuildWarModeDisplayName(const char *pMode)
{
	const SGuildWarModeDef *pDef = GetGuildWarModeDef(ParseGuildWarMode(pMode));
	return pDef ? pDef->m_pDisplayName : (pMode ? pMode : "?");
}

bool IsValidGuildWarMode(const char *pMode)
{
	return ParseGuildWarMode(pMode) != GUILDWAR_MODE_INVALID;
}

bool IsInstagibGuildWarMode(const char *pMode)
{
	const SGuildWarModeDef *pDef = GetGuildWarModeDef(ParseGuildWarMode(pMode));
	return pDef && pDef->m_Instagib;
}

const char *GuildWarArenaMapForMode(const char *pMode)
{
	const SGuildWarModeDef *pDef = GetGuildWarModeDef(ParseGuildWarMode(pMode));
	return pDef ? pDef->m_pArenaMap : "dm1";
}

int GuildWarDefaultTargetScore(const char *pMode)
{
	const SGuildWarModeDef *pDef = GetGuildWarModeDef(ParseGuildWarMode(pMode));
	return pDef ? pDef->m_DefaultTargetScore : 30;
}

static void StripToWeapons(CCharacter *pChar, const int *pWeapons, int NumWeapons, const int *pAmmo)
{
	for(int w = 0; w < NUM_WEAPONS; w++)
		pChar->RemoveWeapon(w);

	for(int i = 0; i < NumWeapons; i++)
	{
		const int Weapon = pWeapons[i];
		const int Ammo = pAmmo ? pAmmo[i] : -1;
		pChar->GiveWeapon(Weapon, Ammo);
	}

	if(NumWeapons > 0)
		pChar->SetWeapon(pWeapons[0]);
}

void ApplyGuildWarModeRules(CPlayer *pPlayer, const char *pMode)
{
	if(!pPlayer)
		return;
	CCharacter *pChar = pPlayer->GetCharacter();
	if(!pChar)
		return;

	const EGuildWarMode Mode = ParseGuildWarMode(pMode);
	switch(Mode)
	{
	case GUILDWAR_MODE_FNG:
	{
		// ddnet-pvp FNG spawn: hammer + laser; other weapons stripped
		const int aWeapons[] = {WEAPON_HAMMER, WEAPON_LASER};
		const int aAmmo[] = {-1, -1};
		StripToWeapons(pChar, aWeapons, 2, aAmmo);
		pChar->SetHealthDirect(10);
		break;
	}
	case GUILDWAR_MODE_ITDM:
	case GUILDWAR_MODE_IDM:
	{
		const int aWeapons[] = {WEAPON_LASER};
		const int aAmmo[] = {3};
		StripToWeapons(pChar, aWeapons, 1, aAmmo);
		pChar->SetHealthDirect(1);
		pChar->ReduceArmor(10);
		break;
	}
	case GUILDWAR_MODE_CTF:
	case GUILDWAR_MODE_TDM:
	{
		const int aWeapons[] = {WEAPON_HAMMER, WEAPON_GUN, WEAPON_SHOTGUN, WEAPON_GRENADE, WEAPON_LASER};
		const int aAmmo[] = {-1, 10, 10, 10, 10};
		StripToWeapons(pChar, aWeapons, 5, aAmmo);
		pChar->SetHealthDirect(10);
		break;
	}
	default:
		break;
	}
}
