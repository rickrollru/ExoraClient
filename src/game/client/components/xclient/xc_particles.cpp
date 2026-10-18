#include "xc_particles.h"

#include "fx_renderer.h"

#include <base/math.h>

#include <algorithm>
#include <cmath>

static constexpr size_t MAX_PARTICLES = 2048;

void CXcParticles::Add(const SParticle &Particle)
{
	if(m_vParticles.size() >= MAX_PARTICLES)
		return;
	m_vParticles.push_back(Particle);
}

void CXcParticles::Update(float Dt)
{
	for(SParticle &Particle : m_vParticles)
	{
		Particle.m_Life += Dt;
		if(Particle.m_Life < 0.0f)
			continue;
		Particle.m_Vel.y += Particle.m_Gravity * Dt;
		Particle.m_Vel *= std::pow(Particle.m_Friction, Dt);
		Particle.m_Pos += Particle.m_Vel * Dt;
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
	for(const SParticle &Particle : m_vParticles)
	{
		if(Particle.m_Life < 0.0f)
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
		case EType::SHARD:
		{
			const ColorRGBA Color = Particle.m_Color.WithMultipliedAlpha(1.0f - t * t);
			const ColorRGBA Light(std::min(Color.r + 0.3f, 1.0f), std::min(Color.g + 0.3f, 1.0f), std::min(Color.b + 0.3f, 1.0f), Color.a);
			Renderer.AddWorld(FxShape::Triangle(Particle.m_Pos, vec2(Size * 0.35f, Size * 0.6f), Light)
						  .Gradient(Color)
						  .Rotate(Particle.m_Rot));
			break;
		}
		}
	}
}
