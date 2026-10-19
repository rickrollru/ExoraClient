#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_FX_RENDERER_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_FX_RENDERER_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/xc_render.h>

#include <game/client/component.h>

#include <vector>

// One SDF shape for the render module. Build it with the factory functions below
// and the chainable modifiers, e.g. FxShape::Ring(...).Glow(8.0f, 0.6f).Additive().
struct SFxShape
{
	XcRender::SShapeGpu m_Gpu;
	bool m_Additive = false;

	SFxShape &Additive()
	{
		m_Additive = true;
		return *this;
	}
	SFxShape &Glow(float Radius, float Strength)
	{
		m_Gpu.m_GlowRadius = Radius;
		m_Gpu.m_GlowStrength = Strength;
		return *this;
	}
	SFxShape &Gradient(ColorRGBA Bottom);
	SFxShape &Outline(float Width, ColorRGBA Color);
	// only the soft light around the shape (use with Additive), see AddGlowing
	SFxShape &GlowOnly(float Radius, float Strength)
	{
		m_Gpu.m_OutlineWidth = -1.0f;
		return Glow(Radius, Strength);
	}
	// ellipse only: Center color in the middle, the shape color at the edge
	SFxShape &RadialGradient(ColorRGBA Center, float Power);
	// ring only: +1 keeps the front (lower) half, -1 the back (upper) half
	SFxShape &Half(float Side)
	{
		m_Gpu.m_Param1 = Side;
		return *this;
	}
	SFxShape &Rotate(float Angle)
	{
		m_Gpu.m_Rotation = Angle;
		return *this;
	}
	SFxShape &Soft(float Softness)
	{
		m_Gpu.m_Softness = Softness;
		return *this;
	}

	vec2 Pos() const { return vec2(m_Gpu.m_X, m_Gpu.m_Y); }
	vec2 Half() const { return vec2(m_Gpu.m_HalfW, m_Gpu.m_HalfH); }
	ColorRGBA Color() const { return ColorRGBA(m_Gpu.m_aColor[0], m_Gpu.m_aColor[1], m_Gpu.m_aColor[2], m_Gpu.m_aColor[3]); }
	ColorRGBA Color2() const { return ColorRGBA(m_Gpu.m_aColor2[0], m_Gpu.m_aColor2[1], m_Gpu.m_aColor2[2], m_Gpu.m_aColor2[3]); }
	int Shape() const { return (int)m_Gpu.m_Shape; }
};

namespace FxShape
{
	SFxShape Ellipse(vec2 Pos, vec2 Half, ColorRGBA Color);
	inline SFxShape Circle(vec2 Pos, float Radius, ColorRGBA Color) { return Ellipse(Pos, vec2(Radius, Radius), Color); }
	SFxShape Ring(vec2 Pos, vec2 Radii, float Thickness, ColorRGBA Color);
	SFxShape RoundRect(vec2 Pos, vec2 Half, float Rounding, ColorRGBA Color);
	SFxShape Star(vec2 Pos, float Radius, int Points, float InnerRatio, ColorRGBA Color);
	SFxShape Triangle(vec2 Pos, vec2 Half, ColorRGBA Color);
	SFxShape Line(vec2 From, vec2 To, float Width, ColorRGBA Color);
	SFxShape Blob(vec2 Pos, float Radius, ColorRGBA Color);
} // namespace FxShape

// Gateway between the effects and the render module.
// With the module: shapes go to the GPU (SDF, additive blending, bloom).
// Without it (Vulkan, old OpenGL, xc_render 0): the same shapes are drawn with IGraphics.
class CFxRenderer : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
	// draws and clears the queued world effects (called by OnRender, or later by CXcOverTiles)
	void FlushWorld();

	// true when shapes are drawn by the render module
	bool ModuleActive() const;

	// Queue a world space shape, drawn once per frame after all world effects.
	void AddWorld(const SFxShape &Shape) { m_vWorld.push_back(Shape); }
	// The shape with its own color (alpha blended, keeps its color on bright maps)
	// plus an additive glow around it that also feeds the bloom.
	void AddWorldGlowing(const SFxShape &Shape, float GlowRadius, float GlowStrength) { AppendGlowing(m_vWorld, Shape, GlowRadius, GlowStrength); }
	static void AppendGlowing(std::vector<SFxShape> &vShapes, const SFxShape &Shape, float GlowRadius, float GlowStrength);
	// Draw shapes right now with the current screen mapping.
	void Draw(const std::vector<SFxShape> &vShapes, bool AllowBloom);

	// Blurs everything drawn so far, used under the in-game menu. Amount 0..1 fades it in.
	void BlurBackground(float Amount, bool Force = false); // Force: ignore xc_menu_blur
	// Call every frame while the in-game menu is open, fades the blur in smoothly.
	void RenderMenuBlur();

private:
	std::vector<SFxShape> m_vWorld;
	std::vector<XcRender::SShapeGpu> m_vAlpha;
	std::vector<XcRender::SShapeGpu> m_vAdditive;
	float m_MenuBlur = 0.0f;
	float m_LastMenuBlurTime = -1.0f;

	void DrawFallback(const std::vector<SFxShape> &vShapes) const;
	void DrawFallbackShape(const SFxShape &Shape) const;
};

#endif
