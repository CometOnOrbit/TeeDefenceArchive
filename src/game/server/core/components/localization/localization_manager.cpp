#include <stdarg.h>
#include <stdio.h>

#include <base/system.h>

#include <engine/shared/jsonparser.h>

#include "localization_manager.h"

unsigned CLocalizationLangHash::hash(const char *pKey)
{
	unsigned Hash = 5381;
	for(const char *p = pKey; *p; p++)
	{
		char c = *p;
		if(c >= 'A' && c <= 'Z')
			c = (char)(c + ('a' - 'A'));
		Hash = ((Hash << 5) + Hash) + (unsigned char)c;
	}
	return Hash;
}

void CLocalizationManager::ClearMaps()
{
	m_KeyMap.clear();
	m_LangMap.clear();
	m_NumLangAliases = 0;
}

void CLocalizationManager::IndexKey(int KeyIdx)
{
	if(KeyIdx < 0 || KeyIdx >= m_NumKeys)
		return;
	m_KeyMap.set(m_aaKeys[KeyIdx], KeyIdx);
}

void CLocalizationManager::IndexLang(int LangIdx)
{
	if(LangIdx < 0 || LangIdx >= MAX_LANGS)
		return;
	m_LangMap.set(m_aLangs[LangIdx].m_aFile, LangIdx);
}

void CLocalizationManager::RegisterLangAlias(const char *pAlias, int LangIdx)
{
	if(!pAlias || !pAlias[0] || LangIdx < 0 || m_NumLangAliases >= LANG_ALIAS_COUNT)
		return;
	char *pStored = m_aaLangAliases[m_NumLangAliases++];
	str_copy(pStored, pAlias, LANG_ID_LEN);
	m_LangMap.set(pStored, LangIdx);
}

void CLocalizationManager::RegisterLangAliases()
{
	const int EnIdx = FindLangByFile("en");
	const int ZhIdx = FindLangByFile("zh-cn");
	if(EnIdx >= 0)
		RegisterLangAlias("english", EnIdx);
	if(ZhIdx >= 0)
	{
		RegisterLangAlias("zh", ZhIdx);
		RegisterLangAlias("chinese", ZhIdx);
	}
}

int CLocalizationManager::FindKey(const char *pKey) const
{
	if(!pKey)
		return -1;
	const int *pIdx = m_KeyMap.get(pKey);
	return pIdx ? *pIdx : -1;
}

int CLocalizationManager::FindLangByFile(const char *pFile) const
{
	if(!pFile || !pFile[0])
		return -1;
	const int *pIdx = m_LangMap.get(pFile);
	return pIdx ? *pIdx : -1;
}

int CLocalizationManager::ResolveLangIndex(const char *pLang) const
{
	if(!pLang || !pLang[0])
		return m_DefaultLang;
	const int *pIdx = m_LangMap.get(pLang);
	return pIdx ? *pIdx : m_DefaultLang;
}

int CLocalizationManager::FindOrAddKey(const char *pKey)
{
	int Idx = FindKey(pKey);
	if(Idx >= 0)
		return Idx;
	if(m_NumKeys >= MAX_KEYS)
		return -1;
	Idx = m_NumKeys++;
	str_copy(m_aaKeys[Idx], pKey, KEY_LEN);
	IndexKey(Idx);
	for(int i = 0; i < MAX_LANGS; i++)
		m_aLangs[i].m_aaValues[Idx][0] = 0;
	return Idx;
}

void CLocalizationManager::LoadLangFile(const char *pPath, int LangIndex)
{
	if(!Storage() || LangIndex < 0 || LangIndex >= MAX_LANGS || LangIndex > m_NumLangs)
		return;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile(pPath, Storage());
	if(!pRoot || pRoot->type != json_object)
	{
		dbg_msg("localization", "failed to load '%s': %s", pPath, Parser.Error());
		return;
	}

	for(unsigned i = 0; i < pRoot->u.object.length; i++)
	{
		const json_object_entry &Ent = pRoot->u.object.values[i];
		if(!Ent.name || !Ent.value || Ent.value->type != json_string)
			continue;
		const int KeyIdx = FindOrAddKey(Ent.name);
		if(KeyIdx < 0)
		{
			dbg_msg("localization", "key limit reached while loading '%s' (key '%s')", pPath, Ent.name);
			continue;
		}
		str_copy(m_aLangs[LangIndex].m_aaValues[KeyIdx], Ent.value->u.string.ptr, STR_LEN);
	}
}

