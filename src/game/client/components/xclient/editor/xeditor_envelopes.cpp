// Envelopes as a timeline, like in After Effects: the envelopes on the left, a row of keyframes per channel
// in the middle with the curve behind it, the selected keyframe on the right. Every change goes through the
// classic envelope actions, so Ctrl+Z, the history and the saved map are the same as in the classic editor.
#include "xeditor.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>
#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/components/xclient/ui/theme.h>
#include <game/client/components/xclient/ui/widgets.h>
#include <game/editor/editor.h>
#include <game/editor/editor_actions.h>
#include <game/editor/mapitems/envelope.h>

#include <engine/external/imgui/imgui.h>

#include <algorithm>
#include <cmath>

using namespace XcUi;

namespace
{
// Font Awesome icons that are not in engine/font_icons.h
const char *const ICON_MOVE = "\xef\x82\xb2";
const char *const ICON_VOLUME = "\xef\x80\xa8";
const char *const ICON_UP = "\xef\x81\xa2";
const char *const ICON_DOWN = "\xef\x81\xa3";
const char *const ICON_CLOCK = "\xef\x80\x97";
const char *const ICON_EXPAND = "\xef\x81\xa5";
const char *const ICON_CURVES = "\xef\x88\x81"; // chart-line

bool MouseIn(ImVec2 Min, ImVec2 Max)
{
	const ImVec2 Mouse = ImGui::GetIO().MousePos;
	return Mouse.x >= Min.x && Mouse.x < Max.x && Mouse.y >= Min.y && Mouse.y < Max.y;
}

struct SChannel
{
	const char *m_pName;
	ImVec4 m_Color;
	float m_Scale; // the value shown = fixed value / 1024 * scale
	const char *m_pFormat;
};

int NumChannels(const CEnvelope &Env) { return Env.GetChannels(); }

SChannel ChannelInfo(const CEnvelope &Env, int Channel)
{
	if(Env.Type() == CEnvelope::EType::POSITION)
	{
		static const SChannel s_aPos[] = {{"X", ImVec4(1.0f, 0.45f, 0.45f, 1.0f), 1.0f, "%.1f"}, {"Y", ImVec4(0.45f, 0.9f, 0.5f, 1.0f), 1.0f, "%.1f"}, {"Rotation", ImVec4(1.0f, 0.8f, 0.3f, 1.0f), 1.0f, "%.0f°"}};
		return s_aPos[std::clamp(Channel, 0, 2)];
	}
	if(Env.Type() == CEnvelope::EType::COLOR)
	{
		static const SChannel s_aColor[] = {{"Red", ImVec4(1.0f, 0.4f, 0.4f, 1.0f), 100.0f, "%.0f%%"}, {"Green", ImVec4(0.4f, 0.9f, 0.45f, 1.0f), 100.0f, "%.0f%%"}, {"Blue", ImVec4(0.45f, 0.6f, 1.0f, 1.0f), 100.0f, "%.0f%%"}, {"Alpha", ImVec4(0.9f, 0.9f, 0.9f, 1.0f), 100.0f, "%.0f%%"}};
		return s_aColor[std::clamp(Channel, 0, 3)];
	}
	return {"Volume", ImVec4(0.45f, 0.75f, 1.0f, 1.0f), 100.0f, "%.0f%%"};
}

const char *TypeIcon(const CEnvelope &Env)
{
	if(Env.Type() == CEnvelope::EType::POSITION)
		return ICON_MOVE;
	if(Env.Type() == CEnvelope::EType::COLOR)
		return "\xef\x94\xbf"; // palette
	return ICON_VOLUME;
}
} // namespace

// pressed wheel (middle button) and drag: moves the time like a hand, in the graph also the values.
// Started inside the area, it keeps going when the mouse leaves it
void CXcEditorUi::PanEnvelopeView(ImVec2 AreaMin, ImVec2 AreaMax, float TrackWidth, float GraphHeight)
{
	const ImGuiIO &Io = ImGui::GetIO();
	if(ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && MouseIn(AreaMin, AreaMax))
		m_EnvPanning = true;
	if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
		m_EnvPanning = false;
	if(!m_EnvPanning)
		return;
	ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
	if(TrackWidth > 0.0f)
		m_EnvScroll = std::max(0.0f, m_EnvScroll - Io.MouseDelta.x / TrackWidth * m_EnvView);
	if(GraphHeight > 0.0f)
		m_EnvValuePan += Io.MouseDelta.y / GraphHeight * (m_EnvGraphHigh - m_EnvGraphLow);
}

