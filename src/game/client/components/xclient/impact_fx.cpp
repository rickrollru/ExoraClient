#include "impact_fx.h"

#include "fx_renderer.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/shared/config.h>

#include <generated/protocol.h>

#include <game/client/gameclient.h>
#include <game/collision.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

void CXcImpactFx::OnReset()
{
	m_Shots.clear();
	m_Lasers.clear();
	m_Particles.Clear();
	m_vHoles.clear();
	m_vRecent.clear();
}

bool CXcImpactFx::ShowFor(int Owner) const
{
	return g_Config.m_XcImpactFx != IMPACT_OFF && ShowTargets(Owner);
}

// the self / dummy / others choice of the card, also for the flash at the gun (it does not need the hits)
bool CXcImpactFx::ShowTargets(int Owner) const
{
	// shots of the map (turrets) count as others
	if(!in_range(Owner, MAX_CLIENTS - 1))
		return g_Config.m_XcImpactOthers;
	return XcHelpers::ShowFor(GameClient(), Owner, g_Config.m_XcImpactSelf, g_Config.m_XcImpactDummy, g_Config.m_XcImpactOthers);
}

ColorRGBA CXcImpactFx::Color(int Weapon, int Owner) const
{
	if(!g_Config.m_XcImpactWeaponColor)
		return EffectColor(GameClient(), g_Config.m_XcImpactColorMode, g_Config.m_XcImpactColor, Owner, random_float());
	switch(Weapon)
	{
	case WEAPON_GUN: return ColorRGBA(1.0f, 0.85f, 0.35f, 1.0f);
	case WEAPON_SHOTGUN: return ColorRGBA(1.0f, 0.6f, 0.25f, 1.0f);
	case WEAPON_LASER: return ColorRGBA(0.45f, 0.75f, 1.0f, 1.0f);
	default: return ColorRGBA(1.0f, 0.75f, 0.4f, 1.0f);
	}
}

int CXcImpactFx::TeeOnLine(vec2 From, vec2 To, int Owner, vec2 *pHit) const
{
	// the server does not tell about hits on tees in DDRace, so a shot that ends at a tee hit it
	int Best = -1;
	float BestDist = 1e9f;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == Owner || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
			continue;
		const vec2 Tee = GameClient()->m_aClients[i].m_RenderPos;
		vec2 Closest;
		if(!closest_point_on_line(From, To, Tee, Closest) || distance(Closest, Tee) > 30.0f)
			continue;
		const float Dist = distance(From, Closest);
		if(Dist < BestDist)
		{
			BestDist = Dist;
			Best = i;
			*pHit = Tee + normalize(Closest - Tee + vec2(0.001f, 0.0f)) * 16.0f;
		}
	}
	return Best;
}

vec2 CXcImpactFx::WallNormal(vec2 Pos, vec2 Before) const
{
	// the side of the hit block the shot came from
	const vec2 Center((std::floor(Pos.x / 32.0f) + 0.5f) * 32.0f, (std::floor(Pos.y / 32.0f) + 0.5f) * 32.0f);
	const vec2 Away = Before - Center;
	if(std::abs(Away.x) > std::abs(Away.y))
		return vec2(Away.x > 0.0f ? 1.0f : -1.0f, 0.0f);
	return vec2(0.0f, Away.y > 0.0f ? 1.0f : -1.0f);
}

