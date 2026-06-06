#ifndef GAME_SERVER_COMPONENT_LOCALIZATION_MANAGER_H
#define GAME_SERVER_COMPONENT_LOCALIZATION_MANAGER_H

#include <base/tl/hashtable.h>

#include <game/server/core/tworld_component.h>

class CLocalizationKeyHash
{
public:
	static unsigned hash(const char *pKey) { return str_quickhash(pKey); }
	static bool equal(const char *pKey1, const char *pKey2) { return str_comp(pKey1, pKey2) == 0; }
};

class CLocalizationLangHash
{
public:
	static unsigned hash(const char *pKey);
	static bool equal(const char *pKey1, const char *pKey2) { return str_comp_nocase(pKey1, pKey2) == 0; }
};

class CLocalizationManager : public TWorldComponent
{
	static constexpr int MAX_LANGS = 32;
	static constexpr int MAX_KEYS = 512;
	static constexpr int KEY_LEN = 64;
	static constexpr int STR_LEN = 512;
	static constexpr int LANG_ID_LEN = 16;
	static constexpr int KEY_MAP_SIZE = 1024;
	static constexpr int LANG_MAP_SIZE = 128;
	static constexpr int LANG_ALIAS_COUNT = 8;

	struct SLanguage
	{
		char m_aFile[LANG_ID_LEN];
		int m_Parent;
		char m_aaValues[MAX_KEYS][STR_LEN];
	};

	char m_aaKeys[MAX_KEYS][KEY_LEN];
	int m_NumKeys = 0;
	SLanguage m_aLangs[MAX_LANGS];
	int m_NumLangs = 0;
	int m_DefaultLang = 0;
	char m_aaLangAliases[LANG_ALIAS_COUNT][LANG_ID_LEN];
	int m_NumLangAliases = 0;
	hash_table<const char *, int, KEY_MAP_SIZE, CLocalizationKeyHash> m_KeyMap;
	hash_table<const char *, int, LANG_MAP_SIZE, CLocalizationLangHash> m_LangMap;

	void ClearMaps();
	void IndexKey(int KeyIdx);
	void IndexLang(int LangIdx);
	void RegisterLangAlias(const char *pAlias, int LangIdx);
	void RegisterLangAliases();
	int FindKey(const char *pKey) const;
	int FindLangByFile(const char *pFile) const;
	int ResolveLangIndex(const char *pLang) const;
	int FindOrAddKey(const char *pKey);
	void LoadLangFile(const char *pPath, int LangIndex);
	bool LoadIndex();

protected:
	void OnInitWorld(const char *pWhereLocalWorld) override;

public:
	const char *Get(const char *pLang, const char *pKey, const char *pDefault) const;
	void Format(char *pBuf, int BufSize, const char *pLang, const char *pKey, const char *pDefault, ...) const;
	const char *DefaultLang() const;
};

#endif
