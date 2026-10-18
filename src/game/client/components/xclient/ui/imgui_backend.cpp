#include "imgui_backend.h"

#include <base/log.h>
#include <base/str.h>

#include <engine/external/imgui/imgui.h>
#include <engine/image.h>
#include <engine/keys.h>
#include <engine/storage.h>

#include <string>

static const char *const s_apFontFamilies[] = {"Manrope", "Onest", "Unbounded"};
static const char *const ICON_FONT = "fonts/Font_Awesome_6_Free-Solid-900.otf";

static ImGuiKey TranslateKey(int Key)
{
	switch(Key)
	{
	case KEY_TAB: return ImGuiKey_Tab;
	case KEY_LEFT: return ImGuiKey_LeftArrow;
	case KEY_RIGHT: return ImGuiKey_RightArrow;
	case KEY_UP: return ImGuiKey_UpArrow;
	case KEY_DOWN: return ImGuiKey_DownArrow;
	case KEY_PAGEUP: return ImGuiKey_PageUp;
	case KEY_PAGEDOWN: return ImGuiKey_PageDown;
	case KEY_HOME: return ImGuiKey_Home;
	case KEY_END: return ImGuiKey_End;
	case KEY_INSERT: return ImGuiKey_Insert;
	case KEY_DELETE: return ImGuiKey_Delete;
	case KEY_BACKSPACE: return ImGuiKey_Backspace;
	case KEY_SPACE: return ImGuiKey_Space;
	case KEY_RETURN: return ImGuiKey_Enter;
	case KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
	case KEY_ESCAPE: return ImGuiKey_Escape;
	case KEY_A: return ImGuiKey_A;
	case KEY_C: return ImGuiKey_C;
	case KEY_V: return ImGuiKey_V;
	case KEY_X: return ImGuiKey_X;
	case KEY_Y: return ImGuiKey_Y;
	case KEY_Z: return ImGuiKey_Z;
	case KEY_LCTRL: return ImGuiKey_LeftCtrl;
	case KEY_RCTRL: return ImGuiKey_RightCtrl;
	case KEY_LSHIFT: return ImGuiKey_LeftShift;
	case KEY_RSHIFT: return ImGuiKey_RightShift;
	case KEY_LALT: return ImGuiKey_LeftAlt;
	case KEY_RALT: return ImGuiKey_RightAlt;
	case KEY_LGUI: return ImGuiKey_LeftSuper;
	case KEY_RGUI: return ImGuiKey_RightSuper;
	default: return ImGuiKey_None;
	}
}

static const char *GetClipboard(ImGuiContext *pContext)
{
	(void)pContext;
	// ImGui only reads the text right away, a static copy is enough
	static std::string s_Clipboard;
	IInput *pInput = (IInput *)ImGui::GetIO().BackendRendererUserData;
	s_Clipboard = pInput->GetClipboardText();
	return s_Clipboard.c_str();
}

static void SetClipboard(ImGuiContext *pContext, const char *pText)
{
	(void)pContext;
	IInput *pInput = (IInput *)ImGui::GetIO().BackendRendererUserData;
	pInput->SetClipboardText(pText);
}

bool CImGuiBackend::Init(IGraphics *pGraphics, IInput *pInput, IStorage *pStorage)
{
	m_pGraphics = pGraphics;
	m_pInput = pInput;
	m_pStorage = pStorage;

	IMGUI_CHECKVERSION();
	m_pContext = ImGui::CreateContext();
	ImGui::SetCurrentContext(m_pContext);
	ImGuiIO &Io = ImGui::GetIO();
	Io.IniFilename = nullptr;
	Io.LogFilename = nullptr;
	Io.BackendPlatformName = "ddnet_xclient";
	Io.BackendRendererName = "ddnet_igraphics";
	Io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
	Io.BackendPlatformUserData = this;
	Io.BackendRendererUserData = pInput;
	Io.ConfigInputTrickleEventQueue = true;
	Io.ConfigWindowsMoveFromTitleBarOnly = true;

	ImGuiPlatformIO &PlatformIo = ImGui::GetPlatformIO();
	PlatformIo.Platform_GetClipboardTextFn = GetClipboard;
	PlatformIo.Platform_SetClipboardTextFn = SetClipboard;
	return true;
}

