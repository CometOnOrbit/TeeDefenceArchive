#ifndef GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_MATCH_MODE_H
#define GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_MATCH_MODE_H

class CPlayer;

enum EGuildWarMode
{
	GUILDWAR_MODE_INVALID = -1,
	GUILDWAR_MODE_FNG = 0,
	GUILDWAR_MODE_CTF,
	GUILDWAR_MODE_TDM,
	GUILDWAR_MODE_ITDM,
	GUILDWAR_MODE_IDM, // legacy alias, treated as team instagib in guild wars
	NUM_GUILDWAR_MODES
};

struct SGuildWarModeDef
{
	const char *m_pId;
	const char *m_pDisplayName;
	const char *m_pDescription;
	const char *m_pArenaMap;
	int m_DefaultTargetScore;
	bool m_TeamBased;
	bool m_Instagib;
};

EGuildWarMode ParseGuildWarMode(const char *pMode);
const char *GuildWarModeToString(EGuildWarMode Mode);
const SGuildWarModeDef *GetGuildWarModeDef(EGuildWarMode Mode);
const char *GuildWarModeDisplayName(const char *pMode);
bool IsValidGuildWarMode(const char *pMode);
bool IsInstagibGuildWarMode(const char *pMode);
const char *GuildWarArenaMapForMode(const char *pMode);
int GuildWarDefaultTargetScore(const char *pMode);
void ApplyGuildWarModeRules(CPlayer *pPlayer, const char *pMode);

#endif
