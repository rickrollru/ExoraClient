// The home page: your tee on a stage with everything it wears, play and the quick buttons; the music as a wide
// bar, friends online, what is new in the client (from its GitHub releases) and small link icons.
#include "feedback.h"
#include "icons.h"
#include "lang.h"
#include "menu.h"
#include "theme.h"
#include "widgets.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/external/imgui/imgui.h>
#include <engine/external/imgui/imgui_internal.h>
#include <engine/external/json-parser/json.h>
#include <engine/friends.h>
#include <engine/http.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>

#include <game/client/gameclient.h>
#include <game/version.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

// what is new when the releases can not be read (offline): the main points of this version
static const char *const s_aaBuiltinUpdates[][4] = {
	{"Shared cosmetics", "Other Exora players see your cape, scarf, accessory, shader and neon, and you see theirs", "Общая косметика", "Другие игроки Exora видят твой плащ, шарф, аксессуар, шейдер и неон, а ты — их"},
	{"New home page", "Your tee on a stage, music, friends, what is new and how many play Exora now", "Новая главная", "Твой ти на сцене, музыка, друзья, что нового и сколько сейчас в Exora"},
	{"Tee shader by parts", "The pattern on the tee, the weapon or the hook, each on its own", "Шейдер на ти по частям", "Узор на ти, оружии или хуке — по отдельности"},
};

// a square button with an icon (the style of the interface: a rounded square, never a circle)
bool CXcMenu::IconSquare(const char *pId, const char *pIcon, float Size, const char *pTip, int Kind)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Size, Size));
	const bool Hovered = ImGui::IsItemHovered();
	const float Hover = Animate(ImGui::GetID(pId) + 1, Hovered ? 1.0f : 0.0f, 16.0f);
	const ImVec2 Max(Pos.x + Size, Pos.y + Size);
	// 0 plain, 1 Discord blue, 2 the accent (running)
	const ImVec4 Discord(0.345f, 0.396f, 0.949f, 1.0f);
	const ImVec4 Green(0.24f, 0.84f, 0.64f, 1.0f);
	const ImVec4 Tint = Kind == 1 ? Discord : (Kind == 2 ? Green : t.m_Accent);
	pDraw->AddRectFilled(Pos, Max, Col(Mix(t.m_Track, Tint, (Kind ? 0.18f : 0.0f) + 0.14f * Hover)), S(10.0f));
	pDraw->AddRect(Pos, Max, Col(Mix(t.m_Border, Tint, Kind ? 0.5f + 0.5f * Hover : Hover)), S(10.0f), 0, 1.0f);
	DrawIcon(pDraw, Fonts().m_pLabel, Size * 0.42f, ImVec2(Pos.x + Size * 0.5f, Pos.y + Size * 0.5f), Col(Kind ? Mix(Tint, ImVec4(1, 1, 1, 1), 0.45f) : Mix(t.m_TextSecondary, t.m_Text, Hover)), pIcon);
	if(Hovered && pTip)
		ImGui::SetTooltip("%s", pTip);
	if(Clicked)
		PlayUiSound(SOUND_CLICK);
	return Clicked;
}

