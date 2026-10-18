#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/input.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <zlib.h>

#include <algorithm>

using namespace XcUi;

static const char *const CONFIG_FOLDER = "xclient/configs";
static const char *const CODE_PREFIX = "XC1:";

// --------------------------------------------------------------------------------------------
// files and codes

void CXcMenu::ConfigPath(const char *pName, char *pBuf, int BufSize) const
{
	str_format(pBuf, BufSize, "%s/%s.cfg", CONFIG_FOLDER, pName);
}

std::string CXcMenu::CurrentConfigText() const
{
	// every XClient setting, written as console commands
	std::string Text = "# XClient config\n";
	char aLine[1024];
	char aEscaped[512];
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) \
	str_format(aLine, sizeof(aLine), "%s %d\n", #ScriptName, g_Config.m_##Name); \
	Text += aLine;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) \
	str_format(aLine, sizeof(aLine), "%s %u\n", #ScriptName, (unsigned)g_Config.m_##Name); \
	Text += aLine;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) \
	{ \
		char *pDst = aEscaped; \
		str_escape(&pDst, g_Config.m_##Name, aEscaped + sizeof(aEscaped)); \
		str_format(aLine, sizeof(aLine), "%s \"%s\"\n", #ScriptName, aEscaped); \
		Text += aLine; \
	}
#include <engine/shared/config_variables_xclient.h>
#undef MACRO_CONFIG_INT
#undef MACRO_CONFIG_COL
#undef MACRO_CONFIG_STR
	return Text;
}

std::string CXcMenu::FilterConfig(const std::string &Text)
{
	// a config may come from anyone: only XClient settings get through, no binds, no connect, no exec
	std::string Result;
	size_t Start = 0;
	while(Start < Text.size())
	{
		size_t End = Text.find('\n', Start);
		if(End == std::string::npos)
			End = Text.size();
		std::string Line = Text.substr(Start, End - Start);
		Start = End + 1;
		if(!Line.empty() && Line.back() == '\r')
			Line.pop_back();
		if(Line.empty() || Line[0] == '#')
			continue;
		// one command per line, no command separators
		if(Line.compare(0, 3, "xc_") != 0 || Line.find(';') != std::string::npos)
			continue;
		Result += Line;
		Result += '\n';
	}
	return Result;
}

std::string CXcMenu::EncodeConfig(const std::string &Text)
{
	std::vector<unsigned char> vCompressed(compressBound((uLong)Text.size()));
	uLongf CompressedSize = (uLongf)vCompressed.size();
	if(compress2(vCompressed.data(), &CompressedSize, (const Bytef *)Text.data(), (uLong)Text.size(), Z_BEST_COMPRESSION) != Z_OK)
		return "";
	std::string Code(CODE_PREFIX);
	std::vector<char> vBase64(CompressedSize * 4 / 3 + 8);
	str_base64(vBase64.data(), (int)vBase64.size(), vCompressed.data(), (int)CompressedSize);
	Code += vBase64.data();
	return Code;
}

bool CXcMenu::DecodeConfig(const char *pCode, std::string &Text)
{
	// accept codes with spaces or line breaks from chat or messengers
	std::string Clean;
	for(const char *p = pCode; *p; p++)
		if(!str_isspace(*p))
			Clean += *p;
	if(Clean.compare(0, str_length(CODE_PREFIX), CODE_PREFIX) != 0)
		return false;
	std::vector<unsigned char> vCompressed(Clean.size());
	const int CompressedSize = str_base64_decode(vCompressed.data(), (int)vCompressed.size(), Clean.c_str() + str_length(CODE_PREFIX));
	if(CompressedSize <= 0)
		return false;
	std::vector<unsigned char> vText(64 * 1024);
	uLongf TextSize = (uLongf)vText.size();
	if(uncompress(vText.data(), &TextSize, vCompressed.data(), CompressedSize) != Z_OK)
		return false;
	Text.assign((const char *)vText.data(), TextSize);
	return true;
}

bool CXcMenu::WriteConfig(const char *pName, const std::string &Text)
{
	Storage()->CreateFolder("xclient", IStorage::TYPE_SAVE);
	Storage()->CreateFolder(CONFIG_FOLDER, IStorage::TYPE_SAVE);
	char aPath[IO_MAX_PATH_LENGTH];
	ConfigPath(pName, aPath, sizeof(aPath));
	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return false;
	io_write(File, Text.data(), (unsigned)Text.size());
	io_close(File);
	m_ConfigsDirty = true;
	return true;
}

bool CXcMenu::ReadConfig(const char *pName, std::string &Text) const
{
	char aPath[IO_MAX_PATH_LENGTH];
	ConfigPath(pName, aPath, sizeof(aPath));
	void *pData;
	unsigned Size;
	if(!Storage()->ReadFile(aPath, IStorage::TYPE_SAVE, &pData, &Size))
		return false;
	Text.assign((const char *)pData, Size);
	free(pData);
	return true;
}

static int ListConfigCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	auto *pList = static_cast<std::vector<std::pair<std::string, long long>> *>(pUser);
	const char *pName = pInfo->m_pName;
	const int Length = str_length(pName);
	if(IsDir || Length <= 4 || str_comp(pName + Length - 4, ".cfg") != 0)
		return 0;
	pList->emplace_back(std::string(pName, Length - 4), (long long)pInfo->m_TimeModified);
	(void)StorageType;
	return 0;
}

