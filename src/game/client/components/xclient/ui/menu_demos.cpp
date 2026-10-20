// Demos page: recorded demos as cards, details with watch / rename / delete on the right.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/bytes.h>
#include <base/io.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/demo.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/storage.h>

#include <algorithm>
#include <ctime>

using namespace XcUi;

static constexpr const char *DEMO_ROOT = "demos";
static constexpr int DEMO_INFOS_PER_FRAME = 12;

enum
{
	DEMO_SORT_DATE = 0,
	DEMO_SORT_NAME,
	DEMO_SORT_LENGTH,
};

int CXcMenu::DemoListCallback(const CFsFileInfo *pInfo, int IsDir, int StorageType, void *pUser)
{
	CXcMenu *pSelf = static_cast<CXcMenu *>(pUser);
	if(str_comp(pInfo->m_pName, ".") == 0 || str_comp(pInfo->m_pName, "..") == 0)
		return 0;
	if(!IsDir && !str_endswith(pInfo->m_pName, ".demo"))
		return 0;
	// the same folder can exist in several storages, show it once
	if(IsDir)
	{
		for(const SDemoEntry &Entry : pSelf->m_vDemos)
			if(Entry.m_IsDir && Entry.m_File == pInfo->m_pName)
				return 0;
	}
	SDemoEntry Entry;
	Entry.m_File = pInfo->m_pName;
	Entry.m_IsDir = IsDir != 0;
	Entry.m_StorageType = StorageType;
	Entry.m_Date = IsDir ? 0 : (int64_t)pInfo->m_TimeModified;
	if(IsDir)
		Entry.m_Name = pInfo->m_pName;
	else
		Entry.m_Name = std::string(pInfo->m_pName, str_length(pInfo->m_pName) - str_length(".demo"));
	pSelf->m_vDemos.push_back(Entry);
	return 0;
}

void CXcMenu::RefreshDemos()
{
	m_DemosDirty = false;
	m_vDemos.clear();
	if(m_DemoFolder.empty())
		m_DemoFolder = DEMO_ROOT;
	Storage()->ListDirectoryInfo(IStorage::TYPE_ALL, m_DemoFolder.c_str(), DemoListCallback, this);
	m_DemosViewDirty = true;
}

void CXcMenu::DemoPath(const SDemoEntry &Entry, char *pBuf, int BufSize) const
{
	str_format(pBuf, BufSize, "%s/%s", m_DemoFolder.c_str(), Entry.m_File.c_str());
}

void CXcMenu::LoadDemoInfo(SDemoEntry &Entry)
{
	Entry.m_InfoLoaded = true;
	if(Entry.m_IsDir)
		return;
	char aPath[IO_MAX_PATH_LENGTH];
	DemoPath(Entry, aPath, sizeof(aPath));
	CDemoHeader Header;
	CTimelineMarkers Markers;
	CMapInfo MapInfo;
	IOHANDLE File = nullptr;
	Entry.m_Valid = DemoPlayer()->GetDemoInfo(Storage(), nullptr, aPath, Entry.m_StorageType, &Header, &Markers, &MapInfo, &File);
	if(File)
	{
		Entry.m_Size = io_length(File);
		io_close(File);
	}
	if(!Entry.m_Valid)
		return;
	str_copy(Entry.m_aMap, Header.m_aMapName);
	Entry.m_Length = bytes_be_to_uint(Header.m_aLength);
	Entry.m_Markers = std::clamp<int>(bytes_be_to_uint(Markers.m_aNumTimelineMarkers), 0, MAX_TIMELINE_MARKERS);
	str_copy(Entry.m_aTimestamp, Header.m_aTimestamp);
}