bool CLocalizationManager::LoadIndex()
{
	if(!Storage())
		return false;

	CJsonParser Parser;
	json_value *pRoot = Parser.ParseFile("server_lang/index.json", Storage());
	if(!pRoot || pRoot->type != json_object)
		return false;

	const json_value &Arr = pRoot->operator[]("language indices");
	if(Arr.type != json_array)
		return false;

	char aParentFile[MAX_LANGS][LANG_ID_LEN];
	mem_zero(aParentFile, sizeof(aParentFile));

	for(unsigned i = 0; i < Arr.u.array.length && m_NumLangs < MAX_LANGS; i++)
	{
		const json_value &Ent = Arr[(int)i];
		if(Ent.type != json_object || Ent["file"].type != json_string)
			continue;

		SLanguage &L = m_aLangs[m_NumLangs];
		mem_zero(&L, sizeof(L));
		L.m_Parent = -1;
		str_copy(L.m_aFile, Ent["file"].u.string.ptr, sizeof(L.m_aFile));

		if(Ent["parent"].type == json_string)
			str_copy(aParentFile[m_NumLangs], Ent["parent"].u.string.ptr, LANG_ID_LEN);

		char aPath[128];
		str_format(aPath, sizeof(aPath), "server_lang/%s.json", L.m_aFile);
		LoadLangFile(aPath, m_NumLangs);
		IndexLang(m_NumLangs);
		m_NumLangs++;
	}

	for(int i = 0; i < m_NumLangs; i++)
	{
		if(!aParentFile[i][0])
			continue;
		m_aLangs[i].m_Parent = FindLangByFile(aParentFile[i]);
	}

	RegisterLangAliases();
	m_DefaultLang = FindLangByFile("zh-cn");
	if(m_DefaultLang < 0)
		m_DefaultLang = FindLangByFile("en");
	if(m_DefaultLang < 0)
		m_DefaultLang = 0;
	return m_NumLangs > 0;
}

void CLocalizationManager::OnInitWorld(const char *pWhereLocalWorld)
{
	(void)pWhereLocalWorld;
	m_NumKeys = 0;
	m_NumLangs = 0;
	m_DefaultLang = 0;
	ClearMaps();
	if(!LoadIndex())
	{
		if(m_NumLangs < MAX_LANGS)
		{
			mem_zero(&m_aLangs[m_NumLangs], sizeof(m_aLangs[m_NumLangs]));
			str_copy(m_aLangs[m_NumLangs].m_aFile, "en", sizeof(m_aLangs[m_NumLangs].m_aFile));
			m_aLangs[m_NumLangs].m_Parent = -1;
			LoadLangFile("server_lang/en.json", m_NumLangs);
			IndexLang(m_NumLangs);
			m_NumLangs++;
		}
		if(m_NumLangs < MAX_LANGS)
		{
			mem_zero(&m_aLangs[m_NumLangs], sizeof(m_aLangs[m_NumLangs]));
			str_copy(m_aLangs[m_NumLangs].m_aFile, "zh-cn", sizeof(m_aLangs[m_NumLangs].m_aFile));
			m_aLangs[m_NumLangs].m_Parent = FindLangByFile("en");
			LoadLangFile("server_lang/zh-cn.json", m_NumLangs);
			IndexLang(m_NumLangs);
			m_NumLangs++;
		}
		RegisterLangAliases();
		m_DefaultLang = FindLangByFile("zh-cn");
		if(m_DefaultLang < 0)
			m_DefaultLang = FindLangByFile("en");
		if(m_DefaultLang < 0)
			m_DefaultLang = 0;
	}

	dbg_msg("localization", "loaded %d languages, %d keys (default: %s)", m_NumLangs, m_NumKeys, DefaultLang());
}

const char *CLocalizationManager::DefaultLang() const
{
	if(m_DefaultLang >= 0 && m_DefaultLang < m_NumLangs)
		return m_aLangs[m_DefaultLang].m_aFile;
	return "en";
}

const char *CLocalizationManager::Get(const char *pLang, const char *pKey, const char *pDefault) const
{
	const int KeyIdx = FindKey(pKey);
	if(KeyIdx < 0)
		return pDefault ? pDefault : pKey;

	int LangIdx = ResolveLangIndex(pLang);
	for(int Depth = 0; Depth < MAX_LANGS && LangIdx >= 0; Depth++)
	{
		if(m_aLangs[LangIdx].m_aaValues[KeyIdx][0])
			return m_aLangs[LangIdx].m_aaValues[KeyIdx];
		LangIdx = m_aLangs[LangIdx].m_Parent;
	}
	return pDefault ? pDefault : pKey;
}

void CLocalizationManager::Format(char *pBuf, int BufSize, const char *pLang, const char *pKey, const char *pDefault, ...) const
{
	const char *pFmt = Get(pLang, pKey, pDefault);
	va_list ap;
	va_start(ap, pDefault);
#if defined(CONF_FAMILY_WINDOWS)
	vsnprintf(pBuf, BufSize, pFmt, ap);
#else
	vsnprintf(pBuf, BufSize, pFmt, ap);
#endif
	va_end(ap);
	pBuf[BufSize - 1] = 0;
}