void CImGuiBackend::Shutdown()
{
	if(!m_pContext)
		return;
	if(m_FontTexture.IsValid())
		m_pGraphics->UnloadTexture(&m_FontTexture);
	if(m_TextInput)
		m_pInput->StopTextInput();
	ImGui::SetCurrentContext(m_pContext);
	ImGuiIO &Io = ImGui::GetIO();
	Io.BackendPlatformUserData = nullptr;
	Io.BackendRendererUserData = nullptr;
	Io.BackendPlatformName = nullptr;
	Io.BackendRendererName = nullptr;
	Io.Fonts->Clear();
	ImGui::DestroyContext(m_pContext);
	m_pContext = nullptr;
}

const unsigned char *CImGuiBackend::ReadFont(const char *pPath, int *pSize)
{
	void *pData = nullptr;
	unsigned Size = 0;
	if(!m_pStorage->ReadFile(pPath, IStorage::TYPE_ALL, &pData, &Size))
	{
		log_error("xclient/ui", "failed to load font '%s'", pPath);
		return nullptr;
	}
	std::vector<unsigned char> &Buffer = m_vFontData.emplace_back((unsigned char *)pData, (unsigned char *)pData + Size);
	free(pData);
	*pSize = (int)Size;
	return Buffer.data();
}

void CImGuiBackend::LoadFonts(int Family, float Scale)
{
	Family = std::clamp(Family, 0, (int)std::size(s_apFontFamilies) - 1);
	if(Family == m_FontFamily && Scale == m_FontScale)
		return;
	m_FontFamily = Family;
	m_FontScale = Scale;

	ImGui::SetCurrentContext(m_pContext);
	ImFontAtlas *pAtlas = ImGui::GetIO().Fonts;
	pAtlas->Clear();
	m_vFontData.clear();
	m_vFontData.reserve(8);

	char aPath[128];
	int RegularSize, SemiBoldSize, BoldSize, IconSize;
	str_format(aPath, sizeof(aPath), "xclient/fonts/%s-Regular.ttf", s_apFontFamilies[Family]);
	const unsigned char *pRegular = ReadFont(aPath, &RegularSize);
	str_format(aPath, sizeof(aPath), "xclient/fonts/%s-SemiBold.ttf", s_apFontFamilies[Family]);
	const unsigned char *pSemiBold = ReadFont(aPath, &SemiBoldSize);
	str_format(aPath, sizeof(aPath), "xclient/fonts/%s-Bold.ttf", s_apFontFamilies[Family]);
	const unsigned char *pBold = ReadFont(aPath, &BoldSize);
	const unsigned char *pIcons = ReadFont(ICON_FONT, &IconSize);

	static const ImWchar s_aIconRanges[] = {0xe000, 0xf8ff, 0};
	const ImWchar *pTextRanges = pAtlas->GetGlyphRangesCyrillic();

	const auto AddFont = [&](const unsigned char *pData, int Size, float Pixels) -> ImFont * {
		if(!pData)
			return pAtlas->AddFontDefault();
		ImFontConfig Config;
		Config.FontDataOwnedByAtlas = false;
		Config.OversampleH = 2;
		Config.OversampleV = 2;
		ImFont *pFont = pAtlas->AddFontFromMemoryTTF((void *)pData, Size, Pixels * Scale, &Config, pTextRanges);
		if(pIcons)
		{
			// icons inside the same font, so text and icons mix in one string
			ImFontConfig IconConfig;
			IconConfig.FontDataOwnedByAtlas = false;
			IconConfig.MergeMode = true;
			IconConfig.PixelSnapH = true;
			IconConfig.GlyphMinAdvanceX = Pixels * Scale;
			IconConfig.GlyphOffset = ImVec2(0.0f, 0.5f * Scale);
			pAtlas->AddFontFromMemoryTTF((void *)pIcons, IconSize, Pixels * Scale * 0.9f, &IconConfig, s_aIconRanges);
		}
		return pFont;
	};

	// first font is the default one
	m_Fonts.m_pBody = AddFont(pRegular, RegularSize, 15.0f);
	m_Fonts.m_pLabel = AddFont(pSemiBold, SemiBoldSize, 14.0f);
	m_Fonts.m_pTitle = AddFont(pBold, BoldSize, 16.0f);
	m_Fonts.m_pHeader = AddFont(pBold, BoldSize, 24.0f);
	m_Fonts.m_pSmall = AddFont(pRegular, RegularSize, 13.0f);
	m_Fonts.m_pIcons = AddFont(pSemiBold, SemiBoldSize, 18.0f);
	pAtlas->Build();

	unsigned char *pPixels;
	int Width, Height;
	pAtlas->GetTexDataAsRGBA32(&pPixels, &Width, &Height);
	if(m_FontTexture.IsValid())
		m_pGraphics->UnloadTexture(&m_FontTexture);
	CImageInfo Image;
	Image.m_Width = Width;
	Image.m_Height = Height;
	Image.m_Format = CImageInfo::FORMAT_RGBA;
	Image.m_pData = pPixels;
	m_FontTexture = m_pGraphics->LoadTextureRaw(Image, 0, "xclient_ui_font");
	Image.m_pData = nullptr; // owned by the atlas
	pAtlas->SetTexID((ImTextureID)(uintptr_t)TextureId(m_FontTexture));
}

