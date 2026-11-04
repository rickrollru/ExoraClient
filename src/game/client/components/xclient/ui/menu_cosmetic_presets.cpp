// Cosmetics sets: the cape, scarf, accessory, own hat, tee shader and neon outline saved under a name and put on with
// one click. Each set is a small config file in xclient/cosmetics with only those settings.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/io.h>
#include <base/str.h>

#include <engine/console.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <algorithm>
#include <map>

using namespace XcUi;

static const char *const COSMETICS_FOLDER = "xclient/cosmetics";
// the settings a set holds: everything of these parts (who sees them too), nothing else
static const char *const s_apCosmeticPrefixes[] = {"xc_cape", "xc_scarf", "xc_accessory", "xc_hat ", "xc_hat_", "xc_glow", "xc_tee_shader"};

static bool IsCosmeticLine(const std::string &Line)
{
	for(const char *pPrefix : s_apCosmeticPrefixes)
		if(Line.compare(0, str_length(pPrefix), pPrefix) == 0)
			return true;
	return false;
}

std::string CXcMenu::CurrentCosmeticsText() const
{
	const std::string All = FilterConfig(CurrentConfigText());
	std::string Text;
	size_t Start = 0;
	while(Start < All.size())
	{
		size_t End = All.find('\n', Start);
		if(End == std::string::npos)
			End = All.size();
		const std::string Line = All.substr(Start, End - Start);
		Start = End + 1;
		if(IsCosmeticLine(Line))
			Text += Line + "\n";
	}
	return Text;
}

// what a set wears, in a few words: "Crown · Cape · Galaxy"
static std::string CosmeticsSummary(const std::string &Text)
{
	std::map<std::string, std::string> Values;
	size_t Start = 0;
	while(Start < Text.size())
	{
		size_t End = Text.find('\n', Start);
		if(End == std::string::npos)
			End = Text.size();
		const std::string Line = Text.substr(Start, End - Start);
		Start = End + 1;
		const size_t Space = Line.find(' ');
		if(Space != std::string::npos)
			Values[Line.substr(0, Space)] = Line.substr(Space + 1);
	}
	const auto Int = [&](const char *pName) { auto It = Values.find(pName); return It == Values.end() ? 0 : str_toint(It->second.c_str()); };
	static const char *const s_apAccessories[] = {"", "Cone hat", "Halo", "Crown", "Horns", "Sprout", "Stars", "Flame", "Bow", "Sign", "Cross"};
	static const char *const s_apShaders[] = {"", "Gradient", "Rainbow", "Galaxy", "Waves", "Chrome"};
	std::vector<std::string> vParts;
	const int Accessory = Int("xc_accessory");
	if(Accessory > 0 && Accessory < (int)std::size(s_apAccessories))
		vParts.emplace_back(Tr(s_apAccessories[Accessory]));
	if(Int("xc_hat"))
		vParts.emplace_back(Tr("Own hat"));
	if(Int("xc_cape"))
		vParts.emplace_back(Tr("Cape"));
	if(Int("xc_scarf"))
		vParts.emplace_back(Tr("Scarf"));
	const int Shader = Int("xc_tee_shader");
	if(Shader > 0 && Shader < (int)std::size(s_apShaders))
		vParts.emplace_back(Tr(s_apShaders[Shader]));
	if(Int("xc_glow"))
		vParts.emplace_back(Tr("Neon outline"));
	if(vParts.empty())
		return Tr("Nothing worn");
	std::string Summary;
	for(size_t i = 0; i < vParts.size(); i++)
		Summary += (i ? " · " : "") + vParts[i];
	return Summary;
}

static int ListCosmeticsCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	auto *pList = static_cast<std::vector<std::pair<std::string, long long>> *>(pUser);
	const char *pName = pInfo->m_pName;
	const int Length = str_length(pName);
	if(!IsDir && Length > 4 && str_comp(pName + Length - 4, ".cfg") == 0)
		pList->emplace_back(std::string(pName, Length - 4), (long long)pInfo->m_TimeModified);
	(void)StorageType;
	return 0;
}