void CXcMenu::RefreshConfigList()
{
	std::vector<std::pair<std::string, long long>> vFiles;
	Storage()->ListDirectoryInfo(IStorage::TYPE_SAVE, CONFIG_FOLDER, ListConfigCallback, &vFiles);
	m_vConfigs.clear();
	for(const auto &File : vFiles)
		m_vConfigs.push_back({File.first, File.second});
	// newest first
	std::sort(m_vConfigs.begin(), m_vConfigs.end(), [](const SConfigEntry &a, const SConfigEntry &b) { return a.m_Modified > b.m_Modified; });
	m_ConfigsDirty = false;
}

static void SanitizeName(char *pName)
{
	str_sanitize_filename(pName);
	for(char *p = pName; *p; p++)
		if(*p == '/' || *p == '\\' || *p == '.')
			*p = '_';
}

// --------------------------------------------------------------------------------------------
// page

void CXcMenu::RenderConfigs()
{
	const STheme &t = Theme();
	if(m_ConfigsDirty)
		RefreshConfigList();

	const float DetailsWidth = S(340.0f);
	const ImVec2 Area = ImGui::GetContentRegionAvail();
	const float ListWidth = Area.x - DetailsWidth - S(16.0f);
	const ImVec2 ListPos = ImGui::GetCursorScreenPos();

	ImGui::BeginChild("##config_list", ImVec2(ListWidth, Area.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	Header(Tr("Configs"));
	Description(Tr("Your XClient settings, saved locally. Share a config with a short code."));
	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));

	BeginCardGrid("##config_actions", 2);
	if(BeginCard("##new", Tr("New config"), Tr("Saves your current settings")))
	{
		InputText("##new_name", m_aNewConfigName, sizeof(m_aNewConfigName), Tr("Config name"));
		if(Button(Tr("Save current"), true, -1.0f))
		{
			char aName[32];
			str_copy(aName, m_aNewConfigName[0] ? m_aNewConfigName : "config");
			SanitizeName(aName);
			if(WriteConfig(aName, CurrentConfigText()))
			{
				m_SelectedConfig = aName;
				m_SelectedCode.clear();
				m_aNewConfigName[0] = '\0';
				Toast(Tr("Saved"));
			}
		}
		EndCard();
	}
	if(BeginCard("##import", Tr("Import"), Tr("Paste a config code from a friend")))
	{
		InputText("##import_code", m_aImportCode, sizeof(m_aImportCode), "XC1:...");
		if(Button(Tr("Import"), false, -1.0f))
		{
			std::string Text;
			if(DecodeConfig(m_aImportCode, Text) && !FilterConfig(Text).empty())
			{
				char aName[32];
				str_format(aName, sizeof(aName), "imported_%d", (int)m_vConfigs.size() + 1);
				WriteConfig(aName, "# XClient config (imported)\n" + FilterConfig(Text));
				m_SelectedConfig = aName;
				m_SelectedCode.clear();
				m_aImportCode[0] = '\0';
				Toast(Tr("Imported"));
			}
			else
			{
				Toast(Tr("This code is not a valid config"), TOAST_ERROR);
			}
		}
		EndCard();
	}
	EndCardGrid();

	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	Title(Tr("Saved configs"));
	if(m_vConfigs.empty())
		Description(Tr("No configs yet"));

	// config cards, three per row
	const float Gap = S(12.0f);
	const int Columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + Gap) / (S(220.0f) + Gap)));
	const float CardWidth = (ImGui::GetContentRegionAvail().x - Gap * (Columns - 1)) / Columns;
	const float CardHeight = S(72.0f);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	for(size_t i = 0; i < m_vConfigs.size(); i++)
	{
		const SConfigEntry &Entry = m_vConfigs[i];
		if(i % Columns != 0)
			ImGui::SameLine(0.0f, Gap);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + CardWidth, Min.y + CardHeight);
		ImGui::PushID(Entry.m_Name.c_str());
		if(ImGui::InvisibleButton("##config", ImVec2(CardWidth, CardHeight)))
		{
			m_SelectedConfig = Entry.m_Name;
			m_SelectedCode.clear();
		}
		const bool Hovered = ImGui::IsItemHovered();
		const ImGuiID Id = ImGui::GetItemID();
		ImGui::PopID();
		const float Hover = Animate(Id, Hovered ? 1.0f : 0.0f, 16.0f);
		const float Select = Animate(Id + 1, m_SelectedConfig == Entry.m_Name ? 1.0f : 0.0f, 16.0f);
		pDraw->AddRectFilled(Min, Max, Col(Mix(t.m_Card, t.m_CardHover, Hover)), S(12.0f));
		pDraw->AddRect(Min, Max, Col(Mix(t.m_Border, t.m_Accent, Select)), S(12.0f), 0, 1.0f + Select);
		ImFont *pLabel = Fonts().m_pLabel;
		ImFont *pSmall = Fonts().m_pSmall;
		const ImVec4 Clip(Min.x + S(14.0f), Min.y, Max.x - S(14.0f), Max.y);
		char aTitle[96];
		str_format(aTitle, sizeof(aTitle), "%s  %s", XC_ICON_FOLDER, Entry.m_Name.c_str());
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + S(14.0f), Min.y + S(14.0f)), Col(t.m_Text), aTitle, nullptr, 0.0f, &Clip);
		char aDate[64];
		str_timestamp_ex((time_t)Entry.m_Modified, aDate, sizeof(aDate), "%d.%m.%Y %H:%M");
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(14.0f), Max.y - S(14.0f) - pSmall->FontSize), Col(t.m_TextMuted), aDate, nullptr, 0.0f, &Clip);
	}
	ImGui::EndChild();

	// details of the selected config
	ImGui::SetCursorScreenPos(ImVec2(ListPos.x + ListWidth + S(16.0f), ListPos.y));
	ImGui::BeginChild("##config_details_area", ImVec2(DetailsWidth, Area.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	const ImVec2 PanelMin = ImGui::GetCursorScreenPos();
	PanelRect(ImGui::GetWindowDrawList(), PanelMin, ImVec2(PanelMin.x + DetailsWidth, PanelMin.y + Area.y), S(14.0f), t.m_Card);
	const float Pad = S(18.0f);
	ImGui::SetCursorScreenPos(ImVec2(PanelMin.x + Pad, PanelMin.y + Pad));
	ImGui::BeginChild("##config_details", ImVec2(DetailsWidth - Pad * 2.0f, Area.y - Pad * 2.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	const auto Selected = std::find_if(m_vConfigs.begin(), m_vConfigs.end(), [&](const SConfigEntry &Entry) { return Entry.m_Name == m_SelectedConfig; });
	// the rename field follows the selection, however it changed (click, save, import, rename)
	if(m_RenameFor != m_SelectedConfig)
	{
		m_RenameFor = m_SelectedConfig;
		str_copy(m_aRenameBuffer, m_SelectedConfig.c_str());
		m_DeleteArmedUntil = 0.0f;
	}
	if(Selected == m_vConfigs.end())
	{
		Title(Tr("Configs"));
		Description(Tr("Pick a config to load, rename or share it"));
	}
	else
	{
		ImGui::PushFont(Fonts().m_pHeader);
		ImGui::TextColored(t.m_Text, "%s", Selected->m_Name.c_str());
		ImGui::PopFont();
		char aDate[64];
		str_timestamp_ex((time_t)Selected->m_Modified, aDate, sizeof(aDate), "%d.%m.%Y %H:%M");
		Description(aDate);
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

		char aPath[IO_MAX_PATH_LENGTH];
		ConfigPath(Selected->m_Name.c_str(), aPath, sizeof(aPath));
		if(Button(Tr("Load"), true, -1.0f))
		{
			// run only the XClient settings, even if the file was edited by hand
			std::string Text;
			if(ReadConfig(Selected->m_Name.c_str(), Text))
			{
				const std::string Safe = FilterConfig(Text);
				size_t Start = 0;
				while(Start < Safe.size())
				{
					const size_t End = Safe.find('\n', Start);
					const std::string Line = Safe.substr(Start, End - Start);
					Console()->ExecuteLine(Line.c_str(), IConsole::CLIENT_ID_UNSPECIFIED, false);
					Start = End == std::string::npos ? Safe.size() : End + 1;
				}
				Toast(Tr("Loaded"));
			}
		}
		if(Button(Tr("Overwrite with current"), false, -1.0f))
		{
			WriteConfig(Selected->m_Name.c_str(), CurrentConfigText());
			m_SelectedCode.clear();
			Toast(Tr("Saved"));
		}

		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ParamLabel(Tr("Rename"));
		InputText("##rename", m_aRenameBuffer, sizeof(m_aRenameBuffer));
		if(Button(Tr("Rename"), false, -1.0f) && m_aRenameBuffer[0])
		{
			char aNewName[32];
			str_copy(aNewName, m_aRenameBuffer);
			SanitizeName(aNewName);
			char aNewPath[IO_MAX_PATH_LENGTH];
			ConfigPath(aNewName, aNewPath, sizeof(aNewPath));
			if(Storage()->RenameFile(aPath, aNewPath, IStorage::TYPE_SAVE))
			{
				m_SelectedConfig = aNewName;
				m_ConfigsDirty = true;
				Toast(Tr("Renamed"));
			}
		}

		// delete needs a second click within three seconds
		const bool Armed = ImGui::GetTime() < m_DeleteArmedUntil;
		if(Button(Armed ? Tr("Click again to delete") : Tr("Delete"), false, -1.0f))
		{
			if(Armed)
			{
				Storage()->RemoveFile(aPath, IStorage::TYPE_SAVE);
				m_SelectedConfig.clear();
				m_ConfigsDirty = true;
				m_DeleteArmedUntil = 0.0f;
				Toast(Tr("Deleted"));
			}
			else
			{
				m_DeleteArmedUntil = (float)ImGui::GetTime() + 3.0f;
			}
		}

		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		Title(Tr("Share code"));
		if(m_SelectedCode.empty())
		{
			std::string Text;
			if(ReadConfig(m_SelectedConfig.c_str(), Text))
				m_SelectedCode = EncodeConfig(FilterConfig(Text));
		}
		ImGui::PushFont(Fonts().m_pSmall);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextColored(t.m_TextMuted, "%.120s%s", m_SelectedCode.c_str(), m_SelectedCode.size() > 120 ? "..." : "");
		ImGui::PopTextWrapPos();
		ImGui::PopFont();
		if(Button(Tr("Copy code"), false, -1.0f))
		{
			Input()->SetClipboardText(m_SelectedCode.c_str());
			Toast(Tr("Code copied"));
		}
	}
	ImGui::EndChild();
	ImGui::EndChild();
}
