// The HUD editor: every element gets a small name plate with its icon; it is dragged with the mouse and follows on a
// spring, leaning a little with its speed and lifting while held; the middle lines of the screen catch it. A right
// click on an element opens its settings (size, its own options, remove), a right click on an empty place asks what
// to add there. Everything moves on the frame time.
#include "xc_hud.h"

#include <base/str.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/feedback.h>
#include <game/client/components/xclient/ui/icons.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

static constexpr float SNAP_DISTANCE = 10.0f;

namespace
{
	float Smooth(float x)
	{
		x = std::clamp(x, 0.0f, 1.0f);
		return x * x * (3.0f - 2.0f * x);
	}
	float Ease(float Value, float Target, float Rate, float Dt) { return Value + (Target - Value) * (1.0f - std::exp(-Dt * Rate)); }
	int IndexOf(const char *pStr, char c)
	{
		for(int i = 0; pStr[i]; i++)
			if(pStr[i] == c)
				return i;
		return -1;
	}
}

const char *CXcHud::ElementIcon(int Index)
{
	switch(Index)
	{
	case ELEMENT_WATERMARK: return XC_ICON_WINDOW;
	case ELEMENT_TIMER: return XC_ICON_CLOCK;
	case ELEMENT_SPEED: return XC_ICON_GAUGE;
	case ELEMENT_WEAPON: return XC_ICON_HAMMER;
	case ELEMENT_MUSIC: return XC_ICON_MUSIC;
	case ELEMENT_KEYS: return XC_ICON_KEYBOARD;
	case ELEMENT_MINITOP: return XC_ICON_STAR;
	case ELEMENT_EFFECTS: return XC_ICON_BOLT;
	case ELEMENT_LATENCY: return XC_ICON_SIGNAL;
	default: return XC_ICON_LAYERS;
	}
}

// the drawn centers follow the saved ones on springs while editing; otherwise they are the saved ones
void CXcHud::UpdateEditorMotion(float Dt)
{
	const ImVec2 Screen((float)Graphics()->ScreenWidth(), (float)Graphics()->ScreenHeight());
	const float Stiff = AnimationSpeed() > 0.0f ? 230.0f * AnimationSpeed() * AnimationSpeed() : 0.0f;
	for(int i = 0; i < NUM_ELEMENTS; i++)
	{
		const SElementConfig Config = Element(i);
		const ImVec2 Target(*Config.m_pX / 1000.0f * Screen.x, *Config.m_pY / 1000.0f * Screen.y);
		if(!m_Editing || !m_MotionValid || Stiff <= 0.0f)
		{
			m_aaDrawPos[i][0] = Target.x;
			m_aaDrawPos[i][1] = Target.y;
			m_aaDrawVel[i][0] = m_aaDrawVel[i][1] = 0.0f;
			m_aLean[i] = 0.0f;
			continue;
		}
		const float Damp = 2.0f * std::sqrt(Stiff) * 0.85f;
		for(int a = 0; a < 2; a++)
		{
			const float T = a ? Target.y : Target.x;
			// small steps: the spring stays calm at any frame time
			const int Steps = std::max(1, (int)std::ceil(Dt / 0.008f));
			const float h = Dt / Steps;
			for(int s = 0; s < Steps; s++)
			{
				m_aaDrawVel[i][a] += ((T - m_aaDrawPos[i][a]) * Stiff - m_aaDrawVel[i][a] * Damp) * h;
				m_aaDrawPos[i][a] += m_aaDrawVel[i][a] * h;
			}
		}
		// it leans with its speed while held, and stands up again after
		const float LeanTarget = i == m_Dragging ? std::clamp(m_aaDrawVel[i][0] * 0.0035f, -0.12f, 0.12f) : 0.0f;
		m_aLean[i] = Ease(m_aLean[i], LeanTarget, 14.0f, Dt);
		m_aLift[i] = Ease(m_aLift[i], i == m_Dragging ? 1.0f : 0.0f, 16.0f, Dt);
		m_aPop[i] = Ease(m_aPop[i], 1.0f, 10.0f, Dt);
	}
	m_MotionValid = m_Editing;
}

