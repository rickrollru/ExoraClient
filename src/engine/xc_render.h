#ifndef ENGINE_XC_RENDER_H
#define ENGINE_XC_RENDER_H

// XClient render module: types shared between the game (which builds the shapes)
// and the render thread (which draws them with our own OpenGL 3.3 shaders).

#include <base/color.h>

namespace XcRender
{

	enum EShape
	{
		SHAPE_ELLIPSE = 0, // filled circle / ellipse (half width, half height)
		SHAPE_RING, // ellipse outline, m_Param0 = thickness
		SHAPE_ROUNDRECT, // m_Param0 = corner radius
		SHAPE_STAR, // m_Param0 = number of points, m_Param1 = inner radius ratio (0..1)
		SHAPE_TRIANGLE, // isosceles, apex up, half width at the base and half height
		SHAPE_LINE, // capsule along the x axis, half height = half thickness
		SHAPE_BLOB, // soft gaussian dot, no hard edge
		NUM_SHAPES,
	};

	// One instance as uploaded to the GPU. Plain floats only, 24 floats = 96 bytes.
	struct SShapeGpu
	{
		float m_X, m_Y; // center
		float m_HalfW, m_HalfH;
		float m_Rotation;
		float m_Shape;
		float m_Param0, m_Param1;
		float m_Softness; // extra edge blur in world units, 0 = crisp anti aliased edge
		float m_OutlineWidth; // negative = glow only: just the soft light around the shape, no body
		float m_GlowRadius;
		float m_GlowStrength;
		float m_aColor[4]; // top color (straight alpha)
		float m_aColor2[4]; // bottom color, vertical gradient
		float m_aOutlineColor[4];
	};
	static_assert(sizeof(SShapeGpu) == 24 * sizeof(float), "SShapeGpu must stay tightly packed");

	struct SDrawSettings
	{
		float m_BloomStrength = 1.0f; // 0 = no bloom
		float m_BloomThreshold = 0.0f;
		int m_Quality = 2; // 0 low, 1 medium, 2 high
	};

	struct SBlurSettings
	{
		int m_Passes = 4; // more passes = wider blur
		float m_Offset = 1.0f;
		ColorRGBA m_Tint = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f); // mixed over the blurred image
	};

	// Set by the render thread once the module has initialized successfully.
	bool IsAvailable();

} // namespace XcRender

#endif
