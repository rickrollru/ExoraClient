#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_XC_PARTICLES_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_XC_PARTICLES_H

#include <base/color.h>
#include <base/vmath.h>

#include <vector>

class CFxRenderer;
class CXcSprites;

// Fine settings of the particles of each effect: amount, life time, speed and size ("xc_fxtune_<group>_*").
// An effect emits its particles inside a scope of its group; CXcParticles::Add applies the scale.
namespace FxTune
{
enum EGroup
{
	JUMP,
	DUST,
	TRAIL,
	HOOK,
	HAMMER,
	EXPLOSION,
	FREEZE,
	DEATH,
	FINISH,
	TELEPORT,
	CHECKPOINT,
	ITEMS,
	NUM_GROUPS,
};
// config names of the groups, "xc_fxtune_<name>_amount" etc.
extern const char *const g_apNames[NUM_GROUPS];
struct SScale
{
	float m_Amount = 1.0f;
	float m_Life = 1.0f;
	float m_Speed = 1.0f;
	float m_Size = 1.0f;
};
SScale For(int Group);
// the settings of the group array: amount, life, speed, size
int *Setting(int Group, int Which);
extern SScale g_Current;
class CScope
{
	SScale m_Old;

public:
	explicit CScope(int Group) :
		m_Old(g_Current) { g_Current = For(Group); }
	~CScope() { g_Current = m_Old; }
	CScope(const CScope &) = delete;
	CScope &operator=(const CScope &) = delete;
};
} // namespace FxTune

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
		FLASH, // bright soft disc that grows and fades quickly
		ICON, // a picture particle of the new style (m_Sprite), e.g. a heart or a snowflake
		JUMP_CIRCLE, // a flat circle on the ground that opens fast and fades softly (m_Sprite: its texture)
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
		int m_Sprite = -1; // ICON: CXcSprites::ESprite
	};

	void Add(const SParticle &Particle);
	void Update(float Dt);
	// Queues the particles as world shapes, drawn after all world effects.
	void Render(CFxRenderer &Renderer) const;
	// the new effect style: soft textured sprites, returns false when they are not available
	bool RenderSprites(CXcSprites &Sprites) const;
	void Clear() { m_vParticles.clear(); }
	bool Empty() const { return m_vParticles.empty(); }

private:
	std::vector<SParticle> m_vParticles;
};

#endif
