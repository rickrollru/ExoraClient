#include "xc_particles.h"

#include "fx_renderer.h"
#include "sprite_fx.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/collision.h>

#include <algorithm>
#include <cmath>

static constexpr size_t MAX_PARTICLES = 6000;
static const CCollision *s_pCollision = nullptr;

void CXcParticles::SetCollision(const CCollision *pCollision)
{
	s_pCollision = pCollision;
}

// the particles that are small pieces of matter: they can fall, bounce and change their look
static bool IsPiece(CXcParticles::EType Type)
{
	return Type == CXcParticles::EType::SPARK || Type == CXcParticles::EType::STAR || Type == CXcParticles::EType::SHARD ||
	       Type == CXcParticles::EType::CONFETTI || Type == CXcParticles::EType::ICON;
}

namespace FxTune
{
const char *const g_apNames[NUM_GROUPS] = {"jump", "dust", "trail", "hook", "hammer", "explosion", "freeze", "death", "finish", "teleport", "checkpoint", "items", "impact"};
SScale g_Current;

int *Setting(int Group, int Which)
{
#define XC_TUNE(Name) {&g_Config.m_XcFxtune##Name##Amount, &g_Config.m_XcFxtune##Name##Life, &g_Config.m_XcFxtune##Name##Speed, &g_Config.m_XcFxtune##Name##Size}
	static int *const s_aapSettings[NUM_GROUPS][4] = {XC_TUNE(Jump), XC_TUNE(Dust), XC_TUNE(Trail), XC_TUNE(Hook), XC_TUNE(Hammer), XC_TUNE(Explosion), XC_TUNE(Freeze), XC_TUNE(Death), XC_TUNE(Finish), XC_TUNE(Teleport), XC_TUNE(Checkpoint), XC_TUNE(Items), XC_TUNE(Impact)};
#undef XC_TUNE
	return s_aapSettings[std::clamp(Group, 0, NUM_GROUPS - 1)][std::clamp(Which, 0, 3)];
}

SScale For(int Group)
{
	SScale Scale;
	if(Group < 0 || Group >= NUM_GROUPS)
		return Scale;
	Scale.m_Amount = *Setting(Group, 0) / 100.0f;
	Scale.m_Life = *Setting(Group, 1) / 100.0f;
	Scale.m_Speed = *Setting(Group, 2) / 100.0f;
	Scale.m_Size = *Setting(Group, 3) / 100.0f;
	return Scale;
}
} // namespace FxTune

void CXcParticles::Add(const SParticle &Particle)
{
	const FxTune::SScale &Tune = FxTune::g_Current;
	SParticle Scaled = Particle;
	Scaled.m_LifeSpan *= Tune.m_Life;
	Scaled.m_Life *= Tune.m_Life; // delays stretch too
	Scaled.m_Vel *= Tune.m_Speed;
	Scaled.m_RotSpeed *= std::max(Tune.m_Speed, 0.25f);
	Scaled.m_StartSize *= Tune.m_Size;
	Scaled.m_EndSize *= Tune.m_Size;
	Scaled.m_Thickness *= Tune.m_Size;
	// one look for all small particles (xc_particles_shape)
	if(g_Config.m_XcParticlesShape > 0 && (Particle.m_Type == EType::SPARK || Particle.m_Type == EType::STAR || Particle.m_Type == EType::ICON))
	{
		static const int s_aSprites[] = {-1, CXcSprites::SPRITE_STAR, CXcSprites::SPRITE_HEART, CXcSprites::SPRITE_RHOMBUS, CXcSprites::SPRITE_SNOWFLAKE, CXcSprites::SPRITE_FIREFLY};
		const int Shape = std::clamp(g_Config.m_XcParticlesShape, 1, (int)std::size(s_aSprites));
		if(Shape == 1)
			Scaled.m_Type = EType::SPARK;
		else
		{
			Scaled.m_Type = EType::ICON;
			Scaled.m_Sprite = s_aSprites[Shape - 1];
			Scaled.m_StartSize *= 1.6f;
			Scaled.m_EndSize *= 1.6f;
			if(Scaled.m_RotSpeed == 0.0f)
				Scaled.m_RotSpeed = random_float(-3.0f, 3.0f);
		}
	}

	// rings, flashes and circles are one piece of an effect, more of them would only make it brighter
	const bool Countable = Particle.m_Type == EType::SPARK || Particle.m_Type == EType::STAR || Particle.m_Type == EType::DUST ||
			       Particle.m_Type == EType::CONFETTI || Particle.m_Type == EType::SHARD || Particle.m_Type == EType::ICON;
	int Copies = 1;
	if(Countable)
	{
		// 2.5 = two copies and a half chance of a third
		Copies = (int)Tune.m_Amount;
		if(random_float() < Tune.m_Amount - Copies)
			Copies++;
	}
	for(int i = 0; i < Copies; i++)
	{
		if(m_vParticles.size() >= MAX_PARTICLES)
			return;
		SParticle Copy = Scaled;
		if(i > 0)
		{
			// extra copies spread a little, so they do not sit on top of each other
			const float Speed = length(Copy.m_Vel);
			if(Speed > 0.001f)
				Copy.m_Vel = direction(angle(Copy.m_Vel) + random_float(-0.35f, 0.35f)) * Speed * random_float(0.8f, 1.15f);
			else
				Copy.m_Pos += vec2(random_float(-6.0f, 6.0f), random_float(-6.0f, 6.0f)) * Tune.m_Size;
			Copy.m_LifeSpan *= random_float(0.85f, 1.1f);
			Copy.m_Rot += random_float(0.0f, 2.0f * pi);
		}
		m_vParticles.push_back(Copy);
	}
}