// turns and scales what an element drew (its vertices since VtxStart) around its middle
void CXcHud::TransformElement(ImDrawList *pDraw, int VtxStart, int Index)
{
	if(!m_Editing)
		return;
	const float Angle = m_aLean[Index];
	const float Scale = (1.0f + 0.04f * m_aLift[Index]) * (0.6f + 0.4f * m_aPop[Index]);
	if(std::abs(Angle) < 0.0005f && std::abs(Scale - 1.0f) < 0.0005f)
		return;
	const ImVec2 C((m_aaRects[Index][0] + m_aaRects[Index][2]) * 0.5f, (m_aaRects[Index][1] + m_aaRects[Index][3]) * 0.5f);
	const float Cos = std::cos(Angle) * Scale, Sin = std::sin(Angle) * Scale;
	for(int v = VtxStart; v < pDraw->VtxBuffer.Size; v++)
	{
		ImDrawVert &Vert = pDraw->VtxBuffer[v];
		const float x = Vert.pos.x - C.x, y = Vert.pos.y - C.y;
		Vert.pos = ImVec2(C.x + x * Cos - y * Sin, C.y + x * Sin + y * Cos);
	}
}

void CXcHud::RenderEditor(ImDrawList *pDraw)
{
	const STheme &t = Theme();
	ImGuiIO &Io = ImGui::GetIO();
	const ImVec2 Screen = Io.DisplaySize;
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	ImFont *pTitle = m_Backend.Fonts().m_pTitle;
	const float U = Graphics()->ScreenHeight() / 1080.0f;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_EditorOpen = std::min(m_EditorOpen + Dt / 0.4f, 1.0f);
	const float Open = Smooth(m_EditorOpen);

	// the buttons of the editor live in one window over the whole screen (the menu opens above it)
	ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
	ImGui::SetNextWindowSize(Screen);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##hud_editor_layer", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing);
	ImGui::PopStyleVar();
	// ---- the bar at the bottom: what to do, reset, done
	bool OverUi = false;
	{
		const char *pHint = Tr("Left mouse: drag   Right mouse: settings   Right mouse on an empty place: add   Wheel: size");
		const char *pReset = Tr("Reset");
		const char *pDone = Tr("Done");
		const float HintW = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pHint).x;
		const float ResetW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pReset).x + 28.0f * U;
		const float DoneW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pDone).x + 32.0f * U;
		const float BarH = 50.0f * U, BtnH = 34.0f * U;
		const float BarW = 18.0f * U + HintW + 16.0f * U + ResetW + 8.0f * U + DoneW + 8.0f * U;
		const ImVec2 Min(Screen.x * 0.5f - BarW * 0.5f, Screen.y - 28.0f * U - BarH + (1.0f - Open) * 24.0f * U);
		const ImVec2 Max(Min.x + BarW, Min.y + BarH);
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.9f), Open), 16.0f * U);
		pDraw->AddRect(Min, Max, Col(t.m_Border, Open), 16.0f * U, 0, 1.0f);
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + 18.0f * U, Min.y + (BarH - pSmall->FontSize) * 0.5f), Col(t.m_TextSecondary, Open), pHint);
		OverUi = ImGui::IsMouseHoveringRect(Min, Max);
		const auto Button = [&](const char *pId, const char *pText, float X, float W, bool Solid) {
			ImGui::SetCursorScreenPos(ImVec2(X, Min.y + (BarH - BtnH) * 0.5f));
			const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(W, BtnH));
			const float Hover = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
			const ImVec2 BMin(X, Min.y + (BarH - BtnH) * 0.5f - Hover * 1.5f * U), BMax(X + W, BMin.y + BtnH);
			if(Solid)
				pDraw->AddRectFilled(BMin, BMax, Col(Mix(t.m_Text, ImVec4(1, 1, 1, 1), Hover), Open), 10.0f * U);
			else
			{
				pDraw->AddRectFilled(BMin, BMax, Col(ImVec4(1, 1, 1, 0.07f + 0.05f * Hover), Open), 10.0f * U);
				pDraw->AddRect(BMin, BMax, Col(t.m_Border, Open), 10.0f * U, 0, 1.0f);
			}
			const float TW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pText).x;
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(X + (W - TW) * 0.5f, BMin.y + (BtnH - pLabel->FontSize) * 0.5f), Col(Solid ? ImVec4(0.06f, 0.07f, 0.1f, 1.0f) : t.m_Text, Open), pText);
			if(Clicked)
				PlayUiSound(SOUND_CLICK);
			return Clicked;
		};
		float X = Min.x + 18.0f * U + HintW + 16.0f * U;
		if(Button("##hud_reset", pReset, X, ResetW, false))
		{
			for(int i = 0; i < NUM_ELEMENTS; i++)
			{
				const SElementConfig Config = Element(i);
				*Config.m_pX = Config.m_DefaultX;
				*Config.m_pY = Config.m_DefaultY;
				*Config.m_pScale = 100;
			}
		}
		X += ResetW + 8.0f * U;
		if(Button("##hud_done", pDone, X, DoneW, true))
		{
			ImGui::End();
			SetEditing(false);
			return;
		}
	}
	ImGui::End();

	// ---- the menu (drawn as a window so its rows take the clicks first)
	const bool MenuOpen = m_MenuKind != MENU_NONE;
	const bool OverMenu = MenuOpen && RenderEditorMenu(U);

	// which element is under the mouse (the last drawn wins)
	int Hovered = -1;
	if(!OverUi && !OverMenu && m_Dragging < 0)
	{
		for(int i = 0; i < NUM_ELEMENTS; i++)
		{
			if(!*Element(i).m_pEnabled)
				continue;
			if(Io.MousePos.x >= m_aaRects[i][0] && Io.MousePos.x <= m_aaRects[i][2] && Io.MousePos.y >= m_aaRects[i][1] && Io.MousePos.y <= m_aaRects[i][3])
				Hovered = i;
		}
	}

	// a right click: the settings of an element, or what to add on an empty place
	if(!OverUi && !OverMenu && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		m_MenuKind = Hovered >= 0 ? MENU_ELEMENT : MENU_ADD;
		m_MenuElement = Hovered;
		m_aMenuPos[0] = Io.MousePos.x;
		m_aMenuPos[1] = Io.MousePos.y;
		m_MenuOpenTime = 0.0f;
		PlayUiSound(SOUND_CLICK);
	}
	else if(MenuOpen && !OverMenu && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		m_MenuKind = MENU_NONE;

	// drag: the saved center follows the mouse (the drawn one on its spring), snapping to the middle of the screen
	if(Hovered >= 0 && !MenuOpen && !m_IslandOverControl && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_Dragging = Hovered;
		m_aDragStart[0] = Io.MousePos.x;
		m_aDragStart[1] = Io.MousePos.y;
		const SElementConfig Config = Element(Hovered);
		m_aDragOffset[0] = *Config.m_pX / 1000.0f * Screen.x - Io.MousePos.x;
		m_aDragOffset[1] = *Config.m_pY / 1000.0f * Screen.y - Io.MousePos.y;
	}
	if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		// a click on the island without moving it opens or closes its player
		if(m_Dragging == ELEMENT_ISLAND && std::abs(Io.MousePos.x - m_aDragStart[0]) + std::abs(Io.MousePos.y - m_aDragStart[1]) < 5.0f * U)
		{
			m_IslandPlayerPinned = !m_IslandPlayerPinned;
			m_IslandPlayerUntil = -1.0f;
			PlayUiSound(SOUND_CLICK);
		}
		m_Dragging = -1;
	}
	m_aSnapped[0] = m_aSnapped[1] = false;
	if(m_Dragging >= 0)
	{
		const SElementConfig Config = Element(m_Dragging);
		ImVec2 C(Io.MousePos.x + m_aDragOffset[0], Io.MousePos.y + m_aDragOffset[1]);
		if(std::abs(C.x - Screen.x * 0.5f) < SNAP_DISTANCE * U)
		{
			C.x = Screen.x * 0.5f;
			m_aSnapped[0] = true;
		}
		if(std::abs(C.y - Screen.y * 0.5f) < SNAP_DISTANCE * U)
		{
			C.y = Screen.y * 0.5f;
			m_aSnapped[1] = true;
		}
		*Config.m_pX = std::clamp((int)std::round(C.x / Screen.x * 1000.0f), 0, 1000);
		*Config.m_pY = std::clamp((int)std::round(C.y / Screen.y * 1000.0f), 0, 1000);
	}
	// wheel: size in steps of 10 %
	const int Target = m_Dragging >= 0 ? m_Dragging : Hovered;
	if(Target >= 0 && !OverMenu && Io.MouseWheel != 0.0f)
	{
		int *pScale = Element(Target).m_pScale;
		*pScale = std::clamp(*pScale + (Io.MouseWheel > 0.0f ? 10 : -10), 50, 200);
	}

	// ---- the middle lines fade in while they hold the element
	for(int a = 0; a < 2; a++)
	{
		m_aGuide[a] = Ease(m_aGuide[a], m_aSnapped[a] ? 1.0f : 0.0f, 18.0f, Dt);
		if(m_aGuide[a] < 0.01f)
			continue;
		if(a == 0)
			pDraw->AddLine(ImVec2(Screen.x * 0.5f, 0.0f), ImVec2(Screen.x * 0.5f, Screen.y), Col(t.m_Accent, 0.6f * m_aGuide[a]), 1.0f);
		else
			pDraw->AddLine(ImVec2(0.0f, Screen.y * 0.5f), ImVec2(Screen.x, Screen.y * 0.5f), Col(t.m_Accent, 0.6f * m_aGuide[a]), 1.0f);
	}

	// ---- a thin frame and the name plate of every element
	for(int i = 0; i < NUM_ELEMENTS; i++)
	{
		const SElementConfig Config = Element(i);
		if(!*Config.m_pEnabled || m_aaRects[i][2] <= m_aaRects[i][0])
			continue;
		const bool Active = i == m_Dragging || i == Hovered;
		m_aFrame[i] = Ease(m_aFrame[i], Active ? 1.0f : 0.0f, 14.0f, Dt);
		const float Lift = m_aLift[i] * 3.0f * U;
		const ImVec2 Min(m_aaRects[i][0] - 6.0f * U, m_aaRects[i][1] - 6.0f * U - Lift);
		const ImVec2 Max(m_aaRects[i][2] + 6.0f * U, m_aaRects[i][3] + 6.0f * U - Lift);
		pDraw->AddRect(Min, Max, Col(Mix(ImVec4(1, 1, 1, 0.16f), ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.85f), m_aFrame[i]), Open * m_aPop[i]), 16.0f * U, 0, 1.0f + m_aFrame[i]);
		// the plate: an icon in the accent and the name, above the frame (below it at the top of the screen)
		const char *pName = Tr(Config.m_pName);
		const float NameSize = 12.5f * U;
		const float PlateH = NameSize + 10.0f * U;
		const float IconW = 16.0f * U;
		const float PlateW = 8.0f * U + IconW + 5.0f * U + pLabel->CalcTextSizeA(NameSize, FLT_MAX, 0.0f, pName).x + 10.0f * U;
		const bool Below = Min.y - PlateH - 6.0f * U < 0.0f;
		const float Rise = (1.0f - Open) * 6.0f * U;
		const ImVec2 PMin(Min.x, Below ? Max.y + 6.0f * U + Rise : Min.y - PlateH - 6.0f * U + Rise);
		const ImVec2 PMax(PMin.x + PlateW, PMin.y + PlateH);
		const float a = Open * m_aPop[i];
		pDraw->AddRectFilled(PMin, PMax, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.9f), a), 8.0f * U);
		pDraw->AddRect(PMin, PMax, Col(Mix(t.m_Border, ImVec4(t.m_Accent.x, t.m_Accent.y, t.m_Accent.z, 0.5f), m_aFrame[i]), a), 8.0f * U, 0, 1.0f);
		DrawIcon(pDraw, pLabel, NameSize, ImVec2(PMin.x + 8.0f * U + IconW * 0.5f, (PMin.y + PMax.y) * 0.5f), Col(t.m_Accent, a), ElementIcon(i));
		pDraw->AddText(pLabel, NameSize, ImVec2(PMin.x + 8.0f * U + IconW + 5.0f * U, PMin.y + (PlateH - NameSize) * 0.5f), Col(t.m_Text, a), pName);
	}
	(void)pTitle;
}