void CXcMenu::LoadCosmeticPresets()
{
	m_CosmeticPresetsLoaded = true;
	m_vCosmeticPresets.clear();
	std::vector<std::pair<std::string, long long>> vFiles;
	Storage()->ListDirectoryInfo(IStorage::TYPE_SAVE, COSMETICS_FOLDER, ListCosmeticsCallback, &vFiles);
	// the oldest first, so a new set goes to the end of the row
	std::sort(vFiles.begin(), vFiles.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
	for(const auto &File : vFiles)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s.cfg", COSMETICS_FOLDER, File.first.c_str());
		void *pData;
		unsigned Size;
		if(!Storage()->ReadFile(aPath, IStorage::TYPE_SAVE, &pData, &Size))
			continue;
		SCosmeticPreset Preset;
		Preset.m_Name = File.first;
		// only the settings of the cosmetics get through, even when the file was edited by hand
		const std::string Text = FilterConfig(std::string((const char *)pData, Size));
		free(pData);
		size_t Start = 0;
		while(Start < Text.size())
		{
			size_t End = Text.find('\n', Start);
			if(End == std::string::npos)
				End = Text.size();
			const std::string Line = Text.substr(Start, End - Start);
			Start = End + 1;
			if(IsCosmeticLine(Line))
				Preset.m_Text += Line + "\n";
		}
		Preset.m_Summary = CosmeticsSummary(Preset.m_Text);
		m_vCosmeticPresets.push_back(Preset);
	}
}

