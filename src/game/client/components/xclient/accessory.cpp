#include "accessory.h"

#include "fx_renderer.h"
#include "sprite_fx.h"
#include "xc_helpers.h"

#include <base/math.h>

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <cmath>

using namespace XcHelpers;

// distance from the tee center to the top of its head at the default tee size
static constexpr float HEAD_OFFSET = 27.0f;

// spring that lets the accessory lag behind sudden vertical speed changes
static constexpr float SPRING_STIFFNESS = 250.0f;
static constexpr float SPRING_DAMPING = 11.0f;
static constexpr float SPRING_IMPULSE = 0.12f;
static constexpr float SPRING_MAX = 8.0f;

void CAccessory::OnReset()
{
	for(auto &State : m_aStates)
		State = SState();
}

void CAccessory::OnMapLoad()
{
	OnReset();
}

void CAccessory::UpdateState(int ClientId, vec2 Pos, float Dt)
{
	SState &State = m_aStates[ClientId];
	const vec2 Vel = CharacterVelocity(GameClient(), ClientId);
	const float Physics = g_Config.m_XcAccessoryPhysics / 100.0f;

	if(!State.m_Valid || distance(State.m_Pos, Pos) > 200.0f)
	{
		State = SState();
		State.m_Valid = true;
		State.m_Pos = Pos;
		State.m_LastVel = Vel;
		return;
	}

	// lean into the running direction
	const float TargetTilt = std::clamp(Vel.x / 2000.0f, -0.3f, 0.3f) * Physics;
	State.m_Tilt = mix(State.m_Tilt, TargetTilt, SmoothFactor(8.0f, Dt));

	// a jump or a landing pushes the accessory down, then it springs back
	const float DeltaVelY = Vel.y - State.m_LastVel.y;
	State.m_SpringVel += -DeltaVelY * SPRING_IMPULSE * Physics;

	// fixed small steps keep the spring stable on any frame rate
	constexpr float STEP = 1.0f / 240.0f;
	float Remaining = Dt;
	while(Remaining > 0.0f)
	{
		const float h = std::min(Remaining, STEP);
		const float Accel = -SPRING_STIFFNESS * State.m_Spring - SPRING_DAMPING * State.m_SpringVel;
		State.m_SpringVel += Accel * h;
		State.m_Spring += State.m_SpringVel * h;
		Remaining -= h;
	}
	if(std::abs(State.m_Spring) > SPRING_MAX)
	{
		State.m_Spring = std::clamp(State.m_Spring, -SPRING_MAX, SPRING_MAX);
		State.m_SpringVel = 0.0f;
	}

	State.m_Pos = Pos;
	State.m_LastVel = Vel;
}