void CXcParticles::Update(float Dt)
{
	const bool Physics = g_Config.m_XcParticlesPhysics && s_pCollision;
	const float ExtraGravity = Physics ? 1400.0f * g_Config.m_XcParticlesGravity / 100.0f : 0.0f;
	const float Bounce = g_Config.m_XcParticlesBounce / 100.0f;
	for(SParticle &Particle : m_vParticles)
	{
		Particle.m_Life += Dt;
		if(Particle.m_Life < 0.0f)
			continue;
		const bool Piece = Physics && IsPiece(Particle.m_Type);
		Particle.m_Vel.y += (Particle.m_Gravity + (Piece ? ExtraGravity : 0.0f)) * Dt;
		Particle.m_Vel *= std::pow(Particle.m_Friction, Dt);
		const vec2 NewPos = Particle.m_Pos + Particle.m_Vel * Dt;
		if(Piece && s_pCollision->CheckPoint(NewPos))
		{
			// bounce off the side that was hit, lose some speed and spin on the ground
			bool Hit = false;
			if(s_pCollision->CheckPoint(vec2(NewPos.x, Particle.m_Pos.y)))
			{
				Particle.m_Vel.x = -Particle.m_Vel.x * Bounce;
				Hit = true;
			}
			if(s_pCollision->CheckPoint(vec2(Particle.m_Pos.x, NewPos.y)))
			{
				Particle.m_Vel.y = -Particle.m_Vel.y * Bounce;
				Particle.m_Vel.x *= 0.8f;
				Particle.m_RotSpeed *= 0.7f;
				Hit = true;
			}
			if(!Hit)
				Particle.m_Vel = -Particle.m_Vel * Bounce;
			if(!s_pCollision->CheckPoint(Particle.m_Pos + Particle.m_Vel * Dt))
				Particle.m_Pos += Particle.m_Vel * Dt;
		}
		else
			Particle.m_Pos = NewPos;
		Particle.m_Rot += Particle.m_RotSpeed * Dt;
	}
	m_vParticles.erase(std::remove_if(m_vParticles.begin(), m_vParticles.end(), [](const SParticle &Particle) { return Particle.m_Life >= Particle.m_LifeSpan; }), m_vParticles.end());
}

static float Progress(const CXcParticles::SParticle &Particle)
{
	return std::clamp(Particle.m_Life / Particle.m_LifeSpan, 0.0f, 1.0f);
}

