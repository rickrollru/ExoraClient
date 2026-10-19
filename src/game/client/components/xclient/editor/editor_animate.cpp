// After Effects like animation presets for the map editor. A preset becomes a standard map envelope
// (position: x, y, rotation; color: RGBA) and is put on the selected quads, optionally staggered in time
// like in After Effects, or on the selected tile layer for the color presets. Everybody sees them, the
// map format is not changed.
#include "editor_style.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/shared/config.h>

#include <game/client/components/xclient/ui/lang.h>
#include <game/client/ui.h>
#include <game/editor/editor.h>
#include <game/editor/mapitems/envelope.h>
#include <game/editor/mapitems/layer_quads.h>
#include <game/editor/mapitems/layer_tiles.h>

#include <array>
#include <cmath>
#include <vector>

using XcUi::Tr;

namespace
{
enum
{
	PRESET_SWAY = 0,
	PRESET_SPIN,
	PRESET_FLOAT,
	PRESET_BOUNCE,
	PRESET_WIGGLE,
	PRESET_ORBIT,
	PRESET_PULSE,
	PRESET_BLINK,
	PRESET_RAINBOW,
	PRESET_FADE,
	NUM_PRESETS,
};
const char *const s_apPresets[NUM_PRESETS] = {"Sway", "Spin", "Float", "Bounce", "Wiggle", "Orbit", "Pulse", "Blink", "Rainbow", "Fade"};
const char *const s_apHints[NUM_PRESETS] = {
	"Rocks left and right around the pivot.",
	"Turns around the pivot all the time.",
	"Floats up and down softly.",
	"Jumps up and falls back like a ball.",
	"Random smooth shaking, like the wiggle expression.",
	"Moves on a circle.",
	"Fades out a little and back.",
	"Turns on and off.",
	"Goes through all colors.",
	"Appears and disappears smoothly.",
};
bool IsColorPreset(int Preset) { return Preset >= PRESET_PULSE; }
// the amount means degrees, units or nothing, depending on the preset
float DefaultAmount(int Preset)
{
	switch(Preset)
	{
	case PRESET_SWAY: return 15.0f;
	case PRESET_SPIN: return 1.0f; // turns per loop
	case PRESET_FLOAT: return 16.0f;
	case PRESET_BOUNCE: return 48.0f;
	case PRESET_WIGGLE: return 12.0f;
	case PRESET_ORBIT: return 32.0f;
	default: return 0.65f; // how strong a color effect is, 0..1
	}
}

struct SAnimateState
{
	int m_Preset = PRESET_SWAY;
	int m_DurationMs = 1600;
	float m_Amount = 15.0f;
	int m_StaggerMs = 0;
	int m_Easing = 0; // 0 smooth, 1 linear, 2 step
	unsigned m_Seed = 1;
};
SAnimateState s_State;

int Curve(int Easing)
{
	return Easing == 1 ? CURVETYPE_LINEAR : (Easing == 2 ? CURVETYPE_STEP : CURVETYPE_SMOOTH);
}

float Random01(unsigned &Seed)
{
	Seed = Seed * 1103515245u + 12345u;
	return ((Seed >> 8) & 0xFFFF) / 65535.0f;
}

std::shared_ptr<CEnvelope> BuildEnvelope(const SAnimateState &State)
{
	const int D = std::max(State.m_DurationMs, 50);
	const float A = State.m_Amount;
	const int C = Curve(State.m_Easing);
	const bool Color = IsColorPreset(State.m_Preset);
	std::shared_ptr<CEnvelope> pEnv = std::make_shared<CEnvelope>(Color ? CEnvelope::EType::COLOR : CEnvelope::EType::POSITION);
	str_format(pEnv->m_aName, sizeof(pEnv->m_aName), "ae %s", s_apPresets[State.m_Preset]);
	pEnv->m_Synchronized = true;
	// x, y, rotation for positions; r, g, b, a for colors
	const auto Point = [&](int TimeMs, float a, float b, float c, float d, int Curvetype) {
		pEnv->AddPoint(CFixedTime(TimeMs), {f2fx(a), f2fx(b), f2fx(c), f2fx(d)});
		for(auto &EnvPoint : pEnv->m_vPoints)
			if(EnvPoint.m_Time.GetInternal() == TimeMs)
				EnvPoint.m_Curvetype = Curvetype;
	};
	unsigned Seed = State.m_Seed;
	switch(State.m_Preset)
	{
	case PRESET_SWAY:
		Point(0, 0, 0, 0, 0, C);
		Point(D / 4, 0, 0, A, 0, C);
		Point(D / 2, 0, 0, 0, 0, C);
		Point(D * 3 / 4, 0, 0, -A, 0, C);
		Point(D, 0, 0, 0, 0, C);
		break;
	case PRESET_SPIN:
		Point(0, 0, 0, 0, 0, CURVETYPE_LINEAR);
		Point(D, 0, 0, 360.0f * std::max(A, 0.25f), 0, CURVETYPE_LINEAR);
		break;
	case PRESET_FLOAT:
		Point(0, 0, 0, 0, 0, C);
		Point(D / 2, 0, -A, 0, 0, C);
		Point(D, 0, 0, 0, 0, C);
		break;
	case PRESET_BOUNCE:
		// up fast and slowing down, then falling faster and faster, then a short rest
		Point(0, 0, 0, 0, 0, CURVETYPE_FAST);
		Point(D * 35 / 100, 0, -A, 0, 0, CURVETYPE_SLOW);
		Point(D * 70 / 100, 0, 0, 0, 0, CURVETYPE_STEP);
		Point(D, 0, 0, 0, 0, CURVETYPE_STEP);
		break;
	case PRESET_WIGGLE:
	{
		const int Keys = 8;
		float aX[Keys], aY[Keys], aR[Keys];
		for(int i = 0; i < Keys; i++)
		{
			aX[i] = (Random01(Seed) * 2.0f - 1.0f) * A;
			aY[i] = (Random01(Seed) * 2.0f - 1.0f) * A;
			aR[i] = (Random01(Seed) * 2.0f - 1.0f) * A * 0.4f;
		}
		for(int i = 0; i <= Keys; i++)
			Point(D * i / Keys, aX[i % Keys], aY[i % Keys], aR[i % Keys], 0, C); // the last key equals the first, the loop has no jump
		break;
	}
	case PRESET_ORBIT:
		for(int i = 0; i <= 8; i++)
		{
			const float Angle = 2.0f * pi * i / 8.0f;
			Point(D * i / 8, std::cos(Angle) * A, std::sin(Angle) * A, 0, 0, CURVETYPE_SMOOTH);
		}
		break;
	case PRESET_PULSE:
		Point(0, 1, 1, 1, 1, C);
		Point(D / 2, 1, 1, 1, 1.0f - std::clamp(A, 0.0f, 1.0f), C);
		Point(D, 1, 1, 1, 1, C);
		break;
	case PRESET_BLINK:
		Point(0, 1, 1, 1, 1, CURVETYPE_STEP);
		Point(D / 2, 1, 1, 1, 1.0f - std::clamp(A, 0.0f, 1.0f), CURVETYPE_STEP);
		Point(D, 1, 1, 1, 1, CURVETYPE_STEP);
		break;
	case PRESET_RAINBOW:
		for(int i = 0; i <= 6; i++)
		{
			const ColorRGBA Rgb = color_cast<ColorRGBA>(ColorHSLA((i % 6) / 6.0f, 1.0f, 0.5f + 0.5f * (1.0f - std::clamp(A, 0.0f, 1.0f))));
			Point(D * i / 6, Rgb.r, Rgb.g, Rgb.b, 1, CURVETYPE_LINEAR);
		}
		break;
	case PRESET_FADE:
		Point(0, 1, 1, 1, 0, C);
		Point(D / 2, 1, 1, 1, std::clamp(A, 0.05f, 1.0f), C);
		Point(D, 1, 1, 1, 0, C);
		break;
	}
	return pEnv;
}

// a value with - and + buttons
bool StepperRow(CEditor *pEditor, CUIRect *pView, const char *pLabel, const char *pValue, const void *pIdMinus, const void *pIdPlus, int *pDirection)
{
	CUIRect Row, Label, Minus, Value, Plus;
	pView->HSplitTop(2.0f, nullptr, pView);
	pView->HSplitTop(12.0f, &Row, pView);
	Row.VMargin(5.0f, &Row);
	Row.VSplitLeft(100.0f, &Label, &Row);
	Row.VSplitLeft(16.0f, &Minus, &Row);
	Row.VSplitRight(16.0f, &Value, &Plus);
	pEditor->Ui()->DoLabel(&Label, pLabel, 10.0f, TEXTALIGN_ML);
	pEditor->Ui()->DoLabel(&Value, pValue, 10.0f, TEXTALIGN_MC);
	*pDirection = 0;
	if(pEditor->DoButton_Editor(pIdMinus, "-", 0, &Minus, BUTTONFLAG_LEFT, nullptr))
		*pDirection = -1;
	if(pEditor->DoButton_Editor(pIdPlus, "+", 0, &Plus, BUTTONFLAG_LEFT, nullptr))
		*pDirection = 1;
	return *pDirection != 0;
}
} // namespace

