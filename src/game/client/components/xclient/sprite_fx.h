#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_SPRITE_FX_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_SPRITE_FX_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/graphics.h>

#include <game/client/component.h>

#include <vector>

// Soft textured sprites for the "new" effect style (xc_fx_style 1): light is built from layers of
// blurred textures added on top of each other (a wide bloom, a glow and a bright core).
// Textures from data/xclient/fx (see CREDITS.txt there). Drawn with IGraphics,
// so it works on every backend.
class CXcSprites : public CComponent
{
public:
	enum ESprite
	{
		SPRITE_CORE = 0, // small bright dot
		SPRITE_GLOW, // medium soft light
		SPRITE_BLOOM, // wide faint light
		SPRITE_DASH_BLOOM,
		SPRITE_JUMP_CIRCLE, // filled glowing circle
		SPRITE_JUMP_RING, // ring with a glow
		SPRITE_SOFT_BLOOM,
		SPRITE_SOFT_GLOW,
		// icon particles
		SPRITE_CROWN,
		SPRITE_DOLLAR,
		SPRITE_FIREFLY,
		SPRITE_FIREPART,
		SPRITE_GHOST,
		SPRITE_HEART,
		SPRITE_PUMPKIN,
		SPRITE_RHOMBUS,
		SPRITE_SNOWFLAKE,
		SPRITE_STAR,
		// animations: strips of frames that play once over the life of a dash
		SPRITE_DASH_STRETCH, // a dash grows long and shrinks again
		SPRITE_DASH_POP, // a dot grows into a ring and fades
		SPRITE_DASH_SPIN,
		SPRITE_DASH_BLINK,
		SPRITE_DASH_SPARK,
		NUM_SPRITES,
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;

	// true when the textures are loaded and the new style is chosen
	bool Enabled() const;
	static int NumFrames(int Sprite);
	// width / height of one frame
	static float Aspect(int Sprite);

	// draws many sprites of one texture: Begin, Draw..., End
	void Begin(int Sprite, bool Additive);
	// Size is the width, the height follows the aspect of the frame; Frame 0..1 picks the frame of an animation
	void Draw(vec2 Pos, float Size, float Rotation, ColorRGBA Color, float Frame = 0.0f, float Height = 0.0f);
	void End();

	// one point of light: a wide bloom, a glow and a whiter core, Radius is the radius of the glow
	void Light(vec2 Pos, float Radius, ColorRGBA Color, float Intensity = 1.0f);

	// world sprites drawn later together with the other world effects (CFxRenderer::FlushWorld);
	// Height 0 keeps the aspect of the frame
	void Queue(int Sprite, bool Additive, vec2 Pos, float Size, float Rotation, ColorRGBA Color, float Frame = 0.0f, float Height = 0.0f);
	// the bloom, glow and core of Light() as queued sprites
	void QueueLight(vec2 Pos, float Radius, ColorRGBA Color, float Intensity = 1.0f);
	// soft light along a line: glow sprites close to each other, Width is the width of the light
	void QueueLightLine(vec2 From, vec2 To, float Width, ColorRGBA Color, float Intensity = 1.0f);
	void FlushQueue();

	// sprites collected by an effect that draws right away (in its own place among the layers)
	struct SSprite
	{
		int m_Sprite;
		bool m_Additive;
		vec2 m_Pos;
		float m_Size;
		float m_Height;
		float m_Rotation;
		float m_Frame;
		ColorRGBA m_Color;
	};
	static void Push(std::vector<SSprite> &vList, int Sprite, bool Additive, vec2 Pos, float Size, float Rotation, ColorRGBA Color, float Frame = 0.0f, float Height = 0.0f);
	static void PushLight(std::vector<SSprite> &vList, vec2 Pos, float Radius, ColorRGBA Color, float Intensity = 1.0f);
	static void PushLightLine(std::vector<SSprite> &vList, vec2 From, vec2 To, float Width, ColorRGBA Color, float Intensity = 1.0f);
	// draws the list (normal sprites first, then the light) and clears it
	void DrawList(std::vector<SSprite> &vList);

private:
	std::vector<SSprite> m_vQueue;
	IGraphics::CTextureHandle m_aTextures[NUM_SPRITES];
	bool m_Loaded = false;
	int m_Current = -1;
	bool m_Additive = false;
};

#endif