void CXcParticles::Render(CFxRenderer &Renderer) const
{
	const FxGlow::CScope GlowScope(FxGlow::PARTICLES);
	// the new style draws soft sprites, only the solid confetti and shards stay shapes
	const bool Sprites = RenderSprites(Renderer.Sprites());
	for(const SParticle &Particle : m_vParticles)
	{
		if(Particle.m_Life < 0.0f)
			continue;
		if(Sprites && Particle.m_Type != EType::CONFETTI && Particle.m_Type != EType::SHARD)
			continue;
		const float t = Progress(Particle);
		const float Size = mix(Particle.m_StartSize, Particle.m_EndSize, t);
		switch(Particle.m_Type)
		{
		case EType::RING:
		{
			// ease out: fast at the start, slows down at the end
			const float Ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
			const float Radius = mix(Particle.m_StartSize, Particle.m_EndSize, Ease);
			const float Thickness = mix(Particle.m_Thickness, Particle.m_Thickness / 3.0f, t);
			Renderer.AddWorldGlowing(FxShape::Ring(Particle.m_Pos, vec2(Radius, Radius * Particle.m_Squash), Thickness, Particle.m_Color.WithMultipliedAlpha(1.0f - t)), Thickness * 3.0f, 0.5f);
			break;
		}
		case EType::SPARK:
			// hot core with a soft glow around it
			Renderer.AddWorld(FxShape::Circle(Particle.m_Pos, Size * 0.3f, Particle.m_Color.WithMultipliedAlpha(1.0f - t))
						  .Glow(Size * 0.9f, 0.7f)
						  .Additive());
			break;
		case EType::STAR:
			Renderer.AddWorldGlowing(FxShape::Star(Particle.m_Pos, Size * 0.5f, 4, 0.38f, Particle.m_Color.WithMultipliedAlpha(1.0f - t)).Rotate(Particle.m_Rot), Size * 0.35f, 0.45f);
			break;
		case EType::DUST:
			Renderer.AddWorld(FxShape::Blob(Particle.m_Pos, Size * 0.5f, Particle.m_Color.WithMultipliedAlpha(1.0f - t)));
			break;
		case EType::CONFETTI:
		{
			// fade only at the very end, confetti should stay colorful
			const float Alpha = std::clamp((1.0f - t) * 4.0f, 0.0f, 1.0f);
			const float Flip = 0.25f + 0.75f * std::abs(std::sin(Particle.m_Rot * 1.7f));
			Renderer.AddWorld(FxShape::RoundRect(Particle.m_Pos, vec2(Size * 0.5f, Size * 0.275f * Flip), Size * 0.08f, Particle.m_Color.WithMultipliedAlpha(Alpha))
						  .Rotate(Particle.m_Rot));
			break;
		}
		case EType::FLASH:
		{
			const float Ease = 1.0f - (1.0f - t) * (1.0f - t);
			const float Radius = mix(Particle.m_StartSize, Particle.m_EndSize, Ease);
			Renderer.AddWorld(FxShape::Circle(Particle.m_Pos, Radius * 0.45f, Particle.m_Color.WithMultipliedAlpha((1.0f - t) * (1.0f - t)))
						  .Glow(Radius * 0.55f, 0.8f)
						  .Additive());
			break;
		}
		case EType::SHARD:
		{
			const ColorRGBA Color = Particle.m_Color.WithMultipliedAlpha(1.0f - t * t);
			const ColorRGBA Light(std::min(Color.r + 0.3f, 1.0f), std::min(Color.g + 0.3f, 1.0f), std::min(Color.b + 0.3f, 1.0f), Color.a);
			Renderer.AddWorld(FxShape::Triangle(Particle.m_Pos, vec2(Size * 0.35f, Size * 0.6f), Light)
						  .Gradient(Color)
						  .Rotate(Particle.m_Rot));
			break;
		}
		case EType::JUMP_CIRCLE:
		{
			const float Ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
			const float Radius = mix(Particle.m_StartSize, Particle.m_EndSize, Ease) * 0.42f;
			Renderer.AddWorldGlowing(FxShape::Ring(Particle.m_Pos, vec2(Radius, Radius * Particle.m_Squash), 2.5f, Particle.m_Color.WithMultipliedAlpha(1.0f - t)), 7.5f, 0.5f);
			break;
		}
		case EType::ICON:
		{
			// without the textures an icon is a soft star
			Renderer.AddWorldGlowing(FxShape::Star(Particle.m_Pos, Size * 0.5f, 5, 0.45f, Particle.m_Color.WithMultipliedAlpha(1.0f - t)).Rotate(Particle.m_Rot), Size * 0.35f, 0.45f);
			break;
		}
		}
	}
}