void CAccessory::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(g_Config.m_XcAccessory == ACCESSORY_OFF && !GameClient()->m_XcCosmetics.AnyRemote())
	{
		OnReset();
		return;
	}

	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	m_Time += Dt;

	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		// a player with a shared set wears his own accessory
		const CXcCosmeticsSync::CScope Shared(GameClient()->m_XcCosmetics, ClientId);
		if(g_Config.m_XcAccessory == ACCESSORY_OFF || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			(!ShowFor(GameClient(), ClientId, g_Config.m_XcAccessorySelf, g_Config.m_XcAccessoryDummy, g_Config.m_XcAccessoryOthers) &&
				!(g_Config.m_XcAccessoryFriends && GameClient()->m_aClients[ClientId].m_Friend)))
		{
			State.m_Valid = false;
			continue;
		}
		const float Alpha = PlayerAlpha(GameClient(), ClientId) * g_Config.m_XcAccessoryAlpha / 100.0f;
		if(Alpha <= 0.0f)
		{
			State.m_Valid = false;
			continue;
		}

		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const vec2 Pos = Data.m_RenderPos;
		UpdateState(ClientId, Pos, Dt);
		m_ColorClient = ClientId;

		const float TeeScale = Data.m_RenderInfo.m_Size / 64.0f;
		const float Scale = TeeScale * g_Config.m_XcAccessorySize / 100.0f;
		// a sign or a cross floats higher than a halo or a crown
		const bool High = g_Config.m_XcAccessory == ACCESSORY_SIGN || g_Config.m_XcAccessory == ACCESSORY_CROSS || g_Config.m_XcAccessory == ACCESSORY_STARS;
		State.m_Lift = (High ? 30.0f : 20.0f) * Scale * g_Config.m_XcAccessoryHeight / 100.0f;
		const float Spring = State.m_Spring * Scale;
		// pushed down = squashed, pulled up = stretched
		const float Squash = std::clamp(1.0f - State.m_Spring / SPRING_MAX * 0.2f, 0.8f, 1.2f);
		// the head moves around the tee center when tilting
		const vec2 Head = Rotate(Pos + vec2(0.0f, -HEAD_OFFSET * TeeScale + Spring), Pos, State.m_Tilt);

		switch(g_Config.m_XcAccessory)
		{
		case ACCESSORY_CONE: RenderCone(Head, Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_HALO: RenderHalo(Head + vec2(0.0f, std::sin(m_Time * 2.2f + ClientId) * 2.0f * Scale), Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_CROWN: RenderCrown(Head, Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_HORNS: RenderHorns(Head, Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_SPROUT: RenderSprout(Head, Scale, State.m_Tilt, Squash, Alpha, State.m_Spring / SPRING_MAX); break;
		case ACCESSORY_STARS: RenderStars(Head, Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_FLAME: RenderFlame(Head, Scale, State.m_Tilt, Squash, Alpha, CharacterVelocity(GameClient(), ClientId)); break;
		case ACCESSORY_BOW: RenderBow(Head, Scale, State.m_Tilt, Squash, Alpha); break;
		case ACCESSORY_SIGN: RenderSign(Head + vec2(0.0f, std::sin(m_Time * 2.6f + ClientId) * 1.5f * Scale), Scale, State.m_Tilt, Squash, Alpha, Data.m_FreezeEnd != 0 || Data.m_RegularPredicted.m_FreezeEnd != 0 || Data.m_DeepFrozen); break;
		case ACCESSORY_CROSS: RenderCross(Head + vec2(0.0f, std::sin(m_Time * 2.0f + ClientId) * 2.0f * Scale), Scale, State.m_Tilt, Squash, Alpha); break;
		}
	}
}

void CAccessory::RenderPreview(vec2 TeePos, float TeeSize, float Alpha, float Time)
{
	if(g_Config.m_XcAccessory == ACCESSORY_OFF || Alpha <= 0.0f)
		return;
	m_ColorClient = GameClient()->m_Snap.m_LocalClientId;
	Alpha *= g_Config.m_XcAccessoryAlpha / 100.0f;
	const float TeeScale = TeeSize / 64.0f;
	const float Scale = TeeScale * g_Config.m_XcAccessorySize / 100.0f;
	const vec2 Head = TeePos + vec2(0.0f, -HEAD_OFFSET * TeeScale);
	switch(g_Config.m_XcAccessory)
	{
	case ACCESSORY_CONE: RenderCone(Head, Scale, 0.0f, 1.0f, Alpha); break;
	case ACCESSORY_HALO: RenderHalo(Head + vec2(0.0f, std::sin(Time * 2.2f) * 2.0f * Scale), Scale, 0.0f, 1.0f, Alpha); break;
	case ACCESSORY_CROWN: RenderCrown(Head, Scale, 0.0f, 1.0f, Alpha); break;
	case ACCESSORY_HORNS: RenderHorns(Head, Scale, 0.0f, 1.0f, Alpha); break;
	case ACCESSORY_SPROUT: RenderSprout(Head, Scale, 0.0f, 1.0f, Alpha, 0.0f); break;
	case ACCESSORY_STARS: RenderStars(Head, Scale, 0.0f, 1.0f, Alpha); break;
	case ACCESSORY_FLAME: RenderFlame(Head, Scale, 0.0f, 1.0f, Alpha, vec2(0.0f, 0.0f)); break;
	case ACCESSORY_BOW: RenderBow(Head, Scale, 0.0f, 1.0f, Alpha); break;
	case ACCESSORY_SIGN: RenderSign(Head + vec2(0.0f, std::sin(Time * 2.6f) * 1.5f * Scale), Scale, 0.0f, 1.0f, Alpha, false); break;
	case ACCESSORY_CROSS: RenderCross(Head + vec2(0.0f, std::sin(Time * 2.0f) * 2.0f * Scale), Scale, 0.0f, 1.0f, Alpha); break;
	}
	GameClient()->m_FxRenderer.FlushNow();
}

ColorRGBA CAccessory::BaseColor(float Offset, float Alpha) const
{
	return EffectColor(GameClient(), g_Config.m_XcAccessoryColorMode, g_Config.m_XcAccessoryColor, m_ColorClient, Offset, Alpha);
}

static ColorRGBA Shade(ColorRGBA Color, float Factor)
{
	return ColorRGBA(std::min(Color.r * Factor, 1.0f), std::min(Color.g * Factor, 1.0f), std::min(Color.b * Factor, 1.0f), Color.a);
}

void CAccessory::RenderCone(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	constexpr float BRIM_SQUASH = 0.3f;
	const float Radius = 30.0f * Scale;
	const float Height = 24.0f * Scale * Squash * g_Config.m_XcAccessoryHeight / 100.0f;
	const vec2 Base = Rotate(Head + vec2(0.0f, 4.0f * Scale), Head, Tilt);
	const vec2 Body = Rotate(Head + vec2(0.0f, 4.0f * Scale - Height * 0.5f), Head, Tilt);
	const vec2 Brim = vec2(Radius, Radius * BRIM_SQUASH);

	// underside of the brim, then the cone, then the glowing rim on top
	Fx.AddWorld(FxShape::Ellipse(Base, Brim, Shade(BaseColor(0.5f, Alpha * 0.8f), 0.7f)).Rotate(Tilt));
	Fx.AddWorld(FxShape::Triangle(Body, vec2(Radius * 0.96f, Height * 0.5f), Shade(BaseColor(0.0f, Alpha * 0.9f), 1.3f))
			    .Gradient(Shade(BaseColor(0.3f, Alpha * 0.8f), 0.85f))
			    .Rotate(Tilt));
	Fx.AddWorldGlowing(FxShape::Ring(Base, Brim, 2.0f * Scale, Shade(BaseColor(0.5f, Alpha), 1.2f)).Rotate(Tilt), 5.0f * Scale, 0.5f);
}

void CAccessory::RenderHalo(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	const vec2 Center = Rotate(Head + vec2(0.0f, -14.0f * Scale * Squash), Head, Tilt);
	const vec2 Radius = vec2(15.0f, 15.0f * 0.32f * Squash) * Scale;
	const ColorRGBA Color = BaseColor(0.0f, Alpha);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	Fx.AddWorld(FxShape::Ring(Center, Radius, 3.0f * Scale, Color).Rotate(Tilt));
	// the glow is built from soft lights along the ring, so it is equally wide all around;
	// the glow of the flattened ring itself would be flattened too: thin on top, thick at the sides
	const FxGlow::SScale Glow = FxGlow::g_Current;
	if(Glow.m_Strength <= 0.0f || Glow.m_Radius <= 0.0f)
		return;
	const int NumLights = 18;
	const float c = std::cos(Tilt), s = std::sin(Tilt);
	CXcSprites &Sprites = GameClient()->m_XcSprites;
	for(int i = 0; i < NumLights; i++)
	{
		const float a = 2.0f * pi * i / NumLights;
		const vec2 Local(std::cos(a) * Radius.x, std::sin(a) * Radius.y);
		const vec2 Pos = Center + vec2(Local.x * c - Local.y * s, Local.x * s + Local.y * c);
		if(Sprites.Enabled())
		{
			// a colored layer keeps the glow visible on bright maps, the light is added on top
			Sprites.Queue(CXcSprites::SPRITE_GLOW, false, Pos, 13.0f * Scale * Glow.m_Radius, 0.0f, Color.WithMultipliedAlpha(std::min(0.16f * Glow.m_Strength, 0.5f)));
			Sprites.Queue(CXcSprites::SPRITE_GLOW, true, Pos, 18.0f * Scale * Glow.m_Radius, 0.0f, Color.WithMultipliedAlpha(0.4f * Glow.m_Strength));
		}
		else
			Fx.AddWorld(FxShape::Circle(Pos, 1.0f * Scale, Color).GlowOnly(5.0f * Scale, 0.18f).Additive()); // Glow() applies the scope
	}
}

void CAccessory::RenderCrown(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	// the crown is drawn a bit bigger than the other accessories to match their visual weight
	Scale *= 1.3f;
	const ColorRGBA GoldTop(1.0f, 0.88f, 0.42f, Alpha);
	const ColorRGBA GoldBottom(0.9f, 0.62f, 0.16f, Alpha);
	const ColorRGBA Outline(0.42f, 0.27f, 0.05f, Alpha);
	const float HalfWidth = 13.0f * Scale;
	const float Bottom = 5.0f * Scale;
	const float BandTop = Bottom - 7.0f * Scale * Squash;
	const float Border = 1.2f * Scale;
	const auto P = [&](float x, float y) { return Rotate(Head + vec2(x, y), Head, Tilt); };

	struct SSpike
	{
		float m_Left, m_Right, m_Height;
	};
	const SSpike aSpikes[] = {
		{-HalfWidth, -HalfWidth / 3.0f, 9.0f * Scale},
		{-HalfWidth / 3.0f, HalfWidth / 3.0f, 13.0f * Scale},
		{HalfWidth / 3.0f, HalfWidth, 9.0f * Scale},
	};
	const ColorRGBA Gem = Shade(BaseColor(0.0f, Alpha), 1.1f);
	for(const SSpike &Spike : aSpikes)
	{
		const float Height = Spike.m_Height * Squash + 1.0f * Scale; // reaches into the band
		const float CenterX = (Spike.m_Left + Spike.m_Right) * 0.5f;
		const float CenterY = BandTop + 1.0f * Scale - Height * 0.5f;
		Fx.AddWorld(FxShape::Triangle(P(CenterX, CenterY), vec2((Spike.m_Right - Spike.m_Left) * 0.5f, Height * 0.5f), GoldTop)
				    .Gradient(GoldBottom)
				    .Outline(Border, Outline)
				    .Rotate(Tilt));
		Fx.AddWorldGlowing(FxShape::Circle(P(CenterX, BandTop - Spike.m_Height * Squash), 2.0f * Scale, Gem), 3.0f * Scale, 0.5f);
	}
	Fx.AddWorld(FxShape::RoundRect(P(0.0f, (BandTop + Bottom) * 0.5f), vec2(HalfWidth, (Bottom - BandTop) * 0.5f), 1.5f * Scale, GoldTop)
			    .Gradient(GoldBottom)
			    .Outline(Border, Outline)
			    .Rotate(Tilt));
	const vec2 Center = P(0.0f, (BandTop + Bottom) * 0.5f);
	Fx.AddWorldGlowing(FxShape::Circle(Center, 2.4f * Scale, Gem), 3.5f * Scale, 0.5f);
	Fx.AddWorld(FxShape::Circle(Center + vec2(-0.8f, -0.8f) * Scale, 0.7f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, Alpha * 0.8f)).Additive());
}

// two neon horns like crescents, in the look of the cone hat: a see-through fill and a glowing outline
void CAccessory::RenderHorns(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const ColorRGBA Main = BaseColor(0.0f, Alpha);
	const ColorRGBA Core(mix(Main.r, 1.0f, 0.55f), mix(Main.g, 1.0f, 0.55f), mix(Main.b, 1.0f, 0.55f), Alpha);
	const auto Bezier = [](vec2 a, vec2 b, vec2 c, float t) { return a * ((1.0f - t) * (1.0f - t)) + b * (2.0f * t * (1.0f - t)) + c * (t * t); };
	constexpr int NUM = 12;
	Graphics()->TextureClear();
	for(float Side : {-1.0f, 1.0f})
	{
		// a crescent: wide on the head, both edges bow outwards, the sharp tip points up and a little out
		const vec2 BaseIn(Side * 4.0f, 3.0f), BaseOut(Side * 11.5f, 3.0f), Tip(Side * 13.5f, -12.5f * Squash);
		const vec2 CtrlIn(Side * 10.5f, -1.0f * Squash), CtrlOut(Side * 15.0f, -1.5f * Squash);
		vec2 aIn[NUM + 1], aOut[NUM + 1];
		for(int i = 0; i <= NUM; i++)
		{
			const float t = (float)i / NUM;
			aIn[i] = Rotate(Head + Bezier(BaseIn, CtrlIn, Tip, t) * Scale, Head, Tilt);
			aOut[i] = Rotate(Head + Bezier(BaseOut, CtrlOut, Tip, t) * Scale, Head, Tilt);
		}
		// the see-through fill between the two edges, lighter to the tip
		Graphics()->QuadsBegin();
		for(int i = 0; i < NUM; i++)
		{
			const float t = (float)i / NUM;
			Graphics()->SetColor(ColorRGBA(mix(Main.r, Core.r, t), mix(Main.g, Core.g, t), mix(Main.b, Core.b, t), Alpha * 0.3f));
			const IGraphics::CFreeformItem Item(aIn[i], aOut[i], aIn[i + 1], aOut[i + 1]);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
		Graphics()->QuadsEnd();
		// the glowing outline: both edges and the base, a white-hot core in the middle of the line
		const auto Edge = [&](vec2 A, vec2 B) {
			Fx.AddWorldGlowing(FxShape::Line(A, B, 1.6f * Scale, Main), 5.0f * Scale, 0.7f);
			Fx.AddWorld(FxShape::Line(A, B, 0.6f * Scale, Core).Additive());
		};
		for(int i = 0; i < NUM; i++)
		{
			Edge(aIn[i], aIn[i + 1]);
			Edge(aOut[i], aOut[i + 1]);
		}
		Edge(aIn[0], aOut[0]);
	}
}

// a little sprout on a thin stem: it bends with the running and springs on jumps
void CAccessory::RenderSprout(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, float Spring) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const ColorRGBA Leaf = BaseColor(0.0f, Alpha);
	const ColorRGBA Stem = Shade(Leaf, 0.6f);
	// leans against the running, sways a little by itself
	const float Bend = -Tilt * 2.2f + std::sin(m_Time * 1.7f + m_ColorClient) * 0.12f + Spring * 0.25f;
	const float Length = 12.0f * Scale * Squash;
	vec2 Prev = Head + vec2(0.0f, 3.0f * Scale);
	const int NumPieces = 4;
	float Angle = -pi / 2.0f;
	for(int i = 0; i < NumPieces; i++)
	{
		Angle += Bend / NumPieces;
		const vec2 Next = Prev + direction(Angle) * (Length / NumPieces);
		Fx.AddWorld(FxShape::Line(Prev, Next, 1.6f * Scale, Stem));
		Prev = Next;
	}
	// two leaves at the top, and a bud between them
	for(float Side : {-1.0f, 1.0f})
	{
		const float LeafAngle = Angle + Side * 1.0f + std::sin(m_Time * 2.3f + Side) * 0.08f;
		const vec2 Center = Prev + direction(LeafAngle) * 4.2f * Scale;
		Fx.AddWorld(FxShape::Ellipse(Center, vec2(4.6f, 2.2f) * Scale, Leaf).Gradient(Shade(Leaf, 0.7f)).Outline(0.5f * Scale, Shade(Leaf, 0.45f)).Rotate(LeafAngle));
		Fx.AddWorld(FxShape::Line(Prev, Prev + direction(LeafAngle) * 7.0f * Scale, 0.5f * Scale, Shade(Leaf, 0.55f)));
	}
	Fx.AddWorldGlowing(FxShape::Circle(Prev, 1.5f * Scale, Shade(Leaf, 1.3f)), 3.0f * Scale, 0.4f);
}

// little stars that fly round the head like after a hit in a cartoon
void CAccessory::RenderStars(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const vec2 Center = Rotate(Head + vec2(0.0f, -5.0f * Scale * Squash), Head, Tilt);
	const int NumStars = 5;
	for(int i = 0; i < NumStars; i++)
	{
		const float a = m_Time * 2.4f + 2.0f * pi * i / NumStars + m_ColorClient;
		const float Front = std::sin(a); // the near half is lower and bigger
		const vec2 Pos = Center + vec2(std::cos(a) * 16.0f, Front * 4.5f) * Scale;
		const float Size = (3.2f + 1.0f * Front) * Scale;
		const ColorRGBA c = BaseColor(i / (float)NumStars, Alpha * (0.65f + 0.35f * Front));
		Fx.AddWorldGlowing(FxShape::Star(Pos, Size, 5, 0.45f, c).Outline(0.5f * Scale, Shade(c, 0.5f)).Rotate(m_Time * 3.0f + i), 4.0f * Scale, 0.5f);
	}
}

// a small flame over the head; it flickers and blows back when running
void CAccessory::RenderFlame(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, vec2 Vel) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const ColorRGBA Main = BaseColor(0.0f, Alpha);
	const ColorRGBA Hot(mix(Main.r, 1.0f, 0.7f), mix(Main.g, 1.0f, 0.7f), mix(Main.b, 0.85f, 0.7f), Alpha);
	const float Lean = std::clamp(-Vel.x / 1500.0f, -0.6f, 0.6f) - Tilt;
	const vec2 Base = Rotate(Head + vec2(0.0f, -1.0f * Scale), Head, Tilt);
	// tongues of the flame: from the outer and darker to the inner white-hot one
	const float aSizes[] = {1.0f, 0.7f, 0.42f};
	for(int Layer = 0; Layer < 3; Layer++)
	{
		const float k = aSizes[Layer];
		const float Flicker = 1.0f + 0.12f * std::sin(m_Time * (13.0f + Layer * 4.0f) + m_ColorClient * 1.3f);
		const float Height = 15.0f * Scale * k * Flicker * Squash;
		const vec2 Tip = Base + direction(-pi / 2.0f + Lean) * Height;
		const ColorRGBA c = Layer == 0 ? Shade(Main, 0.85f).WithMultipliedAlpha(0.95f) : (Layer == 1 ? ColorRGBA(mix(Main.r, Hot.r, 0.4f), mix(Main.g, Hot.g, 0.4f), mix(Main.b, Hot.b, 0.4f), Alpha * 0.6f) : Hot.WithMultipliedAlpha(0.7f));
		// a round bottom and a pointed top; the outer tongue is solid color, so it shows on bright maps too
		SFxShape Round = FxShape::Circle(Base + (Tip - Base) * 0.2f, 5.2f * Scale * k, c);
		SFxShape Point = FxShape::Triangle((Base + Tip) * 0.5f + (Tip - Base) * 0.05f, vec2(4.9f * Scale * k, length(Tip - Base) * 0.5f), c).Rotate(Lean);
		if(Layer > 0)
		{
			Round.Additive();
			Point.Additive();
		}
		Fx.AddWorld(Round);
		Fx.AddWorld(Point);
	}
	Fx.AddWorld(FxShape::Circle(Base + vec2(0.0f, -3.0f * Scale), 1.0f, Main).GlowOnly(14.0f * Scale, 0.6f).Additive());
	// now and then a spark flies up
	const float Spark = std::fmod(m_Time * 1.3f + m_ColorClient * 0.37f, 1.0f);
	const vec2 SparkPos = Base + vec2(std::sin(m_Time * 5.0f) * 3.0f * Scale + Lean * 10.0f * Scale * Spark, -(16.0f + 14.0f * Spark) * Scale);
	Fx.AddWorldGlowing(FxShape::Circle(SparkPos, 0.9f * Scale * (1.0f - Spark), Hot.WithMultipliedAlpha(1.0f - Spark)).Additive(), 3.0f * Scale, 0.6f * (1.0f - Spark));
}

// a bow on the side of the head
void CAccessory::RenderBow(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const ColorRGBA Main = BaseColor(0.0f, Alpha);
	const ColorRGBA Edge = Shade(Main, 0.5f);
	const vec2 Knot = Head + vec2(9.0f * Scale, 3.0f * Scale);
	const float Wiggle = std::sin(m_Time * 3.0f + m_ColorClient) * 0.06f;
	const float Angle = -0.35f + Tilt + Wiggle;
	const auto P = [&](float x, float y) { return Rotate(Knot + vec2(x, y) * Scale, Knot, Angle); };
	// the two loops are triangles pointing at the knot, the two tails hang down
	for(float Side : {-1.0f, 1.0f})
	{
		Fx.AddWorld(FxShape::Triangle(P(Side * 4.5f, 0.0f), vec2(4.2f, 4.5f) * Scale * vec2(Squash, 1.0f), Main).Gradient(Shade(Main, 0.75f)).Outline(0.7f * Scale, Edge).Rotate(Angle - Side * pi / 2.0f)); // the tip at the knot
		Fx.AddWorld(FxShape::Line(P(Side * 1.0f, 1.5f), P(Side * 3.0f, 7.0f), 2.0f * Scale, Shade(Main, 0.85f)).Outline(0.5f * Scale, Edge));
	}
	Fx.AddWorld(FxShape::Circle(P(0.0f, 0.0f), 2.2f * Scale, Shade(Main, 1.15f)).Outline(0.7f * Scale, Edge));
	Fx.AddWorld(FxShape::Circle(P(-0.7f, -0.7f), 0.7f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.6f * Alpha)).Additive());
}

