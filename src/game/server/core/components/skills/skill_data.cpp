#include "skill_data.h"

const char *SkillEmoticonName(int EmoticonId)
{
	if(EmoticonId < 0)
		return "none";
	static const char *const s_apNames[NUM_SKILL_EMOTICONS] = {
		"OOP", "!!!", "❤", "Drop", "...", "Music", 
		"Sorry", "(0.0)", "Angry", ">:(", "#$$>:(", "#!!*#*!", 
		"Zzz", "WTF", ":D", "???",
	};
	if(EmoticonId >= 0 && EmoticonId < NUM_SKILL_EMOTICONS)
		return s_apNames[EmoticonId];
	return "?";
}
