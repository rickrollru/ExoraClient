// The editor wiki: articles about the editor (for now the automapper and its builder) with search, role pictures
// and condition grids. The texts are in xeditor_wiki_pages.h, English, translated with XcUi::Tr.
#include "automap_rules.h"
#include "editor_style.h"
#include "xeditor.h"

#include <base/str.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdlib>
#include <iterator>
#include <string>
#include <vector>

using namespace XcUi;

namespace XcWiki
{
	enum
	{
		BLOCK_HEAD = 0,
		BLOCK_TEXT,
		BLOCK_TIP,
		BLOCK_WARN,
		BLOCK_STEPS, // lines, numbered
		BLOCK_ROLE, // m_pExtra: the role number, the text explains it
		BLOCK_GRID, // m_pExtra: 25 cells of a condition grid, the text is the caption
		BLOCK_CODE, // not translated
	};
	struct SBlock
	{
		int m_Type;
		const char *m_pText;
		const char *m_pExtra;
	};
	struct SPage
	{
		const char *m_pId;
		const char *m_pTitle;
		const SBlock *m_pBlocks;
		int m_NumBlocks;
	};
} // namespace XcWiki

#include "xeditor_wiki_pages.h"

using namespace XcWiki;

void XcDrawRolePattern(ImDrawList *pDraw, int Role, ImVec2 Min, float Size); // xeditor_automap.cpp

void CXcEditorUi::OpenWiki(const char *pPageId)
{
	m_WikiPage = 0;
	for(int i = 0; i < (int)std::size(s_aPages); i++)
		if(pPageId && str_comp(s_aPages[i].m_pId, pPageId) == 0)
			m_WikiPage = i;
	m_aWikiSearch[0] = '\0';
	m_WikiScrollTop = true;
	m_WikiRequest = true;
}

// does a page mention the search words (title or any text, in the language shown)
static bool PageMatches(const SPage &Page, const char *pSearch)
{
	if(!pSearch[0])
		return true;
	if(str_utf8_find_nocase(Tr(Page.m_pTitle), pSearch) || str_utf8_find_nocase(Page.m_pTitle, pSearch))
		return true;
	for(int i = 0; i < Page.m_NumBlocks; i++)
	{
		const SBlock &Block = Page.m_pBlocks[i];
		const char *pText = Block.m_Type == BLOCK_CODE ? Block.m_pText : Tr(Block.m_pText);
		if(str_utf8_find_nocase(pText, pSearch) || str_utf8_find_nocase(Block.m_pText, pSearch))
			return true;
	}
	return false;
}

// a box with a word on top: tips and warnings
static void NoteBox(const char *pLabel, const char *pText, const ImVec4 &Color)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float W = ImGui::GetContentRegionAvail().x;
	const float Pad = S(12.0f);
	ImFont *pSmall = Fonts().m_pSmall;
	const ImVec2 TextSize = ImGui::CalcTextSize(pText, nullptr, false, W - Pad * 2.0f);
	const float H = Pad * 2.0f + pSmall->FontSize + S(4.0f) + TextSize.y;
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	pDraw->AddRectFilled(Min, ImVec2(Min.x + W, Min.y + H), Col(Color, 0.12f), S(10.0f));
	pDraw->AddRect(Min, ImVec2(Min.x + W, Min.y + H), Col(Color, 0.35f), S(10.0f));
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + Pad, Min.y + Pad), Col(Color), pLabel);
	ImGui::SetCursorScreenPos(ImVec2(Min.x + Pad, Min.y + Pad + pSmall->FontSize + S(4.0f)));
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W - Pad * 2.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, t.m_Text);
	ImGui::TextUnformatted(pText);
	ImGui::PopStyleColor();
	ImGui::PopTextWrapPos();
	ImGui::SetCursorScreenPos(ImVec2(Min.x, Min.y + H));
	ImGui::Dummy(ImVec2(W, 0.0f));
}

