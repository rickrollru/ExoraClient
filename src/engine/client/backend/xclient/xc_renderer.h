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
		MAX_GLOW_LEVELS = 5,
	};

	bool m_Initialized = false;
	// low latency: the fence of the last frame, waited for before the next one is shown
	void *m_pFrameFence = nullptr;
	bool m_Failed = false;

	unsigned m_ShapeProgram = 0;
	unsigned m_DownProgram = 0;
	unsigned m_UpProgram = 0;
	unsigned m_CompositeProgram = 0;
	unsigned m_GlowProgram = 0;

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
	int m_LocGlowCapture = -1;
	int m_LocGlowBlur = -1;
	int m_LocGlowColor = -1;
	int m_LocGlowRimColor = -1;
	int m_LocGlowStrength = -1;
	int m_LocGlowRimWidth = -1;
	int m_LocGlowTexel = -1;

	// sprite capture for the neon outline
	STarget m_Capture;
	bool m_Capturing = false;
	int m_CaptureSavedDraw = 0;
	int m_CaptureSavedRead = 0;
	int m_aCaptureRegion[4] = {};

	bool Init();
	void DrawShapes(const XcRender::SCommand_DrawShapes *pCommand);
	void BlurScreen(const XcRender::SCommand_BlurScreen *pCommand);
	void CaptureBegin(const XcRender::SCommand_CaptureBegin *pCommand);
	void CaptureEnd(const XcRender::SCommand_CaptureEnd *pCommand);
	static float BlurSpread(int Levels);
	// world rectangle (x0, y0, x1, y1) to a pixel region (x, y, w, h, rows from the bottom) plus a margin
	static bool WorldToRegion(const float *pScreen, const float *pWorld, int Width, int Height, float Margin, int *pRegion);

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
