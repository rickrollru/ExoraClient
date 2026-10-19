// A short guide over the map editor: the screen dims, one part of the editor stays lit and a card
// next to it says in plain words what this part does. Opens by itself the first time and with the
// "Guide" button in the menu bar.
#include "editor_style.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/ui.h>
#include <game/editor/editor.h>

#include <algorithm>

using XcUi::Tr;

namespace
{
struct SGuideStep
{
	int m_Zone; // -1 = no highlight, the card is centered
	const char *m_pTitle;
	const char *m_pText;
};

const SGuideStep s_aSteps[] = {
	{-1, "Welcome to the map editor", "This short tour shows what every part of the editor does. It looks like a photo editor: layers on the left, tools on top, the picture in the middle."},
	{CEditor::XC_ZONE_MENU, "Menu", "File: new map, open, save, save as and export. Tools: automapper, removing unused images and sounds and other helpers. Settings: grid, colors and editor options."},
	{CEditor::XC_ZONE_TABS, "Open maps", "Every open map has a tab, like documents in Photoshop. Switch between maps here, the cross closes a map."},
	{CEditor::XC_ZONE_TOOLBAR, "Tool bar", "HD shows detail layers, play starts the animations, Proof shows what a player sees on the screen. Then zoom, grid, flip and rotate for the brush, and colors to paint quads."},
	{CEditor::XC_ZONE_TOOLBOX, "Layers", "Like layers in Photoshop: groups hold layers, the eye hides a layer, a click selects it, a right click opens its settings. The Game layer is where the physics are: walls, freeze, death. The icons on top switch to images and sounds."},
	{CEditor::XC_ZONE_VIEW, "Canvas", "Left mouse: select tiles and make them a brush, then paint with it. Right mouse: drop the brush. Wheel: zoom. Middle mouse or space: move around. Ctrl+Z undoes, Ctrl+S saves."},
	{CEditor::XC_ZONE_STATUS, "Hints and panels", "The line at the bottom explains what is under the mouse. On the right: History (every change, click to go back), Server settings of the map and Envelopes (animations for quads and colors)."},
	{-1, "XClient extras", "Images in other formats (jpg, bmp, gif, tga) turn into png by themselves when you add them. In the image settings you can upscale an image two or four times: sharp for pixel art or smooth for drawings. This guide is always behind the Guide button on top."},
};
constexpr int NUM_STEPS = (int)std::size(s_aSteps);
} // namespace

