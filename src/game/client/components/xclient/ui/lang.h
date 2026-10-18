#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_LANG_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_LANG_H

namespace XcUi
{
	// Russian translation of a menu string when the game language is Russian, otherwise the English text.
	const char *Tr(const char *pEnglish);
	bool IsRussian();
} // namespace XcUi

#endif
