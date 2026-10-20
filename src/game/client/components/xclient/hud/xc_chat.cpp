// The new chat (xc_chat_new): CChat keeps the lines, the input and the commands, the HUD draws them
// in one of five looks: minimal text, a plate per message, glass cards, messenger bubbles or a panel.

#include "xc_hud.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/xc_render.h>

#include <game/client/animstate.h>
#include <game/client/components/chat.h>
#include <game/client/components/xclient/ui/icons.h>
#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <ctime>
#include <vector>

using namespace XcUi;

namespace
{
struct SChatTee
{
	CXcHud *m_pHud;
	int m_LineIndex;
	float m_aPos[2];
	float m_Size;
	float m_Alpha;
};

// one wrapped row of a message
struct SRow
{
	const char *m_pBegin;
	const char *m_pEnd;
};

// a message laid out, before it is drawn
struct SItem
{
	int m_Index; // in CChat::m_aLines
	bool m_Header; // the first message of a group: avatar and name
	bool m_Own;
	int m_Kind; // 0 normal, 1 mention, 2 team, 3 whisper, 4 server
	float m_Alpha;
	float m_Slide; // x offset of the appear animation
	float m_Height;
	float m_ContentWidth; // the widest row with the name
	float m_NameWidth;
	std::vector<SRow> m_vRows;
	float m_Y; // top
};

ImVec4 ToImVec(ColorRGBA c) { return ImVec4(c.r, c.g, c.b, c.a); }

// word wrap with a narrower first row (the name sits before it)
void WrapText(ImFont *pFont, float Size, const char *pText, float FirstWidth, float Width, std::vector<SRow> &vRows)
{
	const char *pEnd = pText + str_length(pText);
	const char *p = pText;
	const float Scale = Size / pFont->FontSize;
	bool First = true;
	while(p < pEnd)
	{
		const float Avail = std::max(First ? FirstWidth : Width, Size * 2.0f);
		const char *pBreak = pFont->CalcWordWrapPositionA(Scale, p, pEnd, Avail);
		// the first row can be too short for even one word: start the text on the next row
		if(pBreak == p && First && FirstWidth < Width)
		{
			vRows.push_back({p, p});
			First = false;
			continue;
		}
		if(pBreak == p)
			pBreak = p + std::max(1, (int)str_utf8_forward(p, 0));
		const char *pNewline = (const char *)memchr(p, '\n', pBreak - p);
		if(pNewline)
			pBreak = pNewline;
		vRows.push_back({p, pBreak});
		p = pBreak;
		while(p < pEnd && (*p == ' ' || *p == '\n'))
			p++;
		First = false;
	}
	if(vRows.empty())
		vRows.push_back({pText, pText});
}
} // namespace

bool CXcHud::DrawsChat() const
{
	return g_Config.m_XcChatNew && g_Config.m_XcHud && m_Backend.IsReady() &&
	       (Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK);
}

void CXcHud::ChatTeeCallback(const ImDrawList *pList, const ImDrawCmd *pCmd)
{
	(void)pList;
	const SChatTee *pData = static_cast<const SChatTee *>(pCmd->UserCallbackData);
	CXcHud *pHud = pData->m_pHud;
	const auto &Line = pHud->GameClient()->m_Chat.m_aLines[pData->m_LineIndex];
	if(!Line.m_pManagedTeeRenderInfo)
		return;
	CTeeRenderInfo Info = Line.m_pManagedTeeRenderInfo->TeeRenderInfo();
	Info.m_Size = pData->m_Size;
	const CAnimState *pIdle = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdle, &Info, OffsetToMid);
	pHud->RenderTools()->RenderTee(pIdle, &Info, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(pData->m_aPos[0], pData->m_aPos[1]) + OffsetToMid, pData->m_Alpha);
}

