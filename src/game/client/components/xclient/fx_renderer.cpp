#include "fx_renderer.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <cmath>

static void SetColor(float *pOut, ColorRGBA Color)
{
	pOut[0] = Color.r;
	pOut[1] = Color.g;
	pOut[2] = Color.b;
	pOut[3] = Color.a;
}

static SFxShape MakeShape(XcRender::EShape Type, vec2 Pos, vec2 Half, ColorRGBA Color)
{
	SFxShape Shape;
	XcRender::SShapeGpu &Gpu = Shape.m_Gpu;
	Gpu = {};
	Gpu.m_X = Pos.x;
	Gpu.m_Y = Pos.y;
	Gpu.m_HalfW = Half.x;
	Gpu.m_HalfH = Half.y;
	Gpu.m_Shape = (float)Type;
	SetColor(Gpu.m_aColor, Color);
	SetColor(Gpu.m_aColor2, Color);
	return Shape;
}

SFxShape &SFxShape::Gradient(ColorRGBA Bottom)
{
	SetColor(m_Gpu.m_aColor2, Bottom);
	return *this;
}

SFxShape &SFxShape::RadialGradient(ColorRGBA Center, float Power)
{
	SetColor(m_Gpu.m_aColor2, Center);
	m_Gpu.m_Param1 = Power;
	return *this;
}

SFxShape &SFxShape::Outline(float Width, ColorRGBA Color)
{
	m_Gpu.m_OutlineWidth = Width;
	SetColor(m_Gpu.m_aOutlineColor, Color);
	return *this;
}

namespace FxShape
{

	SFxShape Ellipse(vec2 Pos, vec2 Half, ColorRGBA Color)
	{
		return MakeShape(XcRender::SHAPE_ELLIPSE, Pos, Half, Color);
	}

	SFxShape Ring(vec2 Pos, vec2 Radii, float Thickness, ColorRGBA Color)
	{
		SFxShape Shape = MakeShape(XcRender::SHAPE_RING, Pos, Radii, Color);
		Shape.m_Gpu.m_Param0 = Thickness;
		return Shape;
	}

	SFxShape RoundRect(vec2 Pos, vec2 Half, float Rounding, ColorRGBA Color)
	{
		SFxShape Shape = MakeShape(XcRender::SHAPE_ROUNDRECT, Pos, Half, Color);
		Shape.m_Gpu.m_Param0 = Rounding;
		return Shape;
	}

	SFxShape Star(vec2 Pos, float Radius, int Points, float InnerRatio, ColorRGBA Color)
	{
		SFxShape Shape = MakeShape(XcRender::SHAPE_STAR, Pos, vec2(Radius, Radius), Color);
		Shape.m_Gpu.m_Param0 = (float)Points;
		Shape.m_Gpu.m_Param1 = InnerRatio;
		return Shape;
	}

	SFxShape Triangle(vec2 Pos, vec2 Half, ColorRGBA Color)
	{
		return MakeShape(XcRender::SHAPE_TRIANGLE, Pos, Half, Color);
	}

	SFxShape Line(vec2 From, vec2 To, float Width, ColorRGBA Color)
	{
		const float Length = distance(From, To);
		SFxShape Shape = MakeShape(XcRender::SHAPE_LINE, (From + To) * 0.5f, vec2(Length * 0.5f + Width * 0.5f, Width * 0.5f), Color);
		Shape.m_Gpu.m_Rotation = Length > 0.0f ? angle(To - From) : 0.0f;
		return Shape;
	}

	SFxShape Blob(vec2 Pos, float Radius, ColorRGBA Color)
	{
		return MakeShape(XcRender::SHAPE_BLOB, Pos, vec2(Radius, Radius), Color);
	}

} // namespace FxShape

void CFxRenderer::AppendGlowing(std::vector<SFxShape> &vShapes, const SFxShape &Shape, float GlowRadius, float GlowStrength)
{
	SFxShape Body = Shape;
	Body.m_Additive = false;
	vShapes.push_back(Body);
	SFxShape Light = Shape;
	vShapes.push_back(Light.GlowOnly(GlowRadius, GlowStrength).Additive());
}

bool CFxRenderer::ModuleActive() const
{
	return g_Config.m_XcRender && Graphics()->XcRenderAvailable();
}

void CFxRenderer::OnRender()
{
	if(m_vWorld.empty())
		return;
	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		Draw(m_vWorld, true);
	m_vWorld.clear();
}