void CXcMenu::RenderCosmeticPresets()
{
	if(!m_CosmeticPresetsLoaded)
		LoadCosmeticPresets();
	if(!m_CosmeticDeleteArmed.empty() && ImGui::GetTime() > m_CosmeticDeleteArmedUntil)
		m_CosmeticDeleteArmed.clear();
	if(!BeginCard("##cosmetic_presets", Tr("Cosmetics sets"), Tr("Save what your tee wears and put a whole set on with one click")))
		return;

	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const std::string Current = CurrentCosmeticsText();
	const float Gap = S(8.0f);
	const float Avail = AvailWidth();
	const int Columns = std::max(1, (int)((Avail + Gap) / (S(190.0f) + Gap)));
	const float Width = (Avail - Gap * (Columns - 1)) / Columns;
	const float Height = S(54.0f);
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	int Apply = -1, Remove = -1;
	for(int i = 0; i < (int)m_vCosmeticPresets.size(); i++)
	{
		const SCosmeticPreset &Preset = m_vCosmeticPresets[i];
		if(i % Columns != 0)
			ImGui::SameLine(0.0f, Gap);
		else if(i > 0)
			ImGui::Dummy(ImVec2(0.0f, Gap * 0.25f));
		ImGui::PushID(i);
		const ImVec2 Pos = ImGui::GetCursorScreenPos();
		const bool Clicked = ImGui::InvisibleButton("##set", ImVec2(Width, Height));
		const bool Hovered = ImGui::IsItemHovered();
		const bool Active = Preset.m_Text == Current;
		const float Hover = Animate(ImGui::GetID("##hover"), Hovered ? 1.0f : 0.0f, 16.0f);
		const float Select = Animate(ImGui::GetID("##select"), Active ? 1.0f : 0.0f, 16.0f);
		const ImVec2 Max(Pos.x + Width, Pos.y + Height);
		pDraw->AddRectFilled(Pos, Max, Col(Mix(Mix(t.m_Track, t.m_CardHover, Hover), ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.16f), Select)), S(10.0f));
		pDraw->AddRect(Pos, Max, Col(Mix(t.m_Border, t.m_Accent, Select)), S(10.0f), 0, 1.0f + S(1.0f) * Select);
		const float TextX = Pos.x + S(12.0f);
		const float TextRight = Max.x - S(36.0f);
		ImGui::PushFont(pLabel);
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_Text);
		ImGui::RenderTextEllipsis(pDraw, ImVec2(TextX, Pos.y + S(9.0f)), ImVec2(TextRight, Pos.y + S(9.0f) + pLabel->FontSize + S(2.0f)), TextRight, TextRight, Preset.m_Name.c_str(), nullptr, nullptr);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::PushFont(pSmall);
		ImGui::PushStyleColor(ImGuiCol_Text, Active ? Mix(t.m_Accent, t.m_Text, 0.4f) : t.m_TextMuted);
		ImGui::RenderTextEllipsis(pDraw, ImVec2(TextX, Max.y - S(9.0f) - pSmall->FontSize), ImVec2(Max.x - S(10.0f), Max.y - S(6.0f)), Max.x - S(10.0f), Max.x - S(10.0f), Preset.m_Summary.c_str(), nullptr, nullptr);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		if(Active)
			DrawIcon(pDraw, pSmall, pSmall->FontSize, ImVec2(Max.x - S(20.0f), Pos.y + S(17.0f)), Col(t.m_Accent), XC_ICON_CHECK);
		// delete: a small trash in the corner on hover, asks once more
		bool OverTrash = false;
		const bool Armed = m_CosmeticDeleteArmed == Preset.m_Name;
		if((Hover > 0.01f || Armed) && !Active)
		{
			const float Radius = S(10.0f);
			const ImVec2 Center(Max.x - S(8.0f) - Radius, Pos.y + S(7.0f) + Radius);
			OverTrash = ImGui::IsMouseHoveringRect(ImVec2(Center.x - Radius, Center.y - Radius), ImVec2(Center.x + Radius, Center.y + Radius));
			pDraw->AddRectFilled(ImVec2(Center.x - Radius, Center.y - Radius), ImVec2(Center.x + Radius, Center.y + Radius), Armed ? Col(ImVec4(1.0f, 0.3f, 0.3f, 1.0f)) : Col(t.m_Track, OverTrash ? 1.6f : 1.0f), S(6.0f));
			DrawIcon(pDraw, pSmall, pSmall->FontSize, Center, Col(Armed ? ImVec4(1, 1, 1, 1) : t.m_TextSecondary), XC_ICON_TRASH);
			if(OverTrash)
				ImGui::SetTooltip("%s", Armed ? Tr("Click again to delete") : Tr("Delete"));
		}
		if(Clicked && OverTrash)
		{
			if(Armed)
				Remove = i;
			else
			{
				m_CosmeticDeleteArmed = Preset.m_Name;
				m_CosmeticDeleteArmedUntil = (float)ImGui::GetTime() + 3.0f;
			}
		}
		else if(Clicked)
			Apply = i;
		ImGui::PopID();
	}
	if(m_vCosmeticPresets.empty())
		Description(Tr("No sets yet: dress your tee up and save it below"));

	if(Apply >= 0)
	{
		const std::string &Text = m_vCosmeticPresets[Apply].m_Text;
		size_t Start = 0;
		while(Start < Text.size())
		{
			size_t End = Text.find('\n', Start);
			if(End == std::string::npos)
				End = Text.size();
			const std::string Line = Text.substr(Start, End - Start);
			Start = End + 1;
			if(!Line.empty())
				Console()->ExecuteLine(Line.c_str(), IConsole::CLIENT_ID_UNSPECIFIED, false);
		}
		PlayUiSound(SOUND_TAB);
		Toast(Tr("Set put on"));
	}
	if(Remove >= 0)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s.cfg", COSMETICS_FOLDER, m_vCosmeticPresets[Remove].m_Name.c_str());
		Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
		m_CosmeticDeleteArmed.clear();
		m_CosmeticPresetsLoaded = false;
		Toast(Tr("Deleted"));
	}

	// save what is worn now under a name
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	const float ButtonWidth = S(150.0f);
	InputText("##cosmetic_name", m_aCosmeticPresetName, sizeof(m_aCosmeticPresetName), Tr("Name of the set"), std::max(AvailWidth() - ButtonWidth - S(8.0f), S(80.0f)));
	const bool Enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
	ImGui::SameLine(0.0f, S(8.0f));
	if((Button(Tr("Save current"), true, ButtonWidth) || Enter) && m_aCosmeticPresetName[0])
	{
		char aName[64];
		str_copy(aName, m_aCosmeticPresetName);
		str_sanitize_filename(aName);
		for(char *p = aName; *p; p++)
			if(*p == '/' || *p == '\\' || *p == '.')
				*p = '_';
		Storage()->CreateFolder("xclient", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(COSMETICS_FOLDER, IStorage::TYPE_SAVE);
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "%s/%s.cfg", COSMETICS_FOLDER, aName);
		IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(File)
		{
			const std::string Text = "# Exora Client cosmetics set\n" + Current;
			io_write(File, Text.data(), (unsigned)Text.size());
			io_close(File);
			m_aCosmeticPresetName[0] = '\0';
			m_CosmeticPresetsLoaded = false;
			Toast(Tr("Set saved"));
		}
		else
			Toast(Tr("Could not save"), TOAST_ERROR);
	}
	EndCard();
}