void CXcHud::AddChatTee(ImDrawList *pDraw, int LineIndex, ImVec2 Center, float Size, float Alpha)
{
	const SChatTee Data = {this, LineIndex, {Center.x, Center.y}, Size, Alpha};
	pDraw->AddCallback(ChatTeeCallback, (void *)&Data, sizeof(Data));
	pDraw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void CXcHud::RenderChat(ImDrawList *pDraw, float Dt, float HudAlpha)
{
	CChat &Chat = GameClient()->m_Chat;
	const STheme &t = Theme();
	const int Look = g_Config.m_XcChatLook;
	const bool Typing = Chat.IsActive();
	const bool ShowAll = Chat.m_PrevShowChat;
	const float Speed = AnimationSpeed() > 0.0f ? 10.0f * AnimationSpeed() : 1000.0f;
	m_ChatOpen = Typing ? std::min(m_ChatOpen + Dt * Speed, 1.0f) : std::max(m_ChatOpen - Dt * Speed, 0.0f);
	m_vChatBlurRects.clear();

	const float H = (float)Graphics()->ScreenHeight();
	const float U0 = H / 1080.0f;
	const float U = U0 * g_Config.m_XcChatScale / 100.0f;
	const float Width = g_Config.m_XcChatWidth * U0;
	const float Left = 16.0f * U0;
	const float StatusBar = g_Config.m_TcStatusBar ? g_Config.m_TcStatusBarHeight / 300.0f * H : 0.0f;
	const float InputHeight = 34.0f * U;
	const float InputBottom = H - (22.0f + g_Config.m_XcChatY) * U0 - StatusBar;
	const float InputTop = InputBottom - InputHeight;
	// the messages stand on the input, and drop down to its place when it is closed
	const float MessagesBottom = mix(InputBottom, InputTop - 8.0f * U, m_ChatOpen);
	const float TopLimit = MessagesBottom - H * (ShowAll ? 0.5f : 0.3f);

	ImFont *pName = m_Backend.Fonts().m_pLabel;
	ImFont *pBody = m_Backend.Fonts().m_pBody;
	ImFont *pSmall = m_Backend.Fonts().m_pSmall;
	const float TextSize = 14.5f * U;
	const float SmallSize = 11.5f * U;
	const float RowHeight = TextSize * 1.32f;
	const bool Avatars = g_Config.m_XcChatAvatars;
	const bool Times = g_Config.m_XcChatTime;
	const float Avatar = (Look == XC_CHAT_MINIMAL || Look == XC_CHAT_PANEL ? 20.0f : 26.0f) * U;
	const bool Inline = Look == XC_CHAT_MINIMAL || Look == XC_CHAT_PLATES || Look == XC_CHAT_PANEL;
	const float PadX = Look == XC_CHAT_MINIMAL ? 2.0f * U : 10.0f * U;
	const float PadY = Look == XC_CHAT_MINIMAL ? 1.0f * U : (Look == XC_CHAT_PANEL ? 2.0f * U : 6.0f * U);
	const float Gap = Look == XC_CHAT_MINIMAL || Look == XC_CHAT_PANEL ? 2.0f * U : 5.0f * U;
	const float AvatarColumn = Avatars ? Avatar + 8.0f * U : 0.0f;
	const float BubbleMax = Width * 0.82f;

	const int64_t Now = time();
	const int64_t Freq = time_freq();
	const float Lifetime = (float)g_Config.m_XcChatLifetime;
	const int MaxItems = ShowAll ? g_Config.m_XcChatHistory : g_Config.m_XcChatLines;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	const int DummyId = Client()->DummyConnected() ? GameClient()->m_aLocalIds[!g_Config.m_ClDummy] : -1;

	// lay the messages out, newest first
	std::vector<SItem> vItems;
	float y = MessagesBottom;
	for(int i = 0; i < CChat::MAX_LINES && (int)vItems.size() < MaxItems; i++)
	{
		const int Index = ((Chat.m_CurrentLine - i) + CChat::MAX_LINES) % CChat::MAX_LINES;
		const auto &Line = Chat.m_aLines[Index];
		if(!Line.m_Initialized)
			break;
		const float Age = (Now - Line.m_Time) / (float)Freq;
		if(!ShowAll && Age > Lifetime)
			break;

		SItem Item;
		Item.m_Index = Index;
		Item.m_Own = Line.m_ClientId >= 0 && (Line.m_ClientId == LocalId || Line.m_ClientId == DummyId);
		Item.m_Kind = Line.m_Highlighted ? 1 : (Line.m_Whisper ? 3 : (Line.m_Team ? 2 : (Line.m_ClientId < 0 ? 4 : 0)));
		// a group: several messages of one player in a row, only the first one gets the avatar and the name
		Item.m_Header = true;
		if(g_Config.m_XcChatGroup && (Line.m_ClientId >= 0 || Line.m_ClientId == CChat::SERVER_MSG))
		{
			const auto &Older = Chat.m_aLines[(Index - 1 + CChat::MAX_LINES) % CChat::MAX_LINES];
			if(Older.m_Initialized && Older.m_ClientId == Line.m_ClientId && Older.m_Team == Line.m_Team && Older.m_Whisper == Line.m_Whisper &&
				str_comp(Older.m_aName, Line.m_aName) == 0 && (Line.m_Time - Older.m_Time) < 90 * Freq && Older.m_Time <= Line.m_Time)
				Item.m_Header = false;
		}

		// fade out at the end of the life time, appear with the chosen animation
		float Alpha = ShowAll ? 1.0f : std::clamp((Lifetime - Age) / 1.5f, 0.0f, 1.0f);
		Item.m_Slide = 0.0f;
		if(g_Config.m_XcChatAnim)
		{
			const float Appear = std::clamp(Age / 0.3f, 0.0f, 1.0f);
			const float Ease = 1.0f - std::pow(1.0f - Appear, 3.0f);
			Alpha *= Ease;
			if(g_Config.m_XcChatAnimStyle == 0)
				Item.m_Slide = -(1.0f - Ease) * 26.0f * U;
		}
		Item.m_Alpha = Alpha;

		const bool HasName = Line.m_ClientId >= 0 && Line.m_aName[0];
		// the text: translated by TClient when there is a translation
		const char *pText = Line.m_aText;
		if(Line.m_pTranslateResponse && !Line.m_pTranslateResponse->m_Error && Line.m_pTranslateResponse->m_Text[0])
			pText = Line.m_pTranslateResponse->m_Text;
		Item.m_NameWidth = HasName && Item.m_Header ? pName->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, Line.m_aName).x : 0.0f;
		if(Line.m_TimesRepeated > 0)
			Item.m_NameWidth += 26.0f * U;
		const bool Bubble = Look == XC_CHAT_BUBBLES;
		const float Column = (Bubble && Item.m_Own) ? 0.0f : AvatarColumn;
		const float TextWidth = (Bubble ? BubbleMax : Width) - Column - PadX * 2.0f;
		float FirstWidth = TextWidth;
		if(Inline && Item.m_NameWidth > 0.0f)
			FirstWidth -= Item.m_NameWidth + TextSize * 0.45f;
		if(Times && Item.m_Header && (Inline || !HasName))
			FirstWidth -= 44.0f * U;
		WrapText(pBody, TextSize, pText, FirstWidth, TextWidth, Item.m_vRows);
		float Widest = 0.0f;
		for(size_t r = 0; r < Item.m_vRows.size(); r++)
		{
			float w = pBody->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, Item.m_vRows[r].m_pBegin, Item.m_vRows[r].m_pEnd).x;
			if(r == 0 && Inline && Item.m_NameWidth > 0.0f)
				w += Item.m_NameWidth + TextSize * 0.45f;
			Widest = std::max(Widest, w);
		}
		// cards and bubbles: the name on its own row (the time goes next to it, or next to the text without a name)
		const bool NameRow = !Inline && Item.m_Header && HasName && !(Bubble && Item.m_Own && !Times);
		if(NameRow)
			Widest = std::max(Widest, Item.m_NameWidth + (Times ? 46.0f * U : 0.0f));
		else if(!Inline && Times && Item.m_Header)
			Widest += 44.0f * U;
		Item.m_ContentWidth = Widest;
		Item.m_Height = PadY * 2.0f + Item.m_vRows.size() * RowHeight + (NameRow ? RowHeight * 0.95f : 0.0f);
		if(Avatars && Item.m_Header && !(Bubble && Item.m_Own))
			Item.m_Height = std::max(Item.m_Height, Avatar + PadY * 2.0f);
		y -= Item.m_Height;
		if(y < TopLimit)
			break;
		Item.m_Y = y;
		y -= Item.m_Header ? Gap * 1.6f : Gap;
		vItems.push_back(std::move(Item));
	}

	const float ScoreboardFade = 1.0f - m_ScoreboardOpen;
	const float LinesAlpha = HudAlpha * ScoreboardFade;

	// the panel look: one panel behind all messages, a little higher when the chat is open
	if(Look == XC_CHAT_PANEL && (!vItems.empty() || m_ChatOpen > 0.0f))
	{
		float Top = vItems.empty() ? MessagesBottom - 30.0f * U : vItems.back().m_Y;
		float PanelAlpha = 0.0f;
		for(const SItem &Item : vItems)
			PanelAlpha = std::max(PanelAlpha, Item.m_Alpha);
		PanelAlpha = std::max(PanelAlpha, m_ChatOpen) * LinesAlpha;
		const float HeaderH = 24.0f * U * m_ChatOpen;
		Top -= 8.0f * U + HeaderH;
		const ImVec2 Min(Left - 6.0f * U, Top);
		const ImVec2 Max(Left + Width + 6.0f * U, mix(MessagesBottom + 6.0f * U, InputBottom + 6.0f * U, m_ChatOpen));
		pDraw->AddRectFilled(ImVec2(Min.x, Min.y + 4.0f * U), ImVec2(Max.x, Max.y + 4.0f * U), Col(ImVec4(0, 0, 0, 1), 0.12f * PanelAlpha), 14.0f * U);
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, g_Config.m_XcChatOpacity / 100.0f), PanelAlpha), 14.0f * U);
		pDraw->AddRect(Min, Max, Col(t.m_Border, PanelAlpha), 14.0f * U, 0, 1.0f);
		if(HeaderH > 1.0f)
		{
			const float a = PanelAlpha * m_ChatOpen;
			pDraw->AddText(pName, 12.5f * U, ImVec2(Min.x + 12.0f * U, Min.y + 7.0f * U), Col(t.m_Text, a), Tr("Chat"));
			pDraw->AddLine(ImVec2(Min.x + 10.0f * U, Min.y + HeaderH + 2.0f * U), ImVec2(Max.x - 10.0f * U, Min.y + HeaderH + 2.0f * U), Col(t.m_Border, a), 1.0f);
		}
	}

	for(const SItem &Item : vItems)
	{
		const auto &Line = Chat.m_aLines[Item.m_Index];
		const float a = Item.m_Alpha * LinesAlpha;
		if(a <= 0.003f)
			continue;
		const bool Bubble = Look == XC_CHAT_BUBBLES;
		const bool RightSide = Bubble && Item.m_Own;
		const ImVec4 Mark = ToImVec(ChatMarkColor(Item.m_Kind == 0 ? 1 : Item.m_Kind));
		// colors of the client (design system) or of the game (xc_chat_name_color 3)
		ImVec4 NameColor, TextColor;
		if(g_Config.m_XcChatNameColor == XC_CHAT_COLORS_GAME)
		{
			NameColor = ToImVec(Chat.XcNameColor(Item.m_Index));
			TextColor = ToImVec(Chat.XcTextColor(Item.m_Index));
		}
		else
		{
			// the minimal look has no plate: always light text over the game
			const ImVec4 Bright = Look == XC_CHAT_MINIMAL ? ImVec4(0.949f, 0.953f, 0.961f, 1.0f) : t.m_Text;
			const ImVec4 Secondary = Look == XC_CHAT_MINIMAL ? ImVec4(0.85f, 0.86f, 0.88f, 1.0f) : t.m_TextSecondary;
			const ImVec4 Muted = Look == XC_CHAT_MINIMAL ? ImVec4(0.66f, 0.68f, 0.72f, 1.0f) : t.m_TextMuted;
			NameColor = Item.m_Own ? t.m_Accent : Bright;
			if(Item.m_Kind == 2 || Item.m_Kind == 3)
				NameColor = Mark;
			if(g_Config.m_XcChatNameColor == XC_CHAT_COLORS_ACCENT && Line.m_ClientId >= 0)
				NameColor = t.m_Accent;
			else if(g_Config.m_XcChatNameColor == XC_CHAT_COLORS_SKIN && Line.m_ClientId >= 0 && Line.m_pManagedTeeRenderInfo)
				NameColor = ToImVec(Line.m_pManagedTeeRenderInfo->TeeRenderInfo().m_ColorBody.WithAlpha(1.0f));
			TextColor = Item.m_Kind == 4 || Line.m_ClientId == CChat::CLIENT_MSG ? Muted : (Item.m_Kind == 1 ? Bright : Secondary);
		}
		if(RightSide)
			TextColor = NameColor = t.m_OnAccent;

		const float Column = RightSide ? 0.0f : AvatarColumn;
		const float BoxWidth = std::min(Item.m_ContentWidth + PadX * 2.0f, (Bubble ? BubbleMax : Width) - Column);
		float BoxX = Left + Column + Item.m_Slide;
		if(RightSide)
			BoxX = Left + Width - BoxWidth - Item.m_Slide;
		const ImVec2 BoxMin(BoxX, Item.m_Y);
		const ImVec2 BoxMax(BoxX + (Look == XC_CHAT_PANEL || Look == XC_CHAT_MINIMAL ? Width - Column : BoxWidth), Item.m_Y + Item.m_Height);
		const float Rounding = (Bubble ? 14.0f : 12.0f) * U; // cards of the design system: 10-14 px

		// the background of the message
		switch(Look)
		{
		case XC_CHAT_PLATES:
			// a card of the menu: card color, 1 px border, a soft shadow
			pDraw->AddRectFilled(ImVec2(BoxMin.x, BoxMin.y + 2.0f * U), ImVec2(BoxMax.x, BoxMax.y + 2.0f * U), Col(ImVec4(0, 0, 0, 1), 0.12f * a), Rounding);
			pDraw->AddRectFilled(BoxMin, BoxMax, Col(ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, g_Config.m_XcChatOpacity / 100.0f), a), Rounding);
			pDraw->AddRect(BoxMin, BoxMax, Col(t.m_Border, a), Rounding, 0, 1.0f);
			break;
		case XC_CHAT_GLASS:
		{
			// glass: the game blurred behind it (render module), a light film and a bright top edge
			if(a > 0.25f)
				m_vChatBlurRects.push_back({BoxMin.x, BoxMin.y, BoxMax.x, BoxMax.y});
			pDraw->AddRectFilled(ImVec2(BoxMin.x, BoxMin.y + 3.0f * U), ImVec2(BoxMax.x, BoxMax.y + 3.0f * U), Col(ImVec4(0, 0, 0, 1), 0.14f * a), Rounding);
			pDraw->AddRectFilled(BoxMin, BoxMax, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, g_Config.m_XcChatOpacity / 100.0f * 0.55f), a), Rounding);
			pDraw->AddRectFilled(BoxMin, BoxMax, Col(ImVec4(1, 1, 1, 1), 0.04f * a), Rounding);
			pDraw->AddRectFilledMultiColor(ImVec2(BoxMin.x + Rounding * 0.5f, BoxMin.y + 1.0f), ImVec2(BoxMax.x - Rounding * 0.5f, BoxMin.y + (BoxMax.y - BoxMin.y) * 0.45f),
				Col(ImVec4(1, 1, 1, 1), 0.07f * a), Col(ImVec4(1, 1, 1, 1), 0.07f * a), Col(ImVec4(1, 1, 1, 1), 0.0f), Col(ImVec4(1, 1, 1, 1), 0.0f));
			pDraw->AddRect(BoxMin, BoxMax, Col(ImVec4(1, 1, 1, 1), 0.16f * a), Rounding, 0, 1.0f);
			break;
		}
		case XC_CHAT_BUBBLES:
		{
			const ImVec4 Fill = RightSide ? t.m_Accent : ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, std::max(g_Config.m_XcChatOpacity / 100.0f, 0.35f));
			pDraw->AddRectFilled(ImVec2(BoxMin.x, BoxMin.y + 2.0f * U), ImVec2(BoxMax.x, BoxMax.y + 2.0f * U), Col(ImVec4(0, 0, 0, 1), 0.1f * a), Rounding);
			pDraw->AddRectFilled(BoxMin, BoxMax, Col(Fill, a), Rounding);
			// a small tail at the top corner on the side of the avatar
			if(Item.m_Header && Line.m_ClientId >= 0)
			{
				const float Tx = RightSide ? BoxMax.x : BoxMin.x;
				const float Dir = RightSide ? 1.0f : -1.0f;
				pDraw->AddTriangleFilled(ImVec2(Tx - Dir * 8.0f * U, BoxMin.y), ImVec2(Tx + Dir * 6.0f * U, BoxMin.y), ImVec2(Tx - Dir * 2.0f * U, BoxMin.y + 10.0f * U), Col(Fill, a));
			}
			break;
		}
		case XC_CHAT_MINIMAL:
			if(Item.m_Kind == 1)
				pDraw->AddRectFilledMultiColor(BoxMin, ImVec2(BoxMin.x + Width * 0.6f, BoxMax.y), Col(Mark, 0.16f * a), Col(Mark, 0.0f), Col(Mark, 0.0f), Col(Mark, 0.16f * a));
			break;
		default:
			break;
		}
		// mentions, team chat, whispers and server lines get a colored edge
		if(Item.m_Kind != 0 && !Bubble)
			pDraw->AddRectFilled(ImVec2(BoxMin.x + (Look == XC_CHAT_MINIMAL ? -6.0f * U : 0.0f), BoxMin.y + 4.0f * U), ImVec2(BoxMin.x + (Look == XC_CHAT_MINIMAL ? -3.5f * U : 2.5f * U), BoxMax.y - 4.0f * U), Col(Mark, a), 2.0f * U);

		// the avatar: the tee of the player at the time of the message
		if(Avatars && Item.m_Header && !RightSide)
		{
			const ImVec2 Center(Left + Item.m_Slide + Avatar * 0.5f, Item.m_Y + PadY + Avatar * 0.5f);
			if(Look != XC_CHAT_MINIMAL)
				pDraw->AddCircleFilled(Center, Avatar * 0.58f, Col(ImVec4(t.m_Card.x, t.m_Card.y, t.m_Card.z, 0.9f), a), 24);
			if(Line.m_pManagedTeeRenderInfo && Line.m_ClientId >= 0)
				AddChatTee(pDraw, Item.m_Index, Center, Avatar * 0.95f, a);
			else
			{
				const char *pIcon = XC_ICON_SERVER;
				const ImVec2 Dim = pName->CalcTextSizeA(Avatar * 0.5f, FLT_MAX, 0.0f, pIcon);
				pDraw->AddText(pName, Avatar * 0.5f, ImVec2(Center.x - Dim.x * 0.5f, Center.y - Dim.y * 0.5f), Col(t.m_TextMuted, a), pIcon);
			}
		}

		// text with a soft shadow in the minimal look, it has no plate under it
		const bool Shadow = Look == XC_CHAT_MINIMAL && g_Config.m_XcChatShadow;
		const auto Text = [&](ImFont *pFont, float Size, ImVec2 Pos, const ImVec4 &Color, const char *pBegin, const char *pEnd = nullptr) {
			if(Shadow)
				pDraw->AddText(pFont, Size, ImVec2(Pos.x + 1.2f * U, Pos.y + 1.2f * U), Col(ImVec4(0, 0, 0, 1), 0.6f * a), pBegin, pEnd);
			pDraw->AddText(pFont, Size, Pos, Col(Color, a), pBegin, pEnd);
		};
		char aTime[16] = "";
		if(Times && Item.m_Header)
		{
			const time_t Stamp = ::time(nullptr) - (time_t)((Now - Line.m_Time) / Freq);
			tm Local;
#if defined(CONF_FAMILY_WINDOWS)
			localtime_s(&Local, &Stamp);
#else
			localtime_r(&Stamp, &Local);
#endif
			str_format(aTime, sizeof(aTime), "%02d:%02d", Local.tm_hour, Local.tm_min);
		}
		const float ContentX = BoxMin.x + PadX;
		float RowY = Item.m_Y + PadY + (RowHeight - TextSize) * 0.5f;
		const bool HasName = Line.m_ClientId >= 0 && Line.m_aName[0] && Item.m_Header;
		float FirstRowX = ContentX;
		if(Inline)
		{
			if(aTime[0])
			{
				Text(pSmall, SmallSize, ImVec2(FirstRowX, RowY + (TextSize - SmallSize) * 0.6f), t.m_TextMuted, aTime);
				FirstRowX += 40.0f * U;
			}
			if(HasName)
			{
				Text(pName, TextSize, ImVec2(FirstRowX, RowY), NameColor, Line.m_aName);
				FirstRowX += pName->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, Line.m_aName).x;
				if(Line.m_TimesRepeated > 0)
				{
					char aCount[16];
					str_format(aCount, sizeof(aCount), "x%d", Line.m_TimesRepeated + 1);
					pDraw->AddRectFilled(ImVec2(FirstRowX + 4.0f * U, RowY + 1.0f * U), ImVec2(FirstRowX + 24.0f * U, RowY + TextSize), Col(t.m_Track, a), 4.0f * U);
					pDraw->AddText(pSmall, SmallSize, ImVec2(FirstRowX + 7.0f * U, RowY + 2.0f * U), Col(t.m_TextSecondary, a), aCount);
					FirstRowX += 26.0f * U;
				}
				FirstRowX += TextSize * 0.45f;
			}
		}
		else if(HasName || aTime[0])
		{
			// cards and bubbles: the name on its own row, the time at the right of it (or of the text)
			if(HasName && !RightSide)
				Text(pName, TextSize * 0.92f, ImVec2(ContentX, RowY), NameColor, Line.m_aName);
			if(aTime[0])
			{
				const float TimeWidth = pSmall->CalcTextSizeA(SmallSize, FLT_MAX, 0.0f, aTime).x;
				Text(pSmall, SmallSize, ImVec2(BoxMax.x - PadX - TimeWidth, RowY + (TextSize - SmallSize) * 0.6f), RightSide ? t.m_OnAccent : t.m_TextMuted, aTime);
			}
			if(HasName && (!RightSide || aTime[0]))
				RowY += RowHeight * 0.95f;
		}
		for(size_t r = 0; r < Item.m_vRows.size(); r++)
		{
			const float x = (r == 0 && Inline) ? FirstRowX : ContentX;
			Text(pBody, TextSize, ImVec2(x, RowY), TextColor, Item.m_vRows[r].m_pBegin, Item.m_vRows[r].m_pEnd);
			RowY += RowHeight;
		}
	}

	if(m_ChatOpen > 0.0f)
		RenderChatInput(pDraw, ImVec2(Left, InputTop), ImVec2(Left + Width, InputBottom), U, HudAlpha * m_ChatOpen);
}