void CFxRenderer::Draw(const std::vector<SFxShape> &vShapes, bool AllowBloom)
{
	if(vShapes.empty())
		return;
	if(!ModuleActive())
	{
		DrawFallback(vShapes);
		return;
	}

	m_vAlpha.clear();
	m_vAdditive.clear();
	for(const SFxShape &Shape : vShapes)
		(Shape.m_Additive ? m_vAdditive : m_vAlpha).push_back(Shape.m_Gpu);

	XcRender::SDrawSettings Settings;
	Settings.m_Quality = g_Config.m_XcFxQuality;
	Settings.m_BloomStrength = AllowBloom ? g_Config.m_XcBloom / 100.0f : 0.0f;
	Settings.m_BloomThreshold = g_Config.m_XcBloomThreshold / 100.0f;
	Graphics()->XcDrawShapes(m_vAlpha.data(), (int)m_vAlpha.size(), m_vAdditive.data(), (int)m_vAdditive.size(), Settings);
}

void CFxRenderer::BlurBackground(float Amount)
{
	if(!ModuleActive() || !g_Config.m_XcMenuBlur || Amount <= 0.0f)
		return;
	XcRender::SBlurSettings Settings;
	Settings.m_Passes = g_Config.m_XcFxQuality >= 2 ? 4 : (g_Config.m_XcFxQuality == 1 ? 3 : 2);
	Settings.m_Offset = 1.5f * Amount;
	// slight darkening in the theme background color #0A0B0E
	Settings.m_Tint = ColorRGBA(0.039f, 0.043f, 0.055f, 0.25f * Amount);
	Graphics()->XcBlurScreen(Settings);
}

void CFxRenderer::RenderMenuBlur()
{
	const float Now = Client()->GlobalTime();
	// the menu was closed in between: start the fade again
	if(Now - m_LastMenuBlurTime > 0.2f)
		m_MenuBlur = 0.0f;
	m_LastMenuBlurTime = Now;
	m_MenuBlur = std::min(m_MenuBlur + Client()->RenderFrameTime() * 6.0f, 1.0f);
	const float Ease = 1.0f - (1.0f - m_MenuBlur) * (1.0f - m_MenuBlur) * (1.0f - m_MenuBlur);
	BlurBackground(Ease);
}

// ---------------------------------------------------------------------------------------------
// Fallback: the same shapes approximated with IGraphics polygons

void CFxRenderer::DrawFallback(const std::vector<SFxShape> &vShapes) const
{
	for(int Pass = 0; Pass < 2; Pass++)
	{
		const bool AdditivePass = Pass == 1;
		if(AdditivePass)
			Graphics()->BlendAdditive();
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		for(const SFxShape &Shape : vShapes)
			if(Shape.m_Additive == AdditivePass)
				DrawFallbackShape(Shape);
		Graphics()->QuadsEnd();
		if(AdditivePass)
			Graphics()->BlendNormal();
	}
}