// "!" or "?" over the head, "?" while frozen in the auto mode
void CAccessory::RenderSign(vec2 Head, float Scale, float Tilt, float Squash, float Alpha, bool Frozen) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const bool Question = g_Config.m_XcAccessorySign == 1 || (g_Config.m_XcAccessorySign == 2 && Frozen);
	const ColorRGBA Main = BaseColor(0.0f, Alpha);
	const ColorRGBA Edge = Shade(Main, 0.4f);
	const vec2 Center = Rotate(Head + vec2(0.0f, -14.0f * Scale * Squash), Head, Tilt);
	const auto P = [&](float x, float y) { return Rotate(Center + vec2(x, y) * Scale, Center, Tilt); };
	if(Question)
	{
		// the hook as the upper half of a ring and a short line down, then the dot
		Fx.AddWorldGlowing(FxShape::Ring(P(0.0f, -3.5f), vec2(4.2f, 4.2f) * Scale, 2.4f * Scale, Main).Outline(0.6f * Scale, Edge).Half(-1.0f).Rotate(Tilt), 4.0f * Scale, 0.4f);
		Fx.AddWorld(FxShape::Line(P(4.2f, -3.5f), P(0.0f, 2.5f), 2.4f * Scale, Main).Outline(0.6f * Scale, Edge));
		Fx.AddWorld(FxShape::Line(P(0.0f, 2.5f), P(0.0f, 4.0f), 2.4f * Scale, Main).Outline(0.6f * Scale, Edge));
	}
	else
		Fx.AddWorldGlowing(FxShape::RoundRect(P(0.0f, -2.0f), vec2(1.6f, 6.0f) * Scale, 1.4f * Scale, Main).Gradient(Shade(Main, 0.8f)).Outline(0.6f * Scale, Edge).Rotate(Tilt), 4.0f * Scale, 0.4f);
	Fx.AddWorldGlowing(FxShape::Circle(P(0.0f, 7.5f), 1.7f * Scale, Main).Outline(0.6f * Scale, Edge), 4.0f * Scale, 0.4f);
}