void CXcHud::RenderChatInput(ImDrawList *pDraw, ImVec2 Min, ImVec2 Max, float U, float Alpha)
{
	CChat &Chat = GameClient()->m_Chat;
	const STheme &t = Theme();
	const int Look = g_Config.m_XcChatLook;
	ImFont *pName = m_Backend.Fonts().m_pLabel;
	ImFont *pBody = m_Backend.Fonts().m_pBody;
	const float TextSize = 14.5f * U;
	const float Height = Max.y - Min.y;
	const float Rounding = Look == XC_CHAT_MINIMAL ? 8.0f * U : Height * 0.5f;
	// it rises a little when it opens
	const float Rise = (1.0f - Alpha) * 10.0f * U;
	Min.y += Rise;
	Max.y += Rise;
	const bool Team = Chat.m_Mode == CChat::MODE_TEAM;
	const ImVec4 ModeColor = ToImVec(ChatMarkColor(Team ? 2 : 1));

	if(Look == XC_CHAT_GLASS)
	{
		if(Alpha > 0.25f)
			m_vChatBlurRects.push_back({Min.x, Min.y, Max.x, Max.y});
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.55f), Alpha), Rounding);
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(1, 1, 1, 1), 0.06f * Alpha), Rounding);
	}
	else
	{
		pDraw->AddRectFilled(ImVec2(Min.x, Min.y + 3.0f * U), ImVec2(Max.x, Max.y + 3.0f * U), Col(ImVec4(0, 0, 0, 1), 0.14f * Alpha), Rounding);
		pDraw->AddRectFilled(Min, Max, Col(ImVec4(t.m_Panel.x, t.m_Panel.y, t.m_Panel.z, 0.92f), Alpha), Rounding);
	}
	// the border of the menu panels, the accent glows softly around it while typing
	pDraw->AddRect(ImVec2(Min.x - 1.5f * U, Min.y - 1.5f * U), ImVec2(Max.x + 1.5f * U, Max.y + 1.5f * U), Col(ModeColor, 0.18f * Alpha), Rounding + 1.5f * U, 0, 2.0f * U);
	pDraw->AddRect(Min, Max, Col(t.m_Border, Alpha), Rounding, 0, 1.0f);

	// the chip of the channel: all or team
	const char *pMode = Team ? Tr("Team") : Tr("All");
	const float ChipText = pName->CalcTextSizeA(12.0f * U, FLT_MAX, 0.0f, pMode).x;
	const ImVec2 ChipMin(Min.x + 6.0f * U, Min.y + 6.0f * U);
	const ImVec2 ChipMax(ChipMin.x + ChipText + 18.0f * U, Max.y - 6.0f * U);
	pDraw->AddRectFilled(ChipMin, ChipMax, Col(ModeColor, 0.2f * Alpha), Look == XC_CHAT_MINIMAL ? 6.0f * U : (ChipMax.y - ChipMin.y) * 0.5f);
	pDraw->AddText(pName, 12.0f * U, ImVec2(ChipMin.x + 9.0f * U, (ChipMin.y + ChipMax.y - 12.0f * U) * 0.5f), Col(ModeColor, Alpha), pMode);

	// the text, scrolled so the caret stays visible
	const float TextX = ChipMax.x + 10.0f * U;
	const float TextRight = Max.x - 14.0f * U;
	const float TextY = (Min.y + Max.y - TextSize) * 0.5f;
	const char *pString = Chat.m_Input.GetDisplayedString();
	const bool Hidden = pString != Chat.m_Input.GetString();
	const size_t Cursor = Hidden ? str_length(pString) : std::min(Chat.m_Input.GetCursorOffset(), (size_t)str_length(pString));
	const float CaretX = pBody->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pString, pString + Cursor).x;
	const float Visible = TextRight - TextX;
	m_ChatInputScroll = std::clamp(m_ChatInputScroll, CaretX - Visible + 4.0f * U, CaretX);
	m_ChatInputScroll = std::max(m_ChatInputScroll, 0.0f);
	pDraw->PushClipRect(ImVec2(TextX, Min.y), ImVec2(TextRight, Max.y), true);
	const float x0 = TextX - m_ChatInputScroll;
	if(!pString[0])
		pDraw->AddText(pBody, TextSize, ImVec2(TextX, TextY), Col(t.m_TextMuted, Alpha), Team ? Tr("Message to your team...") : Tr("Write a message..."));
	else
	{
		if(!Hidden && Chat.m_Input.HasSelection())
		{
			const size_t SelStart = std::min(Chat.m_Input.GetSelectionStart(), (size_t)str_length(pString));
			const size_t SelEnd = std::min(Chat.m_Input.GetSelectionEnd(), (size_t)str_length(pString));
			const float S0 = pBody->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pString, pString + SelStart).x;
			const float S1 = pBody->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pString, pString + SelEnd).x;
			pDraw->AddRectFilled(ImVec2(x0 + S0, TextY - 2.0f * U), ImVec2(x0 + S1, TextY + TextSize + 2.0f * U), Col(t.m_Accent, 0.35f * Alpha), 3.0f * U);
		}
		pDraw->AddText(pBody, TextSize, ImVec2(x0, TextY), Col(t.m_Text, Alpha), pString);
		// the rest of a server command, like the classic chat
		const char *pInput = Chat.m_Input.GetString();
		if(pInput[0] == '/' && pInput[1] != '\0' && !Chat.m_vServerCommands.empty())
		{
			for(const auto &Command : Chat.m_vServerCommands)
			{
				if(str_startswith_nocase(Command.m_aName, pInput + 1))
				{
					const float EndX = x0 + pBody->CalcTextSizeA(TextSize, FLT_MAX, 0.0f, pString).x;
					pDraw->AddText(pBody, TextSize, ImVec2(EndX, TextY), Col(t.m_TextMuted, Alpha), Command.m_aName + str_length(pInput + 1));
					break;
				}
			}
		}
	}
	// a blinking caret
	if(std::fmod(Client()->GlobalTime(), 1.0f) < 0.6f)
		pDraw->AddRectFilled(ImVec2(x0 + CaretX, TextY - 1.0f * U), ImVec2(x0 + CaretX + 1.6f * U, TextY + TextSize + 1.0f * U), Col(t.m_Accent, Alpha));
	pDraw->PopClipRect();
}

void CXcHud::BlurChat()
{
	if(m_vChatBlurRects.empty() || !GameClient()->m_FxRenderer.ModuleActive())
		return;
	const float W = (float)Graphics()->ScreenWidth();
	const float H = (float)Graphics()->ScreenHeight();
	XcRender::SBlurSettings Settings;
	Settings.m_Passes = 3;
	Settings.m_Offset = 1.2f;
	for(const auto &Rect : m_vChatBlurRects)
	{
		if(Settings.m_NumRects >= XcRender::SBlurSettings::MAX_RECTS)
			break;
		float *pRect = Settings.m_aaRects[Settings.m_NumRects++];
		pRect[0] = Rect[0] / W;
		pRect[1] = Rect[1] / H;
		pRect[2] = Rect[2] / W;
		pRect[3] = Rect[3] / H;
	}
	Graphics()->XcBlurScreen(Settings);
}
