// The automapper builder: make automapper rules for a tileset with the mouse. The wizard asks which tile is
// the inside, the top edge, the corners... and writes a normal DDNet rules file; a live preview shows the rules
// on a shape you can draw on.
#include "automap_rules.h"
#include "editor_style.h"
#include "xeditor.h"

#include <base/io.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/storage.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_tiles.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace XcUi;
using namespace XcAutomap;

namespace
{
	constexpr int PREVIEW_W = 24;
	constexpr int PREVIEW_H = 16;

	// a little world with everything the rules have to handle: hills, holes, a cave, platforms, a column, single blocks
	const char *const s_apDefaultShape[PREVIEW_H] = {
		"........................",
		"..........##............",
		".........####......###..",
		"..######.####......###..",
		"..######..##...........#",
		"..##..##...........#....",
		"..######....#......#....",
		"..######...###.....#....",
		"...........###..........",
		"......................#.",
		"..#########.....######..",
		".############...######..",
		"##############..##..##..",
		"##############..######..",
		"########################",
		"########################",
	};

	ImU32 RoleColor(int Role, float Alpha = 1.0f)
	{
		// spread around the color wheel, readable on dark and light
		const float Hue = std::fmod(Role * 0.137f + 0.55f, 1.0f);
		float r, g, b;
		ImGui::ColorConvertHSVtoRGB(Hue, 0.55f, 0.95f, r, g, b);
		return ImGui::GetColorU32(ImVec4(r, g, b, Alpha));
	}
} // namespace

struct CXcEditorUi::SAutomapBuilder
{
	int m_Image = -1;
	char m_aImageName[128] = "";
	char m_aConfigName[64] = "Exora";
	std::vector<SConfig> m_vConfigs; // the whole rules file of the tileset
	SRoles m_Roles;
	bool m_Derive = true;
	int m_Role = ROLE_INSIDE;
	std::vector<int> m_vShape; // 1: block
	unsigned m_Seed = 1;
	int m_PaintValue = -1; // while dragging on the preview: what is painted
	int m_Page = 0; // 0 the wizard, 1 the rules by hand
	// the rules page: the selected set, run, rule and neighbor cell (dx, dy; 0 0: none)
	int m_Config = 0;
	int m_Run = 0;
	int m_Rule = 0;
	int m_CellX = 0;
	int m_CellY = 0;
	char m_aRename[64] = "";
	SRoles m_GuessUndo; // the roles before "Guess the roles"
	bool m_CanUndoGuess = false;
};

static void AutomapPath(const char *pImageName, char *pPath, int PathSize)
{
	str_format(pPath, PathSize, "editor/automap/%s.rules", pImageName);
}

void CXcEditorUi::OpenAutomapBuilder(int Image)
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	if(Image < 0 || Image >= (int)pMap->m_vpImages.size())
	{
		ShowMessage(Tr("Pick a picture (a tileset) first: select a tile layer with a picture or click a picture in the images list."), Tr("Automapper builder"));
		return;
	}
	m_pAutomap = std::make_unique<SAutomapBuilder>();
	SAutomapBuilder &B = *m_pAutomap;
	B.m_Image = Image;
	str_copy(B.m_aImageName, pMap->m_vpImages[Image]->m_aName);
	for(int y = 0; y < PREVIEW_H; y++)
		for(int x = 0; x < PREVIEW_W; x++)
			B.m_vShape.push_back(s_apDefaultShape[y][x] == '#' ? 1 : 0);

	// the rules the tileset already has stay; the builder's own set opens again where it was left
	char aPath[IO_MAX_PATH_LENGTH];
	AutomapPath(B.m_aImageName, aPath, sizeof(aPath));
	if(char *pText = E.Storage()->ReadFileStr(aPath, IStorage::TYPE_ALL))
	{
		Parse(pText, B.m_vConfigs);
		free(pText);
	}
	bool Found = false;
	for(const SConfig &Config : B.m_vConfigs)
	{
		if(ReadRoles(Config, B.m_Roles, &B.m_Derive))
		{
			str_copy(B.m_aConfigName, Config.m_Name.c_str());
			Found = true;
			break;
		}
	}
	if(!Found)
	{
		// a free name next to the sets made by hand
		for(int i = 1;; i++)
		{
			char aName[64];
			if(i == 1)
				str_copy(aName, "Exora");
			else
				str_format(aName, sizeof(aName), "Exora %d", i);
			if(std::none_of(B.m_vConfigs.begin(), B.m_vConfigs.end(), [&](const SConfig &Config) { return Config.m_Name == aName; }))
			{
				str_copy(B.m_aConfigName, aName);
				break;
			}
		}
	}
	m_AutomapRequest = true;
}

void CXcEditorUi::OpenAutomapBuilderForSelection()
{
	CEditorMap *pMap = m_pEditor->Map();
	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(pMap->SelectedLayerType(0, LAYERTYPE_TILES));
	if(pLayer && pLayer->m_Image >= 0)
		OpenAutomapBuilder(pLayer->m_Image);
	else
		OpenAutomapBuilder(pMap->m_SelectedImage);
}

// one tile of the tileset into a rectangle of the screen, turned and mirrored like the map draws it
static void DrawTile(ImDrawList *pDraw, ImTextureID Texture, int Id, int Flags, ImVec2 Min, float Size, ImU32 Tint = IM_COL32_WHITE)
{
	if(Id <= 0 || Id > 255)
		return;
	float aU[4], aV[4];
	TileUvs(Flags, aU, aV);
	const float Inset = 0.004f; // keep the neighbor tiles from bleeding in
	const float u0 = (Id % 16) / 16.0f, v0 = (Id / 16) / 16.0f;
	ImVec2 aUv[4];
	for(int i = 0; i < 4; i++)
		aUv[i] = ImVec2(u0 + (Inset + aU[i] * (1.0f - 2.0f * Inset)) / 16.0f, v0 + (Inset + aV[i] * (1.0f - 2.0f * Inset)) / 16.0f);
	pDraw->AddImageQuad(Texture, Min, ImVec2(Min.x + Size, Min.y), ImVec2(Min.x + Size, Min.y + Size), ImVec2(Min.x, Min.y + Size), aUv[0], aUv[1], aUv[2], aUv[3], Tint);
}

// the 3x3 picture of a role: the middle block, which neighbors must be blocks and which must be empty
static void DrawRolePattern(ImDrawList *pDraw, int Role, ImVec2 Min, float Size)
{
	const STheme &t = Theme();
	const int *pPattern = RolePattern(Role);
	const float Cell = Size / 3.0f;
	for(int i = 0; i < 9; i++)
	{
		const ImVec2 a(Min.x + (i % 3) * Cell + 1.0f, Min.y + (i / 3) * Cell + 1.0f);
		const ImVec2 b(a.x + Cell - 2.0f, a.y + Cell - 2.0f);
		if(i == 4)
			pDraw->AddRectFilled(a, b, RoleColor(Role), 2.0f);
		else if(pPattern[i] > 0)
			pDraw->AddRectFilled(a, b, Col(t.m_TextSecondary, 0.8f), 2.0f);
		else if(pPattern[i] < 0)
			pDraw->AddRect(a, b, Col(t.m_TextMuted, 0.9f), 2.0f);
		else
			pDraw->AddRectFilled(a, b, Col(t.m_Track, 0.6f), 2.0f);
	}
}