// the settings of one element, or the list of what can be added; true while the mouse is over it
bool CXcHud::RenderEditorMenu(float U)
{
	const STheme &t = Theme();
	ImGuiIO &Io = ImGui::GetIO();
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_MenuOpenTime = std::min(m_MenuOpenTime + Dt / 0.32f, 1.0f);
	// pops out of the click point a little too big, settles
	const float In = Smooth(m_MenuOpenTime);
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	ImFont *pTitle = m_Backend.Fonts().m_pTitle;
	const float W = 260.0f * U;
	const float Pad = 14.0f * U;
	const float RowH = 34.0f * U;

	ImGui::SetNextWindowPos(ImVec2(std::min(m_aMenuPos[0], Io.DisplaySize.x - W - 12.0f * U), std::min(m_aMenuPos[1] + (1.0f - In) * -6.0f * U, Io.DisplaySize.y - m_MenuHeight - 12.0f * U)));
	ImGui::SetNextWindowSize(ImVec2(W, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, In);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad, Pad));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * U);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f * U));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.94f));
	ImGui::PushStyleColor(ImGuiCol_Border, t.m_Border);
	ImGui::Begin("##hud_menu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const float Inner = W - Pad * 2.0f;
	const float a = ImGui::GetStyle().Alpha;

	const auto Header = [&](const char *pText) {
		ImGui::PushFont(pTitle);
		ImGui::TextColored(t.m_Text, "%s", pText);
		ImGui::PopFont();
		ImGui::Dummy(ImVec2(0.0f, 4.0f * U));
	};
	const auto Label = [&](const char *pText, const char *pRight) {
		const ImVec2 P = ImGui::GetCursorScreenPos();
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(P.x + 8.0f * U, P.y + 6.0f * U), Col(t.m_TextMuted, a), pText);
		if(pRight)
		{
			const float RW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, pRight).x;
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(P.x + Inner - 8.0f * U - RW, P.y + 6.0f * U), Col(t.m_Accent, a), pRight);
		}
		ImGui::Dummy(ImVec2(Inner, pLabel->FontSize + 10.0f * U));
	};
	// a row: hover plate, text, a check mark or an icon on the right; Tone 1 = red
	const auto Row = [&](const char *pId, const char *pText, const char *pIcon, bool Checked, int Tone) {
		const ImVec2 P = ImGui::GetCursorScreenPos();
		const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Inner, RowH));
		const float Hover = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
		const float Check = Animate(ImGui::GetID("##chk"), Checked ? 1.0f : 0.0f, 16.0f);
		pDraw->AddRectFilled(P, ImVec2(P.x + Inner, P.y + RowH), Col(ImVec4(1, 1, 1, 0.06f * Hover), a), 9.0f * U);
		const ImVec4 Red(1.0f, 0.30f, 0.30f, 1.0f);
		const ImVec4 TextColor = Tone == 1 ? Red : Mix(t.m_TextSecondary, t.m_Text, Hover);
		pDraw->AddText(pLabel, pLabel->FontSize * 1.05f, ImVec2(P.x + 8.0f * U, P.y + (RowH - pLabel->FontSize * 1.05f) * 0.5f), Col(TextColor, a), pText);
		const ImVec2 Right(P.x + Inner - 16.0f * U, P.y + RowH * 0.5f);
		if(pIcon)
			DrawIcon(pDraw, pLabel, pLabel->FontSize, Right, Col(Tone == 1 ? Red : t.m_TextMuted, a), pIcon);
		else if(Check > 0.01f)
			DrawIcon(pDraw, pLabel, pLabel->FontSize * (0.6f + 0.4f * Check), Right, Col(t.m_Text, a * Check), XC_ICON_CHECK);
		if(Clicked)
			PlayUiSound(SOUND_CLICK);
		return Clicked;
	};

	if(m_MenuKind == MENU_ADD)
	{
		Header(Tr("What do we add?"));
		int Num = 0;
		for(int i = 0; i < NUM_ELEMENTS; i++)
		{
			const SElementConfig Config = Element(i);
			if(*Config.m_pEnabled)
				continue;
			Num++;
			ImGui::PushID(i);
			if(Row("##add", Tr(Config.m_pName), ElementIcon(i), false, 0))
			{
				*Config.m_pEnabled = 1;
				*Config.m_pX = std::clamp((int)std::round(m_aMenuPos[0] / Io.DisplaySize.x * 1000.0f), 0, 1000);
				*Config.m_pY = std::clamp((int)std::round(m_aMenuPos[1] / Io.DisplaySize.y * 1000.0f), 0, 1000);
				m_aPop[i] = 0.0f; // grows in
				m_aaDrawPos[i][0] = m_aMenuPos[0];
				m_aaDrawPos[i][1] = m_aMenuPos[1];
				m_MenuKind = MENU_NONE;
			}
			ImGui::PopID();
		}
		if(!Num)
		{
			ImGui::PushFont(pSmall);
			ImGui::TextColored(t.m_TextMuted, "%s", Tr("Everything is on the screen already"));
			ImGui::PopFont();
		}
	}
	else if(m_MenuKind == MENU_ELEMENT && m_MenuElement >= 0)
	{
		const int i = m_MenuElement;
		const SElementConfig Config = Element(i);
		Header(Tr("Settings"));
		// the size: minus, the value, plus
		{
			char aSize[16];
			str_format(aSize, sizeof(aSize), "%d%%", *Config.m_pScale);
			const ImVec2 P = ImGui::GetCursorScreenPos();
			pDraw->AddText(pLabel, pLabel->FontSize * 1.05f, ImVec2(P.x + 8.0f * U, P.y + (RowH - pLabel->FontSize * 1.05f) * 0.5f), Col(t.m_TextSecondary, a), Tr("Size"));
			const float B = 26.0f * U;
			const float ValueW = 44.0f * U;
			float x = P.x + Inner - B * 2.0f - ValueW - 4.0f * U;
			const auto Step = [&](const char *pId, const char *pIcon, float X, int Delta) {
				ImGui::SetCursorScreenPos(ImVec2(X, P.y + (RowH - B) * 0.5f));
				const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(B, B));
				const float Hover = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
				pDraw->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Col(ImVec4(1, 1, 1, 0.05f + 0.07f * Hover), a), 8.0f * U);
				DrawIcon(pDraw, pSmall, pSmall->FontSize * 0.9f, ImVec2(X + B * 0.5f, P.y + RowH * 0.5f), Col(t.m_Text, a), pIcon);
				if(Clicked)
				{
					*Config.m_pScale = std::clamp(*Config.m_pScale + Delta, 50, 200);
					PlayUiSound(SOUND_CLICK);
				}
			};
			Step("##size_minus", XC_ICON_MINUS, x, -10);
			const float VW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, aSize).x;
			pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(x + B + (ValueW - VW) * 0.5f + 2.0f * U, P.y + (RowH - pLabel->FontSize) * 0.5f), Col(t.m_Text, a), aSize);
			Step("##size_plus", XC_ICON_PLUS, x + B + ValueW + 4.0f * U, 10);
			ImGui::SetCursorScreenPos(ImVec2(P.x, P.y + RowH + 2.0f * U));
			ImGui::Dummy(ImVec2(Inner, 0.0f));
		}
		// the own options of some elements
		if(i == ELEMENT_MUSIC && Row("##music_hide", Tr("Hide on pause"), nullptr, g_Config.m_XcHudMusicAutohide, 0))
			g_Config.m_XcHudMusicAutohide ^= 1;
		if(i == ELEMENT_LATENCY && Row("##latency_pred", Tr("Prediction"), nullptr, g_Config.m_XcHudLatencyPred, 0))
			g_Config.m_XcHudLatencyPred ^= 1;
		if(i == ELEMENT_ISLAND)
			RenderIslandMenu(pDraw, Inner, RowH, U, a);
		ImGui::Dummy(ImVec2(0.0f, 2.0f * U));
		if(Row("##remove", Tr("Remove"), XC_ICON_TRASH, false, 1))
		{
			*Config.m_pEnabled = 0;
			m_MenuKind = MENU_NONE;
		}
	}
	const bool Over = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ImGui::IsAnyItemActive();
	m_MenuHeight = ImGui::GetWindowHeight();
	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(5);
	(void)Label;
	return Over;
}