void CXcEditorUi::RenderEnvelopesDock()
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	const STheme &t = Theme();
	CEditorHistory &History = pMap->m_EnvelopeEditorHistory;

	ImVec2 Min, Max;
	if(!BeginDock("##xe_dock_env", CEditor::EXTRAEDITOR_ENVELOPES, Min, Max))
	{
		ImGui::End();
		return;
	}
	if(pMap->m_SelectedEnvelope >= (int)pMap->m_vpEnvelopes.size())
		pMap->m_SelectedEnvelope = (int)pMap->m_vpEnvelopes.size() - 1;
	if(pMap->m_SelectedEnvelope < 0 && !pMap->m_vpEnvelopes.empty())
		pMap->m_SelectedEnvelope = 0;
	const int EnvIndex = pMap->m_SelectedEnvelope;
	std::shared_ptr<CEnvelope> pEnv = EnvIndex >= 0 ? pMap->m_vpEnvelopes[EnvIndex] : nullptr;
	if(pEnv != m_pEnvLast)
	{
		m_pEnvLast = pEnv;
		m_EnvSelPoint = -1;
		m_EnvScroll = 0.0f;
		m_EnvValuePan = 0.0f;
		m_EnvView = pEnv ? std::max(2.0f, pEnv->EndTime() * 1.25f) : 5.0f;
	}
	if(pEnv && m_EnvSelPoint >= (int)pEnv->m_vPoints.size())
		m_EnvSelPoint = -1;

	ImFont *pSmall = Fonts().m_pSmall;
	const float Height = ImGui::GetContentRegionAvail().y;
	const float ListWidth = S(210.0f);
	const float InspectorWidth = S(240.0f);
	const float TimelineWidth = ImGui::GetContentRegionAvail().x - ListWidth - InspectorWidth - S(20.0f);

	// ---- the envelopes
	ImGui::BeginChild("##envlist", ImVec2(ListWidth, Height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	{
		Title(Tr("Envelopes"));
		// new envelopes: position, color, sound
		const float Small = S(26.0f);
		const auto NewButton = [&](const char *pId, const char *pIcon, const char *pTip, CEnvelope::EType Type) {
			ImGui::PushID(pId);
			if(IconButton("##new", pIcon, pTip, false, true, Small))
			{
				History.Execute(std::make_shared<CEditorActionEnvelopeAdd>(pMap, Type));
				pMap->OnModify();
			}
			ImGui::PopID();
		};
		NewButton("pos", ICON_MOVE, Tr("New movement envelope (position and rotation)"), CEnvelope::EType::POSITION);
		ImGui::SameLine(0.0f, S(4.0f));
		NewButton("color", "\xef\x94\xbf", Tr("New color envelope (color and transparency)"), CEnvelope::EType::COLOR);
		ImGui::SameLine(0.0f, S(4.0f));
		NewButton("sound", ICON_VOLUME, Tr("New sound envelope (volume)"), CEnvelope::EType::SOUND);
		ImGui::SameLine(0.0f, S(12.0f));
		if(IconButton("##up", ICON_UP, Tr("Move up"), false, EnvIndex > 0, Small))
		{
			const int To = pMap->MoveEnvelope(EnvIndex, EnvIndex - 1);
			History.RecordAction(std::make_shared<CEditorActionEnvelopeEdit>(pMap, To, CEditorActionEnvelopeEdit::EEditType::ORDER, EnvIndex, To));
			pMap->m_SelectedEnvelope = To;
			pMap->OnModify();
		}
		ImGui::SameLine(0.0f, S(4.0f));
		if(IconButton("##down", ICON_DOWN, Tr("Move down"), false, EnvIndex >= 0 && EnvIndex < (int)pMap->m_vpEnvelopes.size() - 1, Small))
		{
			const int To = pMap->MoveEnvelope(EnvIndex, EnvIndex + 1);
			History.RecordAction(std::make_shared<CEditorActionEnvelopeEdit>(pMap, To, CEditorActionEnvelopeEdit::EEditType::ORDER, EnvIndex, To));
			pMap->m_SelectedEnvelope = To;
			pMap->OnModify();
		}
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));

		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		const float RowHeight = S(28.0f);
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		ImGui::BeginChild("##envrows", ImVec2(RowWidth, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
		pDraw = ImGui::GetWindowDrawList();
		if(pMap->m_vpEnvelopes.empty())
			Description(Tr("No envelopes yet: create one with the buttons above"));
		for(int i = 0; i < (int)pMap->m_vpEnvelopes.size(); i++)
		{
			const CEnvelope &Env = *pMap->m_vpEnvelopes[i];
			ImGui::PushID(i);
			const ImVec2 RowMin = ImGui::GetCursorScreenPos();
			if(m_EnvRename == i)
			{
				if(m_EnvRenameFocus)
				{
					ImGui::SetKeyboardFocusHere();
					m_EnvRenameFocus = false;
				}
				InputText("##rename", m_aEnvRename, sizeof(m_aEnvRename), Tr("Name"), RowWidth);
				if(ImGui::IsItemDeactivated())
				{
					str_copy(pMap->m_vpEnvelopes[i]->m_aName, m_aEnvRename);
					pMap->OnModify();
					m_EnvRename = -1;
				}
				ImGui::PopID();
				continue;
			}
			if(ImGui::InvisibleButton("##env", ImVec2(RowWidth, RowHeight)))
				pMap->m_SelectedEnvelope = i;
			if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				m_EnvRename = i;
				m_EnvRenameFocus = true;
				str_copy(m_aEnvRename, Env.m_aName);
			}
			const bool Hovered = ImGui::IsItemHovered();
			if(i == EnvIndex)
				pDraw->AddRectFilled(RowMin, ImVec2(RowMin.x + RowWidth, RowMin.y + RowHeight), Col(t.m_Accent, 0.22f), S(6.0f));
			else if(Hovered)
				pDraw->AddRectFilled(RowMin, ImVec2(RowMin.x + RowWidth, RowMin.y + RowHeight), Col(t.m_Card), S(6.0f));
			const float TextY = RowMin.y + (RowHeight - pSmall->FontSize) * 0.5f;
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(RowMin.x + S(8.0f), TextY), Col(t.m_TextMuted), TypeIcon(Env));
			char aName[64];
			if(Env.m_aName[0])
				str_copy(aName, Env.m_aName);
			else
				str_format(aName, sizeof(aName), "%s %d", Tr("Envelope"), i + 1);
			pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(RowMin.x + S(30.0f), TextY), Col(i == EnvIndex ? t.m_Text : t.m_TextSecondary), aName);
			// a dot when something in the map uses it
			const bool Used = pMap->IsEnvelopeUsed(i);
			pDraw->AddCircleFilled(ImVec2(RowMin.x + RowWidth - S(10.0f), RowMin.y + RowHeight * 0.5f), S(3.0f), Col(Used ? ImVec4(0.24f, 0.84f, 0.64f, 1.0f) : t.m_TextMuted, Used ? 1.0f : 0.4f));
			if(Hovered)
				ImGui::SetTooltip("%s", Used ? Tr("Used in the map. Double click to rename") : Tr("Not used yet. Double click to rename"));
			ImGui::PopID();
		}
		ImGui::EndChild();
	}
	ImGui::EndChild();
	ImGui::SameLine(0.0f, S(10.0f));

	// ---- the timeline
	ImGui::BeginChild("##envtimeline", ImVec2(TimelineWidth, Height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if(pEnv)
	{
		CMapEnvelopeEvaluator &Eval = pMap->m_EnvelopeEvaluator;
		ImDrawList *pDraw = ImGui::GetWindowDrawList();
		// toolbar: play, the time, the zoom, the sync toggle and delete
		const float Small = S(26.0f);
		if(IconButton("##play", Eval.m_Animate ? FontIcon::PAUSE : FontIcon::PLAY, Tr("Play the animation in the map (Ctrl+M)"), Eval.m_Animate, true, Small))
		{
			Eval.m_AnimateStart = E.Client()->GlobalTime() - Eval.m_AnimateTime;
			Eval.m_Animate = !Eval.m_Animate;
		}
		ImGui::SameLine(0.0f, S(4.0f));
		if(IconButton("##stop", FontIcon::STOP, Tr("Back to the start"), false, true, Small))
		{
			Eval.m_Animate = false;
			Eval.m_AnimateTime = 0.0f;
		}
		const float EndTime = std::max(pEnv->EndTime(), 0.001f);
		const float Now = std::fmod(Eval.m_AnimateTime * Eval.m_AnimateSpeed, EndTime);
		ImGui::SameLine(0.0f, S(10.0f));
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + S(5.0f));
		ImGui::PushFont(pSmall);
		ImGui::TextColored(t.m_TextSecondary, "%.2f / %.2f %s", Now, pEnv->EndTime(), Tr("s"));
		ImGui::PopFont();
		ImGui::SameLine(0.0f, S(16.0f));
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(5.0f));
		bool Sync = pEnv->m_Synchronized;
		if(IconButton("##sync", ICON_CLOCK, Tr("Synchronized: everybody on the server sees the same moment of the animation"), Sync, true, Small))
		{
			pEnv->m_Synchronized = !Sync;
			History.RecordAction(std::make_shared<CEditorActionEnvelopeEdit>(pMap, EnvIndex, CEditorActionEnvelopeEdit::EEditType::SYNC, Sync, !Sync));
			pMap->OnModify();
		}
		ImGui::SameLine(0.0f, S(4.0f));
		if(IconButton("##fit", ICON_EXPAND, Tr("Show the whole animation"), false, true, Small))
		{
			m_EnvScroll = 0.0f;
			m_EnvValuePan = 0.0f;
			m_EnvView = std::max(1.0f, pEnv->EndTime() * 1.25f);
		}
		ImGui::SameLine(0.0f, S(4.0f));
		if(IconButton("##curves", ICON_CURVES, Tr("Curves like in After Effects (experimental): one graph, keys move in time and value, bezier handles"), g_Config.m_XcEditorEnvCurves != 0, true, Small))
			g_Config.m_XcEditorEnvCurves ^= 1;
		ImGui::SameLine(0.0f, S(4.0f));
		if(IconButton("##delenv", FontIcon::TRASH, Tr("Delete this envelope"), false, true, Small))
		{
			auto vpReferences = pMap->DeleteEnvelope(EnvIndex);
			History.RecordAction(std::make_shared<CEditorActionEnvelopeDelete>(pMap, EnvIndex, vpReferences, pEnv));
			pMap->m_SelectedEnvelope = pMap->m_vpEnvelopes.empty() ? -1 : std::clamp(EnvIndex, 0, (int)pMap->m_vpEnvelopes.size() - 1);
			pMap->OnModify();
			ImGui::EndChild();
			ImGui::End();
			return;
		}

		if(g_Config.m_XcEditorEnvCurves)
			RenderEnvelopeGraph(pEnv, EnvIndex, TimelineWidth, Height, Now);
		else
		{
			// the area: a ruler on top, then a row per channel
			const ImVec2 AreaMin = ImGui::GetCursorScreenPos();
			const float LabelWidth = S(70.0f);
			const float TrackMin = AreaMin.x + LabelWidth;
			const float TrackWidth = TimelineWidth - LabelWidth - S(4.0f);
			const float RulerHeight = S(20.0f);
			const int Channels = NumChannels(*pEnv);
			const float RowHeight = std::max(S(24.0f), (Height - (AreaMin.y - ImGui::GetWindowPos().y) - RulerHeight - S(6.0f)) / Channels);
			const auto TimeToX = [&](float Time) { return TrackMin + (Time - m_EnvScroll) / m_EnvView * TrackWidth; };
			const auto XToTime = [&](float x) { return m_EnvScroll + (x - TrackMin) / TrackWidth * m_EnvView; };

			// the wheel zooms the time, Shift + wheel scrolls it
			const ImVec2 AreaMax(TrackMin + TrackWidth, AreaMin.y + RulerHeight + RowHeight * Channels);
			PanEnvelopeView(AreaMin, AreaMax, TrackWidth, 0.0f);
			if(MouseIn(AreaMin, AreaMax) && ImGui::GetIO().MouseWheel != 0.0f)
			{
				if(ImGui::GetIO().KeyShift)
					m_EnvScroll = std::max(0.0f, m_EnvScroll - ImGui::GetIO().MouseWheel * m_EnvView * 0.1f);
				else
				{
					const float Anchor = XToTime(ImGui::GetIO().MousePos.x);
					m_EnvView = std::clamp(m_EnvView * (ImGui::GetIO().MouseWheel > 0.0f ? 0.85f : 1.18f), 0.2f, 600.0f);
					m_EnvScroll = std::max(0.0f, Anchor - (ImGui::GetIO().MousePos.x - TrackMin) / TrackWidth * m_EnvView);
				}
			}

			// ruler with seconds; a click moves the play head
			pDraw->AddRectFilled(ImVec2(TrackMin, AreaMin.y), ImVec2(TrackMin + TrackWidth, AreaMin.y + RulerHeight), Col(t.m_Track, 0.5f), S(4.0f));
			static const float s_aSteps[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f};
			float Step = s_aSteps[0];
			for(float Candidate : s_aSteps)
			{
				Step = Candidate;
				if(m_EnvView / Candidate <= 12.0f)
					break;
			}
			for(float Time = std::ceil(m_EnvScroll / Step) * Step; Time <= m_EnvScroll + m_EnvView; Time += Step)
			{
				const float x = TimeToX(Time);
				pDraw->AddLine(ImVec2(x, AreaMin.y + RulerHeight * 0.55f), ImVec2(x, AreaMax.y), Col(t.m_Border, 0.6f));
				char aTime[16];
				str_format(aTime, sizeof(aTime), Step < 1.0f ? "%.1f" : "%.0f", Time);
				pDraw->AddText(pSmall, pSmall->FontSize * 0.85f, ImVec2(x + S(3.0f), AreaMin.y + S(2.0f)), Col(t.m_TextMuted), aTime);
			}
			ImGui::SetCursorScreenPos(ImVec2(TrackMin, AreaMin.y));
			ImGui::InvisibleButton("##ruler", ImVec2(TrackWidth, RulerHeight));
			if(ImGui::IsItemActive())
			{
				Eval.m_Animate = false;
				Eval.m_AnimateTime = std::max(0.0f, XToTime(ImGui::GetIO().MousePos.x)) / std::max(Eval.m_AnimateSpeed, 0.01f);
			}
			// the end of the animation
			pDraw->AddLine(ImVec2(TimeToX(pEnv->EndTime()), AreaMin.y), ImVec2(TimeToX(pEnv->EndTime()), AreaMax.y), Col(t.m_TextMuted, 0.5f), 1.0f);

			// the rows: a curve of the channel and its keyframes
			pDraw->PushClipRect(ImVec2(AreaMin.x, AreaMin.y), AreaMax, true);
			const int NumPoints = (int)pEnv->m_vPoints.size();
			int Clicked = -1, Delete = -1;
			for(int c = 0; c < Channels; c++)
			{
				const SChannel Info = ChannelInfo(*pEnv, c);
				const float RowY = AreaMin.y + RulerHeight + c * RowHeight;
				pDraw->AddRectFilled(ImVec2(AreaMin.x, RowY + 1.0f), ImVec2(AreaMax.x, RowY + RowHeight - 1.0f), Col(t.m_Track, c % 2 ? 0.25f : 0.4f), S(4.0f));
				pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(AreaMin.x + S(8.0f), RowY + (RowHeight - pSmall->FontSize) * 0.5f), Col(Info.m_Color), Tr(Info.m_pName));
				// the curve, scaled to the range of the channel
				float Low = 1e9f, High = -1e9f;
				for(const auto &Point : pEnv->m_vPoints)
				{
					Low = std::min(Low, fx2f(Point.m_aValues[c]));
					High = std::max(High, fx2f(Point.m_aValues[c]));
				}
				if(High - Low < 0.0001f)
				{
					Low -= 0.5f;
					High += 0.5f;
				}
				const auto ValueToY = [&](float Value) { return RowY + RowHeight - S(5.0f) - (Value - Low) / (High - Low) * (RowHeight - S(10.0f)); };
				ImVec2 Last;
				const int Samples = 120;
				for(int s = 0; s <= Samples; s++)
				{
					const float Time = m_EnvScroll + m_EnvView * s / Samples;
					ColorRGBA Value;
					pEnv->Eval(std::min(Time, pEnv->EndTime() - 0.0005f), Value, Channels);
					const ImVec2 P(TimeToX(Time), ValueToY(Value[c]));
					if(s > 0)
						pDraw->AddLine(Last, P, Col(Info.m_Color, 0.55f), 1.5f);
					Last = P;
				}
				// keyframes as diamonds
				for(int p = 0; p < NumPoints; p++)
				{
					const CEnvPoint_runtime &Point = pEnv->m_vPoints[p];
					const ImVec2 Center(TimeToX(Point.m_Time.AsSeconds()), ValueToY(fx2f(Point.m_aValues[c])));
					const float r = S(5.5f);
					const bool Selected = p == m_EnvSelPoint;
					pDraw->AddQuadFilled(ImVec2(Center.x, Center.y - r), ImVec2(Center.x + r, Center.y), ImVec2(Center.x, Center.y + r), ImVec2(Center.x - r, Center.y), Col(Selected ? t.m_Accent : Info.m_Color));
					if(Selected)
						pDraw->AddQuad(ImVec2(Center.x, Center.y - r - 2.0f), ImVec2(Center.x + r + 2.0f, Center.y), ImVec2(Center.x, Center.y + r + 2.0f), ImVec2(Center.x - r - 2.0f, Center.y), Col(ImVec4(1, 1, 1, 1)), 1.5f);
					ImGui::SetCursorScreenPos(ImVec2(Center.x - r, Center.y - r));
					ImGui::PushID(c * 1000 + p);
					ImGui::InvisibleButton("##key", ImVec2(r * 2.0f, r * 2.0f));
					if(ImGui::IsItemActivated())
					{
						Clicked = p;
						m_EnvDragOld = Point.m_Time;
					}
					if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
						Delete = p;
					if(ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%.2f %s · %s", Point.m_Time.AsSeconds(), Tr("s"), Tr("drag to move, right click to delete"));
						ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
					}
					// drag in time, between the neighbors
					if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
					{
						m_EnvDragging = true;
						const int Low = p > 0 ? pEnv->m_vPoints[p - 1].m_Time.GetInternal() + 1 : 0;
						const int High = p + 1 < NumPoints ? pEnv->m_vPoints[p + 1].m_Time.GetInternal() - 1 : 1000000;
						int NewTime = round_to_int(XToTime(ImGui::GetIO().MousePos.x) * 1000.0f);
						if(ImGui::GetIO().KeyShift) // snap to 0.1 s
							NewTime = round_to_int(NewTime / 100.0f) * 100;
						pEnv->m_vPoints[p].m_Time = CFixedTime(p == 0 ? 0 : std::clamp(NewTime, Low, High));
					}
					if(ImGui::IsItemDeactivated() && m_EnvDragging)
					{
						m_EnvDragging = false;
						if(pEnv->m_vPoints[p].m_Time != m_EnvDragOld)
						{
							History.RecordAction(std::make_shared<CEditorActionEnvelopeEditPointTime>(pMap, EnvIndex, p, m_EnvDragOld, pEnv->m_vPoints[p].m_Time));
							pMap->OnModify();
						}
					}
					ImGui::PopID();
				}
			}
			// the play head
			const float HeadX = TimeToX(Now);
			pDraw->AddLine(ImVec2(HeadX, AreaMin.y), ImVec2(HeadX, AreaMax.y), Col(t.m_Accent), 2.0f);
			pDraw->AddTriangleFilled(ImVec2(HeadX - S(5.0f), AreaMin.y), ImVec2(HeadX + S(5.0f), AreaMin.y), ImVec2(HeadX, AreaMin.y + S(7.0f)), Col(t.m_Accent));
			pDraw->PopClipRect();

			// a double click on a row adds a keyframe with the value of the curve at that moment
			if(MouseIn(ImVec2(TrackMin, AreaMin.y + RulerHeight), AreaMax) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && Clicked < 0)
			{
				const float Time = std::max(0.0f, XToTime(ImGui::GetIO().MousePos.x));
				const CFixedTime Fixed = CFixedTime::FromSeconds(Time);
				const auto ExactPoint = [&]() {
					for(int i = 0; i < (int)pEnv->m_vPoints.size(); i++)
						if(pEnv->m_vPoints[i].m_Time == Fixed)
							return i;
					return -1;
				};
				if(ExactPoint() < 0)
				{
					ColorRGBA Value(0, 0, 0, 0);
					if(!pEnv->m_vPoints.empty())
						pEnv->Eval(Time, Value, Channels);
					else if(pEnv->Type() == CEnvelope::EType::COLOR)
						Value = ColorRGBA(1, 1, 1, 1);
					History.Execute(std::make_shared<CEditorActionAddEnvelopePoint>(pMap, EnvIndex, Fixed, Value));
					m_EnvSelPoint = ExactPoint();
					pMap->OnModify();
				}
			}
			if(Clicked >= 0)
				m_EnvSelPoint = Clicked;
			if(Delete >= 0)
			{
				History.Execute(std::make_shared<CEditorActionDeleteEnvelopePoint>(pMap, EnvIndex, Delete));
				m_EnvSelPoint = -1;
				pMap->OnModify();
			}
			ImGui::SetCursorScreenPos(ImVec2(AreaMin.x, AreaMax.y + S(4.0f)));
			ImGui::PushFont(pSmall);
			ImGui::TextColored(t.m_TextMuted, "%s", Tr("Double click a row to add a keyframe · wheel zooms, Shift + wheel scrolls · drag the ruler to look at any moment"));
			ImGui::PopFont();
		}
	}
	else
	{
		ImGui::Dummy(ImVec2(0.0f, S(30.0f)));
		Description(Tr("Envelopes make quads and tiles move, turn, change color or sound louder over time. Create one on the left, then pick it in the properties of a quad or a layer."));
	}
	ImGui::EndChild();
	ImGui::SameLine(0.0f, S(10.0f));

	// ---- the selected keyframe
	ImGui::BeginChild("##envkey", ImVec2(InspectorWidth, Height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
	if(pEnv && m_EnvSelPoint >= 0)
	{
		const int p = m_EnvSelPoint;
		CEnvPoint_runtime &Point = pEnv->m_vPoints[p];
		Title(Tr("Keyframe"));
		char aSub[64];
		str_format(aSub, sizeof(aSub), "%d / %d", p + 1, (int)pEnv->m_vPoints.size());
		Description(aSub);
		ImGui::Dummy(ImVec2(0.0f, S(2.0f)));
		const float Width = ImGui::GetContentRegionAvail().x;
		const float LabelWidth = Width * 0.4f;
		ImGui::PushFont(pSmall);
		// the time
		{
			ImGui::TextColored(t.m_TextSecondary, "%s", Tr("Time, s"));
			ImGui::SameLine(LabelWidth);
			float Seconds = Point.m_Time.AsSeconds();
			ImGui::SetNextItemWidth(Width - LabelWidth);
			ImGui::BeginDisabled(p == 0);
			if(HoldMouseWhileDragging(ImGui::DragFloat("##time", &Seconds, 0.01f, 0.0f, 600.0f, "%.2f")))
			{
				const int Low = p > 0 ? pEnv->m_vPoints[p - 1].m_Time.GetInternal() + 1 : 0;
				const int High = p + 1 < (int)pEnv->m_vPoints.size() ? pEnv->m_vPoints[p + 1].m_Time.GetInternal() - 1 : 1000000;
				Point.m_Time = CFixedTime(std::clamp(round_to_int(Seconds * 1000.0f), Low, High));
			}
			if(ImGui::IsItemActivated())
				m_EnvDragOld = Point.m_Time;
			if(ImGui::IsItemDeactivatedAfterEdit() && Point.m_Time != m_EnvDragOld)
			{
				History.RecordAction(std::make_shared<CEditorActionEnvelopeEditPointTime>(pMap, EnvIndex, p, m_EnvDragOld, Point.m_Time));
				pMap->OnModify();
			}
			ImGui::EndDisabled();
		}
		// the values of the channels
		for(int c = 0; c < NumChannels(*pEnv); c++)
		{
			const SChannel Info = ChannelInfo(*pEnv, c);
			ImGui::PushID(c);
			ImGui::TextColored(Info.m_Color, "%s", Tr(Info.m_pName));
			ImGui::SameLine(LabelWidth);
			float Value = fx2f(Point.m_aValues[c]) * Info.m_Scale;
			ImGui::SetNextItemWidth(Width - LabelWidth);
			if(HoldMouseWhileDragging(ImGui::DragFloat("##value", &Value, Info.m_Scale > 1.0f ? 0.5f : 0.25f, 0.0f, 0.0f, Info.m_pFormat)))
				Point.m_aValues[c] = f2fx(Value / Info.m_Scale);
			if(ImGui::IsItemActivated())
				m_EnvValueOld = Point.m_aValues[c];
			if(ImGui::IsItemDeactivatedAfterEdit() && Point.m_aValues[c] != m_EnvValueOld)
			{
				History.RecordAction(std::make_shared<CEditorActionEnvelopeEditPoint>(pMap, EnvIndex, p, c, CEditorActionEnvelopeEditPoint::EEditType::VALUE, m_EnvValueOld, Point.m_aValues[c]));
				pMap->OnModify();
			}
			ImGui::PopID();
		}
		ImGui::PopFont();
		// the easing to the next keyframe
		ImGui::Dummy(ImVec2(0.0f, S(4.0f)));
		ParamLabel(Tr("Easing to the next keyframe"));
		const char *apCurves[] = {Tr("Step"), Tr("Linear"), Tr("Slow"), Tr("Fast"), Tr("Smooth"), Tr("Curve")};
		const bool Curves = g_Config.m_XcEditorEnvCurves != 0;
		int Curve = std::clamp(Point.m_Curvetype, 0, 5);
		if(Point.m_Curvetype == CURVETYPE_BEZIER && !Curves)
			Description(Tr("Curve: turn on the curves view (the chart button) to move its handles"));
		else if(ChoiceChips("##curve", &Curve, apCurves, Curves ? 6 : 5))
			SetEnvelopeCurve(pEnv, EnvIndex, p, Curve);
		ImGui::Dummy(ImVec2(0.0f, S(6.0f)));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
		if(Button(Tr("Delete the keyframe"), false, Width))
		{
			History.Execute(std::make_shared<CEditorActionDeleteEnvelopePoint>(pMap, EnvIndex, p));
			m_EnvSelPoint = -1;
			pMap->OnModify();
		}
		ImGui::PopStyleColor();
	}
	else if(pEnv)
	{
		Title(Tr("Keyframe"));
		Description(Tr("Click a keyframe to change its values and easing"));
	}
	ImGui::EndChild();
	ImGui::End();
}

// the easing of a keyframe; a curve (bezier) gets handles at a third of the way, so it starts straight and the
// handles can be taken right away. One step of Ctrl+Z for all of it.
void CXcEditorUi::SetEnvelopeCurve(const std::shared_ptr<CEnvelope> &pEnv, int EnvIndex, int Point, int Curve)
{
	CEditorMap *pMap = m_pEditor->Map();
	CEditorHistory &History = pMap->m_EnvelopeEditorHistory;
	CEnvPoint_runtime &Key = pEnv->m_vPoints[Point];
	if(Key.m_Curvetype == Curve)
		return;
	std::vector<std::shared_ptr<IEditorAction>> vpActions;
	vpActions.push_back(std::make_shared<CEditorActionEnvelopeEditPoint>(pMap, EnvIndex, Point, 0, CEditorActionEnvelopeEditPoint::EEditType::CURVE_TYPE, Key.m_Curvetype, Curve));
	if(Curve == CURVETYPE_BEZIER && Point + 1 < (int)pEnv->m_vPoints.size())
	{
		const CEnvPoint_runtime &Next = pEnv->m_vPoints[Point + 1];
		const int Third = (Next.m_Time.GetInternal() - Key.m_Time.GetInternal()) / 3;
		for(int c = 0; c < pEnv->GetChannels(); c++)
		{
			const int ValueThird = (Next.m_aValues[c] - Key.m_aValues[c]) / 3;
			if(Key.m_Bezier.m_aOutTangentDeltaX[c].GetInternal() == 0 && Key.m_Bezier.m_aOutTangentDeltaY[c] == 0)
				vpActions.push_back(std::make_shared<CEditorActionEditEnvelopePointValue>(pMap, EnvIndex, Point, c, CEditorActionEditEnvelopePointValue::EType::TANGENT_OUT,
					Key.m_Time, Key.m_aValues[c], Key.m_Time + CFixedTime(Third), Key.m_aValues[c] + ValueThird));
			if(Next.m_Bezier.m_aInTangentDeltaX[c].GetInternal() == 0 && Next.m_Bezier.m_aInTangentDeltaY[c] == 0)
				vpActions.push_back(std::make_shared<CEditorActionEditEnvelopePointValue>(pMap, EnvIndex, Point + 1, c, CEditorActionEditEnvelopePointValue::EType::TANGENT_IN,
					Next.m_Time, Next.m_aValues[c], Next.m_Time - CFixedTime(Third), Next.m_aValues[c] - ValueThird));
		}
	}
	History.Execute(std::make_shared<CEditorActionBulk>(pMap, vpActions, "Change the easing"));
	pMap->OnModify();
}

// Experimental view: one big graph of the chosen channel (the others faint behind it). Keys move in time and
// value, a curve (bezier) key has handles: the two handles of a key stay in one line (Alt breaks them).
void CXcEditorUi::RenderEnvelopeGraph(const std::shared_ptr<CEnvelope> &pEnv, int EnvIndex, float Width, float Height, float Now)
{
	CEditor &E = *m_pEditor;
	CEditorMap *pMap = E.Map();
	CEditorHistory &History = pMap->m_EnvelopeEditorHistory;
	CMapEnvelopeEvaluator &Eval = pMap->m_EnvelopeEvaluator;
	const STheme &t = Theme();
	ImFont *pSmall = Fonts().m_pSmall;
	ImDrawList *pDraw = ImGui::GetWindowDrawList();
	const ImGuiIO &Io = ImGui::GetIO();
	const int Channels = NumChannels(*pEnv);
	m_EnvGraphChannel = std::clamp(m_EnvGraphChannel, 0, Channels - 1);
	const int c = m_EnvGraphChannel;
	const SChannel Info = ChannelInfo(*pEnv, c);
	const bool ColorLike = Channels == 1 || Channels == 4; // color and volume are 0..1

	// the channel to edit
	for(int i = 0; i < Channels; i++)
	{
		const SChannel Other = ChannelInfo(*pEnv, i);
		if(i > 0)
			ImGui::SameLine(0.0f, S(4.0f));
		ImGui::PushID(i);
		const char *pName = Tr(Other.m_pName);
		const float ChipWidth = pSmall->CalcTextSizeA(pSmall->FontSize, FLT_MAX, 0.0f, pName).x + S(26.0f);
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		const ImVec2 Max(Min.x + ChipWidth, Min.y + S(22.0f));
		if(ImGui::InvisibleButton("##ch", ImVec2(ChipWidth, S(22.0f))))
			m_EnvGraphChannel = i;
		const bool Active = i == c;
		pDraw->AddRectFilled(Min, Max, Col(Active ? Other.m_Color : t.m_Track, Active ? 0.25f : (ImGui::IsItemHovered() ? 0.8f : 0.45f)), S(6.0f));
		pDraw->AddCircleFilled(ImVec2(Min.x + S(10.0f), (Min.y + Max.y) * 0.5f), S(3.5f), Col(Other.m_Color));
		pDraw->AddText(pSmall, pSmall->FontSize, ImVec2(Min.x + S(18.0f), Min.y + (S(22.0f) - pSmall->FontSize) * 0.5f), Col(Active ? t.m_Text : t.m_TextSecondary), pName);
		ImGui::PopID();
	}
	ImGui::Dummy(ImVec2(0.0f, S(2.0f)));

	const ImVec2 AreaMin = ImGui::GetCursorScreenPos();
	const float LabelWidth = S(56.0f);
	const float TrackMin = AreaMin.x + LabelWidth;
	const float TrackWidth = Width - LabelWidth - S(4.0f);
	const float RulerHeight = S(20.0f);
	const float GraphTop = AreaMin.y + RulerHeight;
	const float GraphHeight = std::max(S(60.0f), Height - (AreaMin.y - ImGui::GetWindowPos().y) - RulerHeight - S(28.0f));
	const ImVec2 AreaMax(TrackMin + TrackWidth, GraphTop + GraphHeight);
	const auto TimeToX = [&](float Time) { return TrackMin + (Time - m_EnvScroll) / m_EnvView * TrackWidth; };
	const auto XToTime = [&](float x) { return m_EnvScroll + (x - TrackMin) / TrackWidth * m_EnvView; };
	const int NumPoints = (int)pEnv->m_vPoints.size();

	// the value range of the channel with its handles; it stays still while something is dragged
	if(m_EnvGraphDrag == ENVDRAG_NONE)
	{
		float Low = 1e9f, High = -1e9f;
		for(int p = 0; p < NumPoints; p++)
		{
			const CEnvPoint_runtime &Point = pEnv->m_vPoints[p];
			const float Value = fx2f(Point.m_aValues[c]);
			Low = std::min(Low, Value);
			High = std::max(High, Value);
			if(p + 1 < NumPoints && Point.m_Curvetype == CURVETYPE_BEZIER)
			{
				const float Handle = fx2f(Point.m_aValues[c] + Point.m_Bezier.m_aOutTangentDeltaY[c]);
				const float NextHandle = fx2f(pEnv->m_vPoints[p + 1].m_aValues[c] + pEnv->m_vPoints[p + 1].m_Bezier.m_aInTangentDeltaY[c]);
				Low = std::min({Low, Handle, NextHandle});
				High = std::max({High, Handle, NextHandle});
			}
		}
		if(NumPoints == 0)
		{
			Low = 0.0f;
			High = 1.0f;
		}
		if(High - Low < 0.0001f)
		{
			Low -= 0.5f;
			High += 0.5f;
		}
		const float Pad = (High - Low) * 0.15f;
		m_EnvGraphLow = Low - Pad;
		m_EnvGraphHigh = High + Pad;
	}
	// the held wheel moves the view: time to the sides, values up and down
	PanEnvelopeView(AreaMin, AreaMax, TrackWidth, GraphHeight);
	const float Low = m_EnvGraphLow + m_EnvValuePan, High = m_EnvGraphHigh + m_EnvValuePan;
	const auto ValueToY = [&](float Value) { return GraphTop + GraphHeight - (Value - Low) / (High - Low) * GraphHeight; };
	const auto YToValue = [&](float y) { return Low + (GraphTop + GraphHeight - y) / GraphHeight * (High - Low); };

	// the wheel zooms the time, Shift + wheel scrolls it
	if(MouseIn(AreaMin, AreaMax) && Io.MouseWheel != 0.0f)
	{
		if(Io.KeyShift)
			m_EnvScroll = std::max(0.0f, m_EnvScroll - Io.MouseWheel * m_EnvView * 0.1f);
		else
		{
			const float Anchor = XToTime(Io.MousePos.x);
			m_EnvView = std::clamp(m_EnvView * (Io.MouseWheel > 0.0f ? 0.85f : 1.18f), 0.2f, 600.0f);
			m_EnvScroll = std::max(0.0f, Anchor - (Io.MousePos.x - TrackMin) / TrackWidth * m_EnvView);
		}
	}

	// the graph with value lines
	pDraw->AddRectFilled(ImVec2(TrackMin, GraphTop), AreaMax, Col(t.m_Track, 0.3f), S(6.0f));
	{
		const float Range = (High - Low) * Info.m_Scale;
		const float Raw = Range / 5.0f;
		const float Power = std::pow(10.0f, std::floor(std::log10(std::max(Raw, 1e-6f))));
		float ValueStep = Power;
		for(float Nice : {1.0f, 2.0f, 5.0f, 10.0f})
		{
			ValueStep = Nice * Power;
			if(ValueStep >= Raw)
				break;
		}
		for(float Shown = std::ceil(Low * Info.m_Scale / ValueStep) * ValueStep; Shown <= High * Info.m_Scale; Shown += ValueStep)
		{
			const float y = ValueToY(Shown / Info.m_Scale);
			pDraw->AddLine(ImVec2(TrackMin, y), ImVec2(AreaMax.x, y), Col(t.m_Border, std::abs(Shown) < ValueStep * 0.01f ? 1.0f : 0.5f));
			char aValue[32];
			str_format(aValue, sizeof(aValue), Info.m_pFormat, Shown);
			const float TextWidth = pSmall->CalcTextSizeA(pSmall->FontSize * 0.85f, FLT_MAX, 0.0f, aValue).x;
			pDraw->AddText(pSmall, pSmall->FontSize * 0.85f, ImVec2(TrackMin - S(6.0f) - TextWidth, y - pSmall->FontSize * 0.45f), Col(t.m_TextMuted), aValue);
		}
	}

	// the ruler with seconds; a drag moves the play head
	pDraw->AddRectFilled(ImVec2(TrackMin, AreaMin.y), ImVec2(AreaMax.x, GraphTop - S(2.0f)), Col(t.m_Track, 0.5f), S(4.0f));
	{
		static const float s_aSteps[] = {0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 30.0f, 60.0f};
		float Step = s_aSteps[0];
		for(float Candidate : s_aSteps)
		{
			Step = Candidate;
			if(m_EnvView / Candidate <= 12.0f)
				break;
		}
		for(float Time = std::ceil(m_EnvScroll / Step) * Step; Time <= m_EnvScroll + m_EnvView; Time += Step)
		{
			const float x = TimeToX(Time);
			pDraw->AddLine(ImVec2(x, AreaMin.y + RulerHeight * 0.55f), ImVec2(x, AreaMax.y), Col(t.m_Border, 0.45f));
			char aTime[16];
			str_format(aTime, sizeof(aTime), Step < 1.0f ? "%.1f" : "%.0f", Time);
			pDraw->AddText(pSmall, pSmall->FontSize * 0.85f, ImVec2(x + S(3.0f), AreaMin.y + S(2.0f)), Col(t.m_TextMuted), aTime);
		}
	}
	ImGui::SetCursorScreenPos(ImVec2(TrackMin, AreaMin.y));
	ImGui::InvisibleButton("##ruler", ImVec2(TrackWidth, RulerHeight));
	if(ImGui::IsItemActive())
	{
		Eval.m_Animate = false;
		Eval.m_AnimateTime = std::max(0.0f, XToTime(Io.MousePos.x)) / std::max(Eval.m_AnimateSpeed, 0.01f);
	}

	pDraw->PushClipRect(ImVec2(TrackMin, AreaMin.y), AreaMax, true);
	const float EndTime = pEnv->EndTime();
	pDraw->AddLine(ImVec2(TimeToX(EndTime), AreaMin.y), ImVec2(TimeToX(EndTime), AreaMax.y), Col(t.m_TextMuted, 0.5f), 1.0f);
	const int Samples = 220;
	const auto SampleCurve = [&](int Channel, float ChannelLow, float ChannelHigh, ImU32 Color, float Thickness) {
		ImVec2 Last;
		for(int s = 0; s <= Samples; s++)
		{
			const float Time = m_EnvScroll + m_EnvView * s / Samples;
			ColorRGBA Value;
			pEnv->Eval(std::clamp(Time, 0.0f, std::max(EndTime - 0.0005f, 0.0f)), Value, Channels);
			const float y = GraphTop + GraphHeight - (Value[Channel] - ChannelLow) / (ChannelHigh - ChannelLow) * GraphHeight;
			const ImVec2 P(TimeToX(Time), y);
			if(s > 0)
				pDraw->AddLine(Last, P, Color, Thickness);
			Last = P;
		}
	};
	// the other channels faint, each in its own range, only the shape matters
	for(int Other = 0; Other < Channels && NumPoints > 0; Other++)
	{
		if(Other == c)
			continue;
		float OtherLow = 1e9f, OtherHigh = -1e9f;
		for(const auto &Point : pEnv->m_vPoints)
		{
			OtherLow = std::min(OtherLow, fx2f(Point.m_aValues[Other]));
			OtherHigh = std::max(OtherHigh, fx2f(Point.m_aValues[Other]));
		}
		if(OtherHigh - OtherLow < 0.0001f)
		{
			OtherLow -= 0.5f;
			OtherHigh += 0.5f;
		}
		const float Pad = (OtherHigh - OtherLow) * 0.15f;
		SampleCurve(Other, OtherLow - Pad, OtherHigh + Pad, Col(ChannelInfo(*pEnv, Other).m_Color, 0.22f), 1.2f);
	}
	if(NumPoints > 0)
		SampleCurve(c, Low, High, Col(Info.m_Color), 2.2f);

	// the handles: a line from the key and a round knob
	struct SHandle
	{
		int m_Point;
		bool m_In;
		ImVec2 m_Pos;
	};
	std::vector<SHandle> vHandles;
	for(int p = 0; p < NumPoints; p++)
	{
		const CEnvPoint_runtime &Point = pEnv->m_vPoints[p];
		const ImVec2 Key(TimeToX(Point.m_Time.AsSeconds()), ValueToY(fx2f(Point.m_aValues[c])));
		if(p > 0 && pEnv->m_vPoints[p - 1].m_Curvetype == CURVETYPE_BEZIER)
			vHandles.push_back({p, true, ImVec2(TimeToX((Point.m_Time + Point.m_Bezier.m_aInTangentDeltaX[c]).AsSeconds()), ValueToY(fx2f(Point.m_aValues[c] + Point.m_Bezier.m_aInTangentDeltaY[c])))});
		if(p + 1 < NumPoints && Point.m_Curvetype == CURVETYPE_BEZIER)
			vHandles.push_back({p, false, ImVec2(TimeToX((Point.m_Time + Point.m_Bezier.m_aOutTangentDeltaX[c]).AsSeconds()), ValueToY(fx2f(Point.m_aValues[c] + Point.m_Bezier.m_aOutTangentDeltaY[c])))});
		for(size_t h = vHandles.size(); h-- > 0 && vHandles[h].m_Point == p;)
		{
			const bool Hot = m_EnvGraphDragPoint == p && ((m_EnvGraphDrag == ENVDRAG_IN && vHandles[h].m_In) || (m_EnvGraphDrag == ENVDRAG_OUT && !vHandles[h].m_In));
			pDraw->AddLine(Key, vHandles[h].m_Pos, Col(t.m_TextSecondary, 0.8f), 1.2f);
			pDraw->AddCircleFilled(vHandles[h].m_Pos, S(4.5f), Col(Hot ? t.m_Accent : t.m_Card));
			pDraw->AddCircle(vHandles[h].m_Pos, S(4.5f), Col(Hot ? ImVec4(1, 1, 1, 1) : Info.m_Color), 0, 1.5f);
		}
	}

	// the keys first: they win over a handle on the same spot
	int Delete = -1;
	bool AnyHit = false;
	for(int p = 0; p < NumPoints; p++)
	{
		CEnvPoint_runtime &Point = pEnv->m_vPoints[p];
		const ImVec2 Key(TimeToX(Point.m_Time.AsSeconds()), ValueToY(fx2f(Point.m_aValues[c])));
		const float r = S(5.5f);
		const bool Selected = p == m_EnvSelPoint;
		// a square key like After Effects, the curve ones round
		if(Point.m_Curvetype == CURVETYPE_BEZIER || (p > 0 && pEnv->m_vPoints[p - 1].m_Curvetype == CURVETYPE_BEZIER))
			pDraw->AddCircleFilled(Key, r, Col(Selected ? t.m_Accent : Info.m_Color));
		else
			pDraw->AddRectFilled(ImVec2(Key.x - r * 0.85f, Key.y - r * 0.85f), ImVec2(Key.x + r * 0.85f, Key.y + r * 0.85f), Col(Selected ? t.m_Accent : Info.m_Color), S(1.5f));
		if(Selected)
			pDraw->AddCircle(Key, r + S(2.5f), Col(ImVec4(1, 1, 1, 0.9f)), 0, 1.5f);
		ImGui::SetCursorScreenPos(ImVec2(Key.x - r - S(2.0f), Key.y - r - S(2.0f)));
		ImGui::PushID(p);
		ImGui::InvisibleButton("##gkey", ImVec2(r * 2.0f + S(4.0f), r * 2.0f + S(4.0f)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		if(ImGui::IsItemHovered())
		{
			AnyHit = true;
			char aValue[32];
			str_format(aValue, sizeof(aValue), Info.m_pFormat, fx2f(Point.m_aValues[c]) * Info.m_Scale);
			ImGui::SetTooltip("%.2f %s · %s · %s", Point.m_Time.AsSeconds(), Tr("s"), aValue, Tr("drag to move, right click to delete"));
		}
		if(ImGui::IsItemClicked(ImGuiMouseButton_Right))
			Delete = p;
		if(ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			m_EnvSelPoint = p;
			m_EnvGraphDrag = ENVDRAG_KEY;
			m_EnvGraphDragPoint = p;
			m_aEnvGraphOldTime[0] = Point.m_Time;
			m_aEnvGraphOldValue[0] = Point.m_aValues[c];
		}
		if(m_EnvGraphDrag == ENVDRAG_KEY && m_EnvGraphDragPoint == p && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
		{
			// time between the neighbors (the first key stays at 0), value free; Shift snaps the time to 0.1 s
			const int LowTime = p > 0 ? pEnv->m_vPoints[p - 1].m_Time.GetInternal() + 1 : 0;
			const int HighTime = p + 1 < NumPoints ? pEnv->m_vPoints[p + 1].m_Time.GetInternal() - 1 : 1000000;
			int NewTime = round_to_int(XToTime(Io.MousePos.x) * 1000.0f);
			if(Io.KeyShift)
				NewTime = round_to_int(NewTime / 100.0f) * 100;
			Point.m_Time = CFixedTime(p == 0 ? 0 : std::clamp(NewTime, LowTime, HighTime));
			float NewValue = YToValue(Io.MousePos.y);
			if(ColorLike)
				NewValue = std::clamp(NewValue, 0.0f, 1.0f);
			Point.m_aValues[c] = f2fx(NewValue);
		}
		if(ImGui::IsItemDeactivated() && m_EnvGraphDrag == ENVDRAG_KEY && m_EnvGraphDragPoint == p)
		{
			if(Point.m_Time != m_aEnvGraphOldTime[0] || Point.m_aValues[c] != m_aEnvGraphOldValue[0])
			{
				History.RecordAction(std::make_shared<CEditorActionEditEnvelopePointValue>(pMap, EnvIndex, p, c, CEditorActionEditEnvelopePointValue::EType::POINT, m_aEnvGraphOldTime[0], m_aEnvGraphOldValue[0], Point.m_Time, Point.m_aValues[c]));
				pMap->OnModify();
			}
			m_EnvGraphDrag = ENVDRAG_NONE;
		}
		ImGui::PopID();
	}

	// the handles: drag to shape the curve
	const auto HandleTarget = [&](const CEnvPoint_runtime &Point, bool In, CFixedTime &Time, int &Value) {
		Time = Point.m_Time + (In ? Point.m_Bezier.m_aInTangentDeltaX[c] : Point.m_Bezier.m_aOutTangentDeltaX[c]);
		Value = Point.m_aValues[c] + (In ? Point.m_Bezier.m_aInTangentDeltaY[c] : Point.m_Bezier.m_aOutTangentDeltaY[c]);
	};
	const auto SetHandle = [&](CEnvPoint_runtime &Point, bool In, float Time, float Value) {
		const int DeltaX = round_to_int(Time * 1000.0f) - Point.m_Time.GetInternal();
		const int DeltaY = f2fx(Value) - Point.m_aValues[c];
		if(In)
		{
			Point.m_Bezier.m_aInTangentDeltaX[c] = CFixedTime(std::min(DeltaX, 0));
			Point.m_Bezier.m_aInTangentDeltaY[c] = DeltaY;
		}
		else
		{
			Point.m_Bezier.m_aOutTangentDeltaX[c] = CFixedTime(std::max(DeltaX, 0));
			Point.m_Bezier.m_aOutTangentDeltaY[c] = DeltaY;
		}
	};
	for(const SHandle &Handle : vHandles)
	{
		CEnvPoint_runtime &Point = pEnv->m_vPoints[Handle.m_Point];
		const int Kind = Handle.m_In ? ENVDRAG_IN : ENVDRAG_OUT;
		ImGui::SetCursorScreenPos(ImVec2(Handle.m_Pos.x - S(6.0f), Handle.m_Pos.y - S(6.0f)));
		ImGui::PushID(Handle.m_Point * 2 + (Handle.m_In ? 1 : 0));
		ImGui::InvisibleButton("##handle", ImVec2(S(12.0f), S(12.0f)));
		if(ImGui::IsItemHovered())
		{
			AnyHit = true;
			ImGui::SetTooltip("%s", Tr("Drag the handle to shape the curve. Alt: move it alone"));
		}
		// the other handle of the same key, kept in one line with this one
		const bool HasOther = Handle.m_In ? (Handle.m_Point + 1 < NumPoints && Point.m_Curvetype == CURVETYPE_BEZIER) : (Handle.m_Point > 0 && pEnv->m_vPoints[Handle.m_Point - 1].m_Curvetype == CURVETYPE_BEZIER);
		if(ImGui::IsItemActivated())
		{
			m_EnvSelPoint = Handle.m_Point;
			m_EnvGraphDrag = Kind;
			m_EnvGraphDragPoint = Handle.m_Point;
			HandleTarget(Point, Handle.m_In, m_aEnvGraphOldTime[0], m_aEnvGraphOldValue[0]);
			HandleTarget(Point, !Handle.m_In, m_aEnvGraphOldTime[1], m_aEnvGraphOldValue[1]);
		}
		if(m_EnvGraphDrag == Kind && m_EnvGraphDragPoint == Handle.m_Point && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f))
		{
			SetHandle(Point, Handle.m_In, XToTime(Io.MousePos.x), YToValue(Io.MousePos.y));
			if(HasOther && !Io.KeyAlt)
			{
				// the other handle turns to the opposite side, its length on the screen stays
				const ImVec2 Key(TimeToX(Point.m_Time.AsSeconds()), ValueToY(fx2f(Point.m_aValues[c])));
				CFixedTime OtherTime;
				int OtherValue;
				HandleTarget(Point, !Handle.m_In, OtherTime, OtherValue);
				const ImVec2 OtherPos(TimeToX(OtherTime.AsSeconds()), ValueToY(fx2f(OtherValue)));
				const float OtherLength = std::sqrt((OtherPos.x - Key.x) * (OtherPos.x - Key.x) + (OtherPos.y - Key.y) * (OtherPos.y - Key.y));
				ImVec2 Dir(Io.MousePos.x - Key.x, Io.MousePos.y - Key.y);
				const float Length = std::sqrt(Dir.x * Dir.x + Dir.y * Dir.y);
				if(Length > 0.5f && OtherLength > 0.5f)
				{
					const ImVec2 Mirror(Key.x - Dir.x / Length * OtherLength, Key.y - Dir.y / Length * OtherLength);
					SetHandle(Point, !Handle.m_In, XToTime(Mirror.x), YToValue(Mirror.y));
				}
			}
		}
		if(ImGui::IsItemDeactivated() && m_EnvGraphDrag == Kind && m_EnvGraphDragPoint == Handle.m_Point)
		{
			// one step of Ctrl+Z for both handles
			std::vector<std::shared_ptr<IEditorAction>> vpActions;
			CFixedTime NewTime;
			int NewValue;
			HandleTarget(Point, Handle.m_In, NewTime, NewValue);
			if(NewTime != m_aEnvGraphOldTime[0] || NewValue != m_aEnvGraphOldValue[0])
				vpActions.push_back(std::make_shared<CEditorActionEditEnvelopePointValue>(pMap, EnvIndex, Handle.m_Point, c, Handle.m_In ? CEditorActionEditEnvelopePointValue::EType::TANGENT_IN : CEditorActionEditEnvelopePointValue::EType::TANGENT_OUT, m_aEnvGraphOldTime[0], m_aEnvGraphOldValue[0], NewTime, NewValue));
			HandleTarget(Point, !Handle.m_In, NewTime, NewValue);
			if(HasOther && (NewTime != m_aEnvGraphOldTime[1] || NewValue != m_aEnvGraphOldValue[1]))
				vpActions.push_back(std::make_shared<CEditorActionEditEnvelopePointValue>(pMap, EnvIndex, Handle.m_Point, c, Handle.m_In ? CEditorActionEditEnvelopePointValue::EType::TANGENT_OUT : CEditorActionEditEnvelopePointValue::EType::TANGENT_IN, m_aEnvGraphOldTime[1], m_aEnvGraphOldValue[1], NewTime, NewValue));
			if(!vpActions.empty())
			{
				History.RecordAction(std::make_shared<CEditorActionBulk>(pMap, vpActions, "Move the curve handles"));
				pMap->OnModify();
			}
			m_EnvGraphDrag = ENVDRAG_NONE;
		}
		ImGui::PopID();
	}
	if(m_EnvGraphDrag != ENVDRAG_NONE && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		m_EnvGraphDrag = ENVDRAG_NONE; // the item went away while dragged

	// the play head
	const float HeadX = TimeToX(Now);
	pDraw->AddLine(ImVec2(HeadX, AreaMin.y), ImVec2(HeadX, AreaMax.y), Col(t.m_Accent), 2.0f);
	pDraw->AddTriangleFilled(ImVec2(HeadX - S(5.0f), AreaMin.y), ImVec2(HeadX + S(5.0f), AreaMin.y), ImVec2(HeadX, AreaMin.y + S(7.0f)), Col(t.m_Accent));
	pDraw->PopClipRect();

	// a double click on the graph adds a key there: the time and the value of the mouse, the other channels from the curve
	if(MouseIn(ImVec2(TrackMin, GraphTop), AreaMax) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !AnyHit)
	{
		const float Time = std::max(0.0f, XToTime(Io.MousePos.x));
		const CFixedTime Fixed = CFixedTime::FromSeconds(Time);
		bool Exists = false;
		for(const auto &Point : pEnv->m_vPoints)
			Exists |= Point.m_Time == Fixed;
		if(!Exists)
		{
			ColorRGBA Value(0, 0, 0, 0);
			if(!pEnv->m_vPoints.empty())
				pEnv->Eval(Time, Value, Channels);
			else if(pEnv->Type() == CEnvelope::EType::COLOR)
				Value = ColorRGBA(1, 1, 1, 1);
			float MouseValue = YToValue(Io.MousePos.y);
			if(ColorLike)
				MouseValue = std::clamp(MouseValue, 0.0f, 1.0f);
			Value[c] = MouseValue;
			History.Execute(std::make_shared<CEditorActionAddEnvelopePoint>(pMap, EnvIndex, Fixed, Value));
			for(int i = 0; i < (int)pEnv->m_vPoints.size(); i++)
				if(pEnv->m_vPoints[i].m_Time == Fixed)
					m_EnvSelPoint = i;
			pMap->OnModify();
		}
	}
	if(Delete >= 0)
	{
		History.Execute(std::make_shared<CEditorActionDeleteEnvelopePoint>(pMap, EnvIndex, Delete));
		m_EnvSelPoint = -1;
		pMap->OnModify();
	}
	ImGui::SetCursorScreenPos(ImVec2(AreaMin.x, AreaMax.y + S(4.0f)));
	ImGui::PushFont(pSmall);
	ImGui::TextColored(t.m_TextMuted, "%s", Tr("Experimental · drag keys in time and value · double click adds a key · pick «Curve» in the keyframe to get handles"));
	ImGui::PopFont();
}
