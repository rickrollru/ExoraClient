// Settings > TClient: every TClient setting in the style of the menu. The list is built from
// config_variables_tclient.h, so new TClient settings show up on their own (in "Other").
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/color.h>
#include <base/str.h>

#include <engine/external/imgui/imgui.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <vector>

using namespace XcUi;

namespace
{
	enum ESettingType
	{
		TYPE_INT = 0,
		TYPE_COL,
		TYPE_STR,
	};

	struct STcSetting
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

	// clang-format off
const STcSetting gs_aTcSettings[] = {
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) {#ScriptName, TYPE_INT, &g_Config.m_##Name, Min, Max, (unsigned)(Def), nullptr, Flags, 0, Desc},
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) {#ScriptName, TYPE_COL, &g_Config.m_##Name, 0, 0, (unsigned)(Def), nullptr, Flags, 0, Desc},
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) {#ScriptName, TYPE_STR, g_Config.m_##Name, 0, 0, 0, Def, Flags, Len, Desc},
#include <engine/shared/config_variables_tclient.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
};
	// clang-format on

	enum EGroup
	{
		GROUP_GENERAL = 0,
		GROUP_VISUAL,
		GROUP_FROZEN,
		GROUP_NAMEPLATES,
		GROUP_OUTLINES,
		GROUP_INDICATOR,
		GROUP_RAINBOW,
		GROUP_TRAILS,
		GROUP_HUD,
		GROUP_CHAT,
		GROUP_WARLIST,
		GROUP_PROFILES,
		GROUP_PETS,
		GROUP_BGDRAW,
		GROUP_PREDICTION,
		GROUP_REVERT,
		GROUP_OTHER,
		NUM_GROUPS,
	};

	struct SGroupInfo
	{
		const char *m_pTitle;
		const char *m_pDesc;
	};

	const SGroupInfo gs_aGroups[NUM_GROUPS] = {
		{"General", "Mouse, wheels, resolution and the rest"},
		{"Visual", "How tees, weapons and the cursor look"},
		{"Frozen tees", "Frozen tees counter and colors"},
		{"Name plates", "What is shown above the tees"},
		{"Tile outlines", "Lines around freeze, hook, kill and teleporters"},
		{"Player indicator", "Circles around you that point to the other tees"},
		{"Rainbow", "Rainbow tees, hook and weapons"},
		{"TClient trails", "The trail from TClient (the XClient trail is in Visuals)"},
		{"HUD and status bar", "Status bar, last notice and dummy info"},
		{"Chat", "Filter, auto replies and translation"},
		{"War list", "Colors for enemies and friends"},
		{"Profiles", "What a profile changes when applied"},
		{"Pets", "A small tee that follows you"},
		{"Background drawing", "Drawing on the map background"},
		{"Prediction and antiping", "Ghosts, fast input and antiping tuning"},
		{"Revert DDNet changes", "Bring old DDNet behavior back"},
		{"Other", "Votes, commands on join and more"},
	};

	struct SRule
	{
		const char *m_pPrefix;
		int m_Group;
	};