void CXcMenu::UpdateDemoView()
{
	// headers are read a few per frame, sorting by length waits for them
	int Budget = DEMO_INFOS_PER_FRAME;
	for(SDemoEntry &Entry : m_vDemos)
	{
		if(Budget <= 0)
			break;
		if(!Entry.m_InfoLoaded)
		{
			LoadDemoInfo(Entry);
			Budget--;
			m_DemosViewDirty = true;
		}
	}
	if(!m_DemosViewDirty)
		return;
	m_DemosViewDirty = false;

	m_vDemoView.clear();
	for(int i = 0; i < (int)m_vDemos.size(); i++)
	{
		const SDemoEntry &Entry = m_vDemos[i];
		if(m_aDemoSearch[0] && !str_utf8_find_nocase(Entry.m_Name.c_str(), m_aDemoSearch) && !str_utf8_find_nocase(Entry.m_aMap, m_aDemoSearch))
			continue;
		m_vDemoView.push_back(i);
	}
	// folders first, then by the chosen order
	std::stable_sort(m_vDemoView.begin(), m_vDemoView.end(), [&](int a, int b) {
		const SDemoEntry &A = m_vDemos[a];
		const SDemoEntry &B = m_vDemos[b];
		if(A.m_IsDir != B.m_IsDir)
			return A.m_IsDir;
		if(A.m_IsDir || m_DemoSort == DEMO_SORT_NAME)
			return str_comp_nocase(A.m_Name.c_str(), B.m_Name.c_str()) < 0;
		if(m_DemoSort == DEMO_SORT_LENGTH)
			return A.m_Length > B.m_Length;
		return A.m_Date > B.m_Date;
	});
}

static void FormatLength(int Seconds, char *pBuf, int BufSize)
{
	if(Seconds >= 3600)
		str_format(pBuf, BufSize, "%d:%02d:%02d", Seconds / 3600, (Seconds / 60) % 60, Seconds % 60);
	else
		str_format(pBuf, BufSize, "%d:%02d", Seconds / 60, Seconds % 60);
}

static void FormatDate(int64_t Date, char *pBuf, int BufSize)
{
	const time_t Time = (time_t)Date;
	struct tm *pTm = localtime(&Time);
	if(pTm)
		str_format(pBuf, BufSize, "%02d.%02d.%04d %02d:%02d", pTm->tm_mday, pTm->tm_mon + 1, pTm->tm_year + 1900, pTm->tm_hour, pTm->tm_min);
	else
		str_copy(pBuf, "-", BufSize);
}

static void FormatSize(int64_t Size, char *pBuf, int BufSize)
{
	if(Size >= 1024 * 1024)
		str_format(pBuf, BufSize, "%.1f MB", Size / (1024.0 * 1024.0));
	else
		str_format(pBuf, BufSize, "%d KB", (int)(Size / 1024));
}

bool CXcMenu::RenderDemoCard(int Index, float Width, float Height)
{
	const STheme &t = Theme();
	const SDemoEntry &Entry = m_vDemos[Index];
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	ImGui::PushID(Entry.m_File.c_str());
	const bool Clicked = ImGui::InvisibleButton("##demo", ImVec2(Width, Height));
	const bool Hovered = ImGui::IsItemHovered();
	const bool DoubleClicked = Hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
	const ImGuiID Id = ImGui::GetItemID();
	ImGui::PopID();

	const bool Selected = !Entry.m_IsDir && m_SelectedDemo == Entry.m_File;
	const float Hover = Animate(Id, Hovered ? 1.0f : 0.0f, 16.0f);
	const float Select = Animate(Id + 1, Selected ? 1.0f : 0.0f, 16.0f);
	pDraw->AddRectFilled(Min, Max, Col(Mix(t.m_Card, t.m_CardHover, Hover)), S(12.0f));
	pDraw->AddRect(Min, Max, Col(Mix(t.m_Border, t.m_Accent, Select)), S(12.0f), 0, 1.0f + Select);

	const float Pad = S(14.0f);
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	// icon in a round plate on the left
	const float IconRadius = S(18.0f);
	const ImVec2 IconCenter(Min.x + Pad + IconRadius, Min.y + Height * 0.5f);
	pDraw->AddCircleFilled(IconCenter, IconRadius, Col(Entry.m_IsDir ? t.m_Track : ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.14f)), 32);
	const char *pIcon = Entry.m_IsDir ? XC_ICON_FOLDER : XC_ICON_FILM;
	const ImVec2 IconSize = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pIcon);
	pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(IconCenter.x - IconSize.x * 0.5f, IconCenter.y - IconSize.y * 0.5f), Col(Entry.m_IsDir ? t.m_TextSecondary : t.m_Accent), pIcon);

	const float TextX = IconCenter.x + IconRadius + S(12.0f);
	const ImVec4 ClipRect(TextX, Min.y, Max.x - Pad, Max.y);
	pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(TextX, Min.y + Pad), Col(t.m_Text), Entry.m_Name.c_str(), nullptr, 0.0f, &ClipRect);
	char aLine[160];
	if(Entry.m_IsDir)
		str_copy(aLine, Tr("Folder"));
	else if(!Entry.m_InfoLoaded)
		str_copy(aLine, "...");
	else if(!Entry.m_Valid)
		str_copy(aLine, Tr("Broken demo"));
	else
	{
		char aLength[32];
		char aDate[32];
		FormatLength(Entry.m_Length, aLength, sizeof(aLength));
		FormatDate(Entry.m_Date, aDate, sizeof(aDate));
		str_format(aLine, sizeof(aLine), "%s  %s     %s  %s     %s", XC_ICON_MAP, Entry.m_aMap, XC_ICON_PLAY, aLength, aDate);
	}
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(TextX, Max.y - Pad - pSmall->FontSize), Col(Entry.m_Valid || Entry.m_IsDir || !Entry.m_InfoLoaded ? t.m_TextMuted : ImVec4(1.0f, 0.4f, 0.4f, 1.0f)), aLine, nullptr, 0.0f, &ClipRect);

	if(Clicked && !Entry.m_IsDir)
		m_SelectedDemo = Entry.m_File;
	return Entry.m_IsDir ? Clicked : DoubleClicked;
}

