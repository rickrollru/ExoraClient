#include "cursor.h"

#include "xc_helpers.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/components/xclient/ui/theme.h>

#include <cmath>

namespace XcCursor
{
	static ColorRGBA CursorColor()
	{
		switch(g_Config.m_XcCursorColorMode)
		{
		case 1: return XcHelpers::HslConfigColor(g_Config.m_XcCursorColor);
		case 2: return XcHelpers::RainbowColor(time_get() / (float)time_freq() * 0.15f, 1.0f);
		default:
		{
			const ImVec4 Accent = XcUi::AccentColor(g_Config.m_XcUiAccent, true);
			return ColorRGBA(Accent.x, Accent.y, Accent.z, 1.0f);
		}
		}
	}

	// a filled polygon as a fan of triangles (convex parts only)
	static void Triangles(IGraphics *pGraphics, const vec2 *pPoints, const int (*paTris)[3], int NumTris, vec2 Offset, float Scale)
	{
		for(int i = 0; i < NumTris; i++)
		{
			const vec2 A = Offset + pPoints[paTris[i][0]] * Scale;
			const vec2 B = Offset + pPoints[paTris[i][1]] * Scale;
			const vec2 C = Offset + pPoints[paTris[i][2]] * Scale;
			const IGraphics::CFreeformItem Item(A.x, A.y, B.x, B.y, C.x, C.y, C.x, C.y);
			pGraphics->QuadsDrawFreeform(&Item, 1);
		}
	}

	static void Shape(IGraphics *pGraphics, const vec2 *pPoints, const int (*paTris)[3], int NumTris, vec2 Pos, float Scale, ColorRGBA Color, float Alpha)
	{
		// a dark outline: the same shape shifted around, then the colored shape on top
		if(g_Config.m_XcCursorOutline)
		{
			pGraphics->SetColor(0.04f, 0.05f, 0.06f, 0.85f * Alpha);
			const float d = std::max(1.2f, Scale * 1.4f);
			for(int i = 0; i < 8; i++)
				Triangles(pGraphics, pPoints, paTris, NumTris, Pos + direction(i * pi / 4.0f) * d, Scale);
		}
		pGraphics->SetColor(Color.WithMultipliedAlpha(Alpha));
		Triangles(pGraphics, pPoints, paTris, NumTris, Pos, Scale);
	}

	static IGraphics::CTextureHandle s_Picture;
	static char s_aPictureFile[64] = "";

	bool Render(IGraphics *pGraphics, vec2 Pos, float Size, float Alpha)
	{
		const int Style = g_Config.m_XcCursor;
		if(Style == CURSOR_STANDARD)
			return false;
		Size *= g_Config.m_XcCursorSize / 100.0f;
		const ColorRGBA Color = CursorColor();
		const float Scale = Size / 24.0f; // the shapes are drawn for a 24 px cursor

		if(Style == CURSOR_PICTURE)
		{
			if(str_comp(s_aPictureFile, g_Config.m_XcCursorFile) != 0)
			{
				str_copy(s_aPictureFile, g_Config.m_XcCursorFile);
				pGraphics->UnloadTexture(&s_Picture);
				if(s_aPictureFile[0])
				{
					char aPath[IO_MAX_PATH_LENGTH];
					str_format(aPath, sizeof(aPath), "xclient/cursors/%s%s", s_aPictureFile, str_endswith(s_aPictureFile, ".png") ? "" : ".png");
					s_Picture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
				}
			}
			if(!s_Picture.IsValid() || s_Picture.IsNullTexture())
				return false;
			pGraphics->WrapClamp();
			pGraphics->TextureSet(s_Picture);
			pGraphics->QuadsBegin();
			pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
			const IGraphics::CQuadItem Item(Pos.x, Pos.y, Size, Size);
			pGraphics->QuadsDrawTL(&Item, 1);
			pGraphics->QuadsEnd();
			pGraphics->WrapNormal();
			return true;
		}

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		if(g_Config.m_XcCursorGlow)
		{
			const vec2 Center = Style == CURSOR_DOT ? Pos : Pos + vec2(5.0f, 8.0f) * Scale;
			XcHelpers::DrawSoftCircle(pGraphics, Center, 9.0f * Scale, 9.0f * Scale, Color.WithAlpha(0.28f * Alpha));
		}
		if(Style == CURSOR_ARROW)
		{
			// a classic pointer: the tip at Pos
			static const vec2 s_aPoints[] = {{0.0f, 0.0f}, {0.0f, 17.0f}, {4.5f, 12.8f}, {7.8f, 20.2f}, {10.3f, 19.1f}, {7.0f, 11.8f}, {12.4f, 11.8f}};
			static const int s_aaTris[][3] = {{0, 1, 2}, {0, 2, 5}, {0, 5, 6}, {2, 3, 4}, {2, 4, 5}};
			Shape(pGraphics, s_aPoints, s_aaTris, 5, Pos, Scale, Color, Alpha);
		}
		else if(Style == CURSOR_TRIANGLE)
		{
			// a small rounded-looking triangle leaning to the left
			static const vec2 s_aPoints[] = {{0.0f, 0.0f}, {3.0f, 14.0f}, {6.5f, 8.5f}, {13.5f, 6.0f}};
			static const int s_aaTris[][3] = {{0, 1, 2}, {0, 2, 3}};
			Shape(pGraphics, s_aPoints, s_aaTris, 2, Pos, Scale, Color, Alpha);
		}
		else
		{
			// a dot in a thin ring, centered on Pos
			if(g_Config.m_XcCursorOutline)
				XcHelpers::DrawSoftCircle(pGraphics, Pos, 4.6f * Scale, 1.0f, ColorRGBA(0.04f, 0.05f, 0.06f, 0.85f * Alpha));
			XcHelpers::DrawSoftCircle(pGraphics, Pos, 3.4f * Scale, 1.0f, Color.WithMultipliedAlpha(Alpha));
			XcHelpers::DrawGlowRing(pGraphics, Pos, 9.0f * Scale, 1.0f, 1.4f * Scale, 0.8f, Color.WithMultipliedAlpha(0.8f * Alpha));
		}
		pGraphics->QuadsEnd();
		return true;
	}
}