void CXcImpactFx::Muzzle(vec2 Pos, vec2 Dir, int Weapon, int Owner)
{
	const FxTune::CScope TuneScope(FxTune::IMPACT);
	const ColorRGBA Main = Color(Weapon, Owner);
	CXcParticles::SParticle Flash;
	Flash.m_Type = CXcParticles::EType::FLASH;
	Flash.m_Pos = Pos;
	Flash.m_LifeSpan = 0.1f;
	Flash.m_StartSize = 6.0f;
	Flash.m_EndSize = 22.0f;
	Flash.m_Color = ColorRGBA(mix(Main.r, 1.0f, 0.5f), mix(Main.g, 1.0f, 0.5f), mix(Main.b, 1.0f, 0.5f), 0.8f);
	m_Particles.Add(Flash);
	for(int i = 0; i < 3; i++)
	{
		CXcParticles::SParticle Spark;
		Spark.m_Type = CXcParticles::EType::SPARK;
		Spark.m_Pos = Pos;
		Spark.m_Vel = direction(angle(Dir) + random_float(-0.35f, 0.35f)) * random_float(160.0f, 300.0f);
		Spark.m_Friction = 0.02f;
		Spark.m_LifeSpan = random_float(0.08f, 0.16f);
		Spark.m_StartSize = random_float(3.0f, 5.0f);
		Spark.m_Color = Main;
		m_Particles.Add(Spark);
	}
}

void CXcImpactFx::Impact(vec2 Pos, vec2 Dir, vec2 Normal, int Weapon, int Owner)
{
	const FxTune::CScope TuneScope(FxTune::IMPACT);
	const ColorRGBA Main = Color(Weapon, Owner);
	const ColorRGBA Hot(mix(Main.r, 1.0f, 0.6f), mix(Main.g, 1.0f, 0.6f), mix(Main.b, 1.0f, 0.6f), 1.0f);
	const float Size = g_Config.m_XcImpactSize / 100.0f;
	m_vRecent.push_back({Pos, m_Now});
	if(g_Config.m_XcImpactHoles)
		AddHole(Pos, Normal, Main, Weapon == WEAPON_LASER ? 0.8f : 1.0f);
	if(g_Config.m_XcImpactFx == IMPACT_OFF)
		return;
	const vec2 At = Pos + Normal * 2.0f;
	// the shot bounces off the wall: sparks fly around the reflected direction
	const vec2 Reflected = Dir - Normal * (2.0f * dot(Dir, Normal));
	const float BaseAngle = angle(normalize(Reflected * 0.6f + Normal * 0.4f));

	if(g_Config.m_XcImpactFlash > 0)
	{
		CXcParticles::SParticle Flash;
		Flash.m_Type = CXcParticles::EType::FLASH;
		Flash.m_Pos = At;
		Flash.m_LifeSpan = 0.14f;
		Flash.m_StartSize = 6.0f * Size;
		Flash.m_EndSize = 30.0f * Size;
		Flash.m_Color = Hot.WithAlpha(std::min(g_Config.m_XcImpactFlash / 100.0f, 1.0f));
		m_Particles.Add(Flash);
		if(g_Config.m_XcImpactFlash > 100)
		{
			// brighter than usual: a second, wider flash
			Flash.m_EndSize = 46.0f * Size;
			Flash.m_Color = Hot.WithAlpha((g_Config.m_XcImpactFlash - 100) / 100.0f * 0.7f);
			m_Particles.Add(Flash);
		}
	}

	const bool Stars = g_Config.m_XcImpactFx == IMPACT_STARS;
	const int Num = Stars ? 5 : 8;
	for(int i = 0; i < Num; i++)
	{
		CXcParticles::SParticle Spark;
		Spark.m_Type = Stars ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
		Spark.m_Pos = At;
		Spark.m_Vel = direction(BaseAngle + random_float(-1.0f, 1.0f)) * random_float(150.0f, 420.0f) * std::sqrt(Size);
		Spark.m_Gravity = 900.0f;
		Spark.m_Friction = 0.1f;
		Spark.m_LifeSpan = random_float(0.2f, 0.4f);
		Spark.m_StartSize = (Stars ? random_float(9.0f, 13.0f) : random_float(3.5f, 6.5f)) * Size;
		Spark.m_Rot = random_float(0.0f, 2.0f * pi);
		Spark.m_RotSpeed = Stars ? random_float(-8.0f, 8.0f) : 0.0f;
		Spark.m_Color = i % 3 == 0 ? Hot : Main;
		m_Particles.Add(Spark);
	}
	if(g_Config.m_XcImpactFx == IMPACT_DUST)
	{
		// crumbs of the block and a puff of dust
		for(int i = 0; i < 4; i++)
		{
			CXcParticles::SParticle Crumb;
			Crumb.m_Type = CXcParticles::EType::SHARD;
			Crumb.m_Pos = At;
			Crumb.m_Vel = direction(angle(Normal) + random_float(-0.8f, 0.8f)) * random_float(90.0f, 220.0f) * std::sqrt(Size);
			Crumb.m_Gravity = 1100.0f;
			Crumb.m_Friction = 0.3f;
			Crumb.m_LifeSpan = random_float(0.35f, 0.6f);
			Crumb.m_StartSize = random_float(4.0f, 7.0f) * Size;
			Crumb.m_EndSize = Crumb.m_StartSize;
			Crumb.m_Rot = random_float(0.0f, 2.0f * pi);
			Crumb.m_RotSpeed = random_float(-10.0f, 10.0f);
			const float Grey = random_float(0.35f, 0.55f);
			Crumb.m_Color = ColorRGBA(Grey, Grey * 0.95f, Grey * 0.9f, 1.0f);
			m_Particles.Add(Crumb);
		}
		CXcParticles::SParticle Dust;
		Dust.m_Type = CXcParticles::EType::DUST;
		Dust.m_Pos = At + Normal * 4.0f;
		Dust.m_Vel = Normal * 40.0f;
		Dust.m_Gravity = -20.0f;
		Dust.m_LifeSpan = 0.45f;
		Dust.m_StartSize = 10.0f * Size;
		Dust.m_EndSize = 26.0f * Size;
		Dust.m_Color = ColorRGBA(0.7f, 0.68f, 0.64f, 0.4f);
		m_Particles.Add(Dust);
	}
}

