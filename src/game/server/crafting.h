/* (c) TeeDefenceArchive - 2026 */
#ifndef GAME_SERVER_CRAFTING_H
#define GAME_SERVER_CRAFTING_H

class CGameContext;
class CCommandManager;
class IConsole;

void RegisterCraftingChatCommands(CCommandManager *pMgr, CGameContext *pGame);
void RegisterVoteMenuCommands(CCommandManager *pMgr, CGameContext *pGame);
void RegisterCraftingConsoleCommands(IConsole *pConsole, CGameContext *pGame);

/** Returns false on failure. **/
bool TryCraftOneItem(CGameContext *pGame, int ClientID, int Item, char *pErr, int ErrSize);

#endif