void CXcMenu::RenderHome()
{
	if(!m_ServersRequested)
	{
		// the server list also tells which friends are online
		m_ServersRequested = true;
		ServerBrowser()->Refresh(IServerBrowser::TYPE_INTERNET);
	}
	UpdateClientUpdates();

	const ImVec2 Avail = ImGui::GetContentRegionAvail();
	const float Gap = S(16.0f);
	const float LeftWidth = std::min(S(440.0f), Avail.x * 0.4f);
	const float RightWidth = Avail.x - LeftWidth - Gap;
	const ImVec2 Start = ImGui::GetCursorScreenPos();

	ImGui::BeginChild("##home_stage", ImVec2(LeftWidth, Avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	RenderHomeStage(LeftWidth, Avail.y);
	ImGui::EndChild();

	ImGui::SetCursorScreenPos(ImVec2(Start.x + LeftWidth + Gap, Start.y));
	ImGui::BeginChild("##home_side", ImVec2(RightWidth, Avail.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	// the blocks are placed by exact positions so the footer always stays on the screen
	CXcMusic &Music = GameClient()->m_XcMusic;
	const bool HasTrack = g_Config.m_XcMusic && Music.Supported() && Music.HasTrack();
	const float MusicHeight = HasTrack ? S(112.0f) : S(56.0f);
	const float FriendsHeight = S(74.0f);
	const float FooterHeight = S(48.0f);
	const float InnerGap = S(14.0f);
	const ImVec2 Side = ImGui::GetCursorScreenPos();
	float y = Side.y;
	ImGui::SetCursorScreenPos(ImVec2(Side.x, y));
	RenderMusicBar(RightWidth, MusicHeight);
	y += MusicHeight + InnerGap;
	ImGui::SetCursorScreenPos(ImVec2(Side.x, y));
	RenderHomeFriends(RightWidth, FriendsHeight);
	y += FriendsHeight + InnerGap;
	const float RestHeight = std::max(Side.y + Avail.y - FooterHeight - InnerGap - y, S(120.0f));
	ImGui::SetCursorScreenPos(ImVec2(Side.x, y));
	// on a server the map card is more useful than the news
	if(IsOnline() && g_Config.m_XcMenuMapInfo)
	{
		ImGui::BeginChild("##home_map", ImVec2(RightWidth, RestHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		BeginCardGrid("##home_map_grid", 1);
		RenderMapInfoCard();
		EndCardGrid();
		ImGui::EndChild();
	}
	else
		RenderWhatsNew(RightWidth, RestHeight);
	y += RestHeight + InnerGap;
	ImGui::SetCursorScreenPos(ImVec2(Side.x, y));
	RenderHomeFooter(RightWidth, FooterHeight);
	ImGui::EndChild();

	if(m_WantClassic)
	{
		m_WantClassic = false;
		OpenClassicMenu();
	}
}

// your tee on a lit stage, with what it wears as small plates, play and the quick buttons
void CXcMenu::RenderHomeStage(float Width, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	PanelRect(pDraw, Min, Max, S(14.0f), t.m_Card);

	const float Pad = S(22.0f);
	const auto OpenLook = [&]() {
		m_Page = PAGE_VISUALS;
		m_VisualsSection = VISUALS_COSMETICS;
	};

	// what is worn: plain plates, all alike
	std::vector<const char *> vWorn;
	static const char *const s_apAccessories[] = {"", "Cone hat", "Halo", "Crown", "Horns", "Sprout", "Stars", "Flame", "Bow", "Sign", "Cross"};
	if(g_Config.m_XcAccessory > 0 && g_Config.m_XcAccessory < (int)std::size(s_apAccessories) && g_Config.m_XcAccessorySelf)
		vWorn.push_back(Tr(s_apAccessories[g_Config.m_XcAccessory]));
	if(g_Config.m_XcHat && g_Config.m_XcHatSelf)
		vWorn.push_back(Tr("Own hat"));
	if(g_Config.m_XcCape && g_Config.m_XcCapeSelf)
		vWorn.push_back(Tr("Cape"));
	if(g_Config.m_XcScarf && g_Config.m_XcScarfSelf)
		vWorn.push_back(Tr("Scarf"));
	static const char *const s_apShaders[] = {"", "Gradient", "Rainbow", "Galaxy", "Waves", "Chrome"};
	char aShader[64] = "";
	if(g_Config.m_XcTeeShader > 0 && g_Config.m_XcTeeShader < (int)std::size(s_apShaders) && g_Config.m_XcTeeShaderSelf)
	{
		str_format(aShader, sizeof(aShader), "%s · %s", Tr("Shader"), Tr(s_apShaders[g_Config.m_XcTeeShader]));
		vWorn.push_back(aShader);
	}
	if(g_Config.m_XcGlow && g_Config.m_XcGlowSelf)
		vWorn.push_back(Tr("Neon outline"));

	// the plates wrap into rows; the block of the name and the plates sits right over the buttons and grows upwards
	ImFont *pChip = Fonts().m_pSmall;
	const float ChipHeight = pChip->FontSize + S(10.0f);
	const float ChipGap = S(6.0f);
	std::vector<ImVec2> vChipPos(vWorn.size());
	std::vector<float> vChipWidth(vWorn.size());
	int Rows = 1;
	{
		float x = 0.0f;
		for(size_t i = 0; i < vWorn.size(); i++)
		{
			vChipWidth[i] = pChip->CalcTextSizeA(pChip->FontSize, FLT_MAX, 0.0f, vWorn[i]).x + S(18.0f);
			if(x > 0.0f && x + vChipWidth[i] > Width - Pad * 2.0f)
			{
				x = 0.0f;
				Rows++;
			}
			vChipPos[i] = ImVec2(x, (Rows - 1) * (ChipHeight + ChipGap));
			x += vChipWidth[i] + ChipGap;
		}
	}
	const float RowHeight = S(54.0f);
	const float RowY = Max.y - Pad - RowHeight;
	const float ChipsHeight = Rows * ChipHeight + (Rows - 1) * ChipGap;
	const float ChipsY = RowY - S(18.0f) - ChipsHeight;
	ImFont *pName = Fonts().m_pHeader;
	const float NameY = ChipsY - S(10.0f) - pName->FontSize;

	// the stage takes the rest: the tee with its cape, hat, accessory, neon and shader. Not bigger than the skin
	// picture allows (its body is about 96 pixels): stretched further it gets blurry
	const float StageHeight = std::max(NameY - Min.y - S(4.0f), S(120.0f));
	const ImVec2 StageCenter((Min.x + Max.x) * 0.5f, Min.y + StageHeight * 0.5f + S(10.0f));
	const float TeeBox = std::min({Width * 0.6f, StageHeight * 0.75f, 96.0f * 1.7f / 0.62f});
	const ImVec2 TeeMin(StageCenter.x - TeeBox * 0.5f, StageCenter.y - TeeBox * 0.5f);
	AddTeePreview(pDraw, TeeMin, ImVec2(TeeMin.x + TeeBox, TeeMin.y + TeeBox));

	// the look of the tee: a small square button in the corner
	ImGui::SetCursorScreenPos(ImVec2(Max.x - S(14.0f) - S(40.0f), Min.y + S(14.0f)));
	if(IconSquare("##look", XC_ICON_BRUSH, S(40.0f), Tr("Look of your tee")))
		OpenLook();

	// how many Exora clients run now: a plate in the other corner, same height as the button
	const int Online = GameClient()->m_XcCosmetics.Online();
	if(Online >= 0)
	{
		char aOnline[64];
		str_format(aOnline, sizeof(aOnline), "%d", Online);
		const char *pLabel = Tr("in Exora now");
		ImFont *pBold = Fonts().m_pTitle;
		ImFont *pText = Fonts().m_pSmall;
		const float Height = S(40.0f);
		const float NumberWidth = pBold->CalcTextSizeA(pBold->FontSize, FLT_MAX, 0.0f, aOnline).x;
		const float LabelWidth = pText->CalcTextSizeA(pText->FontSize, FLT_MAX, 0.0f, pLabel).x;
		const ImVec2 PMin(Min.x + S(14.0f), Min.y + S(14.0f));
		const ImVec2 PMax(PMin.x + S(14.0f) + S(8.0f) + S(8.0f) + NumberWidth + S(6.0f) + LabelWidth + S(12.0f), PMin.y + Height);
		const ImVec4 Mint(0.24f, 0.84f, 0.64f, 1.0f);
		pDraw->AddRectFilled(PMin, PMax, Col(t.m_Track), S(10.0f));
		pDraw->AddRect(PMin, PMax, Col(t.m_Border), S(10.0f), 0, 1.0f);
		// a calm "live" dot: it breathes a little, no glow around
		const float Breath = 0.75f + 0.25f * std::sin(Client()->GlobalTime() * 2.2f);
		const ImVec2 Dot(PMin.x + S(14.0f) + S(4.0f), (PMin.y + PMax.y) * 0.5f);
		pDraw->AddRectFilled(ImVec2(Dot.x - S(4.0f), Dot.y - S(4.0f)), ImVec2(Dot.x + S(4.0f), Dot.y + S(4.0f)), Col(Mint, Breath), S(2.5f));
		const float TextX = Dot.x + S(4.0f) + S(8.0f);
		const float MidY = (PMin.y + PMax.y) * 0.5f;
		pDraw->AddText(pBold, pBold->FontSize, ImVec2(TextX, std::floor(MidY - TextMiddle(pBold, pBold->FontSize) + 0.5f)), Col(t.m_Text), aOnline);
		pDraw->AddText(pText, pText->FontSize, ImVec2(TextX + NumberWidth + S(6.0f), std::floor(MidY - TextMiddle(pText, pText->FontSize) + 0.5f)), Col(t.m_TextSecondary), pLabel);
		if(ImGui::IsMouseHoveringRect(PMin, PMax))
			ImGui::SetTooltip("%s", Tr("Exora clients running right now, in the game and in the menu"));
	}

	pDraw->AddText(pName, pName->FontSize, ImVec2(Min.x + Pad, NameY), Col(t.m_Text), Client()->PlayerName());
	if(vWorn.empty())
		pDraw->AddText(pChip, pChip->FontSize, ImVec2(Min.x + Pad, ChipsY + S(5.0f)), Col(t.m_TextMuted), Tr("Nothing worn yet: dress up in Visuals"));
	for(size_t i = 0; i < vWorn.size(); i++)
	{
		const ImVec2 P(Min.x + Pad + vChipPos[i].x, ChipsY + vChipPos[i].y);
		ImGui::SetCursorScreenPos(P);
		char aId[16];
		str_format(aId, sizeof(aId), "##worn%d", (int)i);
		if(ImGui::InvisibleButton(aId, ImVec2(vChipWidth[i], ChipHeight)))
			OpenLook();
		const bool Hovered = ImGui::IsItemHovered();
		pDraw->AddRectFilled(P, ImVec2(P.x + vChipWidth[i], P.y + ChipHeight), Col(t.m_Text, Hovered ? 0.1f : 0.06f), S(6.0f));
		pDraw->AddText(pChip, pChip->FontSize, ImVec2(P.x + S(9.0f), std::floor(P.y + ChipHeight * 0.5f - TextMiddle(pChip, pChip->FontSize) + 0.5f)), Col(Hovered ? t.m_Text : t.m_TextSecondary), vWorn[i]);
	}

	// play, the map editor and the local server
	const float IconGap = S(10.0f);
	const float PlayWidth = Width - Pad * 2.0f - (RowHeight + IconGap) * 2.0f;
	ImGui::SetCursorScreenPos(ImVec2(Min.x + Pad, RowY));
	const bool Clicked = ImGui::InvisibleButton("##home_play", ImVec2(PlayWidth, RowHeight));
	const float Hover = Animate(ImGui::GetID("##home_play_hover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
	const ImVec2 PlayMin(Min.x + Pad, RowY);
	const ImVec2 PlayMax(PlayMin.x + PlayWidth, RowY + RowHeight);
	pDraw->AddRectFilled(PlayMin, PlayMax, Col(Mix(t.m_Accent, ImVec4(1, 1, 1, 1), 0.1f * Hover)), S(14.0f));
	const char *pPlay = IsOnline() ? Tr("Back to the game") : Tr("Play");
	ImFont *pPlayFont = Fonts().m_pTitle;
	const float PlaySize = pPlayFont->FontSize * 1.15f;
	const ImVec2 PlayDim = pPlayFont->CalcTextSizeA(PlaySize, FLT_MAX, 0.0f, pPlay);
	pDraw->AddText(pPlayFont, PlaySize, ImVec2((PlayMin.x + PlayMax.x - PlayDim.x) * 0.5f, (PlayMin.y + PlayMax.y - PlayDim.y) * 0.5f), Col(t.m_OnAccent), pPlay);
	const char *pLast = g_Config.m_UiServerAddress;
	const bool HasLast = pLast[0] && !str_startswith(pLast, "localhost") && !str_startswith(pLast, "127.0.0.1");
	if(ImGui::IsItemHovered() && !IsOnline())
		ImGui::SetTooltip("%s", HasLast ? pLast : Tr("Open the server list"));
	if(Clicked)
	{
		PlayUiSound(SOUND_CLICK);
		if(IsOnline())
			SetActive(false);
		else
		{
			// the server list, with the last server picked and in sight: its plate shows what is there
			m_Page = PAGE_SERVERS;
			m_ScrollTarget = 0.0f;
			if(HasLast)
			{
				m_SelectedServer = pLast;
				m_ScrollToSelected = true;
			}
		}
	}
	ImGui::SetCursorScreenPos(ImVec2(PlayMax.x + IconGap, RowY));
	if(IconSquare("##home_editor", XC_ICON_MAP, RowHeight, GameClient()->Editor()->HasUnsavedData() ? Tr("Map editor: unsaved changes") : Tr("Map editor")))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}
	ImGui::SetCursorScreenPos(ImVec2(PlayMax.x + IconGap * 2.0f + RowHeight, RowY));
	const bool Running = GameClient()->m_LocalServer.IsServerRunning();
	if(IconSquare("##home_server", XC_ICON_SERVER, RowHeight, Running ? Tr("Local server: running, click to join (right click stops it)") : Tr("Start a local server"), Running ? 2 : 0))
	{
		if(Running)
			Client()->Connect("localhost:8303");
		else if(GameClient()->m_LocalServer.RunServer({}))
			Toast(Tr("Local server started"));
		else
			Toast(Tr("Could not start the server"), TOAST_ERROR);
	}
	if(Running && ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		GameClient()->m_LocalServer.KillServer();
		Toast(Tr("Local server stopped"), TOAST_INFO);
	}
	ImGui::SetCursorScreenPos(Min);
	ImGui::Dummy(ImVec2(Width, Height));
}

// the music as a wide bar: the cover's colors blurred behind, the cover, the track, a thin progress line, buttons
void CXcMenu::RenderMusicBar(float Width, float Height)
{
	CXcMusic &Music = GameClient()->m_XcMusic;
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const bool Available = g_Config.m_XcMusic && Music.Supported();
	const bool HasTrack = Available && Music.HasTrack();
	// without a track the bar is a slim line
	const float BarHeight = Height;
	const ImVec2 Max(Min.x + Width, Min.y + BarHeight);
	const float Rounding = S(14.0f);
	const IGraphics::CTextureHandle Art = HasTrack ? Music.Art() : IGraphics::CTextureHandle();
	const bool HasArt = Art.IsValid() && !Art.IsNullTexture();
	pDraw->AddRectFilled(Min, Max, Col(t.m_Card), Rounding);
	if(HasArt)
	{
		// the middle of the cover stretched over the bar: its colors, soft like a blur
		pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Art), Min, Max, ImVec2(0.35f, 0.42f), ImVec2(0.65f, 0.58f), Col(ImVec4(1, 1, 1, 1), 0.55f), Rounding);
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(0.04f, 0.045f, 0.06f, 1.0f), 0.45f), Rounding);
	}
	pDraw->AddRect(Min, Max, Col(t.m_Border), Rounding, 0, 1.0f);
	const float Pad = S(20.0f);
	if(!HasTrack)
	{
		const char *pText = !Music.Supported() ? Tr("The music player needs playerctl or the Windows build with it") : (!g_Config.m_XcMusic ? Tr("Music player is off") : Tr("Turn on music in the browser or a player, it shows up here"));
		ImFont *pFont = Fonts().m_pLabel;
		pDraw->AddText(pFont, pFont->FontSize, ImVec2(Min.x + Pad + S(30.0f), std::floor(Min.y + BarHeight * 0.5f - TextMiddle(pFont, pFont->FontSize) + 0.5f)), Col(t.m_TextSecondary), pText);
		DrawIcon(pDraw, Fonts().m_pLabel, Fonts().m_pLabel->FontSize, ImVec2(Min.x + Pad + S(9.0f), Min.y + BarHeight * 0.5f), Col(t.m_Accent), XC_ICON_MUSIC);
		if(Available || !Music.Supported())
		{
			ImGui::Dummy(ImVec2(Width, BarHeight));
			return;
		}
		ImGui::SetCursorScreenPos(ImVec2(Max.x - Pad - S(110.0f), Min.y + (BarHeight - S(34.0f)) * 0.5f));
		if(Button(Tr("Turn on"), true, S(110.0f)))
			g_Config.m_XcMusic = 1;
		ImGui::SetCursorScreenPos(Min);
		ImGui::Dummy(ImVec2(Width, BarHeight));
		return;
	}

	const SXcMediaState &State = Music.State();
	// the cover: a rounded square like the rest of the interface
	const float ArtSize = BarHeight - Pad * 1.4f;
	const ImVec2 ArtMin(Min.x + Pad * 0.8f, Min.y + (BarHeight - ArtSize) * 0.5f);
	const ImVec2 ArtMax(ArtMin.x + ArtSize, ArtMin.y + ArtSize);
	if(HasArt)
		pDraw->AddImageRounded((ImTextureID)(uintptr_t)m_Backend.TextureId(Art), ArtMin, ArtMax, ImVec2(0, 0), ImVec2(1, 1), Col(ImVec4(1, 1, 1, 1)), S(12.0f));
	else
	{
		pDraw->AddRectFilled(ArtMin, ArtMax, Col(t.m_Accent, 0.18f), S(12.0f));
		ImFont *pIcons = Fonts().m_pHeader;
		DrawIcon(pDraw, pIcons, pIcons->FontSize, ImVec2((ArtMin.x + ArtMax.x) * 0.5f, (ArtMin.y + ArtMax.y) * 0.5f), Col(t.m_Accent), XC_ICON_MUSIC);
	}

	// the buttons on the right: rounded squares
	const float Small = S(38.0f), Big = S(44.0f), ButtonGap = S(8.0f);
	const float ButtonsWidth = Small * 2.0f + Big + ButtonGap * 2.0f;
	const float ButtonsX = Max.x - Pad - ButtonsWidth;
	const auto SquareButton = [&](const char *pId, const char *pIcon, float Size, bool Main, float Bx) {
		ImGui::SetCursorScreenPos(ImVec2(Bx, Min.y + (BarHeight - Size) * 0.5f));
		const bool Pressed = ImGui::InvisibleButton(pId, ImVec2(Size, Size));
		const float H = Animate(ImGui::GetID(pId) + 7, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
		const ImVec2 BMin = ImGui::GetItemRectMin(), BMax = ImGui::GetItemRectMax();
		pDraw->AddRectFilled(BMin, BMax, Main ? Col(Mix(ImVec4(1, 1, 1, 1), t.m_Accent, 0.15f * H)) : Col(ImVec4(1, 1, 1, 1), 0.1f + 0.08f * H), S(10.0f));
		DrawIcon(pDraw, Fonts().m_pLabel, Size * 0.4f, ImVec2((BMin.x + BMax.x) * 0.5f, (BMin.y + BMax.y) * 0.5f), Main ? Col(ImVec4(0.08f, 0.09f, 0.12f, 1.0f)) : Col(ImVec4(1, 1, 1, 1)), pIcon);
		if(Pressed)
			PlayUiSound(SOUND_CLICK);
		return Pressed;
	};
	if(SquareButton("##mb_prev", XC_ICON_BACKWARD, Small, false, ButtonsX))
		Music.Previous();
	if(SquareButton("##mb_play", State.m_Playing ? XC_ICON_PAUSE : XC_ICON_PLAY, Big, true, ButtonsX + Small + ButtonGap))
		Music.PlayPause();
	if(SquareButton("##mb_next", XC_ICON_FORWARD, Small, false, ButtonsX + Small + Big + ButtonGap * 2.0f))
		Music.Next();

	// the track and a thin progress line between the cover and the buttons
	const float TextX = ArtMax.x + S(16.0f);
	const float TextWidth = ButtonsX - S(18.0f) - TextX;
	ImFont *pTitle = Fonts().m_pTitle;
	ImFont *pSmall = Fonts().m_pSmall;
	const float TextTop = Min.y + (BarHeight - (pTitle->FontSize + pSmall->FontSize + S(26.0f))) * 0.5f;
	ImGui::PushFont(pTitle);
	ImGui::PushStyleColor(ImGuiCol_Text, t.m_Text);
	ImGui::RenderTextEllipsis(pDraw, ImVec2(TextX, TextTop), ImVec2(TextX + TextWidth, TextTop + pTitle->FontSize + S(2.0f)), TextX + TextWidth, TextX + TextWidth, State.m_Title.c_str(), nullptr, nullptr);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	char aArtist[256];
	str_format(aArtist, sizeof(aArtist), "%s · %s", State.m_Artist.empty() ? Music.AppName() : State.m_Artist.c_str(), Music.AppName());
	ImGui::PushFont(pSmall);
	ImGui::PushStyleColor(ImGuiCol_Text, t.m_TextSecondary);
	ImGui::RenderTextEllipsis(pDraw, ImVec2(TextX, TextTop + pTitle->FontSize + S(4.0f)), ImVec2(TextX + TextWidth, TextTop + pTitle->FontSize + pSmall->FontSize + S(6.0f)), TextX + TextWidth, TextX + TextWidth, aArtist, nullptr, nullptr);
	ImGui::PopStyleColor();
	ImGui::PopFont();
	const double Position = Music.Position();
	if(Position >= 0.0 && State.m_Duration > 0.0)
	{
		const float LineY = TextTop + pTitle->FontSize + pSmall->FontSize + S(18.0f);
		const float Fill = (float)std::clamp(Position / State.m_Duration, 0.0, 1.0);
		ImGui::SetCursorScreenPos(ImVec2(TextX, LineY - S(7.0f)));
		ImGui::InvisibleButton("##mb_seek", ImVec2(TextWidth, S(14.0f)));
		const bool Dragging = (ImGui::IsItemActive() || ImGui::IsItemDeactivated()) && State.m_CanSeek;
		// slower with Shift, the line shows where it will jump
		const float Mouse = Dragging ? DragFraction(ImGui::GetItemID(), Fill, TextX, TextWidth) : Fill;
		if(ImGui::IsItemDeactivated() && State.m_CanSeek)
			Music.Seek(Mouse * State.m_Duration);
		const bool Hovered = ImGui::IsItemHovered() && State.m_CanSeek;
		const float Thick = Hovered ? S(5.0f) : S(3.0f);
		pDraw->AddRectFilled(ImVec2(TextX, LineY - Thick * 0.5f), ImVec2(TextX + TextWidth, LineY + Thick * 0.5f), Col(ImVec4(1, 1, 1, 1), 0.16f), S(2.0f));
		pDraw->AddRectFilled(ImVec2(TextX, LineY - Thick * 0.5f), ImVec2(TextX + TextWidth * (Dragging ? Mouse : Fill), LineY + Thick * 0.5f), Col(ImVec4(1, 1, 1, 1), 0.9f), S(2.0f));
	}
	ImGui::SetCursorScreenPos(Min);
	ImGui::Dummy(ImVec2(Width, BarHeight));
}

// friends online as small plates in a row: those on a server can be joined with one click
void CXcMenu::RenderHomeFriends(float Width, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	PanelRect(pDraw, Min, Max, S(14.0f), t.m_Card);
	struct SFriend
	{
		const char *m_pName;
		const char *m_pServer;
		const char *m_pAddress;
		int m_Color;
	};
	std::vector<SFriend> vFriends;
	IServerBrowser *pBrowser = ServerBrowser();
	for(int i = 0; i < pBrowser->NumServers() && vFriends.size() < 16; i++)
	{
		const CServerInfo *pInfo = pBrowser->Get(i);
		if(!pInfo || pInfo->m_FriendNum <= 0)
			continue;
		for(const CServerInfo::CClient &Player : pInfo->m_vClients)
			if(Player.m_FriendState != IFriends::FRIEND_NO && vFriends.size() < 16)
				vFriends.push_back({Player.m_aName, pInfo->m_aName, pInfo->m_aAddress, Player.m_Country});
	}
	const float Pad = S(16.0f);
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	char aTitle[64];
	str_format(aTitle, sizeof(aTitle), "%s · %d", Tr("Friends"), (int)vFriends.size());
	const float TitleWidth = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, aTitle).x;
	pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + Pad, Min.y + (Height - pLabel->FontSize) * 0.5f), Col(t.m_TextMuted), aTitle);
	if(vFriends.empty())
	{
		const char *pText = GameClient()->Friends()->NumFriends() == 0 ? Tr("Add friends in the scoreboard or the classic menu") : Tr("None of your friends is online");
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + Pad * 2.0f + TitleWidth, Min.y + (Height - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), pText);
		ImGui::Dummy(ImVec2(Width, Height));
		return;
	}
	// the plates scroll sideways with the wheel when they do not fit
	const float ListX = Min.x + Pad * 2.0f + TitleWidth;
	ImGui::SetCursorScreenPos(ImVec2(ListX, Min.y));
	ImGui::BeginChild("##home_friends", ImVec2(Max.x - Pad - ListX, Height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_HorizontalScrollbar);
	if(ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
		ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * S(80.0f));
	ImDrawList *pList = ImGui::GetWindowDrawList();
	const ImVec4 Green(0.24f, 0.84f, 0.64f, 1.0f);
	static const ImVec4 s_aAvatars[] = {{1.0f, 0.54f, 0.24f, 1.0f}, {0.24f, 0.84f, 0.64f, 1.0f}, {1.0f, 0.36f, 0.54f, 1.0f}, {0.31f, 0.55f, 1.0f, 1.0f}, {0.71f, 0.91f, 0.24f, 1.0f}};
	const float PlateHeight = Height - S(24.0f);
	float x = ImGui::GetCursorScreenPos().x;
	const float PlateY = Min.y + S(12.0f);
	for(size_t i = 0; i < vFriends.size(); i++)
	{
		const SFriend &Friend = vFriends[i];
		const float NameWidth = std::max(pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, Friend.m_pName).x, std::min(pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, Friend.m_pServer).x, S(150.0f)));
		const float JoinWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, Tr("Join")).x + S(18.0f);
		const float Avatar = PlateHeight - S(12.0f);
		const float PlateWidth = S(6.0f) + Avatar + S(10.0f) + NameWidth + S(10.0f) + JoinWidth + S(6.0f);
		const ImVec2 PMin(x, PlateY), PMax(x + PlateWidth, PlateY + PlateHeight);
		pList->AddRectFilled(PMin, PMax, Col(Green, 0.08f), S(10.0f));
		pList->AddRect(PMin, PMax, Col(Green, 0.28f), S(10.0f), 0, 1.0f);
		const ImVec2 AMin(PMin.x + S(6.0f), PMin.y + S(6.0f));
		pList->AddRectFilled(AMin, ImVec2(AMin.x + Avatar, AMin.y + Avatar), Col(s_aAvatars[(i + std::abs(Friend.m_Color)) % std::size(s_aAvatars)]), S(8.0f));
		const float TextX = AMin.x + Avatar + S(10.0f);
		pList->AddText(pLabel, pLabel->FontSize, ImVec2(TextX, PMin.y + S(6.0f)), Col(t.m_Text), Friend.m_pName);
		ImGui::PushFont(pSmall);
		ImGui::PushStyleColor(ImGuiCol_Text, Green);
		ImGui::RenderTextEllipsis(pList, ImVec2(TextX, PMax.y - pSmall->FontSize - S(6.0f)), ImVec2(TextX + NameWidth, PMax.y), TextX + NameWidth, TextX + NameWidth, Friend.m_pServer, nullptr, nullptr);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		const ImVec2 JMin(PMax.x - S(6.0f) - JoinWidth, PMin.y + (PlateHeight - S(26.0f)) * 0.5f);
		ImGui::SetCursorScreenPos(JMin);
		char aId[24];
		str_format(aId, sizeof(aId), "##join%d", (int)i);
		const bool Join = ImGui::InvisibleButton(aId, ImVec2(JoinWidth, S(26.0f)));
		const bool Hovered = ImGui::IsItemHovered();
		pList->AddRectFilled(JMin, ImVec2(JMin.x + JoinWidth, JMin.y + S(26.0f)), Col(Green, Hovered ? 1.0f : 0.85f), S(6.0f));
		pList->AddText(pSmall, pSmall->FontSize, ImVec2(JMin.x + S(9.0f), JMin.y + (S(26.0f) - pSmall->FontSize) * 0.5f), Col(ImVec4(0.03f, 0.15f, 0.11f, 1.0f)), Tr("Join"));
		if(Join)
		{
			PlayUiSound(SOUND_CLICK);
			Client()->Connect(Friend.m_pAddress);
		}
		x += PlateWidth + S(8.0f);
	}
	ImGui::SetCursorScreenPos(ImVec2(x, PlateY));
	ImGui::Dummy(ImVec2(1.0f, PlateHeight));
	ImGui::EndChild();
	ImGui::SetCursorScreenPos(Min);
	ImGui::Dummy(ImVec2(Width, Height));
}