// a condition grid like in the builder: . any, E empty, B block, T these tiles, N not these tiles,
// X the cell that gets the tile, Y the cell that gets the tile and must be empty
static void ConditionGrid(const char *pCells, float Cell)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	for(int i = 0; i < 25; i++)
	{
		const ImVec2 a(Min.x + (i % 5) * Cell, Min.y + (i / 5) * Cell);
		const ImVec2 b(a.x + Cell - S(3.0f), a.y + Cell - S(3.0f));
		const ImVec2 ia(a.x + S(5.0f), a.y + S(5.0f)), ib(b.x - S(5.0f), b.y - S(5.0f));
		const char c = pCells[i];
		pDraw->AddRectFilled(a, b, Col(t.m_Track, c == '.' ? 0.45f : 1.0f), S(5.0f));
		switch(c)
		{
		case 'E': pDraw->AddRect(ia, ib, Col(t.m_TextMuted), S(3.0f), 0, S(1.5f)); break;
		case 'B': pDraw->AddRectFilled(ia, ib, Col(t.m_TextSecondary, 0.85f), S(3.0f)); break;
		case 'T': pDraw->AddRectFilled(ia, ib, IM_COL32(61, 214, 164, 200), S(3.0f)); break;
		case 'N':
			pDraw->AddRectFilled(ia, ib, IM_COL32(61, 214, 164, 120), S(3.0f));
			pDraw->AddLine(ImVec2(ia.x, ib.y), ImVec2(ib.x, ia.y), IM_COL32(255, 77, 77, 230), S(2.0f));
			break;
		case 'X': pDraw->AddRectFilled(ia, ib, Col(t.m_Accent), S(3.0f)); break;
		case 'Y':
			pDraw->AddRect(ia, ib, Col(t.m_TextMuted), S(3.0f), 0, S(1.5f));
			pDraw->AddRect(a, b, Col(t.m_Accent), S(5.0f), 0, S(2.0f));
			break;
		}
		if(c == 'X')
			pDraw->AddRect(a, b, Col(t.m_Accent), S(5.0f), 0, S(2.0f));
	}
	ImGui::Dummy(ImVec2(Cell * 5.0f, Cell * 5.0f));
}

