#ifndef ENGINE_CLIENT_BACKEND_XCLIENT_XC_COMMANDS_H
#define ENGINE_CLIENT_BACKEND_XCLIENT_XC_COMMANDS_H

#include <engine/client/graphics_threaded.h>
#include <engine/xc_render.h>

// Our own commands in the DDNet command buffer. They are executed on the render thread
// by CXcRenderer. Backends without the module (Vulkan, old OpenGL) simply skip them.
namespace XcRender
{

	enum
	{
		CMDGROUP_XCLIENT = 30000,
		CMD_XC_DRAW_SHAPES = CMDGROUP_XCLIENT,
		CMD_XC_BLUR_SCREEN,
		CMD_XC_CAPTURE_BEGIN,
		CMD_XC_CAPTURE_END,
		CMD_XC_DRAW_BLOCKS3D,
		CMD_XC_LAST,
	};

	inline bool IsXcCommand(unsigned Cmd)
	{
		return Cmd >= (unsigned)CMDGROUP_XCLIENT && Cmd < (unsigned)CMD_XC_LAST;
	}

	struct SCommand_DrawShapes : public CCommandBuffer::SCommand
	{
		SCommand_DrawShapes() :
			SCommand(CMD_XC_DRAW_SHAPES) {}

		// world rectangle that maps to the viewport (DDNet screen mapping)
		float m_ScreenLeft, m_ScreenTop, m_ScreenRight, m_ScreenBottom;
		bool m_Tilt; // 3D view: m_aTilt maps the world, not the screen rect
		float m_aTilt[16];
		SShapeGpu *m_pAlpha;
		int m_NumAlpha;
		SShapeGpu *m_pAdditive;
		int m_NumAdditive;
		SDrawSettings m_Settings;
	};

	// Everything drawn between CAPTURE_BEGIN and CAPTURE_END goes into an offscreen texture,
	// END draws it back with a glow and an outline around its silhouette.
	struct SCommand_CaptureBegin : public CCommandBuffer::SCommand
	{
		SCommand_CaptureBegin() :
			SCommand(CMD_XC_CAPTURE_BEGIN) {}

		float m_ScreenLeft, m_ScreenTop, m_ScreenRight, m_ScreenBottom;
		float m_aWorldRect[4]; // x0, y0, x1, y1: only this part of the screen is touched
		bool m_Tilt; // 3D view: the whole screen
	};

	struct SCommand_CaptureEnd : public CCommandBuffer::SCommand
	{
		SCommand_CaptureEnd() :
			SCommand(CMD_XC_CAPTURE_END) {}

		SGlowSettings m_Settings;
	};

	struct SCommand_BlurScreen : public CCommandBuffer::SCommand
	{
		SCommand_BlurScreen() :
			SCommand(CMD_XC_BLUR_SCREEN) {}

		SBlurSettings m_Settings;
	};

	// 3D view: the sides of the blocks, extruded behind the world plane, drawn with their own depth
	struct SCommand_DrawBlocks3D : public CCommandBuffer::SCommand
	{
		SCommand_DrawBlocks3D() :
			SCommand(CMD_XC_DRAW_BLOCKS3D) {}

		float m_aMatrix[16]; // world (x, y, depth) to clip space
		int m_X, m_Y, m_W, m_H; // the cells in m_pGrid
		float m_Depth; // world units
		float m_aColor[4]; // blocks
		float m_aColor2[4]; // unhookable blocks
		float m_aColor3[4]; // freeze, alpha: how much it covers
		float m_aFog[4]; // the color the far end fades to, alpha: how much
		float m_ZFrom, m_ZTo; // the depth the sides span
		float m_FogDistance; // first person: the fog grows with the distance to the eye (0: with the depth)
		float m_aSky[4]; // alpha 1: the frame behind is covered with this color first
		unsigned char *m_pGrid; // 4 bytes per cell, see IGraphics::XcDrawBlocks3D
	};

} // namespace XcRender

#endif
