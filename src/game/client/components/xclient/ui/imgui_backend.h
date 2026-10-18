#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_IMGUI_BACKEND_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_IMGUI_BACKEND_H

#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/input.h>

#include <vector>

struct ImGuiContext;
struct ImDrawData;
struct ImFont;
class IStorage;

// Dear ImGui on top of DDNet: input comes from IInput events, drawing goes through IGraphics,
// so the menu works with every backend (OpenGL 1-3, Vulkan).
class CImGuiBackend
{
public:
	struct SFonts
	{
		ImFont *m_pBody = nullptr; // regular, main text
		ImFont *m_pLabel = nullptr; // semi bold, parameter names and buttons
		ImFont *m_pTitle = nullptr; // bold, card titles
		ImFont *m_pHeader = nullptr; // bold, page headers
		ImFont *m_pSmall = nullptr; // regular, descriptions and hints
		ImFont *m_pIcons = nullptr; // big icons for the top bar
	};

	bool Init(IGraphics *pGraphics, IInput *pInput, IStorage *pStorage);
	void Shutdown();
	bool IsReady() const { return m_pContext != nullptr; }

	// Rebuilds the font atlas: Family 0 Manrope, 1 Onest, 2 Unbounded; Scale is the UI scale.
	void LoadFonts(int Family, float Scale);
	const SFonts &Fonts() const { return m_Fonts; }
	int FontFamily() const { return m_FontFamily; }
	float FontScale() const { return m_FontScale; }

	// Input. MousePos is in window coordinates.
	bool OnInput(const IInput::CEvent &Event);
	// Call on every mouse move, so clicks right after a move land on the new position.
	void OnMouseMove(vec2 MousePos);
	void NewFrame(float Dt, vec2 MousePos);
	void Render();

	// Lets ImGui draw a DDNet texture, e.g. skin previews.
	void *TextureId(IGraphics::CTextureHandle Texture);

private:
	IGraphics *m_pGraphics = nullptr;
	IInput *m_pInput = nullptr;
	IStorage *m_pStorage = nullptr;
	ImGuiContext *m_pContext = nullptr;

	SFonts m_Fonts;
	int m_FontFamily = -1;
	float m_FontScale = 0.0f;
	std::vector<std::vector<unsigned char>> m_vFontData; // the atlas keeps pointers into these
	IGraphics::CTextureHandle m_FontTexture;
	std::vector<IGraphics::CTextureHandle> m_vTextures; // index + 1 = ImTextureID

	bool m_TextInput = false;

	const unsigned char *ReadFont(const char *pPath, int *pSize);
	void RenderDrawData(ImDrawData *pDrawData);
};

#endif