void CXcImpactFx::PlayerHit(vec2 Pos, vec2 Dir, int HitTee)
{
	if(g_Config.m_XcHitPlayerFx == 0)
		return;
	if(HitTee < 0)
	{
		// the damage event also comes where a gun bullet hits a block (the stock stars there): only a tee close
		// by makes it a hit on a player, the block gets the hit on blocks
		float Best = 48.0f;
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameClient()->m_Snap.m_aCharacters[i].m_Active)
				continue;
			const float Dist = distance(GameClient()->m_aClients[i].m_RenderPos, Pos);
			if(Dist < Best)
			{
				Best = Dist;
				HitTee = i;
			}
		}
		if(HitTee < 0)
		{
			// a gun bullet that hit a block: the server tells the very place (a shot right into a wall next to
			// the tee is never drawn, the client learns of it only so)
			if((g_Config.m_XcImpactFx == IMPACT_OFF && !g_Config.m_XcImpactHoles) || !ShowTargets(-1))
				return;
			for(const SRecent &Recent : m_vRecent)
				if(m_Now - Recent.m_Time < 0.2f && distance(Recent.m_Pos, Pos) < 48.0f)
					return;
			// the event angle is the server's -atan2(x, y) of the shot: a quarter turn from the flight
			const vec2 Shot = length(Dir) > 0.0f ? normalize(vec2(-Dir.y, Dir.x)) : vec2(0.0f, 1.0f);
			vec2 Col, Before;
			for(const vec2 Way : {Shot, -Shot})
				if(Collision()->IntersectLine(Pos - Way * 40.0f, Pos + Way * 8.0f, &Col, &Before))
				{
					Impact(Col, Way, WallNormal(Col, Before), WEAPON_GUN, -1);
					break;
				}
			return;
		}
	}
	const FxTune::CScope TuneScope(FxTune::IMPACT);
	const float Size = g_Config.m_XcHitPlayerSize / 100.0f;
	const ColorRGBA Main = EffectColor(GameClient(), g_Config.m_XcHitPlayerColorMode, g_Config.m_XcHitPlayerColor, HitTee);
	if(g_Config.m_XcHitPlayerFx == 1)
	{
		// a ring that pops on the tee
		CXcParticles::SParticle Ring;
		Ring.m_Type = CXcParticles::EType::RING;
		Ring.m_Pos = Pos;
		Ring.m_LifeSpan = 0.3f;
		Ring.m_StartSize = 10.0f * Size;
		Ring.m_EndSize = 46.0f * Size;
		Ring.m_Thickness = 4.0f * Size;
		Ring.m_Color = Main;
		m_Particles.Add(Ring);
		return;
	}
	// sparks or stars flying on in the direction of the hit
	const bool Stars = g_Config.m_XcHitPlayerFx == 3;
	const float BaseAngle = angle(length(Dir) > 0.0f ? Dir : vec2(0.0f, -1.0f));
	for(int i = 0; i < (Stars ? 4 : 7); i++)
	{
		CXcParticles::SParticle Particle;
		Particle.m_Type = Stars ? CXcParticles::EType::STAR : CXcParticles::EType::SPARK;
		Particle.m_Pos = Pos;
		Particle.m_Vel = direction(BaseAngle + random_float(-0.9f, 0.9f)) * random_float(140.0f, 380.0f) * std::sqrt(Size);
		Particle.m_Gravity = Stars ? 300.0f : 800.0f;
		Particle.m_LifeSpan = random_float(0.25f, 0.45f);
		Particle.m_StartSize = (Stars ? random_float(11.0f, 16.0f) : random_float(5.0f, 8.0f)) * Size;
		Particle.m_Rot = random_float(0.0f, 2.0f * pi);
		Particle.m_RotSpeed = Stars ? random_float(-6.0f, 6.0f) : 0.0f;
		Particle.m_Color = i % 3 == 0 ? ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f) : Main;
		m_Particles.Add(Particle);
	}
}

