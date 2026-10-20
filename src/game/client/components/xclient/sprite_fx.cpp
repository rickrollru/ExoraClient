#include "sprite_fx.h"

#include <base/math.h>

#include <engine/shared/config.h>
#include <engine/storage.h>

#include <algorithm>

namespace
{
struct SSpriteFile
{
	const char *m_pFile;
	int m_NumFrames;
	float m_Aspect; // of one frame
};

const SSpriteFile s_aFiles[CXcSprites::NUM_SPRITES] = {
	{"xclient/fx/trail_core.png", 1, 1.0f},
	{"xclient/fx/trail_glow.png", 1, 1.0f},
	{"xclient/fx/trail_bloom.png", 1, 1.0f},
	{"xclient/fx/dash_bloom.png", 1, 1.0f},
	{"xclient/fx/jump_circle.png", 1, 1.0f},
	{"xclient/fx/jump_ring.png", 1, 1.0f},
	{"xclient/fx/soft_bloom.png", 1, 1.0f},
	{"xclient/fx/soft_glow.png", 1, 1.0f},
	{"xclient/fx/particles/crown.png", 1, 1.0f},
	{"xclient/fx/particles/dollar.png", 1, 1.0f},
	{"xclient/fx/particles/firefly.png", 1, 1.0f},
	{"xclient/fx/particles/firepart.png", 1, 1.0f},
	{"xclient/fx/particles/ghost.png", 1, 1.0f},
	{"xclient/fx/particles/heart.png", 1, 1.0f},
	{"xclient/fx/particles/pumpkin.png", 1, 1.0f},
	{"xclient/fx/particles/rhombus.png", 1, 1.0f},
	{"xclient/fx/particles/snowflake.png", 1, 1.0f},
	{"xclient/fx/particles/star.png", 1, 1.0f},
	{"xclient/fx/dash_group1.png", 11, 3.0f},
	{"xclient/fx/dash_group2.png", 23, 1.0f},
	{"xclient/fx/dash_group3.png", 32, 2.0f},
	{"xclient/fx/dash_group4.png", 16, 1.0f},
	{"xclient/fx/dash_group5.png", 32, 1.0f},
};
} // namespace

void CXcSprites::OnInit()
{
	m_Loaded = true;
	for(int i = 0; i < NUM_SPRITES; i++)
	{
		m_aTextures[i] = Graphics()->LoadTexture(s_aFiles[i].m_pFile, IStorage::TYPE_ALL);
		if(!m_aTextures[i].IsValid() || m_aTextures[i].IsNullTexture())
			m_Loaded = false;
	}
}

void CXcSprites::OnShutdown()
{
	for(IGraphics::CTextureHandle &Texture : m_aTextures)
		Graphics()->UnloadTexture(&Texture);
	m_Loaded = false;
}

bool CXcSprites::Enabled() const
{
	return m_Loaded && g_Config.m_XcFxStyle == 1;
}

int CXcSprites::NumFrames(int Sprite)
{
	return s_aFiles[Sprite].m_NumFrames;
}

float CXcSprites::Aspect(int Sprite)
{
	return s_aFiles[Sprite].m_Aspect;
}

void CXcSprites::Begin(int Sprite, bool Additive)
{
	m_Current = Sprite;
	m_Additive = Additive;
	if(Additive)
		Graphics()->BlendAdditive();
	Graphics()->TextureSet(m_aTextures[Sprite]);
	Graphics()->QuadsBegin();
}

void CXcSprites::Draw(vec2 Pos, float Size, float Rotation, ColorRGBA Color, float Frame, float Height)
{
	if(m_Current < 0 || Color.a <= 0.0f || Size <= 0.0f)
		return;
	const int NumFrames = s_aFiles[m_Current].m_NumFrames;
	if(NumFrames > 1)
	{
		const int Index = std::clamp((int)(Frame * NumFrames), 0, NumFrames - 1);
		Graphics()->QuadsSetSubset(Index / (float)NumFrames, 0.0f, (Index + 1) / (float)NumFrames, 1.0f);
	}
	else
		Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	if(Height <= 0.0f)
		Height = Size / s_aFiles[m_Current].m_Aspect;
	Graphics()->QuadsSetRotation(Rotation);
	Graphics()->SetColor(Color);
	const IGraphics::CQuadItem Item(Pos.x - Size * 0.5f, Pos.y - Height * 0.5f, Size, Height);
	Graphics()->QuadsDrawTL(&Item, 1);
}

void CXcSprites::End()
{
	Graphics()->QuadsEnd();
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsSetSubset(0.0f, 0.0f, 1.0f, 1.0f);
	if(m_Additive)
		Graphics()->BlendNormal();
	m_Current = -1;
}