void *CImGuiBackend::TextureId(IGraphics::CTextureHandle Texture)
{
	for(size_t i = 0; i < m_vTextures.size(); i++)
	{
		if(m_vTextures[i].Id() == Texture.Id())
			return (void *)(uintptr_t)(i + 1);
	}
	m_vTextures.push_back(Texture);
	return (void *)(uintptr_t)m_vTextures.size();
}

bool CImGuiBackend::OnInput(const IInput::CEvent &Event)
{
	if(!m_pContext)
		return false;
	ImGui::SetCurrentContext(m_pContext);
	ImGuiIO &Io = ImGui::GetIO();

	if(Event.m_Flags & IInput::FLAG_TEXT)
	{
		Io.AddInputCharactersUTF8(Event.m_aText);
		return true;
	}

	const bool Down = (Event.m_Flags & IInput::FLAG_PRESS) != 0;
	const bool Up = (Event.m_Flags & IInput::FLAG_RELEASE) != 0;
	if(!Down && !Up)
		return false;

	if(Event.m_Key >= KEY_MOUSE_1 && Event.m_Key <= KEY_MOUSE_3)
	{
		Io.AddMouseButtonEvent(Event.m_Key - KEY_MOUSE_1, Down);
		return true;
	}
	if(Down && (Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN))
	{
		Io.AddMouseWheelEvent(0.0f, Event.m_Key == KEY_MOUSE_WHEEL_UP ? 1.0f : -1.0f);
		return true;
	}

	const ImGuiKey Key = TranslateKey(Event.m_Key);
	if(Key != ImGuiKey_None)
	{
		Io.AddKeyEvent(Key, Down);
		Io.AddKeyEvent(ImGuiMod_Ctrl, m_pInput->ModifierIsPressed());
		Io.AddKeyEvent(ImGuiMod_Shift, m_pInput->ShiftIsPressed());
		Io.AddKeyEvent(ImGuiMod_Alt, m_pInput->AltIsPressed());
	}
	return true;
}

void CImGuiBackend::OnMouseMove(vec2 MousePos)
{
	if(!m_pContext)
		return;
	ImGui::SetCurrentContext(m_pContext);
	const float ScaleX = m_pGraphics->ScreenWidth() / std::max((float)m_pGraphics->WindowWidth(), 1.0f);
	const float ScaleY = m_pGraphics->ScreenHeight() / std::max((float)m_pGraphics->WindowHeight(), 1.0f);
	ImGui::GetIO().AddMousePosEvent(MousePos.x * ScaleX, MousePos.y * ScaleY);
}

