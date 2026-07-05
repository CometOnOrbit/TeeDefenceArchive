#ifndef GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_ARENA_MAPS_H
#define GAME_SERVER_CORE_COMPONENTS_GUILDS_GUILD_ARENA_MAPS_H

#include <string>
#include <vector>

class IStorage;

void ListArenaMapsForMode(IStorage *pStorage, const char *pMode, std::vector<std::string> &Out);
bool IsArenaMapAllowedForMode(const char *pRelPath, const char *pMode);
bool ArenaMapFileExists(IStorage *pStorage, const char *pRelPath);
void GuildWarDefaultArenaMap(IStorage *pStorage, const char *pMode, char *pBuf, int BufSize);

#endif