void CXcSprites::Light(vec2 Pos, float Radius, ColorRGBA Color, float Intensity)
{
	const ColorRGBA Core(mix(Color.r, 1.0f, 0.6f), mix(Color.g, 1.0f, 0.6f), mix(Color.b, 1.0f, 0.6f), Color.a);
	Begin(SPRITE_BLOOM, true);
	Draw(Pos, Radius * 4.0f, 0.0f, Color.WithMultipliedAlpha(0.35f * Intensity));
	End();
	Begin(SPRITE_GLOW, false);
	Draw(Pos, Radius * 1.6f, 0.0f, Color.WithMultipliedAlpha(0.45f));
	End();
	Begin(SPRITE_GLOW, true);
	Draw(Pos, Radius * 2.0f, 0.0f, Color.WithMultipliedAlpha(0.6f * Intensity));
	End();
	Begin(SPRITE_CORE, true);
	Draw(Pos, Radius * 0.7f, 0.0f, Core.WithMultipliedAlpha(Intensity));
	End();
}

void CXcSprites::Push(std::vector<SSprite> &vList, int Sprite, bool Additive, vec2 Pos, float Size, float Rotation, ColorRGBA Color, float Frame, float Height)
{
	if(Color.a <= 0.0f || Size <= 0.0f || vList.size() >= 8192)
		return;
	vList.push_back({Sprite, Additive, Pos, Size, Height, Rotation, Frame, Color});
}

void CXcSprites::PushLight(std::vector<SSprite> &vList, vec2 Pos, float Radius, ColorRGBA Color, float Intensity)
{
	const ColorRGBA Core(mix(Color.r, 1.0f, 0.6f), mix(Color.g, 1.0f, 0.6f), mix(Color.b, 1.0f, 0.6f), Color.a);
	Push(vList, SPRITE_BLOOM, true, Pos, Radius * 4.0f, 0.0f, Color.WithMultipliedAlpha(0.35f * Intensity));
	// a colored layer with normal blending keeps the color on bright maps
	Push(vList, SPRITE_GLOW, false, Pos, Radius * 1.6f, 0.0f, Color.WithMultipliedAlpha(0.45f));
	Push(vList, SPRITE_GLOW, true, Pos, Radius * 2.0f, 0.0f, Color.WithMultipliedAlpha(0.6f * Intensity));
	Push(vList, SPRITE_CORE, true, Pos, Radius * 0.7f, 0.0f, Core.WithMultipliedAlpha(Intensity));
}

void CXcSprites::PushLightLine(std::vector<SSprite> &vList, vec2 From, vec2 To, float Width, ColorRGBA Color, float Intensity)
{
	const float Length = distance(From, To);
	if(Width <= 0.0f || Color.a <= 0.0f)
		return;
	// the glow texture fades to nothing at its edge, so sprites overlapping by two thirds make an even band
	const float Step = std::max(Width * 0.35f, 2.0f);
	const int Num = std::clamp((int)(Length / Step) + 1, 1, 400);
	// the overlap adds up, keep the band as bright as one sprite
	const float Alpha = std::min(1.0f, 1.6f * Step / Width);
	for(int i = 0; i <= Num; i++)
	{
		const vec2 Pos = mix(From, To, (float)i / Num);
		Push(vList, SPRITE_GLOW, false, Pos, Width, 0.0f, Color.WithMultipliedAlpha(0.4f * Alpha));
		Push(vList, SPRITE_GLOW, true, Pos, Width * 1.3f, 0.0f, Color.WithMultipliedAlpha(0.42f * Alpha * Intensity));
	}
}

void CXcSprites::DrawList(std::vector<SSprite> &vList)
{
	if(vList.empty())
		return;
	// normal sprites first, then the light added on top; one batch per texture
	std::stable_sort(vList.begin(), vList.end(), [](const SSprite &a, const SSprite &b) {
		if(a.m_Additive != b.m_Additive)
			return !a.m_Additive;
		return a.m_Sprite < b.m_Sprite;
	});
	int Sprite = -1;
	bool Additive = false;
	for(const SSprite &Item : vList)
	{
		if(Item.m_Sprite != Sprite || Item.m_Additive != Additive)
		{
			if(Sprite >= 0)
				End();
			Sprite = Item.m_Sprite;
			Additive = Item.m_Additive;
			Begin(Sprite, Additive);
		}
		Draw(Item.m_Pos, Item.m_Size, Item.m_Rotation, Item.m_Color, Item.m_Frame, Item.m_Height);
	}
	if(Sprite >= 0)
		End();
	vList.clear();
}

void CXcSprites::Queue(int Sprite, bool Additive, vec2 Pos, float Size, float Rotation, ColorRGBA Color, float Frame, float Height)
{
	Push(m_vQueue, Sprite, Additive, Pos, Size, Rotation, Color, Frame, Height);
}

void CXcSprites::QueueLight(vec2 Pos, float Radius, ColorRGBA Color, float Intensity)
{
	PushLight(m_vQueue, Pos, Radius, Color, Intensity);
}

void CXcSprites::QueueLightLine(vec2 From, vec2 To, float Width, ColorRGBA Color, float Intensity)
{
	PushLightLine(m_vQueue, From, To, Width, Color, Intensity);
}

void CXcSprites::FlushQueue()
{
	DrawList(m_vQueue);
}