// the island: its parts on and off and in their order (dragged by the handle), the line of the song, an own text
void CXcHud::RenderIslandMenu(ImDrawList *pDraw, float Inner, float RowH, float U, float a)
{
	const STheme &t = Theme();
	ImFont *pLabel = m_Backend.Fonts().m_pLabel;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	struct SPart
	{
		char m_Key;
		const char *m_pName;
	};
	const SPart aAll[] = {{'m', Tr("Music")}, {'n', Tr("Notifications")}, {'w', Tr("Watermark")}};
	// the order: the parts that are on as saved, then those that are off
	char aOrder[4] = "";
	for(const char *p = g_Config.m_XcHudIslandParts; *p; p++)
		if(IndexOf("mnw", *p) >= 0 && IndexOf(aOrder, *p) < 0 && str_length(aOrder) < 3)
		{
			const char aOne[2] = {*p, '\0'};
			str_append(aOrder, aOne);
		}
	const int NumOn = str_length(aOrder);
	for(const SPart &Part : aAll)
	{
		const char aOne[2] = {Part.m_Key, '\0'};
		if(!str_find(aOrder, aOne))
			str_append(aOrder, aOne);
	}
	char aCount[32];
	str_format(aCount, sizeof(aCount), "%d %s %d", NumOn, Tr("of"), 3);
	{
		const ImVec2 P = ImGui::GetCursorScreenPos();
		pDraw->AddText(pLabel, pLabel->FontSize * 1.05f, ImVec2(P.x + 8.0f * U, P.y + 6.0f * U), Col(t.m_TextMuted, a), Tr("Parts"));
		const float CW = pLabel->CalcTextSizeA(pLabel->FontSize, FLT_MAX, 0.0f, aCount).x;
		pDraw->AddText(pLabel, pLabel->FontSize, ImVec2(P.x + Inner - 8.0f * U - CW, P.y + 6.0f * U), Col(t.m_Accent, a), aCount);
		ImGui::Dummy(ImVec2(Inner, pLabel->FontSize + 12.0f * U));
	}
	const float ListTop = ImGui::GetCursorScreenPos().y;
	const float Left = ImGui::GetCursorScreenPos().x;
	const float Slot = RowH + 2.0f * U;
	bool Changed = false;
	for(int r = 0; r < 3; r++)
	{
		const char Key = aOrder[r];
		const SPart *pPart = nullptr;
		for(const SPart &Part : aAll)
			if(Part.m_Key == Key)
				pPart = &Part;
		if(!pPart)
			continue;
		const int k = Key == 'm' ? 0 : (Key == 'n' ? 1 : 2);
		const bool On = r < NumOn;
		// the drawn place eases to the slot; the row held follows the mouse
		const float SlotY = ListTop + r * Slot;
		if(m_aIslandRowY[k] <= 0.0f || std::abs(m_aIslandRowY[k] - SlotY) > Slot * 4.0f)
			m_aIslandRowY[k] = SlotY;
		if(m_IslandRowDrag == k)
			m_aIslandRowY[k] = std::clamp(ImGui::GetIO().MousePos.y - m_IslandRowGrab, ListTop, ListTop + 2.0f * Slot);
		else
			m_aIslandRowY[k] += (SlotY - m_aIslandRowY[k]) * (1.0f - std::exp(-Dt * 16.0f));
		const float Y = m_aIslandRowY[k];
		ImGui::SetCursorScreenPos(ImVec2(Left, Y));
		ImGui::PushID(k);
		// the handle on the left drags, the rest of the row turns the part on and off
		const float HandleW = 24.0f * U;
		const bool Held = ImGui::InvisibleButton("##handle", ImVec2(HandleW, RowH));
		(void)Held;
		const bool HandleActive = ImGui::IsItemActive();
		if(ImGui::IsItemActivated())
		{
			m_IslandRowDrag = k;
			m_IslandRowGrab = ImGui::GetIO().MousePos.y - Y;
		}
		ImGui::SetCursorScreenPos(ImVec2(Left + HandleW, Y));
		const bool Toggle = ImGui::InvisibleButton("##toggle", ImVec2(Inner - HandleW, RowH));
		const float Hover = Animate(ImGui::GetID("##row_h"), (ImGui::IsItemHovered() || HandleActive) ? 1.0f : 0.0f, 18.0f);
		const float Check = Animate(ImGui::GetID("##row_c"), On ? 1.0f : 0.0f, 16.0f);
		ImGui::PopID();
		const float Lift = m_IslandRowDrag == k ? 1.0f : 0.0f;
		pDraw->AddRectFilled(ImVec2(Left, Y), ImVec2(Left + Inner, Y + RowH), Col(Lift > 0.0f ? t.m_Card : ImVec4(1, 1, 1, 0.05f * Hover), a), 9.0f * U);
		// six dots of the handle
		for(int d = 0; d < 6; d++)
			pDraw->AddCircleFilled(ImVec2(Left + 10.0f * U + (d % 2) * 5.0f * U, Y + RowH * 0.5f + (d / 2 - 1) * 5.0f * U), 1.4f * U, Col(t.m_TextMuted, a), 6);
		pDraw->AddText(pLabel, pLabel->FontSize * 1.05f, ImVec2(Left + HandleW + 4.0f * U, Y + (RowH - pLabel->FontSize * 1.05f) * 0.5f), Col(Mix(t.m_TextMuted, t.m_Text, Check), a), pPart->m_pName);
		if(Check > 0.01f)
			DrawIcon(pDraw, pLabel, pLabel->FontSize * (0.6f + 0.4f * Check), ImVec2(Left + Inner - 16.0f * U, Y + RowH * 0.5f), Col(t.m_Text, a * Check), XC_ICON_CHECK);
		if(Toggle)
		{
			PlayUiSound(SOUND_CLICK);
			// on: goes to the end of the parts that are on; off: leaves them
			char aNew[8] = "";
			for(int i = 0; i < NumOn; i++)
				if(aOrder[i] != Key)
				{
					const char aOne[2] = {aOrder[i], '\0'};
					str_append(aNew, aOne);
				}
			if(!On)
			{
				const char aOne[2] = {Key, '\0'};
				str_append(aNew, aOne);
			}
			str_copy(g_Config.m_XcHudIslandParts, aNew);
			Changed = true;
		}
	}
	// the row held: its slot by the mouse, the others make room
	if(m_IslandRowDrag >= 0 && !Changed)
	{
		const char Key = m_IslandRowDrag == 0 ? 'm' : (m_IslandRowDrag == 1 ? 'n' : 'w');
		const int From = IndexOf(aOrder, Key);
		const int To = std::clamp((int)std::round((m_aIslandRowY[m_IslandRowDrag] - ListTop) / Slot), 0, 2);
		if(To != From && From < NumOn && To < NumOn)
		{
			char aNew[4];
			str_copy(aNew, aOrder);
			const char Moved = aNew[From];
			if(From < To)
				for(int i = From; i < To; i++)
					aNew[i] = aNew[i + 1];
			else
				for(int i = From; i > To; i--)
					aNew[i] = aNew[i - 1];
			aNew[To] = Moved;
			aNew[NumOn] = '\0';
			str_copy(g_Config.m_XcHudIslandParts, aNew);
		}
		if(!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			m_IslandRowDrag = -1;
	}
	ImGui::SetCursorScreenPos(ImVec2(Left, ListTop + 3.0f * Slot));
	ImGui::Dummy(ImVec2(Inner, 0.0f));
	const auto Row = [&](const char *pId, const char *pText, bool Checked) {
		const ImVec2 P = ImGui::GetCursorScreenPos();
		const bool Clicked = ImGui::InvisibleButton(pId, ImVec2(Inner, RowH));
		const float Hover = Animate(ImGui::GetItemID(), ImGui::IsItemHovered() ? 1.0f : 0.0f, 18.0f);
		const float Check = Animate(ImGui::GetID("##chk"), Checked ? 1.0f : 0.0f, 16.0f);
		pDraw->AddRectFilled(P, ImVec2(P.x + Inner, P.y + RowH), Col(ImVec4(1, 1, 1, 0.06f * Hover), a), 9.0f * U);
		pDraw->AddText(pLabel, pLabel->FontSize * 1.05f, ImVec2(P.x + 8.0f * U, P.y + (RowH - pLabel->FontSize * 1.05f) * 0.5f), Col(Mix(t.m_TextSecondary, t.m_Text, Hover), a), pText);
		if(Check > 0.01f)
			DrawIcon(pDraw, pLabel, pLabel->FontSize * (0.6f + 0.4f * Check), ImVec2(P.x + Inner - 16.0f * U, P.y + RowH * 0.5f), Col(t.m_Text, a * Check), XC_ICON_CHECK);
		if(Clicked)
			PlayUiSound(SOUND_CLICK);
		return Clicked;
	};
	if(Row("##island_lyrics", Tr("The line of the song"), g_Config.m_XcHudIslandLyrics))
		g_Config.m_XcHudIslandLyrics ^= 1;
	// an own text
	{
		const ImVec2 P = ImGui::GetCursorScreenPos();
		pDraw->AddText(pLabel, pLabel->FontSize * 1.05f, ImVec2(P.x + 8.0f * U, P.y + 6.0f * U), Col(t.m_TextMuted, a), Tr("Own text"));
		ImGui::Dummy(ImVec2(Inner, pLabel->FontSize + 14.0f * U));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * U, 8.0f * U));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * U);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 0.05f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.08f));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.08f));
		ImGui::PushStyleColor(ImGuiCol_TextDisabled, t.m_TextMuted);
		ImGui::PushFont(pLabel);
		ImGui::SetNextItemWidth(Inner);
		ImGui::InputTextWithHint("##island_text", Tr("Enter the text"), g_Config.m_XcHudIslandText, sizeof(g_Config.m_XcHudIslandText));
		ImGui::PopFont();
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar(2);
	}
}