	// the first prefix that fits decides the group
	const SRule gs_aRules[] = {
		{"tc_frozen_tees", GROUP_FROZEN},
		{"tc_color_freeze", GROUP_FROZEN},
		{"tc_nameplate", GROUP_NAMEPLATES},
		{"tc_fake_ctf", GROUP_NAMEPLATES},
		{"tc_remove_anti", GROUP_PREDICTION},
		{"tc_unpred", GROUP_PREDICTION},
		{"tc_pred", GROUP_PREDICTION},
		{"tc_show_others_ghosts", GROUP_PREDICTION},
		{"tc_swap_ghosts", GROUP_PREDICTION},
		{"tc_hide_frozen_ghosts", GROUP_PREDICTION},
		{"tc_render_ghost", GROUP_PREDICTION},
		{"tc_fast_input", GROUP_PREDICTION},
		{"tc_antiping", GROUP_PREDICTION},
		{"tc_prediction_margin_smooth", GROUP_REVERT},
		{"tc_frozen_katana", GROUP_REVERT},
		{"tc_old_team_colors", GROUP_REVERT},
		{"tc_revert", GROUP_REVERT},
		{"tc_outline", GROUP_OUTLINES},
		{"tc_warlist_indicator", GROUP_INDICATOR},
		{"tc_indicator_tees_size", GROUP_VISUAL}, // the tiny tee size, despite the name
		{"tc_indicator", GROUP_INDICATOR},
		{"tc_player_indicator", GROUP_INDICATOR},
		{"tc_warlist", GROUP_WARLIST},
		{"tc_rainbow", GROUP_RAINBOW},
		{"tc_tee_trail", GROUP_TRAILS},
		{"tc_statusbar", GROUP_HUD},
		{"tc_last_notify", GROUP_HUD},
		{"tc_showhud", GROUP_HUD},
		{"tc_mini_debug", GROUP_HUD},
		{"tc_mini_vote_hud", GROUP_HUD},
		{"tc_regex_chat", GROUP_CHAT},
		{"tc_auto_reply", GROUP_CHAT},
		{"tc_translate", GROUP_CHAT},
		{"tc_hide_chat_bubbles", GROUP_CHAT},
		{"tc_show_chat_client", GROUP_CHAT},
		{"tc_profile", GROUP_PROFILES},
		{"tc_pet", GROUP_PETS},
		{"tc_bg_draw", GROUP_BGDRAW},
		{"tc_white_feet", GROUP_VISUAL},
		{"tc_render_weapons", GROUP_VISUAL},
		{"tc_moving_tiles", GROUP_VISUAL},
		{"tc_cursor_in_spec", GROUP_VISUAL},
		{"tc_tiny_tees", GROUP_VISUAL},
		{"tc_show_center", GROUP_VISUAL},
		{"tc_show_player_hit", GROUP_VISUAL},
		{"tc_volleyball", GROUP_VISUAL},
		{"tc_hammer_rotates", GROUP_VISUAL},
		{"tc_cursor_scale", GROUP_VISUAL},
		{"tc_auto_vote", GROUP_OTHER},
		{"tc_change_name", GROUP_OTHER},
		{"tc_finish_name", GROUP_OTHER},
		{"tc_execute", GROUP_OTHER},
		{"tc_custom_communities", GROUP_OTHER},
		{"tc_allow_any_res", GROUP_GENERAL},
		{"tc_limit_mouse", GROUP_GENERAL},
		{"tc_scale_mouse", GROUP_GENERAL},
		{"tc_reset_bindwheel", GROUP_GENERAL},
		{"tc_animate_wheel", GROUP_GENERAL},
		{"tc_discord", GROUP_GENERAL},
		{"tc_show_local_time", GROUP_GENERAL},
		{"tc_custom_font", GROUP_GENERAL},
	};

	// settings of the classic TClient menu itself or for moderators: not shown here
	const char *const gs_apHidden[] = {"tc_tclient_settings_tabs", "tc_ui_", "tc_mod_weapon"};

	// short labels; Options are "|" separated names for every value from Min to Max (segmented control)
	struct SLabel
	{
		const char *m_pName;
		const char *m_pRu;
		const char *m_pOptionsEn;
		const char *m_pOptionsRu;
	};