// the client's releases on GitHub: the newest one gives the "what is new" points
void CXcMenu::UpdateClientUpdates()
{
	if(!m_UpdatesRequested)
	{
		m_UpdatesRequested = true;
		char aUrl[256];
		str_copy(aUrl, "https://api.github.com/repos/rickrollru/ExoraClient/releases?per_page=1");
		m_pUpdatesRequest = HttpGet(aUrl);
		m_pUpdatesRequest->HeaderString("User-Agent", "ExoraClient");
		m_pUpdatesRequest->HeaderString("Accept", "application/vnd.github+json");
		m_pUpdatesRequest->Timeout(CTimeout{10000, 0, 500, 10});
		m_pUpdatesRequest->MaxResponseSize(1024 * 1024);
		m_pUpdatesRequest->LogProgress(HTTPLOG::NONE);
		Http()->Run(m_pUpdatesRequest);
	}
	if(!m_pUpdatesRequest || !m_pUpdatesRequest->Done())
		return;
	std::shared_ptr<IHttpRequest> pRequest = m_pUpdatesRequest;
	m_pUpdatesRequest = nullptr;
	json_value *pJson = pRequest->State() == EHttpState::DONE ? pRequest->ResultJson() : nullptr;
	if(!pJson)
		return;
	const json_value *pRelease = pJson->type == json_array && json_array_length(pJson) > 0 ? json_array_get(pJson, 0) : nullptr;
	const json_value *pTag = pRelease ? json_object_get(pRelease, "tag_name") : nullptr;
	const json_value *pBody = pRelease ? json_object_get(pRelease, "body") : nullptr;
	if(pTag && pTag->type == json_string && pBody && pBody->type == json_string)
	{
		// the release text: Russian first, then English after the "english" anchor; the points of "what's new"
		// are lines "- **Title** (where): text"
		std::string Body = json_string_get(pBody);
		const size_t English = Body.find("<a id=\"english\">");
		std::string Part = English == std::string::npos ? Body : (IsRussian() ? Body.substr(0, English) : Body.substr(English));
		std::vector<SClientUpdate> vItems;
		size_t Start = 0;
		while(Start < Part.size() && vItems.size() < 4)
		{
			size_t End = Part.find('\n', Start);
			if(End == std::string::npos)
				End = Part.size();
			std::string Line = Part.substr(Start, End - Start);
			Start = End + 1;
			if(Line.rfind("- **", 0) != 0)
				continue;
			const size_t TitleEnd = Line.find("**", 4);
			if(TitleEnd == std::string::npos)
				continue;
			SClientUpdate Item;
			Item.m_Title = Line.substr(4, TitleEnd - 4);
			std::string Rest = Line.substr(TitleEnd + 2);
			const size_t Colon = Rest.find(':');
			if(Colon != std::string::npos)
				Rest = Rest.substr(Colon + 1);
			while(!Rest.empty() && Rest[0] == ' ')
				Rest.erase(0, 1);
			Item.m_Text = Rest;
			vItems.push_back(Item);
		}
		if(!vItems.empty())
		{
			m_vClientUpdates = vItems;
			m_ClientUpdatesVersion = json_string_get(pTag);
		}
	}
	json_value_free(pJson);
}