void CXcMenu::PlayDemo(const SDemoEntry &Entry)
{
	char aPath[IO_MAX_PATH_LENGTH];
	DemoPath(Entry, aPath, sizeof(aPath));
	const char *pError = Client()->DemoPlayer_Play(aPath, Entry.m_StorageType);
	if(pError)
		Toast(pError, TOAST_ERROR);
}

void CXcMenu::RenderDemoDetails(SDemoEntry *pEntry, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const float Width = ImGui::GetContentRegionAvail().x;
	PanelRect(pDraw, Min, ImVec2(Min.x + Width, Min.y + Height), S(14.0f), t.m_Card);
	const float Pad = S(18.0f);
	ImGui::SetCursorScreenPos(ImVec2(Min.x + Pad, Min.y + Pad));
	ImGui::BeginChild("##demo_details", ImVec2(Width - Pad * 2.0f, Height - Pad * 2.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	if(!pEntry)
	{
		Title(Tr("Demos"));
		Description(Tr("Pick a demo to watch, rename or delete it. Double click plays it."));
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		if(Button(Tr("Open folder"), false, -1.0f))
		{
			char aFolder[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, m_DemoFolder.c_str(), aFolder, sizeof(aFolder));
			Client()->ViewFile(aFolder);
		}
		// recording and the ghost live here, next to the demos they make
		ImGui::Dummy(ImVec2(0.0f, S(14.0f)));
		Title(Tr("Recording"));
		ToggleRow(Tr("Record demos automatically"), "cl_auto_demo_record", &g_Config.m_ClAutoDemoRecord);
		if(g_Config.m_ClAutoDemoRecord)
			ConfigSlider(Tr("Keep demos (0 = all)"), "cl_auto_demo_max", &g_Config.m_ClAutoDemoMax, 0, 1000);
		ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
		Title(Tr("Ghost"));
		Description(Tr("Your best run plays next to you as a see-through tee"));
		ToggleRow(Tr("Ghost"), "cl_race_ghost", &g_Config.m_ClRaceGhost);
		if(g_Config.m_ClRaceGhost)
		{
			ToggleRow(Tr("Show the ghost"), "cl_race_show_ghost", &g_Config.m_ClRaceShowGhost);
			ToggleRow(Tr("Save the ghost"), "cl_race_save_ghost", &g_Config.m_ClRaceSaveGhost);
			ToggleRow(Tr("Only better runs"), "cl_race_ghost_save_best", &g_Config.m_ClRaceGhostSaveBest);
			ConfigSlider(Tr("Visibility"), "cl_race_ghost_alpha", &g_Config.m_ClRaceGhostAlpha, 0, 100, "%d%%");
		}
		ImGui::EndChild();
		return;
	}

	ImGui::PushFont(Fonts().m_pHeader);
	ImGui::PushTextWrapPos(0.0f);
	ImGui::TextColored(t.m_Text, "%s", pEntry->m_Name.c_str());
	ImGui::PopTextWrapPos();
	ImGui::PopFont();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

	const auto InfoRow = [&](const char *pLabel, const char *pValue) {
		RowLabel(pLabel, ImGui::CalcTextSize(pValue).x);
		ImGui::TextColored(t.m_Text, "%s", pValue);
	};
	char aBuf[64];
	if(pEntry->m_Valid)
	{
		InfoRow(Tr("Map"), pEntry->m_aMap);
		FormatLength(pEntry->m_Length, aBuf, sizeof(aBuf));
		InfoRow(Tr("Length"), aBuf);
		str_format(aBuf, sizeof(aBuf), "%d", pEntry->m_Markers);
		InfoRow(Tr("Markers"), aBuf);
	}
	else
		Description(Tr("This demo can not be read"));
	FormatSize(pEntry->m_Size, aBuf, sizeof(aBuf));
	InfoRow(Tr("Size"), aBuf);
	FormatDate(pEntry->m_Date, aBuf, sizeof(aBuf));
	InfoRow(Tr("Date"), aBuf);
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	if(Button(Tr("Watch"), true, -1.0f) && pEntry->m_Valid)
		PlayDemo(*pEntry);

	// rename: the field is filled again when another demo is picked
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	if(m_DemoRenameFor != pEntry->m_File)
	{
		m_DemoRenameFor = pEntry->m_File;
		str_copy(m_aDemoRename, pEntry->m_Name.c_str());
	}
	ParamLabel(Tr("Rename"));
	InputText("##demo_rename", m_aDemoRename, sizeof(m_aDemoRename));
	if(Button(Tr("Rename"), false, -1.0f) && m_aDemoRename[0] && str_comp(m_aDemoRename, pEntry->m_Name.c_str()) != 0)
	{
		char aOld[IO_MAX_PATH_LENGTH];
		char aNew[IO_MAX_PATH_LENGTH];
		DemoPath(*pEntry, aOld, sizeof(aOld));
		str_format(aNew, sizeof(aNew), "%s/%s.demo", m_DemoFolder.c_str(), m_aDemoRename);
		if(str_find(m_aDemoRename, "/") || str_find(m_aDemoRename, "\\"))
			Toast(Tr("The name can not contain slashes"), TOAST_ERROR);
		else if(Storage()->FileExists(aNew, pEntry->m_StorageType))
			Toast(Tr("A demo with this name already exists"), TOAST_ERROR);
		else if(Storage()->RenameFile(aOld, aNew, pEntry->m_StorageType))
		{
			m_SelectedDemo = std::string(m_aDemoRename) + ".demo";
			m_DemosDirty = true;
			Toast(Tr("Renamed"));
		}
		else
			Toast(Tr("Could not rename"), TOAST_ERROR);
	}

	// delete needs a second click within 3 seconds
	const bool Armed = m_DemoDeleteArmed == pEntry->m_File && ImGui::GetTime() < m_DemoDeleteArmedUntil;
	if(Button(Armed ? Tr("Click again to delete") : Tr("Delete"), false, -1.0f))
	{
		if(Armed)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			DemoPath(*pEntry, aPath, sizeof(aPath));
			if(Storage()->RemoveFile(aPath, pEntry->m_StorageType))
			{
				m_SelectedDemo.clear();
				m_DemosDirty = true;
				Toast(Tr("Deleted"));
			}
			else
				Toast(Tr("Could not delete the file"), TOAST_ERROR);
			m_DemoDeleteArmed.clear();
		}
		else
		{
			m_DemoDeleteArmed = pEntry->m_File;
			m_DemoDeleteArmedUntil = (float)ImGui::GetTime() + 3.0f;
		}
	}
	ImGui::EndChild();
}

void CXcMenu::RenderDemos()
{
	if(m_DemosDirty)
		RefreshDemos();
	UpdateDemoView();

	// top row: search, sort, refresh, and the way up from a sub folder
	const float Avail = ImGui::GetContentRegionAvail().x;
	const float SearchWidth = S(300.0f);
	ImGui::BeginGroup();
	if(m_DemoFolder != DEMO_ROOT)
	{
		if(IconButton(XC_ICON_ARROW_LEFT, Tr("Back")))
		{
			m_DemoFolder.resize(m_DemoFolder.rfind('/'));
			m_DemosDirty = true;
			m_SelectedDemo.clear();
		}
		ImGui::SameLine(0.0f, S(10.0f));
	}
	ImGui::BeginChild("##demo_search_box", ImVec2(SearchWidth, S(36.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	if(InputText("##demo_search", m_aDemoSearch, sizeof(m_aDemoSearch), Tr("Search by name or map")))
		m_DemosViewDirty = true;
	ImGui::EndChild();
	ImGui::SameLine(0.0f, S(12.0f));
	const char *apSorts[] = {Tr("Newest"), Tr("By name"), Tr("By length")};
	ImGui::BeginChild("##demo_sort", ImVec2(S(360.0f), S(36.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	if(Segmented("##demo_sort_mode", &m_DemoSort, apSorts, 3))
		m_DemosViewDirty = true;
	ImGui::EndChild();
	ImGui::SameLine(0.0f, S(12.0f));
	if(IconButton(XC_ICON_ROTATE, Tr("Refresh")))
		m_DemosDirty = true;
	ImGui::SameLine(0.0f, S(12.0f));
	ImGui::AlignTextToFramePadding();
	ImGui::PushFont(Fonts().m_pSmall);
	ImGui::TextColored(Theme().m_TextMuted, "%s", m_DemoFolder.c_str());
	ImGui::PopFont();
	ImGui::EndGroup();
	ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
	(void)Avail;

	SDemoEntry *pSelected = nullptr;
	for(SDemoEntry &Entry : m_vDemos)
		if(!Entry.m_IsDir && Entry.m_File == m_SelectedDemo)
			pSelected = &Entry;

	const float DetailsWidth = S(340.0f);
	const ImVec2 Area = ImGui::GetContentRegionAvail();
	const float ListWidth = Area.x - DetailsWidth - S(16.0f);
	const ImVec2 ListPos = ImGui::GetCursorScreenPos();
	ImGui::BeginChild("##demo_list", ImVec2(ListWidth, Area.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	int Open = -1;
	if(m_vDemoView.empty())
	{
		ImGui::Dummy(ImVec2(0.0f, S(40.0f)));
		Title(m_vDemos.empty() ? Tr("No demos yet") : Tr("Nothing found"));
		Description(Tr("Demos are recorded with the record button in the classic menu or with auto recording"));
	}
	else
	{
		const float Gap = S(12.0f);
		const int Columns = std::max(1, (int)((ListWidth + Gap) / (S(320.0f) + Gap)));
		const float CardWidth = (ImGui::GetContentRegionAvail().x - Gap * (Columns - 1)) / Columns;
		const float CardHeight = S(72.0f);
		const int Rows = ((int)m_vDemoView.size() + Columns - 1) / Columns;
		ImGuiListClipper Clipper;
		Clipper.Begin(Rows, CardHeight + Gap);
		while(Clipper.Step())
		{
			for(int Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; Row++)
			{
				const ImVec2 RowPos = ImGui::GetCursorScreenPos();
				for(int Column = 0; Column < Columns; Column++)
				{
					const int ViewIndex = Row * Columns + Column;
					if(ViewIndex >= (int)m_vDemoView.size())
						break;
					ImGui::SetCursorScreenPos(ImVec2(RowPos.x + Column * (CardWidth + Gap), RowPos.y));
					if(RenderDemoCard(m_vDemoView[ViewIndex], CardWidth, CardHeight))
						Open = m_vDemoView[ViewIndex];
				}
				ImGui::SetCursorScreenPos(ImVec2(RowPos.x, RowPos.y + CardHeight + Gap));
				ImGui::Dummy(ImVec2(0.0f, 0.0f));
			}
		}
	}
	ImGui::EndChild();

	ImGui::SetCursorScreenPos(ImVec2(ListPos.x + ListWidth + S(16.0f), ListPos.y));
	ImGui::BeginChild("##demo_details_area", ImVec2(DetailsWidth, Area.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	RenderDemoDetails(pSelected, Area.y);
	ImGui::EndChild();

	// a click on a folder opens it, a double click on a demo plays it (after drawing, the list changes)
	if(Open >= 0)
	{
		const SDemoEntry Entry = m_vDemos[Open];
		if(Entry.m_IsDir)
		{
			m_DemoFolder += "/" + Entry.m_File;
			m_DemosDirty = true;
			m_SelectedDemo.clear();
			PlayUiSound(SOUND_TAB);
		}
		else if(Entry.m_Valid)
			PlayDemo(Entry);
	}
}
