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

	const float FooterH = S(48.0f);
	const float BodyH = ImGui::GetContentRegionAvail().y - FooterH;
	const float LeftW = S(300.0f);
	const float RightW = std::min(S(470.0f), ImGui::GetContentRegionAvail().x * 0.36f);
	const float Gap = S(16.0f);
	const float MidW = ImGui::GetContentRegionAvail().x - LeftW - RightW - Gap * 2.0f;

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
		ImGui::BeginDisabled(B.m_Roles.m_avTiles[B.m_Role].empty());
		if(Button(Tr("Clear this role"), false, -1.0f))
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

	// --- the preview
	const SConfig Config = Build(B.m_Roles, B.m_aConfigName, B.m_Derive);
	ImGui::SameLine(0.0f, Gap);
	ImGui::BeginChild("##preview", ImVec2(RightW, BodyH), ImGuiChildFlags_None);
	{
		ParamLabel(Tr("Preview"));
		const float Cell = std::floor(std::min(RightW / PREVIEW_W, (BodyH - S(140.0f)) / PREVIEW_H));
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + Cell * PREVIEW_W, Min.y + Cell * PREVIEW_H);
		pDraw = ImGui::GetWindowDrawList();
		pDraw->AddRectFilled(Min, Max, Col(t.m_Track), S(6.0f));
		std::vector<int> vIndices(B.m_vShape), vFlags;
		Apply(Config, PREVIEW_W, PREVIEW_H, vIndices, &vFlags, B.m_Seed);
		for(int y = 0; y < PREVIEW_H; y++)
		{
			for(int x = 0; x < PREVIEW_W; x++)
			{
				const int Cell0 = y * PREVIEW_W + x;
				const ImVec2 a(Min.x + x * Cell, Min.y + y * Cell);
				if(B.m_vShape[Cell0] && vIndices[Cell0] == 1 && B.m_Roles.m_avTiles[ROLE_INSIDE].empty())
					pDraw->AddRectFilled(a, ImVec2(a.x + Cell, a.y + Cell), Col(t.m_TextMuted, 0.5f)); // a block no rule draws yet
				else
					DrawTile(pDraw, Texture, vIndices[Cell0], vFlags[Cell0], a, Cell);
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
	const bool CanSave = !B.m_Roles.m_avTiles[ROLE_INSIDE].empty() && B.m_aConfigName[0];
	const float ButtonW = S(180.0f);
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(20.0f) - ButtonW * 2.0f - S(10.0f));
	if(Button(Tr("Cancel"), false, ButtonW) || ImGui::IsKeyPressed(ImGuiKey_Escape))
		Close = true;
	ImGui::SameLine(0.0f, S(10.0f));
	ImGui::BeginDisabled(!CanSave);
	if(Button(Tr("Save the rules"), true, ButtonW))
	{
		SaveAutomapRules(Config);
		Close = true;
	}
	ImGui::EndDisabled();
	if(!CanSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("%s", Tr("Pick the inside tile first"));

	if(Close)
	{
		ImGui::CloseCurrentPopup();
		m_pAutomap = nullptr;
	}
	ImGui::EndPopup();
}

void CXcEditorUi::SaveAutomapRules(const SConfig &Config)
{
	CEditor &E = *m_pEditor;
	SAutomapBuilder &B = *m_pAutomap;
	// replace the set with this name, keep every other set of the file
	auto It = std::find_if(B.m_vConfigs.begin(), B.m_vConfigs.end(), [&](const SConfig &Other) { return Other.m_Name == Config.m_Name; });
	if(It != B.m_vConfigs.end())
		*It = Config;
	else
		B.m_vConfigs.push_back(Config);
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
		if(str_comp(pImage->m_Automapper.GetConfigName(i), Config.m_Name.c_str()) == 0)
			ConfigIndex = i;
	std::shared_ptr<CLayerTiles> pLayer = std::static_pointer_cast<CLayerTiles>(pMap->SelectedLayerType(0, LAYERTYPE_TILES));
	if(pLayer && pLayer->m_Image == B.m_Image && ConfigIndex >= 0)
		pLayer->m_AutomapperConfig = ConfigIndex;
	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), Tr("Saved the rules \"%s\". Use them with the automapper of the layer or the smart fill"), Config.m_Name.c_str());
	Toast(aMsg, TOAST_SUCCESS);
}