void CXcMenu::RenderWhatsNew(float Width, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	const ImVec2 Max(Min.x + Width, Min.y + Height);
	PanelRect(pDraw, Min, Max, S(14.0f), t.m_Card);
	const float Pad = S(20.0f);
	ImFont *pTitle = Fonts().m_pTitle;
	ImFont *pLabel = Fonts().m_pLabel;
	ImFont *pSmall = Fonts().m_pSmall;
	float y = Min.y + Pad;
	const char *pHead = Tr("What's new in Exora");
	pDraw->AddText(pTitle, pTitle->FontSize, ImVec2(Min.x + Pad, y), Col(t.m_Text), pHead);
	char aVersion[32];
	str_copy(aVersion, m_ClientUpdatesVersion.empty() ? "v" XCLIENT_VERSION : m_ClientUpdatesVersion.c_str());
	const float HeadWidth = pTitle->CalcTextSizeA(pTitle->FontSize, FLT_MAX, 0.0f, pHead).x;
	const float BadgeWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aVersion).x + S(14.0f);
	const ImVec2 BMin(Min.x + Pad + HeadWidth + S(10.0f), y + (pTitle->FontSize - pSmall->FontSize - S(8.0f)) * 0.5f);
	pDraw->AddRectFilled(BMin, ImVec2(BMin.x + BadgeWidth, BMin.y + pSmall->FontSize + S(8.0f)), Col(t.m_Accent, 0.18f), S(6.0f));
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(BMin.x + S(7.0f), BMin.y + S(4.0f)), Col(Mix(t.m_Accent, ImVec4(1, 1, 1, 1), 0.45f)), aVersion);
	// all updates: the releases page
	const char *pAll = Tr("All updates");
	const float AllWidth = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pAll).x;
	ImGui::SetCursorScreenPos(ImVec2(Max.x - Pad - AllWidth, y + S(2.0f)));
	if(ImGui::InvisibleButton("##all_updates", ImVec2(AllWidth, pLabel->FontSize)))
		Client()->ViewLink("https://github.com/rickrollru/ExoraClient/releases");
	pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Max.x - Pad - AllWidth, y + S(2.0f)), Col(ImGui::IsItemHovered() ? Mix(t.m_Accent, ImVec4(1, 1, 1, 1), 0.3f) : t.m_Accent), pAll);
	y += pTitle->FontSize + S(16.0f);

	std::vector<SClientUpdate> vBuiltin;
	const std::vector<SClientUpdate> *pItems = &m_vClientUpdates;
	if(m_vClientUpdates.empty())
	{
		for(const auto &aItem : s_aaBuiltinUpdates)
			vBuiltin.push_back({IsRussian() ? aItem[2] : aItem[0], IsRussian() ? aItem[3] : aItem[1]});
		pItems = &vBuiltin;
	}
	static const ImVec4 s_aDots[] = {{0.31f, 0.55f, 1.0f, 1.0f}, {0.24f, 0.84f, 0.64f, 1.0f}, {1.0f, 0.54f, 0.24f, 1.0f}, {1.0f, 0.36f, 0.54f, 1.0f}};
	const float TextX = Min.x + Pad + S(20.0f);
	const float TextWidth = Max.x - Pad - TextX;
	for(size_t i = 0; i < pItems->size(); i++)
	{
		const SClientUpdate &Item = (*pItems)[i];
		// the text wraps to at most two lines
		const ImVec2 TextDim = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, TextWidth, Item.m_Text.c_str());
		const float TextHeight = std::min(TextDim.y, pSmall->FontSize * 2.0f + S(2.0f));
		const float ItemHeight = pLabel->FontSize + S(4.0f) + TextHeight;
		if(y + ItemHeight > Max.y - Pad * 0.5f)
			break;
		pDraw->AddRectFilled(ImVec2(Min.x + Pad, y + S(5.0f)), ImVec2(Min.x + Pad + S(8.0f), y + S(13.0f)), Col(s_aDots[i % std::size(s_aDots)]), S(2.0f));
		ImGui::PushFont(pLabel);
		ImGui::PushStyleColor(ImGuiCol_Text, t.m_Text);
		ImGui::RenderTextEllipsis(pDraw, ImVec2(TextX, y), ImVec2(TextX + TextWidth, y + pLabel->FontSize + S(2.0f)), TextX + TextWidth, TextX + TextWidth, Item.m_Title.c_str(), nullptr, nullptr);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		const ImVec4 Clip(TextX, y + pLabel->FontSize + S(4.0f), TextX + TextWidth + S(2.0f), y + ItemHeight + S(1.0f));
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(TextX, y + pLabel->FontSize + S(4.0f)), Col(t.m_TextMuted), Item.m_Text.c_str(), nullptr, TextWidth, &Clip);
		y += ItemHeight + S(14.0f);
	}

	// DDNet news under the client's updates, in the space that is left
	const char *pNews = Client()->News();
	const float NewsTop = y + S(6.0f);
	if(pNews[0] && Max.y - NewsTop > S(90.0f))
	{
		pDraw->AddLine(ImVec2(Min.x + Pad, NewsTop), ImVec2(Max.x - Pad, NewsTop), Col(t.m_Border), 1.0f);
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(Min.x + Pad, NewsTop + S(12.0f)), Col(t.m_TextMuted), Tr("DDNet news"));
		const float ListTop = NewsTop + S(12.0f) + pLabel->FontSize + S(10.0f);
		ImGui::SetCursorScreenPos(ImVec2(Min.x + Pad, ListTop));
		ImGui::BeginChild("##home_news", ImVec2(Width - Pad * 2.0f, Max.y - Pad * 0.75f - ListTop), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		char aLine[256];
		const char *pStr = pNews;
		while((pStr = str_next_token(pStr, "\n", aLine, sizeof(aLine))))
		{
			const int Length = str_length(aLine);
			if(Length > 1 && aLine[0] == '|' && aLine[Length - 1] == '|')
			{
				// lines between | are headlines
				aLine[Length - 1] = '\0';
				ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
				ImGui::PushFont(pLabel);
				ImGui::TextColored(t.m_Text, "%s", aLine + 1);
				ImGui::PopFont();
			}
			else
			{
				ImGui::PushFont(pSmall);
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextColored(t.m_TextSecondary, "%s", aLine);
				ImGui::PopTextWrapPos();
				ImGui::PopFont();
			}
		}
		ImGui::EndChild();
	}
	ImGui::SetCursorScreenPos(Min);
	ImGui::Dummy(ImVec2(Width, Height));
}