// a simple cross floating over the head, swaying a little
void CAccessory::RenderCross(vec2 Head, float Scale, float Tilt, float Squash, float Alpha) const
{
	const FxGlow::CScope GlowScope(FxGlow::ACCESSORY);
	CFxRenderer &Fx = GameClient()->m_FxRenderer;
	const ColorRGBA Main = BaseColor(0.0f, Alpha);
	const ColorRGBA Edge = Shade(Main, 0.45f);
	const vec2 Center = Rotate(Head + vec2(0.0f, -15.0f * Scale * Squash), Head, Tilt);
	const float Angle = Tilt * 0.6f + std::sin(m_Time * 1.4f + m_ColorClient) * 0.08f;
	const auto P = [&](float x, float y) { return Rotate(Center + vec2(x, y) * Scale, Center, Angle); };
	// a soft light behind it, then the two bars
	Fx.AddWorld(FxShape::Circle(P(0.0f, -1.5f), 1.0f, Main).GlowOnly(15.0f * Scale, 0.35f).Additive());
	Fx.AddWorldGlowing(FxShape::RoundRect(P(0.0f, 0.0f), vec2(1.7f, 8.5f) * Scale, 0.8f * Scale, Main).Gradient(Shade(Main, 0.8f)).Outline(0.6f * Scale, Edge).Rotate(Angle), 4.0f * Scale, 0.4f);
	Fx.AddWorldGlowing(FxShape::RoundRect(P(0.0f, -3.5f), vec2(5.5f, 1.7f) * Scale, 0.8f * Scale, Main).Gradient(Shade(Main, 0.9f)).Outline(0.6f * Scale, Edge).Rotate(Angle), 4.0f * Scale, 0.4f);
	Fx.AddWorld(FxShape::Line(P(-0.6f, -7.5f), P(-0.6f, 7.5f), 0.6f * Scale, ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f * Alpha)).Additive());
}
