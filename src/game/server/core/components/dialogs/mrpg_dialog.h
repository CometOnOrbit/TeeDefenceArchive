#ifndef GAME_SERVER_CORE_COMPONENTS_DIALOGS_MRPG_DIALOG_H
#define GAME_SERVER_CORE_COMPONENTS_DIALOGS_MRPG_DIALOG_H

#include <game/server/core/tworld_component.h>
#include <engine/shared/jsonparser.h>
#include <base/tl/array.h>

// ── MRPG-style DIALOGFLAG ──
enum
{
	DIALOGFLAG_LEFT_BOT        = 1 << 0,
	DIALOGFLAG_RIGHT_BOT       = 1 << 1,
	DIALOGFLAG_LEFT_PLAYER     = 1 << 2,
	DIALOGFLAG_RIGHT_PLAYER    = 1 << 3,
	DIALOGFLAG_SPEAK_THOUGHTS  = 1 << 4,
	DIALOGFLAG_SPEAK_AUTHOR    = 1 << 5,
};

class CPlayer;

// ── MRPG-style dialog step (speaker model + text) ────────────
struct CDialogStepMrpg
{
	char m_Text[512] = {};
	bool m_Request = false;
	int64_t m_Flags = 0;
	int m_LeftSideID = 0;  // -1=player, >=0=bot ID
	int m_RightSideID = 0;

	void Init(int NpcBotID, const char *pNpcName, const json_value &JsonDialog);
};

// ── CPlayerDialog (state machine for in-progress dialog) ─────
class CPlayerDialog
{
	CGameContext *m_pGS = nullptr;
	CPlayer *m_pPlayer = nullptr;

	int m_BotCID = -1;
	int m_Step = -1;
	char m_aNpcName[64] = {};
	char m_aFormattedText[1024] = {};
	array<CDialogStepMrpg> m_vSteps;

public:
	CPlayerDialog() = default;

	void Init(CGameContext *pGS, CPlayer *pPlayer);
	void Start(int BotCID, const char *pNpcId, const char *pNpcName, const array<CDialogStepMrpg> &Steps);
	void Next();
	void End();
	void Tick();

	bool IsActive() const { return m_Step >= 0; }
	const char *GetFormattedText() const { return m_aFormattedText; }
	int GetBotCID() const { return m_BotCID; }
	int GetCurrentStep() const { return m_Step; }
	int GetStepCount() const { return m_vSteps.size(); }
	const CDialogStepMrpg *GetCurrent() const;

	// Placeholder replacement (called from dialog_manager)
	void ReplacePlaceholders(char *pBuf, int BufSize) const;

private:
	void ShowCurrentDialog();
	void FormatDialog(const CDialogStepMrpg *pDialog);
	void Clear();
};

#endif
