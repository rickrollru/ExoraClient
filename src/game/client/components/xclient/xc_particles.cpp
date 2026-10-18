#include "xc_particles.h"

#include "xc_helpers.h"

#include <base/math.h>

#include <engine/graphics.h>

#include <game/client/gameclient.h>

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

void CXcParticles::Render(const CGameClient *pGameClient) const
{
	if(m_vParticles.empty())
		return;
	IGraphics *pGraphics = pGameClient->Graphics();

	// untextured shapes: rings, confetti, shards
	pGraphics->TextureClear();
	pGraphics->QuadsBegin();
	for(const SParticle &Particle : m_vParticles)
	{
		if(Particle.m_Life < 0.0f)
			continue;
		const float t = Progress(Particle);
		switch(Particle.m_Type)
		{
		case EType::RING:
		{
			// ease out: fast at the start, slows down at the end
			const float Ease = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
			const float Radius = mix(Particle.m_StartSize, Particle.m_EndSize, Ease);
			const float Thickness = mix(Particle.m_Thickness, Particle.m_Thickness / 3.0f, t);
			XcHelpers::DrawGlowRing(pGraphics, Particle.m_Pos, Radius, Particle.m_Squash, Thickness, Thickness * 3.0f, Particle.m_Color.WithMultipliedAlpha(1.0f - t));
			break;
		}
		case EType::CONFETTI:
		{
			// fade only at the very end, confetti should stay colorful
			const float Alpha = std::clamp((1.0f - t) * 4.0f, 0.0f, 1.0f);
			const float Size = mix(Particle.m_StartSize, Particle.m_EndSize, t);
			const float Flip = 0.25f + 0.75f * std::abs(std::sin(Particle.m_Rot * 1.7f));
			pGraphics->SetColor(Particle.m_Color.WithMultipliedAlpha(Alpha));
			pGraphics->QuadsSetRotation(Particle.m_Rot);
			IGraphics::CQuadItem Item(Particle.m_Pos.x, Particle.m_Pos.y, Size, Size * 0.55f * Flip);
			pGraphics->QuadsDraw(&Item, 1);
			pGraphics->QuadsSetRotation(0.0f);
			break;
		}
		case EType::SHARD:
		{
			const float Size = mix(Particle.m_StartSize, Particle.m_EndSize, t);
			const vec2 a = Particle.m_Pos + direction(Particle.m_Rot) * Size;
			const vec2 b = Particle.m_Pos + direction(Particle.m_Rot + 2.4f) * Size * 0.45f;
			const vec2 c = Particle.m_Pos + direction(Particle.m_Rot - 2.4f) * Size * 0.45f;
			const ColorRGBA Color = Particle.m_Color.WithMultipliedAlpha(1.0f - t * t);
			const ColorRGBA Light = ColorRGBA(std::min(Color.r + 0.3f, 1.0f), std::min(Color.g + 0.3f, 1.0f), std::min(Color.b + 0.3f, 1.0f), Color.a);
			pGraphics->SetColor4(Light, Light, Color, Color);
			const IGraphics::CFreeformItem Item(a, a, b, c);
			pGraphics->QuadsDrawFreeform(&Item, 1);
			break;
		}
		default:
			break;
		}
	}
	pGraphics->QuadsEnd();

	// sprite particles
	const struct
	{
		EType m_Type;
		IGraphics::CTextureHandle m_Texture;
	} aPasses[] = {
		{EType::DUST, pGameClient->m_ParticlesSkin.m_SpriteParticleSmoke},
		{EType::SPARK, pGameClient->m_ParticlesSkin.m_SpriteParticleBall},
		{EType::STAR, pGameClient->m_ExtrasSkin.m_SpriteParticleSparkle},
	};
	for(const auto &Pass : aPasses)
	{
		if(Pass.m_Texture.IsValid())
			pGraphics->TextureSet(Pass.m_Texture);
		else
			pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		for(const SParticle &Particle : m_vParticles)
		{
			if(Particle.m_Type != Pass.m_Type || Particle.m_Life < 0.0f)
				continue;
			const float t = Progress(Particle);
			const float Size = mix(Particle.m_StartSize, Particle.m_EndSize, t);
			pGraphics->SetColor(Particle.m_Color.WithMultipliedAlpha(1.0f - t));
			pGraphics->QuadsSetRotation(Particle.m_Rot);
			IGraphics::CQuadItem Item(Particle.m_Pos.x, Particle.m_Pos.y, Size, Size);
			pGraphics->QuadsDraw(&Item, 1);
		}
		pGraphics->QuadsEnd();
	}
	pGraphics->QuadsSetRotation(0.0f);
}