void CXcEditorUi::RenderWiki()
{
	const STheme &t = Theme();
	if(m_WikiRequest)
	{
		ImGui::OpenPopup("##xe_wiki");
		m_WikiRequest = false;
	}
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const ImVec2 Size(std::min(S(1240.0f), Display.x - S(60.0f)), std::min(S(860.0f), Display.y - S(60.0f)));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Size);
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	const bool Open = ImGui::BeginPopupModal("##xe_wiki", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
	if(!Open)
		return;

	const int NumPages = (int)std::size(s_aPages);
	m_WikiPage = std::clamp(m_WikiPage, 0, NumPages - 1);
	bool Close = false;
	const float NavW = S(300.0f);
	const float Gap = S(20.0f);
	const float BodyH = ImGui::GetContentRegionAvail().y;

	// --- the articles
	ImGui::BeginChild("##wiki_nav", ImVec2(NavW, BodyH));
	{
		Header(Tr("Editor wiki"));
		Description(Tr("Everything about the automapper and its builder"));
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		InputText("##wiki_search", m_aWikiSearch, sizeof(m_aWikiSearch), Tr("Search the wiki"), -1.0f);
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		ImGui::BeginChild("##wiki_list", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - S(52.0f)));
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		int Shown = 0;
		for(int i = 0; i < NumPages; i++)
		{
			if(!PageMatches(s_aPages[i], m_aWikiSearch))
				continue;
			Shown++;
			ImGui::PushID(i);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const float W = ImGui::GetContentRegionAvail().x;
			const float RowH = S(38.0f);
			if(ImGui::InvisibleButton("##page", ImVec2(W, RowH)))
			{
				m_WikiPage = i;
				m_WikiScrollTop = true;
			}
			const bool Selected = m_WikiPage == i;
			if(Selected || ImGui::IsItemHovered())
				pDraw->AddRectFilled(Min, ImVec2(Min.x + W, Min.y + RowH), Col(Selected ? t.m_Accent : t.m_CardHover, Selected ? 0.18f : 1.0f), S(8.0f));
			char aNumber[8];
			str_format(aNumber, sizeof(aNumber), "%d", i + 1);
			ImFont *pSmall = Fonts().m_pSmall;
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(10.0f), Min.y + (RowH - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), aNumber);
			pDraw->AddText(ImVec2(Min.x + S(34.0f), Min.y + (RowH - ImGui::GetFontSize()) * 0.5f), Col(Selected ? t.m_Text : t.m_TextSecondary), Tr(s_aPages[i].m_pTitle));
			ImGui::PopID();
		}
		if(!Shown)
			Description(Tr("Nothing found"));
		ImGui::EndChild();
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		if(Button(Tr("Close"), false, -1.0f) || ImGui::IsKeyPressed(ImGuiKey_Escape))
			Close = true;
	}
	ImGui::EndChild();

	// --- the article
	ImGui::SameLine(0.0f, Gap);
	ImGui::BeginChild("##wiki_page", ImVec2(0.0f, BodyH), ImGuiChildFlags_None);
	{
		if(m_WikiScrollTop)
		{
			ImGui::SetScrollY(0.0f);
			m_WikiScrollTop = false;
		}
		const SPage &Page = s_aPages[m_WikiPage];
		const float W = ImGui::GetContentRegionAvail().x - S(12.0f);
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
		Header(Tr(Page.m_pTitle));
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		for(int b = 0; b < Page.m_NumBlocks; b++)
		{
			const SBlock &Block = Page.m_pBlocks[b];
			const char *pText = Block.m_Type == BLOCK_CODE ? Block.m_pText : Tr(Block.m_pText);
			switch(Block.m_Type)
			{
			case BLOCK_HEAD:
				ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
				Title(pText);
				break;
			case BLOCK_TEXT:
				ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextSecondary);
				ImGui::TextUnformatted(pText);
				ImGui::PopStyleColor();
				break;
			case BLOCK_TIP:
				NoteBox(Tr("Tip"), pText, t.m_Accent);
				break;
			case BLOCK_WARN:
				NoteBox(Tr("Important"), pText, ImVec4(1.0f, 0.54f, 0.24f, 1.0f));
				break;
			case BLOCK_STEPS:
			{
				// one numbered line per step
				std::string Steps = pText;
				size_t Start = 0;
				int Number = 1;
				while(Start <= Steps.size())
				{
					size_t End = Steps.find('\n', Start);
					if(End == std::string::npos)
						End = Steps.size();
					const std::string Line = Steps.substr(Start, End - Start);
					const ImVec2 Min = ImGui::GetCursorScreenPos();
					const float Circle = S(22.0f);
					pDraw->AddCircleFilled(ImVec2(Min.x + Circle * 0.5f, Min.y + Circle * 0.5f), Circle * 0.5f, Col(t.m_Accent, 0.9f));
					char aNumber[8];
					str_format(aNumber, sizeof(aNumber), "%d", Number++);
					const ImVec2 NumberSize = ImGui::CalcTextSize(aNumber);
					pDraw->AddText(ImVec2(Min.x + (Circle - NumberSize.x) * 0.5f, Min.y + (Circle - NumberSize.y) * 0.5f), Col(t.m_OnAccent), aNumber);
					ImGui::SetCursorScreenPos(ImVec2(Min.x + Circle + S(10.0f), Min.y + S(1.0f)));
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W - Circle - S(10.0f));
					ImGui::TextUnformatted(Line.c_str());
					ImGui::PopTextWrapPos();
					const float Bottom = std::max(ImGui::GetCursorScreenPos().y, Min.y + Circle);
					ImGui::SetCursorScreenPos(ImVec2(Min.x, Bottom + S(6.0f)));
					Start = End + 1;
				}
				break;
			}
			case BLOCK_ROLE:
			{
				const int Role = std::atoi(Block.m_pExtra);
				const ImVec2 Min = ImGui::GetCursorScreenPos();
				const float Pattern = S(42.0f);
				pDraw->AddRectFilled(Min, ImVec2(Min.x + W, Min.y + Pattern + S(12.0f)), Col(t.m_Track, 0.35f), S(8.0f));
				XcDrawRolePattern(pDraw, Role, ImVec2(Min.x + S(6.0f), Min.y + S(6.0f)), Pattern);
				const float TextX = Min.x + Pattern + S(20.0f);
				pDraw->AddText(ImVec2(TextX, Min.y + S(7.0f)), Col(t.m_Text), Tr(XcAutomap::RoleName(Role)));
				ImGui::SetCursorScreenPos(ImVec2(TextX, Min.y + S(9.0f) + ImGui::GetFontSize()));
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W - Pattern - S(28.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextSecondary);
				ImGui::TextUnformatted(pText);
				ImGui::PopStyleColor();
				ImGui::PopTextWrapPos();
				const float Bottom = std::max(ImGui::GetCursorScreenPos().y, Min.y + Pattern + S(12.0f));
				ImGui::SetCursorScreenPos(ImVec2(Min.x, Bottom + S(4.0f)));
				break;
			}
			case BLOCK_GRID:
			{
				const ImVec2 Min = ImGui::GetCursorScreenPos();
				const float Cell = S(30.0f);
				ConditionGrid(Block.m_pExtra, Cell);
				const float Bottom = ImGui::GetCursorScreenPos().y;
				ImGui::SetCursorScreenPos(ImVec2(Min.x + Cell * 5.0f + S(16.0f), Min.y + S(4.0f)));
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W - Cell * 5.0f - S(16.0f));
				ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextSecondary);
				ImGui::TextUnformatted(pText);
				ImGui::PopStyleColor();
				ImGui::PopTextWrapPos();
				ImGui::SetCursorScreenPos(ImVec2(Min.x, std::max(Bottom, ImGui::GetCursorScreenPos().y)));
				break;
			}
			case BLOCK_CODE:
			{
				const float Pad = S(12.0f);
				ImFont *pSmall = Fonts().m_pSmall;
				const ImVec2 TextSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pText);
				const ImVec2 Min = ImGui::GetCursorScreenPos();
				pDraw->AddRectFilled(Min, ImVec2(Min.x + W, Min.y + TextSize.y + Pad * 2.0f), Col(t.m_Background, 0.9f), S(8.0f));
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + Pad, Min.y + Pad), Col(t.m_TextSecondary), pText);
				ImGui::Dummy(ImVec2(W, TextSize.y + Pad * 2.0f));
				break;
			}
			}
			ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		}
		ImGui::PopTextWrapPos();

		// back and forth through the articles
		ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
		const float ButtonW = (W - S(10.0f)) * 0.5f;
		if(m_WikiPage > 0)
		{
			char aPrev[160];
			str_format(aPrev, sizeof(aPrev), "%s: %s", Tr("Back"), Tr(s_aPages[m_WikiPage - 1].m_pTitle));
			if(Button(aPrev, false, ButtonW))
			{
				m_WikiPage--;
				m_WikiScrollTop = true;
			}
		}
		else
			ImGui::Dummy(ImVec2(ButtonW, 0.0f));
		if(m_WikiPage + 1 < NumPages)
		{
			ImGui::SameLine(0.0f, S(10.0f));
			char aNext[160];
			str_format(aNext, sizeof(aNext), "%s: %s", Tr("Next"), Tr(s_aPages[m_WikiPage + 1].m_pTitle));
			if(Button(aNext, true, ButtonW))
			{
				m_WikiPage++;
				m_WikiScrollTop = true;
			}
		}
		ImGui::Dummy(ImVec2(0.0f, S(10.0f)));
	}
	ImGui::EndChild();

	if(Close)
		ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
}