CUi::EPopupMenuFunctionResult CEditor::XcPopupAnimate(void *pContext, CUIRect View, bool Active)
{
	CEditor *pEditor = static_cast<CEditor *>(pContext);
	SAnimateState &State = s_State;

	CUIRect Title;
	View.HSplitTop(14.0f, &Title, &View);
	pEditor->Ui()->DoLabel(&Title, Tr("Animation (like After Effects)"), 12.0f, TEXTALIGN_MC);

	// what the animation goes on
	std::vector<CQuad *> vpQuads = pEditor->Map()->SelectedQuads();
	std::shared_ptr<CLayerTiles> pTiles = std::static_pointer_cast<CLayerTiles>(pEditor->Map()->SelectedLayerType(0, LAYERTYPE_TILES));
	char aTarget[128];
	if(!vpQuads.empty())
		str_format(aTarget, sizeof(aTarget), "%s: %d", Tr("Selected quads"), (int)vpQuads.size());
	else if(pTiles)
		str_copy(aTarget, Tr("Tile layer: only the color presets"));
	else
		str_copy(aTarget, Tr("Select quads (or a tile layer for colors) first"));
	CUIRect Target;
	View.HSplitTop(12.0f, &Target, &View);
	pEditor->Ui()->DoLabel(&Target, aTarget, 9.0f, TEXTALIGN_MC);
	View.HSplitTop(4.0f, nullptr, &View);

	// the presets in two rows: movement and color
	static char s_aPresetIds[NUM_PRESETS];
	for(int Row = 0; Row < 2; Row++)
	{
		CUIRect Line;
		View.HSplitTop(2.0f, nullptr, &View);
		View.HSplitTop(13.0f, &Line, &View);
		Line.VMargin(5.0f, &Line);
		const float Width = Line.w / 5.0f;
		for(int i = 0; i < 5; i++)
		{
			const int Preset = Row * 5 + i;
			CUIRect Button = {Line.x + i * Width + 1.0f, Line.y, Width - 2.0f, Line.h};
			if(pEditor->DoButton_Editor(&s_aPresetIds[Preset], Tr(s_apPresets[Preset]), State.m_Preset == Preset ? 1 : 0, &Button, BUTTONFLAG_LEFT, Tr(s_apHints[Preset])))
			{
				State.m_Preset = Preset;
				State.m_Amount = DefaultAmount(Preset);
			}
		}
	}
	View.HSplitTop(4.0f, nullptr, &View);

	int Direction;
	char aValue[32];
	static char s_aStepIds[6];
	str_format(aValue, sizeof(aValue), "%.2f s", State.m_DurationMs / 1000.0f);
	if(StepperRow(pEditor, &View, Tr("Duration"), aValue, &s_aStepIds[0], &s_aStepIds[1], &Direction))
		State.m_DurationMs = std::clamp(State.m_DurationMs + Direction * (State.m_DurationMs >= 2000 ? 500 : 100), 100, 60000);
	const bool ColorPreset = IsColorPreset(State.m_Preset);
	if(ColorPreset)
		str_format(aValue, sizeof(aValue), "%d%%", (int)std::round(State.m_Amount * 100.0f));
	else if(State.m_Preset == PRESET_SPIN)
		str_format(aValue, sizeof(aValue), "%.2f %s", State.m_Amount, Tr("turns"));
	else if(State.m_Preset == PRESET_SWAY)
		str_format(aValue, sizeof(aValue), "%.0f°", State.m_Amount);
	else
		str_format(aValue, sizeof(aValue), "%.1f %s", State.m_Amount / 32.0f, Tr("tiles"));
	if(StepperRow(pEditor, &View, Tr("Strength"), aValue, &s_aStepIds[2], &s_aStepIds[3], &Direction))
	{
		const float Step = ColorPreset ? 0.05f : (State.m_Preset == PRESET_SPIN ? 0.25f : (State.m_Preset == PRESET_SWAY ? 5.0f : 8.0f));
		State.m_Amount = std::max(State.m_Amount + Direction * Step, ColorPreset ? 0.05f : Step);
		if(ColorPreset)
			State.m_Amount = std::min(State.m_Amount, 1.0f);
	}
	str_format(aValue, sizeof(aValue), "%d ms", State.m_StaggerMs);
	if(StepperRow(pEditor, &View, Tr("Stagger"), aValue, &s_aStepIds[4], &s_aStepIds[5], &Direction))
		State.m_StaggerMs = std::clamp(State.m_StaggerMs + Direction * 50, 0, 5000);

	// easing
	{
		CUIRect Row, Label, Buttons;
		View.HSplitTop(2.0f, nullptr, &View);
		View.HSplitTop(12.0f, &Row, &View);
		Row.VMargin(5.0f, &Row);
		Row.VSplitLeft(100.0f, &Label, &Buttons);
		pEditor->Ui()->DoLabel(&Label, Tr("Easing"), 10.0f, TEXTALIGN_ML);
		static char s_aEaseIds[3];
		const char *apEasing[] = {"Smooth", "Linear", "Steps"};
		const float Width = Buttons.w / 3.0f;
		for(int i = 0; i < 3; i++)
		{
			CUIRect Button = {Buttons.x + i * Width, Buttons.y, Width, Buttons.h};
			if(pEditor->DoButton_Ex(&s_aEaseIds[i], Tr(apEasing[i]), State.m_Easing == i, &Button, BUTTONFLAG_LEFT, nullptr, i == 0 ? IGraphics::CORNER_L : (i == 2 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE)))
				State.m_Easing = i;
		}
	}

	View.HSplitTop(6.0f, nullptr, &View);
	CUIRect Apply;
	View.HSplitTop(14.0f, &Apply, &View);
	Apply.VMargin(5.0f, &Apply);
	const bool CanApply = !vpQuads.empty() || (pTiles && ColorPreset);
	static int s_ApplyButton = 0;
	if(pEditor->DoButton_Editor(&s_ApplyButton, Tr("Apply"), CanApply ? 1 : -1, &Apply, BUTTONFLAG_LEFT, Tr("Makes an envelope and puts it on the selection. Edit it later in Envelopes below.")) && CanApply)
	{
		State.m_Seed = State.m_Seed * 7919u + 13u; // a new wiggle every time
		std::shared_ptr<CEnvelope> pEnv = BuildEnvelope(State);
		pEditor->Map()->m_vpEnvelopes.push_back(pEnv);
		pEditor->Map()->m_SelectedEnvelope = (int)pEditor->Map()->m_vpEnvelopes.size() - 1;
		const int EnvIndex = (int)pEditor->Map()->m_vpEnvelopes.size() - 1;
		if(!vpQuads.empty())
		{
			for(size_t i = 0; i < vpQuads.size(); i++)
			{
				if(ColorPreset)
				{
					vpQuads[i]->m_ColorEnv = EnvIndex;
					vpQuads[i]->m_ColorEnvOffset = (int)i * State.m_StaggerMs;
				}
				else
				{
					vpQuads[i]->m_PosEnv = EnvIndex;
					vpQuads[i]->m_PosEnvOffset = (int)i * State.m_StaggerMs;
				}
			}
		}
		else
		{
			pTiles->m_ColorEnv = EnvIndex;
			pTiles->m_ColorEnvOffset = 0;
		}
		pEditor->Map()->OnModify();
		// play the animations right away so the result is visible
		pEditor->Map()->m_EnvelopeEvaluator.m_Animate = true;
		return CUi::POPUP_CLOSE_CURRENT;
	}
	return CUi::POPUP_KEEP_OPEN;
}

void CEditor::XcOpenAnimate()
{
	static SPopupMenuId s_PopupAnimateId;
	const CUIRect Screen = *Ui()->Screen();
	Ui()->DoPopupMenu(&s_PopupAnimateId, Screen.w / 2.0f - 170.0f, Screen.h / 2.0f - 90.0f, 340.0f, 158.0f, this, XcPopupAnimate);
}
