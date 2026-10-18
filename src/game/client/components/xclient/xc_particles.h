#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_XC_PARTICLES_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_XC_PARTICLES_H

#include <base/color.h>
#include <base/vmath.h>

#include <vector>

class CGameClient;

// Small world-space particle system shared by all XClient effects.
// Separate from the game's own particles so our effects never steal their slots.
class CXcParticles
{
public:
	enum class EType
	{
		RING, // expanding glowing ellipse
		SPARK, // soft ball
		STAR, // sparkle sprite
		DUST, // smoke sprite
		CONFETTI, // flat rectangle that flips while falling
		SHARD, // triangle splinter
	};

	struct SParticle
	{
		EType m_Type = EType::SPARK;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_Vel = vec2(0.0f, 0.0f);
		float m_Gravity = 0.0f;
		float m_Friction = 0.15f; // fraction of speed left after one second
		float m_Life = 0.0f; // negative = delayed start
		float m_LifeSpan = 1.0f;
		float m_StartSize = 8.0f;
		float m_EndSize = 0.0f;
		float m_Rot = 0.0f;
		float m_RotSpeed = 0.0f;
		float m_Squash = 1.0f; // rings: vertical squash
		float m_Thickness = 4.0f; // rings: line thickness at the start
		ColorRGBA m_Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	};

	void Add(const SParticle &Particle);
	void Update(float Dt);
	void Render(const CGameClient *pGameClient) const;
	void Clear() { m_vParticles.clear(); }
	bool Empty() const { return m_vParticles.empty(); }

private:
	std::vector<SParticle> m_vParticles;
};

#endif
