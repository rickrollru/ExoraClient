#ifndef ENGINE_CLIENT_BACKEND_XCLIENT_XC_RENDERER_H
#define ENGINE_CLIENT_BACKEND_XCLIENT_XC_RENDERER_H

#include "xc_commands.h"

#include <cstdint>

// XClient render module. Lives on the render thread next to the OpenGL 3.3 backend
// and executes the CMD_XC_* commands with its own shaders, buffers and framebuffers.
// Every pass saves the OpenGL state DDNet relies on and restores it afterwards.
class CXcRenderer
{
public:
	CXcRenderer();
	~CXcRenderer();

	// Returns true when the command was one of ours (handled or skipped).
	bool RunCommand(const CCommandBuffer::SCommand *pCommand);
	// Called right before the frame is presented.
	void OnPreSwap();
	// Must be called on the render thread with the context still current.
	void Shutdown();

private:
	struct SProgram
	{
		unsigned m_Id = 0;
	};

	struct STarget
	{
		unsigned m_Fbo = 0;
		unsigned m_Texture = 0;
		int m_Width = 0;
		int m_Height = 0;
	};

	enum
	{
		MAX_LEVELS = 7,
	};

	bool m_Initialized = false;
	bool m_Failed = false;

	unsigned m_ShapeProgram = 0;
	unsigned m_DownProgram = 0;
	unsigned m_UpProgram = 0;
	unsigned m_CompositeProgram = 0;

	unsigned m_ShapeVao = 0;
	unsigned m_QuadVbo = 0;
	unsigned m_InstanceVbo = 0;
	unsigned m_EmptyVao = 0;
	unsigned m_Sampler = 0;

	// bloom chain (half resolution and below) and blur chain (full resolution copy and below)
	STarget m_aBloom[MAX_LEVELS];
	STarget m_aBlur[MAX_LEVELS];
	int m_ViewportW = 0;
	int m_ViewportH = 0;

	// shape uniforms
	int m_LocScreen = -1;
	int m_LocPixel = -1;
	int m_LocBloomPass = -1;
	int m_LocThreshold = -1;
	// filter uniforms
	int m_LocDownTex = -1;
	int m_LocDownHalfPixel = -1;
	int m_LocUpTex = -1;
	int m_LocUpHalfPixel = -1;
	int m_LocCompTex = -1;
	int m_LocCompStrength = -1;
	int m_LocCompTint = -1;

	bool Init();
	void DrawShapes(const XcRender::SCommand_DrawShapes *pCommand);
	void BlurScreen(const XcRender::SCommand_BlurScreen *pCommand);

	void EnsureTargets(int Width, int Height);
	void DestroyTargets();
	void UploadAndDraw(const XcRender::SShapeGpu *pShapes, int Num);
	// Dual Kawase: down to NumLevels-1, then back up to level 0.
	// pRegion (x, y, w, h in full resolution pixels) limits the work to that part of the screen.
	void BlurChain(STarget *pTargets, int NumLevels, float Offset, int FullWidth, const int *pRegion = nullptr);
	static void ScissorScaled(const int *pRegion, int FullWidth, const STarget &Target);
	void DrawFullscreen(unsigned Program, unsigned Texture);
};

#endif