void CXcEditorUi::RenderAutomapBuilder()
{
	if(!m_pAutomap)
		return;
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	SAutomapBuilder &B = *m_pAutomap;
	if(B.m_Image < 0 || B.m_Image >= (int)pMap->m_vpImages.size() || str_comp(pMap->m_vpImages[B.m_Image]->m_aName, B.m_aImageName) != 0)
	{
		// the picture is gone (another map, deleted)
		m_pAutomap = nullptr;
		return;
	}
	CEditorImage *pImage = pMap->m_vpImages[B.m_Image].get();
	const STheme &t = Theme();
	if(m_AutomapRequest)
	{
		ImGui::OpenPopup("##xe_automap");
		m_AutomapRequest = false;
	}
	const ImVec2 Display = ImGui::GetIO().DisplaySize;
	const ImVec2 Size(std::min(S(1500.0f), Display.x - S(60.0f)), std::min(S(860.0f), Display.y - S(60.0f)));
	ImGui::SetNextWindowPos(ImVec2(Display.x * 0.5f, Display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(Size);
	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(16.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.0f), S(18.0f)));
	bool Open = ImGui::BeginPopupModal("##xe_automap", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(2);
	if(!Open)
	{
		m_pAutomap = nullptr;
		return;
	}

	const ImTextureID Texture = (ImTextureID)(uintptr_t)m_Gui.TextureId(pImage->m_Texture);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	bool Close = false;

	// the head: what this is, the name of the rule set
	Header(Tr("Automapper builder"));
	char aInfo[256];
	str_format(aInfo, sizeof(aInfo), Tr("Tileset \"%s\": pick a role on the left, then click its tile in the picture. The preview on the right shows the rules at once."), B.m_aImageName);
	Description(aInfo);
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
	{
		const char *apPages[] = {Tr("Wizard"), Tr("Rules by hand")};
		int Page = B.m_Page;
		ImGui::BeginChild("##am_pages", ImVec2(S(380.0f), ImGui::GetFrameHeight() + S(14.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
		Segmented("##am_page", &Page, apPages, 2);
		ImGui::EndChild();
		if(Page != B.m_Page)
		{
			// the wizard's set goes into the file's list, so it can be changed by hand
			if(Page == 1 && !B.m_Roles.m_avTiles[ROLE_INSIDE].empty() && B.m_aConfigName[0])
			{
				const SConfig Built = Build(B.m_Roles, B.m_aConfigName, B.m_Derive);
				auto It = std::find_if(B.m_vConfigs.begin(), B.m_vConfigs.end(), [&](const SConfig &Other) { return Other.m_Name == Built.m_Name; });
				if(It != B.m_vConfigs.end())
					*It = Built;
				else
					B.m_vConfigs.push_back(Built);
				B.m_Config = (int)(std::find_if(B.m_vConfigs.begin(), B.m_vConfigs.end(), [&](const SConfig &Other) { return Other.m_Name == Built.m_Name; }) - B.m_vConfigs.begin());
				B.m_Run = B.m_Rule = 0;
			}
			B.m_Page = Page;
		}
	}
	ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

	const float FooterH = S(48.0f);
	const float BodyH = ImGui::GetContentRegionAvail().y - FooterH;
	const float LeftW = S(300.0f);
	const float RightW = std::min(S(470.0f), ImGui::GetContentRegionAvail().x * 0.36f);
	const float Gap = S(16.0f);
	const float MidW = ImGui::GetContentRegionAvail().x - LeftW - RightW - Gap * 2.0f;

	if(B.m_Page == 1)
		RenderAutomapRules(Texture, LeftW, MidW, BodyH, Gap);
	else
	{
	// --- the roles
	ImGui::BeginChild("##roles", ImVec2(LeftW, BodyH), ImGuiChildFlags_None);
	{
		ParamLabel(Tr("Rule set name"));
		InputText("##am_name", B.m_aConfigName, sizeof(B.m_aConfigName), nullptr, -1.0f);
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ImGui::BeginChild("##roles_list", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - S(96.0f)));
		ImDrawList *pList = ImGui::GetWindowDrawList();
		const float RowH = S(40.0f);
		for(int Role = 0; Role < NUM_ROLES; Role++)
		{
			ImGui::PushID(Role);
			const ImVec2 Min = ImGui::GetCursorScreenPos();
			const float W = ImGui::GetContentRegionAvail().x;
			if(ImGui::InvisibleButton("##role", ImVec2(W, RowH)))
				B.m_Role = Role;
			const bool Hovered = ImGui::IsItemHovered();
			const bool Selected = B.m_Role == Role;
			if(Selected || Hovered)
				pList->AddRectFilled(Min, ImVec2(Min.x + W, Min.y + RowH), Col(Selected ? t.m_Accent : t.m_CardHover, Selected ? 0.18f : 1.0f), S(8.0f));
			if(Selected)
				pList->AddRect(Min, ImVec2(Min.x + W, Min.y + RowH), Col(t.m_Accent, 0.7f), S(8.0f));
			const float Pat = S(24.0f);
			DrawRolePattern(pList, Role, ImVec2(Min.x + S(8.0f), Min.y + (RowH - Pat) * 0.5f), Pat);
			// the picked tiles, or the turned one the builder would use
			const std::vector<STile> &vTiles = B.m_Roles.m_avTiles[Role];
			const float Thumb = S(26.0f);
			float x = Min.x + W - S(8.0f) - Thumb;
			const char *pState = nullptr;
			if(!vTiles.empty())
			{
				for(int i = std::min((int)vTiles.size(), 3) - 1; i >= 0; i--)
				{
					pList->AddRectFilled(ImVec2(x, Min.y + (RowH - Thumb) * 0.5f), ImVec2(x + Thumb, Min.y + (RowH + Thumb) * 0.5f), Col(t.m_Track), S(4.0f));
					DrawTile(pList, Texture, vTiles[i].m_Id, vTiles[i].m_Flags, ImVec2(x, Min.y + (RowH - Thumb) * 0.5f), Thumb);
					x -= Thumb + S(4.0f);
				}
				if(vTiles.size() > 3)
				{
					char aMore[16];
					str_format(aMore, sizeof(aMore), "+%d", (int)vTiles.size() - 3);
					const ImVec2 TextSize = ImGui::CalcTextSize(aMore);
					pList->AddText(ImVec2(x + Thumb - TextSize.x, Min.y + (RowH - TextSize.y) * 0.5f), Col(t.m_TextMuted), aMore);
				}
			}
			else
			{
				STile Derived;
				if(B.m_Derive && DeriveRole(B.m_Roles, Role, &Derived))
				{
					pList->AddRectFilled(ImVec2(x, Min.y + (RowH - Thumb) * 0.5f), ImVec2(x + Thumb, Min.y + (RowH + Thumb) * 0.5f), Col(t.m_Track), S(4.0f));
					DrawTile(pList, Texture, Derived.m_Id, Derived.m_Flags, ImVec2(x, Min.y + (RowH - Thumb) * 0.5f), Thumb, IM_COL32(255, 255, 255, 150));
					pState = Tr("turned automatically");
				}
				else
					pState = Role == ROLE_INSIDE ? Tr("needed") : Tr("not set");
			}
			const float TextX = Min.x + S(8.0f) + Pat + S(10.0f);
			pList->AddText(ImVec2(TextX, Min.y + S(4.0f)), Col(Selected ? t.m_Text : t.m_TextSecondary), Tr(RoleName(Role)));
			if(pState)
			{
				ImFont *pSmall = Fonts().m_pSmall;
				pList->AddText(pSmall, pSmall->FontSize, ImVec2(TextX, Min.y + RowH - pSmall->FontSize - S(5.0f)), Col(Role == ROLE_INSIDE && vTiles.empty() ? ImVec4(1.0f, 0.55f, 0.24f, 1.0f) : t.m_TextMuted), pState);
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ImGui::TextUnformatted(Tr("Missing sides: turn picked tiles"));
		ImGui::SameLine(LeftW - S(52.0f));
		Toggle("##am_derive", &B.m_Derive);
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		const float HalfW = (LeftW - S(8.0f)) * 0.5f;
		if(B.m_CanUndoGuess)
		{
			if(Button(Tr("Undo the guess"), false, HalfW))
			{
				B.m_Roles = B.m_GuessUndo;
				B.m_CanUndoGuess = false;
			}
		}
		else if(Button(Tr("Guess the roles"), true, HalfW))
			GuessAutomapRoles();
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Tr("The builder looks at the tiles (see-through or differently colored sides) and fills the roles. Check them and fix what is wrong"));
		ImGui::SameLine(0.0f, S(8.0f));
		ImGui::BeginDisabled(B.m_Roles.m_avTiles[B.m_Role].empty());
		if(Button(Tr("Clear this role"), false, HalfW))
			B.m_Roles.m_avTiles[B.m_Role].clear();
		ImGui::EndDisabled();
	}
	ImGui::EndChild();

	// --- the tileset
	ImGui::SameLine(0.0f, Gap);
	ImGui::BeginChild("##tileset", ImVec2(MidW, BodyH), ImGuiChildFlags_None);
	{
		const float Side = std::min(MidW, BodyH - S(28.0f));
		const float Cell = Side / 16.0f;
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Side, Min.y + Side);
		// checkerboard, the picture, a faint grid
		pDraw = ImGui::GetWindowDrawList();
		pDraw->AddRectFilled(Min, Max, Col(t.m_Track));
		for(int y = 0; y < 32; y++)
			for(int x = (y % 2); x < 32; x += 2)
				pDraw->AddRectFilled(ImVec2(Min.x + x * Cell * 0.5f, Min.y + y * Cell * 0.5f), ImVec2(Min.x + (x + 1) * Cell * 0.5f, Min.y + (y + 1) * Cell * 0.5f), Col(t.m_Card, 0.55f));
		pDraw->AddImage(Texture, Min, Max);
		for(int i = 1; i < 16; i++)
		{
			pDraw->AddLine(ImVec2(Min.x + i * Cell, Min.y), ImVec2(Min.x + i * Cell, Max.y), IM_COL32(0, 0, 0, 40));
			pDraw->AddLine(ImVec2(Min.x, Min.y + i * Cell), ImVec2(Max.x, Min.y + i * Cell), IM_COL32(0, 0, 0, 40));
		}
		// the tiles that have a role: a colored corner, the selected role framed
		for(int Role = 0; Role < NUM_ROLES; Role++)
		{
			for(const STile &Tile : B.m_Roles.m_avTiles[Role])
			{
				const ImVec2 a(Min.x + (Tile.m_Id % 16) * Cell, Min.y + (Tile.m_Id / 16) * Cell);
				if(Role == B.m_Role)
					pDraw->AddRect(a, ImVec2(a.x + Cell, a.y + Cell), Col(t.m_Accent), 0.0f, 0, S(2.5f));
				pDraw->AddTriangleFilled(a, ImVec2(a.x + Cell * 0.35f, a.y), ImVec2(a.x, a.y + Cell * 0.35f), RoleColor(Role));
			}
		}
		ImGui::InvisibleButton("##tiles", ImVec2(Side, Side), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		if(ImGui::IsItemHovered())
		{
			const ImVec2 Mouse = ImGui::GetIO().MousePos;
			const int tx = std::clamp((int)((Mouse.x - Min.x) / Cell), 0, 15), ty = std::clamp((int)((Mouse.y - Min.y) / Cell), 0, 15);
			const int Id = ty * 16 + tx;
			const ImVec2 a(Min.x + tx * Cell, Min.y + ty * Cell);
			pDraw->AddRect(a, ImVec2(a.x + Cell, a.y + Cell), IM_COL32(255, 255, 255, 200), 0.0f, 0, S(1.5f));
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			// which roles use it
			std::string Roles;
			for(int Role = 0; Role < NUM_ROLES; Role++)
				for(const STile &Tile : B.m_Roles.m_avTiles[Role])
					if(Tile.m_Id == Id)
						Roles += std::string("\n") + Tr(RoleName(Role));
			ImGui::SetTooltip("%s %d%s", Tr("Tile"), Id, Roles.c_str());
			if(Id != 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				// click: add it to the selected role, again: take it away
				B.m_CanUndoGuess = false;
				std::vector<STile> &vTiles = B.m_Roles.m_avTiles[B.m_Role];
				auto It = std::find_if(vTiles.begin(), vTiles.end(), [&](const STile &Tile) { return Tile.m_Id == Id; });
				if(It != vTiles.end())
					vTiles.erase(It);
				else
				{
					vTiles.push_back({Id, 0});
					// the next role that has nothing yet, so a whole tileset is clicked through quickly
					if(vTiles.size() == 1)
						for(int Next = 1; Next < NUM_ROLES; Next++)
						{
							const int Role = (B.m_Role + Next) % NUM_ROLES;
							if(B.m_Roles.m_avTiles[Role].empty())
							{
								B.m_Role = Role;
								break;
							}
						}
				}
			}
			if(ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				for(auto &vTiles : B.m_Roles.m_avTiles)
					vTiles.erase(std::remove_if(vTiles.begin(), vTiles.end(), [&](const STile &Tile) { return Tile.m_Id == Id; }), vTiles.end());
		}
		ImFont *pSmall = Fonts().m_pSmall;
		ImGui::PushFont(pSmall);
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextMuted);
		ImGui::TextWrapped("%s", Tr("Left click: give the tile to the role (several tiles are mixed randomly), again: take it back. Right click: take it from every role."));
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}
	ImGui::EndChild();
	}

	// --- the preview: the wizard's set, or the set chosen on the rules page with the selected rule marked
	const SConfig Built = Build(B.m_Roles, B.m_aConfigName, B.m_Derive);
	const SConfig Nothing;
	const bool HandPage = B.m_Page == 1;
	const SConfig &Config = !HandPage ? Built : (B.m_Config >= 0 && B.m_Config < (int)B.m_vConfigs.size() ? B.m_vConfigs[B.m_Config] : Nothing);
	ImGui::SameLine(0.0f, Gap);
	ImGui::BeginChild("##preview", ImVec2(RightW, BodyH), ImGuiChildFlags_None);
	{
		ParamLabel(Tr("Preview"));
		const float Cell = std::floor(std::min(RightW / PREVIEW_W, (BodyH - S(140.0f)) / PREVIEW_H));
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Cell * PREVIEW_W, Min.y + Cell * PREVIEW_H);
		pDraw = ImGui::GetWindowDrawList();
		pDraw->AddRectFilled(Min, Max, Col(t.m_Track), S(6.0f));
		std::vector<int> vIndices(B.m_vShape), vFlags, vWriter;
		Apply(Config, PREVIEW_W, PREVIEW_H, vIndices, &vFlags, B.m_Seed, &vWriter);
		const int Selected = HandPage ? B.m_Run * 10000 + B.m_Rule : -2;
		for(int y = 0; y < PREVIEW_H; y++)
		{
			for(int x = 0; x < PREVIEW_W; x++)
			{
				const int Cell0 = y * PREVIEW_W + x;
				const ImVec2 a(Min.x + x * Cell, Min.y + y * Cell);
				if(B.m_vShape[Cell0] && vWriter[Cell0] < 0)
					pDraw->AddRectFilled(a, ImVec2(a.x + Cell, a.y + Cell), Col(t.m_TextMuted, 0.5f)); // a block no rule draws yet
				else
					DrawTile(pDraw, Texture, vIndices[Cell0], vFlags[Cell0], a, Cell);
				if(vWriter[Cell0] == Selected)
					pDraw->AddRect(ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(a.x + Cell - 1.0f, a.y + Cell - 1.0f), Col(t.m_Accent), 0.0f, 0, S(1.5f));
			}
		}
		// draw on it: left adds blocks, right takes them away
		ImGui::InvisibleButton("##shape", ImVec2(Max.x - Min.x, Max.y - Min.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		if(ImGui::IsItemHovered() || B.m_PaintValue >= 0)
		{
			const ImVec2 Mouse = ImGui::GetIO().MousePos;
			const int x = (int)std::floor((Mouse.x - Min.x) / Cell), y = (int)std::floor((Mouse.y - Min.y) / Cell);
			if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				B.m_PaintValue = 1;
			if(ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				B.m_PaintValue = 0;
			if(!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
				B.m_PaintValue = -1;
			if(B.m_PaintValue >= 0 && x >= 0 && y >= 0 && x < PREVIEW_W && y < PREVIEW_H)
				B.m_vShape[y * PREVIEW_W + x] = B.m_PaintValue;
			if(x >= 0 && y >= 0 && x < PREVIEW_W && y < PREVIEW_H)
				pDraw->AddRect(ImVec2(Min.x + x * Cell, Min.y + y * Cell), ImVec2(Min.x + (x + 1) * Cell, Min.y + (y + 1) * Cell), IM_COL32(255, 255, 255, 160));
		}
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		ImFont *pSmall = Fonts().m_pSmall;
		ImGui::PushFont(pSmall);
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextMuted);
		ImGui::TextWrapped("%s", Tr("Draw on the preview: left mouse adds blocks, right mouse removes them."));
		ImGui::PopStyleColor();
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		const float ButtonW = (RightW - S(16.0f)) / 3.0f;
		if(Button(Tr("Shuffle"), false, ButtonW))
			B.m_Seed = B.m_Seed * 1103515245u + 12345u;
		ImGui::SameLine(0.0f, S(8.0f));
		if(Button(Tr("Reset shape"), false, ButtonW))
		{
			B.m_vShape.clear();
			for(int y = 0; y < PREVIEW_H; y++)
				for(int x = 0; x < PREVIEW_W; x++)
					B.m_vShape.push_back(s_apDefaultShape[y][x] == '#' ? 1 : 0);
		}
		ImGui::SameLine(0.0f, S(8.0f));
		if(Button(Tr("Empty"), false, ButtonW))
			std::fill(B.m_vShape.begin(), B.m_vShape.end(), 0);
	}
	ImGui::EndChild();

	// --- save
	ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
	const bool CanSave = HandPage ? !B.m_vConfigs.empty() : !B.m_Roles.m_avTiles[ROLE_INSIDE].empty() && B.m_aConfigName[0];
	const float ButtonW = S(180.0f);
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(20.0f) - ButtonW * 2.0f - S(10.0f));
	if(Button(Tr("Cancel"), false, ButtonW) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		Close = true;
	ImGui::SameLine(0.0f, S(10.0f));
	ImGui::BeginDisabled(!CanSave);
	if(Button(Tr("Save the rules"), true, ButtonW))
	{
		SaveAutomapRules(HandPage ? nullptr : &Built);
		Close = true;
	}
	ImGui::EndDisabled();
	if(!CanSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", HandPage ? Tr("Add a rule set first") : Tr("Pick the inside tile first"));

	if(Close)
	{
		ImGui::CloseCurrentPopup();
		m_pAutomap = nullptr;
	}
	ImGui::EndPopup();
}

void CXcEditorUi::SaveAutomapRules(const SConfig *pConfig)
{
	CEditor &E = *m_pEditor;
	SAutomapBuilder &B = *m_pAutomap;
	// the wizard: replace the set with this name, keep every other set of the file
	if(pConfig)
	{
		auto It = std::find_if(B.m_vConfigs.begin(), B.m_vConfigs.end(), [&](const SConfig &Other) { return Other.m_Name == pConfig->m_Name; });
		if(It != B.m_vConfigs.end())
			*It = *pConfig;
		else
			B.m_vConfigs.push_back(*pConfig);
	}
	const std::string Name = pConfig ? pConfig->m_Name : (B.m_Config >= 0 && B.m_Config < (int)B.m_vConfigs.size() ? B.m_vConfigs[B.m_Config].m_Name : std::string());
	const std::string Text = Write(B.m_vConfigs);

	char aPath[IO_MAX_PATH_LENGTH];
	AutomapPath(B.m_aImageName, aPath, sizeof(aPath));
	E.Storage()->CreateFolder("editor", IStorage::TYPE_SAVE);
	E.Storage()->CreateFolder("editor/automap", IStorage::TYPE_SAVE);
	IOHANDLE File = E.Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
	{
		ShowMessage(Tr("Could not write the rules file."));
		return;
	}
	io_write(File, Text.data(), Text.size());
	io_close(File);

	// the picture reads its rules again; a tile layer with this picture gets the new set
	CEditorMap *pMap = E.Map();
	CEditorImage *pImage = pMap->m_vpImages[B.m_Image].get();
	pImage->m_Automapper.Unload();
	pImage->m_Automapper.Load(pImage->m_aName);
	int ConfigIndex = -1;
	for(int i = 0; i < pImage->m_Automapper.ConfigNamesNum(); i++)
		if(str_comp(pImage->m_Automapper.GetConfigName(i), Name.c_str()) == 0)
			ConfigIndex = i;
	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(pMap->SelectedLayerType(0, LAYERTYPE_TILES));
	if(pLayer && pLayer->m_Image == B.m_Image && ConfigIndex >= 0)
		pLayer->m_AutomapperConfig = ConfigIndex;
	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), Tr("Saved the rules \"%s\". Use them with the automapper of the layer or the smart fill"), Name.c_str());
	Toast(aMsg, TOAST_SUCCESS);
}

// --- the rules by hand: every set of the file, its runs and rules; a rule is a 5x5 grid of conditions around the cell

namespace
{
	enum
	{
		CELL_ANY = -1, // no condition
	};

	// the condition on a neighbor, or null
	SPos *FindPos(SRule &Rule, int dx, int dy)
	{
		for(SPos &Pos : Rule.m_vPos)
			if(Pos.m_X == dx && Pos.m_Y == dy)
				return &Pos;
		return nullptr;
	}

	// a short line about a rule for the list
	void RuleSummary(const SRule &Rule, char *pBuf, int Size)
	{
		const int Conditions = (int)Rule.m_vPos.size();
		char aChance[32] = "";
		if(Rule.m_Chance < 1.0f)
			str_format(aChance, sizeof(aChance), " · %d%%", (int)std::round(Rule.m_Chance * 100.0f));
		if(Conditions == 0)
			str_format(pBuf, Size, "%s%s", Tr("every block"), aChance);
		else
			str_format(pBuf, Size, "%d %s%s", Conditions, Conditions == 1 ? Tr("condition") : Tr("conditions"), aChance);
	}
} // namespace

void CXcEditorUi::RenderAutomapRules(unsigned long long TextureId, float LeftW, float MidW, float BodyH, float Gap)
{
	SAutomapBuilder &B = *m_pAutomap;
	const STheme &t = Theme();
	const ImTextureID Texture = (ImTextureID)TextureId;
	ImFont *pSmall = Fonts().m_pSmall;
	// a set changed by hand is no longer the wizard's (the wizard would build it again from its roles)
	const auto &&Touched = [&](SConfig &Config) {
		Config.m_vComments.erase(std::remove_if(Config.m_vComments.begin(), Config.m_vComments.end(), [](const std::string &Comment) {
			return str_startswith(Comment.c_str(), "exora-builder") || str_startswith(Comment.c_str(), "role ");
		}),
			Config.m_vComments.end());
	};

	B.m_Config = std::clamp(B.m_Config, 0, std::max((int)B.m_vConfigs.size() - 1, 0));
	SConfig *pConfig = B.m_vConfigs.empty() ? nullptr : &B.m_vConfigs[B.m_Config];
	if(pConfig)
	{
		if(pConfig->m_vRuns.empty())
			pConfig->m_vRuns.emplace_back();
		B.m_Run = std::clamp(B.m_Run, 0, (int)pConfig->m_vRuns.size() - 1);
		B.m_Rule = std::clamp(B.m_Rule, 0, std::max((int)pConfig->m_vRuns[B.m_Run].m_vRules.size() - 1, 0));
	}
	SRule *pRule = pConfig && !pConfig->m_vRuns[B.m_Run].m_vRules.empty() ? &pConfig->m_vRuns[B.m_Run].m_vRules[B.m_Rule] : nullptr;

	// --- the sets, runs and rules
	ImGui::BeginChild("##am_list", ImVec2(LeftW, BodyH), ImGuiChildFlags_None);
	{
		ParamLabel(Tr("Rule set"));
		const float W = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(W - ImGui::GetFrameHeight() * 2.0f - S(12.0f));
		if(ImGui::BeginCombo("##am_set", pConfig ? pConfig->m_Name.c_str() : Tr("No rule sets yet")))
		{
			for(int i = 0; i < (int)B.m_vConfigs.size(); i++)
			{
				ImGui::PushID(i);
				if(ImGui::Selectable(B.m_vConfigs[i].m_Name.c_str(), i == B.m_Config))
				{
					B.m_Config = i;
					B.m_Run = B.m_Rule = 0;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine(0.0f, S(6.0f));
		if(ImGui::Button("+##am_addset", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())))
		{
			SConfig New;
			for(int i = 1;; i++)
			{
				char aName[64];
				str_format(aName, sizeof(aName), "%s %d", Tr("New"), i);
				if(std::none_of(B.m_vConfigs.begin(), B.m_vConfigs.end(), [&](const SConfig &Other) { return Other.m_Name == aName; }))
				{
					New.m_Name = aName;
					break;
				}
			}
			New.m_vRuns.emplace_back();
			SRule Base; // the inside: every block gets tile 1
			New.m_vRuns[0].m_vRules.push_back(Base);
			B.m_vConfigs.push_back(New);
			B.m_Config = (int)B.m_vConfigs.size() - 1;
			B.m_Run = B.m_Rule = 0;
			return ImGui::EndChild(), void();
		}
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Tr("New rule set"));
		ImGui::SameLine(0.0f, S(6.0f));
		ImGui::BeginDisabled(!pConfig);
		if(ImGui::Button("x##am_delset", ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight())) && pConfig)
		{
			B.m_vConfigs.erase(B.m_vConfigs.begin() + B.m_Config);
			B.m_Config = std::max(B.m_Config - 1, 0);
			B.m_Run = B.m_Rule = 0;
			ImGui::EndDisabled();
			return ImGui::EndChild(), void();
		}
		ImGui::EndDisabled();
		if(ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", Tr("Delete this rule set"));
		if(pConfig)
		{
			// rename in place
			if(str_comp(B.m_aRename, pConfig->m_Name.c_str()) != 0 && !ImGui::IsAnyItemActive())
				str_copy(B.m_aRename, pConfig->m_Name.c_str());
			if(InputText("##am_rename", B.m_aRename, sizeof(B.m_aRename), Tr("Name"), -1.0f) && B.m_aRename[0])
				pConfig->m_Name = B.m_aRename;
		}
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));

		ImGui::BeginChild("##am_rules", ImVec2(0.0f, ImGui::GetContentRegionAvail().y - S(92.0f)));
		ImDrawList *pList = ImGui::GetWindowDrawList();
		if(pConfig)
		{
			for(int r = 0; r < (int)pConfig->m_vRuns.size(); r++)
			{
				SRun &Run = pConfig->m_vRuns[r];
				ImGui::PushID(r);
				char aRun[64];
				str_format(aRun, sizeof(aRun), "%s %d", Tr("Run"), r + 1);
				ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextMuted);
				ImGui::TextUnformatted(aRun);
				ImGui::PopStyleColor();
				// "NoLayerCopy": the run sees its own changes while it goes
				bool Live = !Run.m_Copy;
				ImGui::SameLine(LeftW - S(46.0f));
				if(Toggle("##am_live", &Live))
				{
					Run.m_Copy = !Live;
					Touched(*pConfig);
				}
				if(ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", Tr("On: the rules of this run see what earlier rules of the same run already changed (NoLayerCopy)"));
				const float RowH = S(36.0f);
				for(int i = 0; i < (int)Run.m_vRules.size(); i++)
				{
					const SRule &Rule = Run.m_vRules[i];
					ImGui::PushID(i);
					const ImVec2 Min = ImGui::GetCursorScreenPos();
					const float RowW = ImGui::GetContentRegionAvail().x;
					if(ImGui::InvisibleButton("##rule", ImVec2(RowW, RowH)))
					{
						B.m_Run = r;
						B.m_Rule = i;
						B.m_CellX = B.m_CellY = 0;
					}
					const bool Selected = r == B.m_Run && i == B.m_Rule;
					if(Selected || ImGui::IsItemHovered())
						pList->AddRectFilled(Min, ImVec2(Min.x + RowW, Min.y + RowH), Col(Selected ? t.m_Accent : t.m_CardHover, Selected ? 0.18f : 1.0f), S(8.0f));
					const float Thumb = S(26.0f);
					const ImVec2 ThumbMin(Min.x + S(6.0f), Min.y + (RowH - Thumb) * 0.5f);
					pList->AddRectFilled(ThumbMin, ImVec2(ThumbMin.x + Thumb, ThumbMin.y + Thumb), Col(t.m_Track), S(4.0f));
					DrawTile(pList, Texture, Rule.m_Id, Rule.m_Flags, ThumbMin, Thumb);
					char aLine[64], aSummary[96];
					str_format(aLine, sizeof(aLine), "%s %d", Tr("Tile"), Rule.m_Id);
					RuleSummary(Rule, aSummary, sizeof(aSummary));
					pList->AddText(ImVec2(ThumbMin.x + Thumb + S(10.0f), Min.y + S(3.0f)), Col(Selected ? t.m_Text : t.m_TextSecondary), aLine);
					pList->AddText(pSmall, pSmall->FontSize, ImVec2(ThumbMin.x + Thumb + S(10.0f), Min.y + RowH - pSmall->FontSize - S(4.0f)), Col(t.m_TextMuted), aSummary);
					ImGui::PopID();
				}
				ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		// the buttons for the list
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		const float ButtonW = (W - S(12.0f)) / 3.0f;
		ImGui::BeginDisabled(!pConfig);
		if(Button(Tr("+ Rule"), true, ButtonW) && pConfig)
		{
			std::vector<SRule> &vRules = pConfig->m_vRuns[B.m_Run].m_vRules;
			SRule New;
			if(pRule)
				New.m_Id = pRule->m_Id;
			const int At = vRules.empty() ? 0 : B.m_Rule + 1;
			vRules.insert(vRules.begin() + At, New);
			B.m_Rule = At;
			B.m_CellX = B.m_CellY = 0;
			Touched(*pConfig);
			pRule = nullptr;
		}
		ImGui::SameLine(0.0f, S(6.0f));
		if(Button(Tr("+ Run"), false, ButtonW) && pConfig)
		{
			pConfig->m_vRuns.insert(pConfig->m_vRuns.begin() + B.m_Run + 1, SRun());
			B.m_Run++;
			B.m_Rule = 0;
			Touched(*pConfig);
			pRule = nullptr;
		}
		ImGui::SameLine(0.0f, S(6.0f));
		if(Button(Tr("Copy"), false, ButtonW) && pRule)
		{
			std::vector<SRule> &vRules = pConfig->m_vRuns[B.m_Run].m_vRules;
			const SRule Copy = *pRule;
			vRules.insert(vRules.begin() + B.m_Rule + 1, Copy);
			B.m_Rule++;
			Touched(*pConfig);
			pRule = nullptr;
		}
		if(Button(Tr("Up"), false, ButtonW) && pRule && B.m_Rule > 0)
		{
			std::vector<SRule> &vRules = pConfig->m_vRuns[B.m_Run].m_vRules;
			std::swap(vRules[B.m_Rule], vRules[B.m_Rule - 1]);
			B.m_Rule--;
			Touched(*pConfig);
			pRule = nullptr;
		}
		ImGui::SameLine(0.0f, S(6.0f));
		if(Button(Tr("Down"), false, ButtonW) && pRule && B.m_Rule + 1 < (int)pConfig->m_vRuns[B.m_Run].m_vRules.size())
		{
			std::vector<SRule> &vRules = pConfig->m_vRuns[B.m_Run].m_vRules;
			std::swap(vRules[B.m_Rule], vRules[B.m_Rule + 1]);
			B.m_Rule++;
			Touched(*pConfig);
			pRule = nullptr;
		}
		ImGui::SameLine(0.0f, S(6.0f));
		if(Button(Tr("Delete"), false, ButtonW) && pConfig)
		{
			std::vector<SRule> &vRules = pConfig->m_vRuns[B.m_Run].m_vRules;
			if(!vRules.empty())
				vRules.erase(vRules.begin() + B.m_Rule);
			else if(pConfig->m_vRuns.size() > 1)
				pConfig->m_vRuns.erase(pConfig->m_vRuns.begin() + B.m_Run); // an empty run goes away
			B.m_Rule = std::max(B.m_Rule - 1, 0);
			Touched(*pConfig);
			pRule = nullptr;
		}
		ImGui::EndDisabled();
	}
	ImGui::EndChild();

	// --- the selected rule
	ImGui::SameLine(0.0f, Gap);
	ImGui::BeginChild("##am_rule", ImVec2(MidW, BodyH), ImGuiChildFlags_None);
	if(!pRule)
	{
		Description(pConfig ? Tr("This run has no rules yet: add one with \"+ Rule\".") : Tr("Make a rule set with \"+\" on the left, or build one with the wizard."));
		ImGui::EndChild();
		return;
	}
	{
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const float W = ImGui::GetContentRegionAvail().x;
		// the conditions: 5x5 cells around the tile, click to go through the kinds
		const float GridCell = std::floor(std::min(S(52.0f), (W * 0.42f) / 5.0f));
		const ImVec2 GridMin = ImGui::GetCursorScreenPos();
		ImGui::BeginGroup();
		ParamLabel(Tr("Conditions around the tile"));
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		for(int dy = -2; dy <= 2; dy++)
		{
			for(int dx = -2; dx <= 2; dx++)
			{
				const ImVec2 a(Min.x + (dx + 2) * GridCell, Min.y + (dy + 2) * GridCell);
				const ImVec2 b(a.x + GridCell - S(3.0f), a.y + GridCell - S(3.0f));
				ImGui::SetCursorScreenPos(a);
				ImGui::PushID((dy + 2) * 5 + dx + 2);
				ImGui::InvisibleButton("##cell", ImVec2(GridCell - S(3.0f), GridCell - S(3.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				const bool Hovered = ImGui::IsItemHovered();
				const bool Center = dx == 0 && dy == 0;
				SPos *pPos = FindPos(*pRule, dx, dy);
				if(Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					// any -> empty -> block -> these tiles -> not these tiles -> any
					if(!pPos)
					{
						SPos New;
						New.m_X = dx;
						New.m_Y = dy;
						New.m_Type = SPos::EMPTY;
						pRule->m_vPos.push_back(New);
					}
					else if(pPos->m_Type == SPos::EMPTY)
						pPos->m_Type = SPos::FULL;
					else if(pPos->m_Type == SPos::FULL)
					{
						pPos->m_Type = SPos::INDEX;
						if(pPos->m_vIndices.empty())
							pPos->m_vIndices.push_back({pRule->m_Id, 0, false});
					}
					else if(pPos->m_Type == SPos::INDEX)
						pPos->m_Type = SPos::NOTINDEX;
					else
						pRule->m_vPos.erase(pRule->m_vPos.begin() + (pPos - pRule->m_vPos.data()));
					B.m_CellX = dx;
					B.m_CellY = dy;
					Touched(*pConfig);
					pPos = FindPos(*pRule, dx, dy);
				}
				if(Hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && pPos)
				{
					pRule->m_vPos.erase(pRule->m_vPos.begin() + (pPos - pRule->m_vPos.data()));
					Touched(*pConfig);
					pPos = nullptr;
				}
				const bool Picked = B.m_CellX == dx && B.m_CellY == dy && pPos && (pPos->m_Type == SPos::INDEX || pPos->m_Type == SPos::NOTINDEX);
				// the look of the cell
				pDraw->AddRectFilled(a, b, Col(t.m_Track, pPos ? 1.0f : 0.45f), S(6.0f));
				if(Center && !pPos)
				{
					DrawTile(pDraw, Texture, pRule->m_Id, pRule->m_Flags, ImVec2(a.x + S(4.0f), a.y + S(4.0f)), GridCell - S(11.0f));
					pDraw->AddRect(a, b, Col(t.m_Accent), S(6.0f), 0, S(2.0f));
				}
				else if(pPos)
				{
					const char *pLabel = "";
					ImU32 LabelColor = Col(t.m_Text);
					switch(pPos->m_Type)
					{
					case SPos::EMPTY:
						pDraw->AddRect(ImVec2(a.x + S(6.0f), a.y + S(6.0f)), ImVec2(b.x - S(6.0f), b.y - S(6.0f)), Col(t.m_TextMuted), S(4.0f), 0, S(1.5f));
						pLabel = Tr("empty");
						break;
					case SPos::FULL:
						pDraw->AddRectFilled(ImVec2(a.x + S(6.0f), a.y + S(6.0f)), ImVec2(b.x - S(6.0f), b.y - S(6.0f)), Col(t.m_TextSecondary, 0.85f), S(4.0f));
						pLabel = Tr("block");
						LabelColor = Col(t.m_Card);
						break;
					default:
					{
						const int Shown = std::min((int)pPos->m_vIndices.size(), 4);
						const float Small = (GridCell - S(11.0f)) / (Shown > 1 ? 2.0f : 1.0f);
						for(int i = 0; i < Shown; i++)
							DrawTile(pDraw, Texture, pPos->m_vIndices[i].m_Id, pPos->m_vIndices[i].m_Flags, ImVec2(a.x + S(4.0f) + (i % 2) * Small, a.y + S(4.0f) + (i / 2) * Small), Small);
						if(pPos->m_Type == SPos::NOTINDEX)
							pDraw->AddLine(ImVec2(a.x + S(4.0f), b.y - S(4.0f)), ImVec2(b.x - S(4.0f), a.y + S(4.0f)), IM_COL32(255, 77, 77, 230), S(2.5f));
						pLabel = pPos->m_Type == SPos::INDEX ? Tr("is") : Tr("is not");
						break;
					}
					}
					const ImVec2 TextSize = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pLabel);
					pDraw->AddText(pSmall, pSmall->FontSize, ImVec2((a.x + b.x - TextSize.x) * 0.5f, b.y - pSmall->FontSize - S(1.0f)), LabelColor, pLabel);
					if(Center)
						pDraw->AddRect(a, b, Col(t.m_Accent), S(6.0f), 0, S(2.0f));
				}
				if(Picked)
					pDraw->AddRect(ImVec2(a.x - S(2.0f), a.y - S(2.0f)), ImVec2(b.x + S(2.0f), b.y + S(2.0f)), IM_COL32(255, 255, 255, 220), S(7.0f), 0, S(2.0f));
				if(Hovered)
				{
					pDraw->AddRect(a, b, IM_COL32(255, 255, 255, 120), S(6.0f));
					ImGui::SetTooltip("%s", Tr("Click: any, empty, block, these tiles, not these tiles. Right click: any"));
				}
				ImGui::PopID();
			}
		}
		ImGui::SetCursorScreenPos(ImVec2(Min.x, Min.y + GridCell * 5.0f));
		ImGui::Dummy(ImVec2(GridCell * 5.0f, 0.0f));
		ImGui::EndGroup();

		// the result and the options, next to the grid
		ImGui::SameLine(0.0f, S(20.0f));
		ImGui::BeginGroup();
		{
			const float OptW = W - GridCell * 5.0f - S(20.0f);
			ImGui::BeginChild("##am_opts", ImVec2(OptW, GridCell * 5.0f + ImGui::GetFrameHeight()), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
			ParamLabel(Tr("Puts this tile"));
			const float Big = S(56.0f);
			const ImVec2 TileMin = ImGui::GetCursorScreenPos();
			ImDrawList *pOpt = ImGui::GetWindowDrawList();
			pOpt->AddRectFilled(TileMin, ImVec2(TileMin.x + Big, TileMin.y + Big), Col(t.m_Track), S(6.0f));
			DrawTile(pOpt, Texture, pRule->m_Id, pRule->m_Flags, TileMin, Big);
			ImGui::Dummy(ImVec2(Big, Big));
			ImGui::SameLine(0.0f, S(10.0f));
			ImGui::BeginGroup();
			const struct
			{
				const char *m_pLabel;
				int m_Flag;
			} s_aFlags[] = {{"Mirror left-right", FLAG_XFLIP}, {"Mirror up-down", FLAG_YFLIP}, {"Turn 90°", FLAG_ROTATE}};
			for(const auto &Flag : s_aFlags)
			{
				bool On = pRule->m_Flags & Flag.m_Flag;
				if(ImGui::Checkbox(Tr(Flag.m_pLabel), &On))
				{
					pRule->m_Flags = On ? pRule->m_Flags | Flag.m_Flag : pRule->m_Flags & ~Flag.m_Flag;
					Touched(*pConfig);
				}
			}
			ImGui::EndGroup();
			int Chance = (int)std::round(pRule->m_Chance * 100.0f);
			if(SliderInt(Tr("Chance"), &Chance, 1, 100, "%d%%"))
			{
				pRule->m_Chance = Chance / 100.0f;
				Touched(*pConfig);
			}
			bool OnEmpty = pRule->m_NoDefault;
			ImGui::TextUnformatted(Tr("Also on empty cells"));
			ImGui::SameLine(OptW - S(48.0f));
			if(Toggle("##am_noddefault", &OnEmpty))
			{
				pRule->m_NoDefault = OnEmpty;
				Touched(*pConfig);
			}
			if(!pRule->m_vModulo.empty())
				Description(Tr("This rule also has a repeating pattern (Modulo), it is kept as it is."));
			ImGui::EndChild();
		}
		ImGui::EndGroup();

		// the tileset: pick the result, or the tiles of the chosen condition
		SPos *pPicked = FindPos(*pRule, B.m_CellX, B.m_CellY);
		const bool PickForCell = pPicked && (pPicked->m_Type == SPos::INDEX || pPicked->m_Type == SPos::NOTINDEX) && !(B.m_CellX == 0 && B.m_CellY == 0 && false);
		ImGui::Dummy(ImVec2(0.0f, S(8.0f)));
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextMuted);
		ImGui::TextWrapped("%s", PickForCell ? Tr("Click tiles to add them to the chosen condition, again to take them out.") : Tr("Click a tile: the rule puts it. Choose a condition cell set to \"is\" / \"is not\" to pick its tiles instead."));
		ImGui::PopStyleColor();
		const float Side = std::min(W, ImGui::GetContentRegionAvail().y - S(4.0f));
		const float Cell = Side / 16.0f;
		const ImVec2 TMin = ImGui::GetCursorScreenPos();
		const ImVec2 TMax(TMin.x + Side, TMin.y + Side);
		pDraw = ImGui::GetWindowDrawList();
		pDraw->AddRectFilled(TMin, TMax, Col(t.m_Track));
		pDraw->AddImage(Texture, TMin, TMax);
		// the result framed, the condition's tiles marked
		const auto &&Mark = [&](int Id, ImU32 Color) {
			const ImVec2 a(TMin.x + (Id % 16) * Cell, TMin.y + (Id / 16) * Cell);
			pDraw->AddRect(a, ImVec2(a.x + Cell, a.y + Cell), Color, 0.0f, 0, S(2.0f));
		};
		Mark(pRule->m_Id, Col(t.m_Accent));
		if(PickForCell)
			for(const SIndex &Index : pPicked->m_vIndices)
				Mark(Index.m_Id, IM_COL32(255, 255, 255, 230));
		ImGui::InvisibleButton("##am_tiles", ImVec2(Side, Side));
		if(ImGui::IsItemHovered())
		{
			const ImVec2 Mouse = ImGui::GetIO().MousePos;
			const int Id = std::clamp((int)((Mouse.y - TMin.y) / Cell), 0, 15) * 16 + std::clamp((int)((Mouse.x - TMin.x) / Cell), 0, 15);
			Mark(Id, IM_COL32(255, 255, 255, 140));
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			ImGui::SetTooltip("%s %d", Tr("Tile"), Id);
			if(ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				if(PickForCell)
				{
					auto It = std::find_if(pPicked->m_vIndices.begin(), pPicked->m_vIndices.end(), [&](const SIndex &Index) { return Index.m_Id == Id; });
					if(It != pPicked->m_vIndices.end())
					{
						if(pPicked->m_vIndices.size() > 1)
							pPicked->m_vIndices.erase(It);
					}
					else
						pPicked->m_vIndices.push_back({Id, 0, false});
				}
				else if(Id != 0)
					pRule->m_Id = Id;
				Touched(*pConfig);
			}
		}
		(void)GridMin;
	}
	ImGui::EndChild();
}

void CXcEditorUi::GuessAutomapRoles()
{
	CEditor &E = *m_pEditor;
	SAutomapBuilder &B = *m_pAutomap;
	CEditorImage *pImage = E.Map()->m_vpImages[B.m_Image].get();
	// the pixels: the editor keeps them, else the picture from the game files
	CImageInfo Loaded;
	const unsigned char *pRgba = nullptr;
	int Width = 0, Height = 0;
	if(pImage->m_pData && pImage->m_Format == CImageInfo::FORMAT_RGBA)
	{
		pRgba = pImage->m_pData;
		Width = pImage->m_Width;
		Height = pImage->m_Height;
	}
	else
	{
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "mapres/%s.png", pImage->m_aName);
		if(E.Graphics()->LoadPng(Loaded, aPath, IStorage::TYPE_ALL) && Loaded.m_Format == CImageInfo::FORMAT_RGBA)
		{
			pRgba = Loaded.m_pData;
			Width = Loaded.m_Width;
			Height = Loaded.m_Height;
		}
	}
	if(!pRgba)
	{
		ShowMessage(Tr("Could not read the pixels of this picture."), Tr("Automapper builder"));
		return;
	}
	B.m_GuessUndo = B.m_Roles;
	SRoles Guess;
	const int Found = GuessRoles(pRgba, Width, Height, Guess);
	Loaded.Free();
	if(Found == 0)
	{
		Toast(Tr("No roles found: pick the tiles by hand"), TOAST_ERROR);
		return;
	}
	B.m_Roles = Guess;
	B.m_CanUndoGuess = true;
	B.m_Role = ROLE_INSIDE;
	char aMsg[128];
	str_format(aMsg, sizeof(aMsg), Tr("Guessed %d of %d roles: check them in the preview"), Found, (int)NUM_ROLES);
	Toast(aMsg, TOAST_SUCCESS);
}
