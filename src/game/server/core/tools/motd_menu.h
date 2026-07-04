#ifndef GAME_SERVER_CORE_TOOLS_MOTD_MENU_H
#define GAME_SERVER_CORE_TOOLS_MOTD_MENU_H

#include <engine/input_events.h>
#include <base/math.h>

#include <optional>
#include <memory>
#include <map>
#include <string>
#include <vector>

// ── Constants ────────────────────────────────────────────────
constexpr int MOTD_MENU_TEXT_LIMIT = 24;
constexpr int MOTD_MENU_TEXT_ELLIPSIS = 2;

enum
{
	NOPE = -2
};

// ── Flags ────────────────────────────────────────────────────
enum MotdMenuFlags
{
	MTFLAG_CLOSE_BUTTON      = 1 << 0,
	MTFLAG_CLOSE_ON_SELECT   = 1 << 1,
	MTTEXTINPUTFLAG_ONLY_NUMERIC = 1 << 0,
	MTTEXTINPUTFLAG_PASSWORD = 1 << 1,
};

// ── Forward declarations ─────────────────────────────────────
class CGS;
class CPlayer;
class CGameContext;

// ── A single line/option in the MotdMenu ─────────────────────
class MotdOption
{
public:
	std::string m_Command { "NULL" };
	char m_aDesc[32] {};
	std::string m_FullDesc {};
	int m_FullDescLength {};
	int m_ScrollOffset {};
	int m_NextScrollTick { -1 };
	int m_MenuID { NOPE };
	int m_MenuExtra { NOPE };
};

// ── The MRPG-style MOTD Menu ─────────────────────────────────
class MotdMenu
{
	CGameContext *m_pGS;
	int m_ClientID;
	int m_Flags {};
	std::vector<MotdOption> m_Points {};
	std::optional<int> m_MenuExtra {};
	struct MenuState { int Menulist { NOPE }; std::optional<int> MenuExtra {}; };
	std::vector<MenuState> m_MenuHistory {};
	int m_LastMenulist { NOPE };
	int m_Menulist { NOPE };
	int m_ResendMotdTick {};
	std::string m_LastBuffer {};
	std::string m_Description {};

	CGameContext *GS() const;
	CPlayer *GetPlayer() const;

	MotdOption &AddImpl(const char *pCommand, const char *pDescription);
	void UpdateMotd();
	void ApplyScrollbar(CPlayer *pPlayer, int Index, std::string &rBuffer);

public:
	MotdMenu(CGameContext *pGS, int ClientID, const char *pDescription = "")
		: m_pGS(pGS), m_ClientID(ClientID), m_Description(pDescription) {}
	MotdMenu(CGameContext *pGS, int ClientID, int Flags, const char *pDescription = "")
		: m_pGS(pGS), m_ClientID(ClientID), m_Flags(Flags), m_Description(pDescription) {}

	// Building
	void AddText(const char *pText);
	MotdOption &AddOption(const char *pCommand, const char *pDescription);
	void AddMenu(int MenuID, int Extra, const char *pDescription);
	void AddField(int TextID, int64_t Flags = 0);
	void AddLine();
	void AddSeparateLine();
	void AddBackpage();

	// Dialog use: rebuild without destroying the MotdMenu
	void ClearOptions();
	void SetDescription(const char *pDesc);

	// Lifecycle
	void Tick();
	void Send(int Menulist);
	void ClearMotd();

	// Field editing
	bool ApplyFieldEdit(const std::string &Message);

	// Queries
	int GetLastMenulist() const { return m_LastMenulist; }
	int GetMenulist() const { return m_Menulist; }
	int GetNumOptions() const { return (int)m_Points.size(); }
	std::optional<int> GetMenuExtra() const { return m_MenuExtra; }
	const char *HandleSelect(int Index, int &OutMenuID);
};

// ── Per-player MOTD data (lives in CPlayer) ──────────────────
class CMotdPlayerData
{
public:
	friend class MotdMenu;

	class ScrollManager
	{
		int m_ScrollPos {};
		int m_MaxScrollPos {};
		int m_MaxItemsVisible {};

	public:
		explicit ScrollManager(int VisibleLines) : m_MaxItemsVisible(VisibleLines) {}
		void SetMaxScrollPos(int ItemCount) { m_MaxScrollPos = maximum(0, ItemCount - m_MaxItemsVisible); }
		int GetScrollPos() const { return m_ScrollPos; }
		int GetEndScrollPos() const { return minimum(m_ScrollPos + m_MaxItemsVisible, m_MaxScrollPos + m_MaxItemsVisible); }
		int GetMaxVisibleItems() const { return m_MaxItemsVisible; }
		void ScrollUp() { m_ScrollPos = minimum(m_ScrollPos + 1, m_MaxScrollPos); }
		void ScrollDown() { m_ScrollPos = maximum(m_ScrollPos - 1, 0); }
		bool CanScrollUp() const { return m_ScrollPos > 0; }
		bool CanScrollDown() const { return m_ScrollPos < m_MaxScrollPos; }
		void Reset() { m_ScrollPos = m_MaxScrollPos = 0; }
	};

	struct ActiveInputTextField { bool Active {}; int TextID {}; };
	struct TextField { std::string Message {}; int64_t Flags {}; };

	std::optional<std::string> GetFieldStr(size_t index) const
	{
		auto it = m_vFields.find(index);
		if(it == m_vFields.end() || it->second.Message.empty()) return std::nullopt;
		return it->second.Message;
	}
	MotdOption *GetCurrent() { return m_pCurrent; }

private:
	MotdOption *m_pCurrent {};
	ActiveInputTextField m_CurrentInputField {};
	int m_HoveredItemIndex { NOPE };
	std::map<int, TextField> m_vFields {};
	ScrollManager m_ScrollManager { 13 };
};

#endif // GAME_SERVER_CORE_TOOLS_MOTD_MENU_H