void CEditor::XcRenderGuide()
{
	if(!XcEditorStyle::Enabled())
		return;
	// the first time the editor opens, the guide opens too
	if(!g_Config.m_XcEditorGuideSeen)
	{
		g_Config.m_XcEditorGuideSeen = 1;
		m_XcGuideOpen = true;
		m_XcGuideStep = 0;
	}
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_XcGuideAnim = m_XcGuideOpen ? std::min(m_XcGuideAnim + Dt * 6.0f, 1.0f) : std::max(m_XcGuideAnim - Dt * 6.0f, 0.0f);
	if(m_XcGuideAnim <= 0.0f)
		return;
	const float Alpha = m_XcGuideAnim * m_XcGuideAnim * (3.0f - 2.0f * m_XcGuideAnim);

	const CUIRect Screen = *Ui()->Screen();
	const SGuideStep &Step = s_aSteps[std::clamp(m_XcGuideStep, 0, NUM_STEPS - 1)];
	const bool Highlight = Step.m_Zone >= 0 && m_GuiActive;
	CUIRect Lit = Highlight ? m_aXcZones[Step.m_Zone] : CUIRect{Screen.w * 0.5f, Screen.h * 0.5f, 0.0f, 0.0f};
	if(Highlight)
		Lit.Margin(-2.0f, &Lit);

	// everything except the lit part gets dark; an invisible button over the whole screen keeps the
	// clicks away from the editor while the guide is open
	if(m_XcGuideOpen)
	{
		static int s_Blocker = 0;
		Ui()->DoButtonLogic(&s_Blocker, 0, &Screen, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
	}
	const ColorRGBA Shade(0.0f, 0.0f, 0.0f, 0.62f * Alpha);
	const CUIRect aShades[] = {
		{0.0f, 0.0f, Screen.w, Lit.y},
		{0.0f, Lit.y + Lit.h, Screen.w, Screen.h - Lit.y - Lit.h},
		{0.0f, Lit.y, Lit.x, Lit.h},
		{Lit.x + Lit.w, Lit.y, Screen.w - Lit.x - Lit.w, Lit.h},
	};
	for(const CUIRect &Rect : aShades)
		if(Rect.w > 0.0f && Rect.h > 0.0f)
			Rect.Draw(Shade, IGraphics::CORNER_NONE, 0.0f);
	if(Highlight)
	{
		// accent frame around the lit part, pulsing a little
		const float Pulse = 0.75f + 0.25f * std::sin(Client()->GlobalTime() * 4.0f);
		const ColorRGBA Frame = XcEditorStyle::Accent(Alpha * Pulse);
		const float t = 1.5f;
		const CUIRect aFrame[] = {
			{Lit.x, Lit.y, Lit.w, t},
			{Lit.x, Lit.y + Lit.h - t, Lit.w, t},
			{Lit.x, Lit.y, t, Lit.h},
			{Lit.x + Lit.w - t, Lit.y, t, Lit.h},
		};
		for(const CUIRect &Rect : aFrame)
			Rect.Draw(Frame, IGraphics::CORNER_NONE, 0.0f);
	}

	// the card: under the lit part when there is room, otherwise next to it or in the middle
	const float CardW = 330.0f;
	const float Pad = 10.0f;
	const float TitleSize = 14.0f;
	const float TextSize = 10.5f;
	const char *pTitle = Tr(Step.m_pTitle);
	const char *pText = Tr(Step.m_pText);
	const STextBoundingBox TextBox = TextRender()->TextBoundingBox(TextSize, pText, -1, CardW - Pad * 2.0f);
	const float CardH = Pad + TitleSize + 6.0f + TextBox.m_H + 10.0f + 18.0f + Pad;
	CUIRect Card;
	Card.w = CardW;
	Card.h = CardH;
	if(!Highlight)
	{
		Card.x = (Screen.w - CardW) * 0.5f;
		Card.y = (Screen.h - CardH) * 0.5f;
	}
	else if(Lit.y + Lit.h + CardH + 12.0f < Screen.h && Lit.h < Screen.h * 0.5f)
	{
		Card.x = std::clamp(Lit.x + 12.0f, 8.0f, Screen.w - CardW - 8.0f);
		Card.y = Lit.y + Lit.h + 10.0f;
	}
	else if(Lit.x + Lit.w + CardW + 12.0f < Screen.w && Lit.w < Screen.w * 0.5f)
	{
		Card.x = Lit.x + Lit.w + 10.0f;
		Card.y = std::clamp(Lit.y + 10.0f, 8.0f, Screen.h - CardH - 8.0f);
	}
	else
	{
		Card.x = (Screen.w - CardW) * 0.5f;
		Card.y = std::clamp(Lit.y + Lit.h * 0.5f - CardH * 0.5f, 8.0f, Screen.h - CardH - 8.0f);
	}
	Card.y += (1.0f - Alpha) * 8.0f; // slides up while it fades in

	CUIRect Shadow = Card;
	Shadow.Margin(-3.0f, &Shadow);
	Shadow.y += 2.0f;
	Shadow.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f * Alpha), IGraphics::CORNER_ALL, 9.0f);
	Card.Draw(XcEditorStyle::Panel().WithAlpha(0.98f * Alpha), IGraphics::CORNER_ALL, 7.0f);
	CUIRect Top = {Card.x + 14.0f, Card.y, Card.w - 28.0f, 1.5f};
	Top.Draw(XcEditorStyle::Accent(Alpha), IGraphics::CORNER_NONE, 0.0f);

	CUIRect Body;
	Card.Margin(Pad, &Body);
	CUIRect TitleRect, StepRect;
	Body.HSplitTop(TitleSize, &TitleRect, &Body);
	TitleRect.VSplitRight(40.0f, &TitleRect, &StepRect);
	TextRender()->TextColor(ColorRGBA(0.95f, 0.95f, 0.96f, Alpha));
	Ui()->DoLabel(&TitleRect, pTitle, TitleSize, TEXTALIGN_ML);
	char aStep[16];
	str_format(aStep, sizeof(aStep), "%d / %d", m_XcGuideStep + 1, NUM_STEPS);
	TextRender()->TextColor(ColorRGBA(0.55f, 0.57f, 0.61f, Alpha));
	Ui()->DoLabel(&StepRect, aStep, TextSize, TEXTALIGN_MR);
	Body.HSplitTop(6.0f, nullptr, &Body);
	TextRender()->TextColor(ColorRGBA(0.79f, 0.80f, 0.82f, Alpha));
	TextRender()->Text(Body.x, Body.y, TextSize, pText, CardW - Pad * 2.0f);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	// progress dots and the buttons
	CUIRect Buttons;
	Card.Margin(Pad, &Buttons);
	Buttons.HSplitBottom(18.0f, nullptr, &Buttons);
	for(int i = 0; i < NUM_STEPS; i++)
	{
		const CUIRect Dot = {Buttons.x + i * 9.0f, Buttons.y + 7.0f, i == m_XcGuideStep ? 12.0f : 4.0f, 4.0f};
		Dot.Draw(i == m_XcGuideStep ? XcEditorStyle::Accent(Alpha) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.2f * Alpha), IGraphics::CORNER_ALL, 2.0f);
		if(i == m_XcGuideStep)
			Buttons.x += 8.0f;
	}
	if(!m_XcGuideOpen)
		return;
	CUIRect Next, Back, Close;
	Buttons.VSplitRight(70.0f, &Buttons, &Next);
	Buttons.VSplitRight(5.0f, &Buttons, nullptr);
	Buttons.VSplitRight(55.0f, &Buttons, &Back);
	Buttons.VSplitRight(5.0f, &Buttons, nullptr);
	Buttons.VSplitRight(55.0f, &Buttons, &Close);
	const bool Last = m_XcGuideStep >= NUM_STEPS - 1;
	static int s_NextButton = 0, s_BackButton = 0, s_CloseButton = 0;
	if(DoButton_Editor(&s_NextButton, Last ? Tr("Done") : Tr("Next"), 1, &Next, BUTTONFLAG_LEFT, nullptr) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
	{
		if(Last)
			m_XcGuideOpen = false;
		else
			m_XcGuideStep++;
	}
	if(m_XcGuideStep > 0 && DoButton_Editor(&s_BackButton, Tr("Back"), 0, &Back, BUTTONFLAG_LEFT, nullptr))
		m_XcGuideStep--;
	if(DoButton_Editor(&s_CloseButton, Tr("Close"), 0, &Close, BUTTONFLAG_LEFT, nullptr))
		m_XcGuideOpen = false;
}