bool CXcParticles::RenderSprites(CXcSprites &Sprites) const
{
	if(!Sprites.Enabled())
		return false;
	// glow settings of the particles group: brightness of the light layers and their reach
	const float Glow = FxGlow::g_Current.m_Strength;
	const float Reach = FxGlow::g_Current.m_Radius;
	for(const SParticle &Particle : m_vParticles)
	{
		if(Particle.m_Life < 0.0f)
			continue;
		const float t = Progress(Particle);
		const float Size = mix(Particle.m_StartSize, Particle.m_EndSize, t);
		// fast at the start, soft at the end
		const float EaseOut = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
		const float Fade = 1.0f - t * t;
		const ColorRGBA Color = Particle.m_Color;
		switch(Particle.m_Type)
		{
		case EType::RING:
		{
			// a glowing ring texture, flat rings are squashed
			const float Radius = mix(Particle.m_StartSize, Particle.m_EndSize, EaseOut);
			const float Width = Radius * 4.0f; // the ring of the texture is half of its size, the rest is its glow
			// the ring in its color, then the same ring added on top as light
			Sprites.Queue(CXcSprites::SPRITE_JUMP_RING, false, Particle.m_Pos, Width, 0.0f, Color.WithMultipliedAlpha(1.0f - t), 0.0f, Width * Particle.m_Squash);
			Sprites.Queue(CXcSprites::SPRITE_JUMP_RING, true, Particle.m_Pos, Width, 0.0f, Color.WithMultipliedAlpha((1.0f - t) * 0.7f * std::min(Glow, 1.5f)), 0.0f, Width * Particle.m_Squash);
			break;
		}
		case EType::SPARK:
			Sprites.QueueLight(Particle.m_Pos, Size * 0.7f * Reach, Color.WithMultipliedAlpha(Fade), std::min(Glow, 2.0f));
			break;
		case EType::STAR:
			Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Particle.m_Pos, Size * 2.2f * Reach, 0.0f, Color.WithMultipliedAlpha(0.45f * Fade * Glow));
			Sprites.Queue(CXcSprites::SPRITE_STAR, true, Particle.m_Pos, Size * 1.25f, Particle.m_Rot, Color.WithMultipliedAlpha(Fade));
			break;
		case EType::DUST:
			Sprites.Queue(CXcSprites::SPRITE_GLOW, false, Particle.m_Pos, Size * 1.4f, 0.0f, Color.WithMultipliedAlpha(0.55f * (1.0f - t)));
			break;
		case EType::FLASH:
		{
			const float Radius = mix(Particle.m_StartSize, Particle.m_EndSize, EaseOut);
			Sprites.Queue(CXcSprites::SPRITE_SOFT_BLOOM, true, Particle.m_Pos, Radius * 2.6f * Reach, 0.0f, Color.WithMultipliedAlpha((1.0f - t) * (1.0f - t) * Glow));
			Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Particle.m_Pos, Radius * 1.2f, 0.0f, Color.WithMultipliedAlpha((1.0f - t) * (1.0f - t)));
			break;
		}
		case EType::ICON:
			if(Particle.m_Sprite >= 0)
			{
				Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Particle.m_Pos, Size * 2.0f * Reach, 0.0f, Color.WithMultipliedAlpha(0.35f * Fade * Glow));
				Sprites.Queue(Particle.m_Sprite, true, Particle.m_Pos, Size, Particle.m_Rot, Color.WithMultipliedAlpha(Fade));
			}
			break;
		case EType::JUMP_CIRCLE:
		{
			// opens from a fifth of its size with an ease out,
			// stays bright and fades in the second part of its life
			const float Size = mix(Particle.m_StartSize, Particle.m_EndSize, EaseOut);
			const float x = std::clamp((t - 0.2f) / 0.8f, 0.0f, 1.0f);
			const float Alpha = 1.0f - x * x * (3.0f - 2.0f * x);
			const int Sprite = Particle.m_Sprite >= 0 ? Particle.m_Sprite : CXcSprites::SPRITE_JUMP_RING;
			if(Sprite == CXcSprites::SPRITE_JUMP_RING)
			{
				// a soft wide band under the thin bright line, so the circle reads well from the side too
				const float Band = Size * 0.62f * Reach;
				Sprites.Queue(CXcSprites::SPRITE_JUMP_CIRCLE, true, Particle.m_Pos, Band, 0.0f, Color.WithMultipliedAlpha(Alpha * 0.35f * std::min(Glow, 1.5f)), 0.0f, Band * Particle.m_Squash);
			}
			Sprites.Queue(Sprite, false, Particle.m_Pos, Size, 0.0f, Color.WithMultipliedAlpha(Alpha), 0.0f, Size * Particle.m_Squash);
			Sprites.Queue(Sprite, true, Particle.m_Pos, Size, 0.0f, Color.WithMultipliedAlpha(Alpha * std::min(Glow, 1.5f)), 0.0f, Size * Particle.m_Squash);
			break;
		}
		case EType::CONFETTI:
			// solid pieces keep their shapes (see Render()), a faint light under them makes them sparkle
			Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Particle.m_Pos, Size * 2.6f * Reach, 0.0f, Color.WithMultipliedAlpha(0.22f * std::clamp((1.0f - t) * 4.0f, 0.0f, 1.0f) * Glow));
			break;
		case EType::SHARD:
			Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Particle.m_Pos, Size * 2.2f * Reach, 0.0f, Color.WithMultipliedAlpha(0.3f * (1.0f - t * t) * Glow));
			break;
		}
	}
	return true;
}