void CImGuiBackend::NewFrame(float Dt, vec2 MousePos)
{
	ImGui::SetCurrentContext(m_pContext);
	ImGuiIO &Io = ImGui::GetIO();
	// render in drawable pixels, the mouse comes in window coordinates
	const float Width = (float)m_pGraphics->ScreenWidth();
	const float Height = (float)m_pGraphics->ScreenHeight();
	Io.DisplaySize = ImVec2(Width, Height);
	Io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
	Io.DeltaTime = std::max(Dt, 1.0f / 1000.0f);
	const float ScaleX = Width / std::max((float)m_pGraphics->WindowWidth(), 1.0f);
	const float ScaleY = Height / std::max((float)m_pGraphics->WindowHeight(), 1.0f);
	Io.AddMousePosEvent(MousePos.x * ScaleX, MousePos.y * ScaleY);

	ImGui::NewFrame();
}

void CImGuiBackend::Render()
{
	ImGui::SetCurrentContext(m_pContext);
	ImGui::Render();
	RenderDrawData(ImGui::GetDrawData());

	// text fields need the system text input (also shows the on screen keyboard on phones)
	const bool WantText = ImGui::GetIO().WantTextInput;
	if(WantText != m_TextInput)
	{
		m_TextInput = WantText;
		if(WantText)
			m_pInput->StartTextInput();
		else
			m_pInput->StopTextInput();
	}
}

void CImGuiBackend::RenderDrawData(ImDrawData *pDrawData)
{
	if(!pDrawData || pDrawData->TotalVtxCount == 0)
		return;
	IGraphics *pGraphics = m_pGraphics;
	const CScreenRect SavedScreen = pGraphics->GetScreen();
	pGraphics->MapScreen(CScreenRect(0.0f, 0.0f, pDrawData->DisplaySize.x, pDrawData->DisplaySize.y));
	pGraphics->WrapClamp();

	for(const ImDrawList *pList : pDrawData->CmdLists)
	{
		const ImDrawVert *pVertices = pList->VtxBuffer.Data;
		const ImDrawIdx *pIndices = pList->IdxBuffer.Data;
		for(const ImDrawCmd &Cmd : pList->CmdBuffer)
		{
			if(Cmd.UserCallback)
			{
				if(Cmd.UserCallback != ImDrawCallback_ResetRenderState)
					Cmd.UserCallback(pList, &Cmd);
				continue;
			}
			const ImVec4 Clip = Cmd.ClipRect;
			if(Clip.z <= Clip.x || Clip.w <= Clip.y)
				continue;
			pGraphics->ClipEnable((int)Clip.x, (int)Clip.y, (int)(Clip.z - Clip.x + 0.5f), (int)(Clip.w - Clip.y + 0.5f));

			const size_t TextureIndex = (size_t)Cmd.GetTexID();
			if(TextureIndex >= 1 && TextureIndex <= m_vTextures.size())
				pGraphics->TextureSet(m_vTextures[TextureIndex - 1]);
			else
				pGraphics->TextureClear();

			pGraphics->TrianglesBegin();
			for(unsigned i = 0; i + 2 < Cmd.ElemCount; i += 3)
			{
				const ImDrawVert &a = pVertices[Cmd.VtxOffset + pIndices[Cmd.IdxOffset + i]];
				const ImDrawVert &b = pVertices[Cmd.VtxOffset + pIndices[Cmd.IdxOffset + i + 1]];
				const ImDrawVert &c = pVertices[Cmd.VtxOffset + pIndices[Cmd.IdxOffset + i + 2]];
				const auto ToColor = [](ImU32 Col) {
					return ColorRGBA(((Col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f, ((Col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
						((Col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f, ((Col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
				};
				const ColorRGBA ColorC = ToColor(c.col);
				pGraphics->SetColor4(ToColor(a.col), ToColor(b.col), ColorC, ColorC);
				pGraphics->QuadsSetSubsetFree(a.uv.x, a.uv.y, b.uv.x, b.uv.y, c.uv.x, c.uv.y, c.uv.x, c.uv.y);
				// a quad with the last two corners on the same point is one triangle
				const IGraphics::CFreeformItem Item(a.pos.x, a.pos.y, b.pos.x, b.pos.y, c.pos.x, c.pos.y, c.pos.x, c.pos.y);
				pGraphics->QuadsDrawFreeform(&Item, 1);
			}
			pGraphics->TrianglesEnd();
		}
	}

	pGraphics->ClipDisable();
	pGraphics->WrapNormal();
	pGraphics->MapScreen(SavedScreen);
}