	const SLabel gs_aLabels[] = {
		{"tc_allow_any_res", "Любое разрешение при зуме (на Windows с багами)", nullptr, nullptr},
		{"tc_show_chat_client", "Сообщения клиента в чате (echo и т. п.)", nullptr, nullptr},
		{"tc_frozen_tees_text", "Текст о замороженных в команде", "Off|Alive|Frozen", "Выкл|Живые|Замороженные"},
		{"tc_frozen_tees_hud", "Панель замороженных ти", nullptr, nullptr},
		{"tc_frozen_tees_hud_skins", "Скин ниндзя / затемнение на панели", nullptr, nullptr},
		{"tc_frozen_tees_size", "Размер ти на панели", nullptr, nullptr},
		{"tc_frozen_tees_max_rows", "Максимум строк на панели", nullptr, nullptr},
		{"tc_frozen_tees_only_inteam", "Панель только в команде", nullptr, nullptr},
		{"tc_nameplate_ping_circle", "Кружок пинга у ника", nullptr, nullptr},
		{"tc_nameplate_country", "Флаг страны у ника", nullptr, nullptr},
		{"tc_nameplate_skins", "Название скина у ника", nullptr, nullptr},
		{"tc_fake_ctf_flags", "Ненастоящие CTF-флаги на игроках", "Off|Red|Blue", "Выкл|Красный|Синий"},
		{"tc_limit_mouse_to_screen", "Не выпускать мышь за экран", "Off|Screen|Square", "Выкл|Экран|Квадрат"},
		{"tc_scale_mouse_distance", "Точнее мышь (дистанция до 1000)", nullptr, nullptr},
		{"tc_hammer_rotates_with_cursor", "Молоток поворачивается за курсором", "Off|On|Always", "Выкл|Вкл|Всегда"},
		{"tc_mini_vote_hud", "Маленькое окно голосования", nullptr, nullptr},
		{"tc_remove_anti", "Убирать антипинг во фризе", nullptr, nullptr},
		{"tc_remove_anti_ticks", "Сколько тиков предсказания убирать", nullptr, nullptr},
		{"tc_remove_anti_delay_ticks", "За сколько тиков убирать после фриза", nullptr, nullptr},
		{"tc_unpred_others_in_freeze", "Не предсказывать других, пока ты во фризе", nullptr, nullptr},
		{"tc_pred_margin_in_freeze", "Другой запас предсказания во фризе", nullptr, nullptr},
		{"tc_pred_margin_in_freeze_amount", "Запас предсказания во фризе", nullptr, nullptr},
		{"tc_show_others_ghosts", "Призраки других игроков (без предсказания)", nullptr, nullptr},
		{"tc_swap_ghosts", "Поменять местами призраков и игроков", nullptr, nullptr},
		{"tc_hide_frozen_ghosts", "Прятать призраков замороженных", nullptr, nullptr},
		{"tc_pred_ghosts_alpha", "Прозрачность предсказанных призраков", nullptr, nullptr},
		{"tc_unpred_ghosts_alpha", "Прозрачность непредсказанных призраков", nullptr, nullptr},
		{"tc_render_ghost_as_circle", "Призраки кружками", nullptr, nullptr},
		{"tc_show_center", "Линии центра экрана", nullptr, nullptr},
		{"tc_show_center_width", "Толщина линий центра", nullptr, nullptr},
		{"tc_show_center_color", "Цвет линий центра", nullptr, nullptr},
		{"tc_fast_input", "Быстрый ввод (предсказание до тика)", nullptr, nullptr},
		{"tc_fast_input_amount", "Быстрый ввод, мс", nullptr, nullptr},
		{"tc_fast_input_others", "Быстрый ввод и для других", nullptr, nullptr},
		{"tc_fast_input_repredict_hook", "Перепредсказывать хук при смене направления", nullptr, nullptr},
		{"tc_antiping_improved", "Улучшенное сглаживание антипинга", nullptr, nullptr},
		{"tc_antiping_negative_buffer", "Осторожный антипинг (для гореса)", nullptr, nullptr},
		{"tc_antiping_stable_direction", "Смелее по устойчивой оси", nullptr, nullptr},
		{"tc_antiping_uncertainty_scale", "Длительность неуверенности (от пинга)", nullptr, nullptr},
		{"tc_color_freeze", "Цвет скина у замороженных", nullptr, nullptr},
		{"tc_color_freeze_darken", "Затемнение замороженных", nullptr, nullptr},
		{"tc_color_freeze_feet", "И ноги тоже", nullptr, nullptr},
		{"tc_prediction_margin_smooth", "Плавный запас предсказания", nullptr, nullptr},
		{"tc_frozen_katana", "Катана у замороженных", nullptr, nullptr},
		{"tc_old_team_colors", "Старые радужные цвета команд", nullptr, nullptr},
		{"tc_revert_hook_line", "Старый кончик линии хука", nullptr, nullptr},
		{"tc_outline", "Обводка тайлов", nullptr, nullptr},
		{"tc_outline_in_entities", "Только в режиме entities", nullptr, nullptr},
		{"tc_outline_solid", "Вокруг хукаемых и нехукаемых", nullptr, nullptr},
		{"tc_outline_freeze", "Вокруг фриза и дипа", nullptr, nullptr},
		{"tc_outline_unfreeze", "Вокруг анфриза и андипа", nullptr, nullptr},
		{"tc_outline_kill", "Вокруг килл-тайлов", nullptr, nullptr},
		{"tc_outline_tele", "Вокруг телепортов", nullptr, nullptr},
		{"tc_outline_width_solid", "Толщина: стены", nullptr, nullptr},
		{"tc_outline_width_freeze", "Толщина: фриз", nullptr, nullptr},
		{"tc_outline_width_unfreeze", "Толщина: анфриз", nullptr, nullptr},
		{"tc_outline_width_kill", "Толщина: килл", nullptr, nullptr},
		{"tc_outline_width_tele", "Толщина: телепорты", nullptr, nullptr},
		{"tc_outline_color_solid", "Цвет: стены", nullptr, nullptr},
		{"tc_outline_color_freeze", "Цвет: фриз", nullptr, nullptr},
		{"tc_outline_color_unfreeze", "Цвет: анфриз", nullptr, nullptr},
		{"tc_outline_color_kill", "Цвет: килл", nullptr, nullptr},
		{"tc_outline_color_tele", "Цвет: телепорты", nullptr, nullptr},
		{"tc_indicator_alive", "Цвет живых", nullptr, nullptr},
		{"tc_indicator_freeze", "Цвет замороженных", nullptr, nullptr},
		{"tc_indicator_dead", "Цвет тех, кого спасают", nullptr, nullptr},
		{"tc_indicator_offset", "Расстояние от ти", nullptr, nullptr},
		{"tc_indicator_offset_max", "Максимальное расстояние", nullptr, nullptr},
		{"tc_indicator_variable_distance", "Дальше ти — дальше кружок", nullptr, nullptr},
		{"tc_indicator_variable_max_distance", "Дальность для переменного расстояния", nullptr, nullptr},
		{"tc_indicator_radius", "Размер кружков", nullptr, nullptr},
		{"tc_indicator_opacity", "Непрозрачность кружков", nullptr, nullptr},
		{"tc_player_indicator", "Индикатор игроков", nullptr, nullptr},
		{"tc_player_indicator_freeze", "Только замороженные", nullptr, nullptr},
		{"tc_indicator_inteam", "Только в команде", nullptr, nullptr},
		{"tc_indicator_tees", "Ти вместо кружков", nullptr, nullptr},
		{"tc_indicator_hide_visible_tees", "Не показывать тех, кто на экране", nullptr, nullptr},
		{"tc_reset_bindwheel_mouse", "Сбрасывать мышь при открытии колеса биндов", nullptr, nullptr},
		{"tc_regex_chat_ignore", "Скрывать сообщения по regex", nullptr, nullptr},
		{"tc_white_feet", "Белые ноги у всех", nullptr, nullptr},
		{"tc_white_feet_skin", "Скин для белых ног", nullptr, nullptr},
		{"tc_render_weapons_as_gun", "Всё оружие как пистолет", "Off|With hue|Without hue", "Выкл|С оттенком|Без оттенка"},
		{"tc_moving_tiles_entities", "Движущиеся тайлы в entities", nullptr, nullptr},
		{"tc_mini_debug", "Позиция и угол на экране", nullptr, nullptr},
		{"tc_last_notify", "Сообщать, что ты последний", nullptr, nullptr},
		{"tc_last_notify_text", "Текст «последний»", nullptr, nullptr},
		{"tc_last_notify_color", "Цвет текста «последний»", nullptr, nullptr},
		{"tc_last_notify_x", "Положение по горизонтали, %", nullptr, nullptr},
		{"tc_last_notify_y", "Положение по вертикали, %", nullptr, nullptr},
		{"tc_last_notify_size", "Размер текста", nullptr, nullptr},
		{"tc_cursor_in_spec", "Курсор в свободном наблюдении", nullptr, nullptr},
		{"tc_cursor_in_spec_alpha", "Прозрачность курсора в наблюдении", nullptr, nullptr},
		{"tc_tiny_tees", "Маленькие ти", nullptr, nullptr},
		{"tc_indicator_tees_size", "Размер маленьких ти", nullptr, nullptr},
		{"tc_tiny_tees_others", "Маленькие и другие ти", nullptr, nullptr},
		{"tc_cursor_scale", "Размер курсора в игре, %", nullptr, nullptr},
		{"tc_profile_skin", "Скин", nullptr, nullptr},
		{"tc_profile_name", "Ник", nullptr, nullptr},
		{"tc_profile_clan", "Клан", nullptr, nullptr},
		{"tc_profile_flag", "Флаг", nullptr, nullptr},
		{"tc_profile_colors", "Цвета", nullptr, nullptr},
		{"tc_profile_emote", "Эмоция", nullptr, nullptr},
		{"tc_profile_overwrite_clan_with_empty", "Пустой клан тоже применять", nullptr, nullptr},
		{"tc_rainbow_tees", "Радужный ти", nullptr, nullptr},
		{"tc_rainbow_hook", "Радужный хук", nullptr, nullptr},
		{"tc_rainbow_weapon", "Радужное оружие", nullptr, nullptr},
		{"tc_rainbow_others", "Радуга и у других", nullptr, nullptr},
		{"tc_rainbow_mode", "Режим", "Rainbow|Pulse|Darkness|Random", "Радуга|Пульс|Тьма|Случайно"},
		{"tc_rainbow_speed", "Скорость, %", nullptr, nullptr},
		{"tc_warlist", "Цвета вар-листа", nullptr, nullptr},
		{"tc_warlist_show_clan_if_war", "Клан у ника, если война", nullptr, nullptr},
		{"tc_warlist_reason", "Показывать причину", nullptr, nullptr},
		{"tc_warlist_chat", "Цвета в чате", nullptr, nullptr},
		{"tc_warlist_scoreboard", "Цвета в таблице игроков", nullptr, nullptr},
		{"tc_warlist_allow_duplicates", "Разрешить повторы", nullptr, nullptr},
		{"tc_warlist_spectate", "Цвета в меню наблюдения", nullptr, nullptr},
		{"tc_warlist_indicator", "Вар-лист в индикаторе", nullptr, nullptr},
		{"tc_warlist_indicator_colors", "Цвета вар-листа вместо цветов фриза", nullptr, nullptr},
		{"tc_warlist_indicator_all", "Все группы", nullptr, nullptr},
		{"tc_warlist_indicator_enemy", "Первая группа (враги)", nullptr, nullptr},
		{"tc_warlist_indicator_team", "Вторая группа (свои)", nullptr, nullptr},
		{"tc_statusbar", "Строка состояния", nullptr, nullptr},
		{"tc_statusbar_12_hour_clock", "12-часовые часы", nullptr, nullptr},
		{"tc_statusbar_local_time_seconds", "Секунды в часах", nullptr, nullptr},
		{"tc_statusbar_height", "Высота строки", nullptr, nullptr},
		{"tc_statusbar_color", "Цвет фона", nullptr, nullptr},
		{"tc_statusbar_text_color", "Цвет текста", nullptr, nullptr},
		{"tc_statusbar_alpha", "Прозрачность фона", nullptr, nullptr},
		{"tc_statusbar_text_alpha", "Прозрачность текста", nullptr, nullptr},
		{"tc_statusbar_labels", "Подписи у пунктов", nullptr, nullptr},
		{"tc_statusbar_scheme", "Порядок пунктов", nullptr, nullptr},
		{"tc_tee_trail", "Шлейф TClient", nullptr, nullptr},
		{"tc_tee_trail_others", "Шлейф и у других", nullptr, nullptr},
		{"tc_tee_trail_width", "Ширина", nullptr, nullptr},
		{"tc_tee_trail_length", "Длина", nullptr, nullptr},
		{"tc_tee_trail_alpha", "Непрозрачность", nullptr, nullptr},
		{"tc_tee_trail_color", "Цвет", nullptr, nullptr},
		{"tc_tee_trail_taper", "Сужать к концу", nullptr, nullptr},
		{"tc_tee_trail_fade", "Растворять к концу", nullptr, nullptr},
		{"tc_tee_trail_color_mode", "Цвет шлейфа", "Solid|Tee|Rainbow|Speed|Random", "Свой|Ти|Радуга|Скорость|Случайно"},
		{"tc_auto_reply_muted", "Автоответ замьюченным", nullptr, nullptr},
		{"tc_auto_reply_muted_message", "Текст автоответа замьюченным", nullptr, nullptr},
		{"tc_auto_reply_minimized", "Автоответ, когда игра свёрнута", nullptr, nullptr},
		{"tc_auto_reply_minimized_message", "Текст, когда игра свёрнута", nullptr, nullptr},
		{"tc_auto_vote_when_far", "Голосовать «нет», если далеко на карте", nullptr, nullptr},
		{"tc_auto_vote_when_far_message", "Сообщение при таком голосе", nullptr, nullptr},
		{"tc_auto_vote_when_far_time", "Через сколько секунд голосовать", nullptr, nullptr},
		{"tc_custom_font", "Свой шрифт игры", nullptr, nullptr},
		{"tc_bg_draw_width", "Толщина линий", nullptr, nullptr},
		{"tc_bg_draw_fade_time", "Исчезают через, с (0 — никогда)", nullptr, nullptr},
		{"tc_bg_draw_max_items", "Максимум линий", nullptr, nullptr},
		{"tc_bg_draw_color", "Цвет линий", nullptr, nullptr},
		{"tc_bg_draw_auto_save_load", "Сохранять и загружать рисунки", nullptr, nullptr},
		{"tc_translate_backend", "Сервис перевода (google, libretranslate)", nullptr, nullptr},
		{"tc_translate_target", "Переводить на язык (код из 2 букв)", nullptr, nullptr},
		{"tc_translate_endpoint", "Адрес сервиса (https)", nullptr, nullptr},
		{"tc_translate_key", "Ключ API", nullptr, nullptr},
		{"tc_translate_auto", "Переводить входящие сообщения", nullptr, nullptr},
		{"tc_translate_outgoing", "Переводить свои сообщения", nullptr, nullptr},
		{"tc_translate_outgoing_target", "Язык для своих сообщений", nullptr, nullptr},
		{"tc_animate_wheel_time", "Анимация колёс эмоций и биндов, мс", nullptr, nullptr},
		{"tc_pet_show", "Питомец", nullptr, nullptr},
		{"tc_pet_skin", "Скин питомца", nullptr, nullptr},
		{"tc_pet_size", "Размер питомца, %", nullptr, nullptr},
		{"tc_pet_alpha", "Непрозрачность питомца", nullptr, nullptr},
		{"tc_change_name_near_finish", "Менять ник у финиша", nullptr, nullptr},
		{"tc_finish_name", "Ник у финиша", nullptr, nullptr},
		{"tc_volleyball_better_ball", "Замороженные как мячи в волейболе", "Off|Volleyball maps|Always", "Выкл|На волейбольных|Всегда"},
		{"tc_volleyball_better_ball_skin", "Скин мяча", nullptr, nullptr},
		{"tc_show_player_hit_boxes", "Хитбоксы игроков", "Off|Predicted|Both", "Выкл|Предсказ.|Оба"},
		{"tc_hide_chat_bubbles", "Прятать свои пузыри чата (rcon)", nullptr, nullptr},
		{"tc_execute_on_connect", "Команда при подключении", nullptr, nullptr},
		{"tc_execute_on_join", "Команда при входе в игру", nullptr, nullptr},
		{"tc_execute_on_join_delay", "Задержка команды при входе, тики", nullptr, nullptr},
		{"tc_custom_communities_url", "Адрес своих сообществ", nullptr, nullptr},
		{"tc_discord_rpc", "Статус в Discord (после перезапуска)", nullptr, nullptr},
		{"tc_show_local_time_seconds", "Секунды в местном времени", nullptr, nullptr},
		{"tc_showhud_dummy_position", "Позиция дамми", nullptr, nullptr},
		{"tc_showhud_dummy_speed", "Скорость дамми", nullptr, nullptr},
		{"tc_showhud_dummy_angle", "Угол прицела дамми", nullptr, nullptr},
	};