void CXcImpactFx::Projectile(int ItemId, vec2 Pos, vec2 Vel, int Weapon, int Owner, float Age, float Speed, vec2 StartPos, vec2 StartVel, float Curvature)
{
	auto It = m_Shots.find(ItemId);
	if(It == m_Shots.end())
	{
		// a new bullet: a flash at the gun if it was just shot (an old one may only have come into view)
		if(g_Config.m_XcImpactMuzzle && Age < 0.08f && Weapon != WEAPON_GRENADE && ShowTargets(Owner) && length(Vel) > 0.001f)
			Muzzle(Pos, normalize(Vel), Weapon, Owner);
		m_Shots[ItemId] = {Pos, Vel, Weapon, Owner, m_Frame, Speed, Age, StartPos, StartVel, Curvature};
		return;
	}
	It->second = {Pos, Vel, Weapon, Owner, m_Frame, Speed, Age, StartPos, StartVel, Curvature};
}

void CXcImpactFx::Laser(vec2 From, vec2 To, int StartTick, int Owner, int Type)
{
	if((g_Config.m_XcImpactFx == IMPACT_OFF && !g_Config.m_XcImpactHoles && !g_Config.m_XcImpactMuzzle && !g_Config.m_XcHitPlayerFx) || !ShowTargets(Owner) || distance(From, To) < 1.0f)
		return;
	const long long Key = ((long long)(Owner + 1) << 40) ^ ((long long)Type << 32) ^ (unsigned)StartTick;
	auto It = m_Lasers.find(Key);
	const bool New = It == m_Lasers.end();
	if(New && g_Config.m_XcImpactMuzzle && Client()->GameTick(g_Config.m_ClDummy) - StartTick < 3)
		Muzzle(From, normalize(To - From), WEAPON_LASER, Owner);
	// every new end point is a hit, when there is a block right there
	if(New || distance(It->second.m_To, To) > 1.0f)
	{
		const vec2 Dir = normalize(To - From);
		vec2 Col, Before, Hit;
		if(Collision()->IntersectLine(To - Dir * 4.0f, To + Dir * 6.0f, &Col, &Before))
		{
			if(g_Config.m_XcImpactFx != IMPACT_OFF || g_Config.m_XcImpactHoles)
				Impact(Col, Dir, WallNormal(Col, Before), WEAPON_LASER, Owner);
		}
		else if(g_Config.m_XcHitPlayerFx)
		{
			// a laser that stops in the air stopped on a tee
			const int Tee = TeeOnLine(To - Dir * 24.0f, To + Dir * 8.0f, Owner, &Hit);
			if(Tee >= 0)
				PlayerHit(Hit, Dir, Tee);
		}
	}
	m_Lasers[Key] = {To, m_Frame};
}

