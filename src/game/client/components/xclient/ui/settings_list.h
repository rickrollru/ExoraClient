// A full list of config variables in the style of the menu (Settings > TClient, Settings > DDNet): the
// variables come from a config_variables*.h file through the macros, so new ones show up on their own.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_SETTINGS_LIST_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_SETTINGS_LIST_H

namespace XcSettingsList
{
	enum
	{
		TYPE_INT = 0,
		TYPE_COL,
		TYPE_STR,
	};

	struct SSetting
	{
		const char *m_pName;
		int m_Type;
		void *m_pValue;
		int m_Min;
		int m_Max;
		unsigned m_Default;
		const char *m_pDefaultStr;
		int m_Flags;
		int m_Length;
		const char *m_pDesc;
	};

	// a short label; Options are "|" separated names for every value from Min to Max (segmented control)
	struct SLabel
	{
		const char *m_pName;
		const char *m_pRu;
		const char *m_pOptionsEn;
		const char *m_pOptionsRu;
	};

	struct SGroup
	{
		const char *m_pTitle; // English, translated with Tr()
		const char *m_pDesc;
	};

	// the first prefix that fits decides the group
	struct SRule
	{
		const char *m_pPrefix;
		int m_Group;
	};

	struct SList
	{
		const char *m_pId;
		const char *m_pSearchHint;
		const SSetting *m_pSettings;
		int m_NumSettings;
		const SGroup *m_pGroups;
		int m_NumGroups;
		int m_OtherGroup; // where a variable without a rule goes
		const SRule *m_pRules;
		int m_NumRules;
		const char *const *m_ppHidden; // prefixes that are not shown
		int m_NumHidden;
		const SLabel *m_pLabels;
		int m_NumLabels;
	};
} // namespace XcSettingsList


#endif