	int GroupOf(const char *pName)
	{
		for(const SRule &Rule : gs_aRules)
		{
			if(str_startswith(pName, Rule.m_pPrefix))
				return Rule.m_Group;
		}
		return GROUP_OTHER;
	}

	bool Hidden(const char *pName)
	{
		for(const char *pPrefix : gs_apHidden)
		{
			if(str_startswith(pName, pPrefix))
				return true;
		}
		return false;
	}

	const SLabel *FindLabel(const char *pName)
	{
		for(const SLabel &Label : gs_aLabels)
		{
			if(str_comp(Label.m_pName, pName) == 0)
				return &Label;
		}
		return nullptr;
	}

	bool IsChanged(const STcSetting &Setting)
	{
		switch(Setting.m_Type)
		{
		case TYPE_INT: return *(int *)Setting.m_pValue != (int)Setting.m_Default;
		case TYPE_COL: return *(unsigned *)Setting.m_pValue != Setting.m_Default;
		default: return str_comp((const char *)Setting.m_pValue, Setting.m_pDefaultStr) != 0;
		}
	}

	// splits "a|b|c" into the list, true when it has exactly Count items
	bool SplitOptions(const char *pOptions, std::vector<std::string> *pList, int Count)
	{
		pList->clear();
		if(!pOptions)
			return false;
		const char *pStart = pOptions;
		while(true)
		{
			const char *pBar = str_find(pStart, "|");
			pList->emplace_back(pStart, pBar ? pBar - pStart : str_length(pStart));
			if(!pBar)
				break;
			pStart = pBar + 1;
		}
		return (int)pList->size() == Count;
	}
} // namespace