void CXcImpactFx::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		m_Shots.clear();
		m_Lasers.clear();
		return;
	}
	// bullets that were not drawn this frame are gone: a block right in front means they hit it
	for(auto It = m_Shots.begin(); It != m_Shots.end();)
	{
		const SShot &Shot = It->second;
		if(Shot.m_Frame == m_Frame)
		{
			++It;
			continue;
		}
		if(Shot.m_Weapon != WEAPON_GRENADE && ShowTargets(Shot.m_Owner) && length(Shot.m_Vel) > 0.001f)
		{
			// the server takes the bullet away on the tick it hits, and the client draws a tick or two behind:
			// the last place drawn can be 100 units and more before the block. Follow its curve on for a few
			// ticks (only next to the block, as before, it was found every time)
			vec2 Col, Before, Hit;
			bool Wall = false;
			vec2 From = Shot.m_Pos, Dir = normalize(Shot.m_Vel);
			const float Step = 0.004f;
			for(float t = Shot.m_Age + Step; t <= Shot.m_Age + 0.1f; t += Step)
			{
				const vec2 To = CalcPos(Shot.m_StartPos, Shot.m_StartVel, Shot.m_Curvature, Shot.m_Speed, t);
				if(distance(From, To) > 0.001f)
					Dir = normalize(To - From);
				Wall = Collision()->IntersectLine(From, To, &Col, &Before);
				// a tee before the wall took the shot
				const int Tee = g_Config.m_XcHitPlayerFx ? TeeOnLine(From, Wall ? Col : To, Shot.m_Owner, &Hit) : -1;
				if(Tee >= 0)
				{
					PlayerHit(Hit, Dir, Tee);
					Wall = false;
					break;
				}
				if(Wall)
					break;
				From = To;
			}
			if(Wall && (ShowFor(Shot.m_Owner) || (g_Config.m_XcImpactHoles && ShowTargets(Shot.m_Owner))))
				Impact(Col, Dir, WallNormal(Col, Before), Shot.m_Weapon, Shot.m_Owner);
		}
		It = m_Shots.erase(It);
	}
	for(auto It = m_Lasers.begin(); It != m_Lasers.end();)
		It = It->second.m_Frame == m_Frame ? std::next(It) : m_Lasers.erase(It);
	m_Frame++;

	m_Now += Client()->RenderFrameTime();
	m_vRecent.erase(std::remove_if(m_vRecent.begin(), m_vRecent.end(), [&](const SRecent &Recent) { return m_Now - Recent.m_Time > 0.5f; }), m_vRecent.end());
	UpdateHoles(Client()->RenderFrameTime());
	m_Particles.Update(std::min(Client()->RenderFrameTime(), 0.1f));
	m_Particles.Render(GameClient()->m_FxRenderer);
}

void CXcImpactFx::AddHole(vec2 Pos, vec2 Normal, ColorRGBA Color, float Size)
{
	// on the face of the block that was hit, not inside it
	vec2 Face = Pos;
	if(Normal.x != 0.0f)
		Face.x = (std::floor(Pos.x / 32.0f) + (Normal.x > 0.0f ? 1.0f : 0.0f)) * 32.0f;
	else
		Face.y = (std::floor(Pos.y / 32.0f) + (Normal.y > 0.0f ? 1.0f : 0.0f)) * 32.0f;
	// a shot into the same hole does not stack a new one
	for(SHole &Hole : m_vHoles)
		if(distance(Hole.m_Pos, Face) < 3.0f && Hole.m_Normal == Normal)
		{
			Hole.m_Time = 0.0f;
			Hole.m_Color = Color;
			return;
		}
	m_vHoles.push_back({Face, Normal, Color, 0.0f, Size * random_float(0.85f, 1.15f)});
	const int Max = g_Config.m_XcImpactHoleMax;
	if((int)m_vHoles.size() > Max)
		m_vHoles.erase(m_vHoles.begin(), m_vHoles.begin() + ((int)m_vHoles.size() - Max));
}