void CFxRenderer::DrawFallbackShape(const SFxShape &Shape) const
{
	using namespace XcHelpers;
	const XcRender::SShapeGpu &Gpu = Shape.m_Gpu;
	const vec2 Pos = Shape.Pos();
	const vec2 Half = Shape.Half();
	const ColorRGBA Color = Shape.Color();
	const ColorRGBA Color2 = Shape.Color2();
	const float Rotation = Gpu.m_Rotation;
	const auto Point = [&](vec2 Local) { return Rotate(Pos + Local, Pos, Rotation); };

	if(Gpu.m_OutlineWidth < 0.0f)
	{
		// glow only: a soft band fading out from the shape outline
		const float Radius = std::max(Half.x, 0.01f);
		const float Squash = Half.y / Radius;
		DrawGlowRing(Graphics(), Pos, Radius, Squash, Shape.Shape() == XcRender::SHAPE_RING ? Gpu.m_Param0 : 0.0f, Gpu.m_GlowRadius, Color.WithMultipliedAlpha(Gpu.m_GlowStrength), Rotation);
		return;
	}

	switch(Shape.Shape())
	{
	case XcRender::SHAPE_ELLIPSE:
	case XcRender::SHAPE_BLOB:
	{
		const bool Blob = Shape.Shape() == XcRender::SHAPE_BLOB;
		const float Radius = std::max(Half.x, 0.01f);
		const float Squash = Half.y / Radius;
		if(Blob)
		{
			// soft dot: fully feathered circle
			DrawGlowRing(Graphics(), Pos, Radius * 0.25f, Squash, Radius * 0.5f, Radius * 0.75f, Color, Rotation);
			break;
		}
		if(std::abs(Squash - 1.0f) < 0.01f)
			DrawSoftCircle(Graphics(), Pos, Radius, 1.0f, Color);
		else
			DrawGlowRing(Graphics(), Pos, Radius * 0.5f, Squash, Radius, 1.0f, Color, Rotation);
		if(Gpu.m_GlowRadius > 0.0f)
			DrawGlowRing(Graphics(), Pos, Radius, Squash, 0.0f, Gpu.m_GlowRadius, Color.WithMultipliedAlpha(Gpu.m_GlowStrength), Rotation);
		break;
	}
	case XcRender::SHAPE_RING:
	{
		const float Radius = std::max(Half.x, 0.01f);
		DrawGlowRing(Graphics(), Pos, Radius, Half.y / Radius, Gpu.m_Param0, Gpu.m_GlowRadius > 0.0f ? Gpu.m_Param0 * 2.0f : 0.0f, Color, Rotation);
		break;
	}
	case XcRender::SHAPE_ROUNDRECT:
	case XcRender::SHAPE_LINE:
	{
		if(Gpu.m_OutlineWidth > 0.0f)
		{
			const vec2 o = Half + vec2(Gpu.m_OutlineWidth * 0.5f, Gpu.m_OutlineWidth * 0.5f);
			const ColorRGBA Outline(Gpu.m_aOutlineColor[0], Gpu.m_aOutlineColor[1], Gpu.m_aOutlineColor[2], Gpu.m_aOutlineColor[3]);
			Graphics()->SetColor(Outline);
			const IGraphics::CFreeformItem Item(Point(vec2(-o.x, -o.y)), Point(vec2(o.x, -o.y)), Point(vec2(-o.x, o.y)), Point(vec2(o.x, o.y)));
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
		Graphics()->SetColor4(Color, Color, Color2, Color2);
		const IGraphics::CFreeformItem Item(Point(vec2(-Half.x, -Half.y)), Point(vec2(Half.x, -Half.y)), Point(vec2(-Half.x, Half.y)), Point(vec2(Half.x, Half.y)));
		Graphics()->QuadsDrawFreeform(&Item, 1);
		break;
	}
	case XcRender::SHAPE_TRIANGLE:
	{
		const vec2 Apex = Point(vec2(0.0f, -Half.y));
		if(Gpu.m_OutlineWidth > 0.0f)
		{
			const float o = Gpu.m_OutlineWidth;
			const ColorRGBA Outline(Gpu.m_aOutlineColor[0], Gpu.m_aOutlineColor[1], Gpu.m_aOutlineColor[2], Gpu.m_aOutlineColor[3]);
			Graphics()->SetColor(Outline);
			const vec2 OuterApex = Point(vec2(0.0f, -Half.y - o * 1.5f));
			const IGraphics::CFreeformItem Item(OuterApex, OuterApex, Point(vec2(-Half.x - o, Half.y + o * 0.5f)), Point(vec2(Half.x + o, Half.y + o * 0.5f)));
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
		Graphics()->SetColor4(Color, Color, Color2, Color2);
		const IGraphics::CFreeformItem Item(Apex, Apex, Point(vec2(-Half.x, Half.y)), Point(vec2(Half.x, Half.y)));
		Graphics()->QuadsDrawFreeform(&Item, 1);
		break;
	}
	case XcRender::SHAPE_STAR:
	{
		const int Points = std::max((int)Gpu.m_Param0, 3);
		const float Outer = Half.x;
		const float Inner = Half.x * Gpu.m_Param1;
		Graphics()->SetColor(Color);
		for(int i = 0; i < Points; i++)
		{
			// two triangles per point, the last vertex repeated to make a triangle out of a quad
			const float a = Rotation - pi / 2.0f + 2.0f * pi * i / Points;
			const vec2 Tip = Pos + direction(a) * Outer;
			const vec2 Before = Pos + direction(a - pi / Points) * Inner;
			const vec2 After = Pos + direction(a + pi / Points) * Inner;
			const IGraphics::CFreeformItem aItems[] = {
				IGraphics::CFreeformItem(Pos, Before, Tip, Tip),
				IGraphics::CFreeformItem(Pos, Tip, After, After),
			};
			Graphics()->QuadsDrawFreeform(aItems, 2);
		}
		break;
	}
	}
}