// color with a swatch on the right, the picker opens in a popup
void CXcMenu::ConfigColorRow(const char *pLabel, unsigned *pValue, bool Alpha)
{
	ImGui::PushID(pValue);
	RowLabel(pLabel, S(44.0f));
	const ColorRGBA Rgba = color_cast<ColorRGBA>(ColorHSLA(*pValue, Alpha));
	float aColor[4] = {Rgba.r, Rgba.g, Rgba.b, Alpha ? Rgba.a : 1.0f};
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const ImVec2 Size(S(44.0f), S(24.0f));
	if(ImGui::InvisibleButton("##swatch", Size))
		ImGui::OpenPopup("##picker");
	const bool Hovered = ImGui::IsItemHovered();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const STheme &t = Theme();
	pDraw->AddRectFilled(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y), Col(ImVec4(aColor[0], aColor[1], aColor[2], 1.0f)), S(6.0f));
	pDraw->AddRect(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y), Col(Hovered ? t.m_Accent : t.m_Border), S(6.0f), 0, Hovered ? 2.0f : 1.0f);
	if(ImGui::BeginPopup("##picker"))
	{
		ImGuiColorEditFlags Flags = ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayHex;
		if(!Alpha)
			Flags |= ImGuiColorEditFlags_NoAlpha;
		ImGui::SetNextItemWidth(S(220.0f));
		if(Alpha ? ImGui::ColorPicker4("##pick", aColor, Flags | ImGuiColorEditFlags_AlphaBar) : ImGui::ColorPicker3("##pick", aColor, Flags))
		{
			const ColorHSLA Hsl = color_cast<ColorHSLA>(ColorRGBA(aColor[0], aColor[1], aColor[2], Alpha ? aColor[3] : 1.0f));
			*pValue = Hsl.Pack(Alpha);
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}

void CXcMenu::RenderSettingsTClient()
{
	const STheme &t = Theme();
	const bool Russian = IsRussian();

	// search and filters on top
	const float ButtonWidth = S(190.0f);
	InputText("##tc_search", m_aTcSearch, sizeof(m_aTcSearch), Tr("Search the TClient settings"), std::max(AvailWidth() - ButtonWidth - S(10.0f), S(160.0f)));
	ImGui::SameLine(0.0f, S(10.0f));
	if(Button(Tr("Only changed"), m_TcOnlyChanged, ButtonWidth))
		m_TcOnlyChanged = !m_TcOnlyChanged;
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	// what fits the filters, per group
	std::vector<int> avGroups[NUM_GROUPS];
	for(int i = 0; i < (int)std::size(gs_aTcSettings); i++)
	{
		const STcSetting &Setting = gs_aTcSettings[i];
		if(Hidden(Setting.m_pName) || !(Setting.m_Flags & CFGFLAG_CLIENT))
			continue;
		if(m_TcOnlyChanged && !IsChanged(Setting))
			continue;
		if(m_aTcSearch[0])
		{
			const SLabel *pLabel = FindLabel(Setting.m_pName);
			if(!str_find_nocase(Setting.m_pName, m_aTcSearch) && !str_find_nocase(Setting.m_pDesc, m_aTcSearch) &&
				!(pLabel && str_utf8_find_nocase(pLabel->m_pRu, m_aTcSearch)) && !str_utf8_find_nocase(Tr(gs_aGroups[GroupOf(Setting.m_pName)].m_pTitle), m_aTcSearch))
				continue;
		}
		avGroups[GroupOf(Setting.m_pName)].push_back(i);
	}

	bool Any = false;
	BeginCardGrid("##tclient", 2);
	std::vector<std::string> vOptions;
	for(int Group = 0; Group < NUM_GROUPS; Group++)
	{
		if(avGroups[Group].empty())
			continue;
		Any = true;
		char aId[32];
		str_format(aId, sizeof(aId), "##tc_group_%d", Group);
		if(!BeginCard(aId, Tr(gs_aGroups[Group].m_pTitle), Tr(gs_aGroups[Group].m_pDesc)))
			continue;
		for(int Index : avGroups[Group])
		{
			const STcSetting &Setting = gs_aTcSettings[Index];
			const SLabel *pLabel = FindLabel(Setting.m_pName);
			const char *pText = Russian && pLabel ? pLabel->m_pRu : Setting.m_pDesc;
			if(!pText[0])
				pText = Setting.m_pName;
			ImGui::PushID(Setting.m_pName);
			const ImVec2 RowStart = ImGui::GetCursorScreenPos();
			switch(Setting.m_Type)
			{
			case TYPE_INT:
			{
				int *pValue = (int *)Setting.m_pValue;
				const int Count = Setting.m_Max - Setting.m_Min + 1;
				if(Setting.m_Min == 0 && Setting.m_Max == 1)
					ToggleRow(pText, Setting.m_pName, pValue);
				else if(pLabel && Count <= 5 && SplitOptions(Russian ? pLabel->m_pOptionsRu : pLabel->m_pOptionsEn, &vOptions, Count))
				{
					ParamLabel(pText);
					const char *apItems[5];
					int aValues[5];
					for(int i = 0; i < Count; i++)
					{
						apItems[i] = vOptions[i].c_str();
						aValues[i] = Setting.m_Min + i;
					}
					ConfigSegmented("##options", Setting.m_pName, pValue, apItems, Count, aValues);
				}
				else
					ConfigSlider(pText, Setting.m_pName, pValue, Setting.m_Min, Setting.m_Max);
				break;
			}
			case TYPE_COL:
				ConfigColorRow(pText, (unsigned *)Setting.m_pValue, (Setting.m_Flags & CFGFLAG_COLALPHA) != 0);
				break;
			default:
				ConfigText(pText, Setting.m_pName, (const char *)Setting.m_pValue, Setting.m_Length);
				break;
			}
			// changed settings get a small accent mark on the left, the console name shows on hover
			const ImVec2 RowEnd = ImGui::GetCursorScreenPos();
			if(IsChanged(Setting))
				ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(RowStart.x - S(8.0f), RowStart.y + S(9.0f)), S(2.5f), Col(t.m_Accent), 12);
			if(ImGui::IsMouseHoveringRect(RowStart, ImVec2(RowStart.x + AvailWidth(), RowEnd.y)) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
				ImGui::SetTooltip("%s\n%s", Setting.m_pName, Setting.m_pDesc);
			ImGui::PopID();
		}
		EndCard();
	}
	EndCardGrid();
	if(!Any)
		Description(m_TcOnlyChanged && !m_aTcSearch[0] ? Tr("Nothing is changed yet") : Tr("Nothing found"));
}