// a thin line: the version, an update notice, and the link icons
void CXcMenu::RenderHomeFooter(float Width, float Height)
{
	const STheme &t = Theme();
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetCursorScreenPos();
	ImFont *pSmall = Fonts().m_pSmall;
	char aLine[160];
	str_format(aLine, sizeof(aLine), "Exora Client v%s · DDNet %s · TClient %s", XCLIENT_VERSION, GAME_RELEASE_VERSION, TCLIENT_VERSION);
	pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(4.0f), Min.y + (Height - pSmall->FontSize) * 0.5f), Col(t.m_TextMuted), aLine);
	const char *pLatest = Client()->LatestVersion();
	if(pLatest[0] && str_comp(pLatest, "0") != 0 && str_comp(pLatest, GAME_RELEASE_VERSION) != 0)
	{
		char aUpdate[96];
		str_format(aUpdate, sizeof(aUpdate), "· %s: DDNet %s", Tr("Update available"), pLatest);
		const float LineWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, aLine).x;
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(10.0f) + LineWidth, Min.y + (Height - pSmall->FontSize) * 0.5f), Col(t.m_Accent), aUpdate);
	}
	const float Size = S(38.0f);
	const float IconGap = S(8.0f);
	float x = Min.x + Width - Size;
	const float y = Min.y + (Height - Size) * 0.5f;
	ImGui::SetCursorScreenPos(ImVec2(x, y));
	if(IconSquare("##link_dev", XC_ICON_USER, Size, Tr("Developer on Discord: rickroll.ru")))
	{
		Input()->SetClipboardText("rickroll.ru");
		Toast(Tr("Discord name copied"));
	}
	x -= Size + IconGap;
	ImGui::SetCursorScreenPos(ImVec2(x, y));
	if(IconSquare("##link_discord", XC_ICON_COMMENTS, Size, Tr("Discord server of the client"), 1) && g_Config.m_XcLinkDiscord[0])
		Client()->ViewLink(g_Config.m_XcLinkDiscord);
	x -= Size + IconGap;
	ImGui::SetCursorScreenPos(ImVec2(x, y));
	if(IconSquare("##link_github", XC_ICON_CODE, Size, "GitHub") && g_Config.m_XcLinkGithub[0])
		Client()->ViewLink(g_Config.m_XcLinkGithub);
	ImGui::SetCursorScreenPos(Min);
	ImGui::Dummy(ImVec2(Width, Height));
}
