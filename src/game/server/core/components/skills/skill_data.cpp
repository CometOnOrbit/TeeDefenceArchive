#include "skill_data.h"

const char *SkillEmoticonName(int EmoticonId)
{
	if(EmoticonId < 0)
		return "none";
	static const char *const s_apNames[NUM_SKILL_EMOTICONS] = {
		"O.O", ":-)", ":-(", ":-D", ":-P", ":-O", ":-|", ":-*",
		"8-)", ">:-)", ":-$", ":-@", ":-#", ":-%", ":-&", ":-!",
	};
	if(EmoticonId >= 0 && EmoticonId < NUM_SKILL_EMOTICONS)
		return s_apNames[EmoticonId];
	return "?";
}
