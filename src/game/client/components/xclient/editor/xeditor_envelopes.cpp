// Envelopes as a timeline, like in After Effects: the envelopes on the left, a row of keyframes per channel
// in the middle with the curve behind it, the selected keyframe on the right. Every change goes through the
// classic envelope actions, so Ctrl+Z, the history and the saved map are the same as in the classic editor.
#include "xeditor.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/font_icons.h>

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
			m_EnvView = std::max(1.0f, pEnv->EndTime() * 1.25f);
		}
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
			if(ImGui::DragFloat("##time", &Seconds, 0.01f, 0.0f, 600.0f, "%.2f"))
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
			if(ImGui::DragFloat("##value", &Value, Info.m_Scale > 1.0f ? 0.5f : 0.25f, 0.0f, 0.0f, Info.m_pFormat))
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
		const char *apCurves[] = {Tr("Step"), Tr("Linear"), Tr("Slow"), Tr("Fast"), Tr("Smooth")};
		int Curve = std::clamp(Point.m_Curvetype, 0, 4);
		if(Point.m_Curvetype == CURVETYPE_BEZIER)
			Description(Tr("Bezier: edit the handles in the classic editor"));
		else if(ChoiceChips("##curve", &Curve, apCurves, 5))
		{
			History.Execute(std::make_shared<CEditorActionEnvelopeEditPoint>(pMap, EnvIndex, p, 0, CEditorActionEnvelopeEditPoint::EEditType::CURVE_TYPE, Point.m_Curvetype, Curve));
			pMap->OnModify();
		}
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