void CXcImpactFx::UpdateHoles(float Dt)
{
	if(!g_Config.m_XcImpactHoles)
	{
		m_vHoles.clear();
		return;
	}
	const float Stay = (float)g_Config.m_XcImpactHoleTime;
	for(SHole &Hole : m_vHoles)
		Hole.m_Time += Dt;
	m_vHoles.erase(std::remove_if(m_vHoles.begin(), m_vHoles.end(), [&](const SHole &Hole) { return Hole.m_Time >= Stay; }), m_vHoles.end());
}

void CXcImpactFx::QueueHoles()
{
	if(m_vHoles.empty() || (Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK))
		return;
	const float Stay = (float)g_Config.m_XcImpactHoleTime;
	const float Opacity = g_Config.m_XcImpactHoleOpacity / 100.0f;
	const float Scale = g_Config.m_XcImpactHoleSize / 100.0f;
	CFxRenderer &Renderer = GameClient()->m_FxRenderer;
	for(const SHole &Hole : m_vHoles)
	{
		// fades out over the last second (or the last third of a short stay)
		const float FadeTime = std::min(1.0f, Stay / 3.0f);
		const float Fade = std::clamp((Stay - Hole.m_Time) / FadeTime, 0.0f, 1.0f) * Opacity;
		const float Radius = 6.5f * Hole.m_Size * Scale;
		// flat along the face, sunk a little into the block
		const vec2 Center = Hole.m_Pos - Hole.m_Normal * Radius * 0.35f;
		const float Rot = angle(vec2(-Hole.m_Normal.y, Hole.m_Normal.x));
		ColorRGBA Edge(0.08f, 0.07f, 0.07f, 0.55f * Fade), Core(0.02f, 0.02f, 0.02f, 0.95f * Fade);
		if(g_Config.m_XcImpactHoleStyle == 2)
		{
			Edge = ColorRGBA(Hole.m_Color.r * 0.5f, Hole.m_Color.g * 0.5f, Hole.m_Color.b * 0.5f, 0.6f * Fade);
			Core = ColorRGBA(Hole.m_Color.r * 0.2f, Hole.m_Color.g * 0.2f, Hole.m_Color.b * 0.2f, 0.95f * Fade);
		}
		Renderer.AddWorld(FxShape::Ellipse(Center, vec2(Radius * 1.25f, Radius * 0.8f), Edge).Rotate(Rot).Soft(0.6f));
		Renderer.AddWorld(FxShape::Ellipse(Center, vec2(Radius * 0.7f, Radius * 0.45f), Core).Rotate(Rot).Soft(0.3f));
		if(g_Config.m_XcImpactHoleStyle == 1 && Hole.m_Time < 1.5f)
		{
			// a hot edge that cools down: white, the color of the shot, then gone
			const float Heat = 1.0f - Hole.m_Time / 1.5f;
			const ColorRGBA Hot(mix(Hole.m_Color.r, 1.0f, Heat * 0.6f), mix(Hole.m_Color.g, 1.0f, Heat * 0.6f), mix(Hole.m_Color.b, 1.0f, Heat * 0.6f), Heat * Heat * Fade);
			Renderer.AddWorld(FxShape::Ellipse(Center, vec2(Radius * 0.85f, Radius * 0.55f), Hot).Rotate(Rot).Soft(0.5f).Glow(Radius * 1.5f, Heat).Additive());
		}
	}
}
