#include "hats.h"

#include "xc_helpers.h"

#include <base/io.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace XcHelpers;

// a hat spring like the accessory one: lags behind sudden vertical speed changes
static constexpr float SPRING_STIFFNESS = 220.0f;
static constexpr float SPRING_DAMPING = 10.0f;
static constexpr float SPRING_IMPULSE = 0.1f;
static constexpr float SPRING_MAX = 7.0f;

// hats are small on screen: pictures up to 512 px, gifs up to 256 px, no maps
CXcHats::CXcHats() :
	m_Images(CXcBackgrounds::HATS_FOLDER, 512, 256, false)
{
}

static constexpr const char *PLACEMENTS_FILE = "xclient/hat_placements.txt";

void CXcHats::OnInit()
{
	m_Images.Init(Graphics(), Storage(), Kernel()->RequestInterface<IEngine>());
	LoadPlacements();
}

void CXcHats::OnShutdown()
{
	StoreCurrentPlacement();
	SavePlacements();
	m_Images.Shutdown();
}

CXcHats::SPlacement CXcHats::DefaultPlacement(const char *pName)
{
	// the hats shipped with the client sit right from the start
	struct SBuiltin
	{
		const char *m_pName;
		SPlacement m_Placement;
	};
	static const SBuiltin s_aBuiltin[] = {
		{"bat_wings.png", {0, -14, 132, 0, 1}},
		{"cat_ears.png", {0, -28, 70, 0}},
		{"party_hat.png", {4, -44, 42, 12}},
		{"santa_hat.png", {2, -34, 72, 0}},
		{"sunglasses.png", {6, -2, 54, 0}},
		{"top_hat.png", {0, -40, 62, 0}},
		{"wings.png", {0, -12, 140, 0, 1}},
		{"wizard_hat.png", {2, -46, 70, 0}},
	};
	for(const SBuiltin &Builtin : s_aBuiltin)
	{
		if(str_comp(Builtin.m_pName, pName) == 0)
			return Builtin.m_Placement;
	}
	return {0, -34, 64, 0};
}

void CXcHats::StoreCurrentPlacement()
{
	if(g_Config.m_XcHatFile[0])
		m_Placements[g_Config.m_XcHatFile] = {g_Config.m_XcHatX, g_Config.m_XcHatY, g_Config.m_XcHatSize, g_Config.m_XcHatRotation, g_Config.m_XcHatBehind};
}

void CXcHats::SelectHat(const char *pName)
{
	if(str_comp(g_Config.m_XcHatFile, pName) == 0)
		return;
	StoreCurrentPlacement();
	str_copy(g_Config.m_XcHatFile, pName);
	auto It = m_Placements.find(pName);
	const SPlacement Placement = It != m_Placements.end() ? It->second : DefaultPlacement(pName);
	g_Config.m_XcHatX = Placement.m_X;
	g_Config.m_XcHatY = Placement.m_Y;
	g_Config.m_XcHatSize = Placement.m_Size;
	g_Config.m_XcHatRotation = Placement.m_Rotation;
	g_Config.m_XcHatBehind = Placement.m_Behind;
	SavePlacements();
}

void CXcHats::ResetPlacement()
{
	const SPlacement Placement = DefaultPlacement(g_Config.m_XcHatFile);
	g_Config.m_XcHatX = Placement.m_X;
	g_Config.m_XcHatY = Placement.m_Y;
	g_Config.m_XcHatSize = Placement.m_Size;
	g_Config.m_XcHatRotation = Placement.m_Rotation;
	g_Config.m_XcHatBehind = Placement.m_Behind;
}

void CXcHats::LoadPlacements()
{
	m_Placements.clear();
	void *pData;
	unsigned Size;
	if(!Storage()->ReadFile(PLACEMENTS_FILE, IStorage::TYPE_SAVE, &pData, &Size))
		return;
	std::string Text((const char *)pData, Size);
	free(pData);
	size_t Start = 0;
	while(Start < Text.size())
	{
		size_t End = Text.find('\n', Start);
		if(End == std::string::npos)
			End = Text.size();
		const std::string Line = Text.substr(Start, End - Start);
		Start = End + 1;
		SPlacement Placement;
		int NameOffset = 0;
		if(std::sscanf(Line.c_str(), "%d %d %d %d %n", &Placement.m_X, &Placement.m_Y, &Placement.m_Size, &Placement.m_Rotation, &NameOffset) == 4 && NameOffset > 0)
		{
			std::string Name = Line.substr(NameOffset);
			// newer files mark the layer as "b0" / "b1" before the name
			if(Name.size() > 3 && Name[0] == 'b' && (Name[1] == '0' || Name[1] == '1') && Name[2] == ' ')
			{
				Placement.m_Behind = Name[1] - '0';
				Name = Name.substr(3);
			}
			while(!Name.empty() && (Name.back() == '\r' || Name.back() == ' '))
				Name.pop_back();
			if(!Name.empty())
			{
				Placement.m_X = std::clamp(Placement.m_X, -96, 96);
				Placement.m_Y = std::clamp(Placement.m_Y, -128, 96);
				Placement.m_Size = std::clamp(Placement.m_Size, 8, 256);
				Placement.m_Rotation = std::clamp(Placement.m_Rotation, -180, 180);
				m_Placements[Name] = Placement;
			}
		}
	}
}

void CXcHats::SavePlacements()
{
	IOHANDLE File = Storage()->OpenFile(PLACEMENTS_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;
	for(const auto &[Name, Placement] : m_Placements)
	{
		char aLine[256];
		str_format(aLine, sizeof(aLine), "%d %d %d %d b%d %s\n", Placement.m_X, Placement.m_Y, Placement.m_Size, Placement.m_Rotation, Placement.m_Behind, Name.c_str());
		io_write(File, aLine, str_length(aLine));
	}
	io_close(File);
}

void CXcHats::OnReset()
{
	for(auto &State : m_aStates)
		State = SState();
}

void CXcHats::OnMapLoad()
{
	OnReset();
}

void CXcHats::Update()
{
	// both the game and the menu call this, the gif must advance only once a frame
	const float Now = Client()->GlobalTime();
	const float Dt = m_LastUpdate < 0.0f ? 0.0f : std::clamp(Now - m_LastUpdate, 0.0f, 0.1f);
	if(Now == m_LastUpdate)
		return;
	m_LastUpdate = Now;
	m_Images.Update(g_Config.m_XcHat ? g_Config.m_XcHatFile : "", Dt, g_Config.m_XcHatGifSpeed / 100.0f);
}

void CXcHats::UpdateState(int ClientId, vec2 Pos, float Dt)
{
	SState &State = m_aStates[ClientId];
	const vec2 Vel = CharacterVelocity(GameClient(), ClientId);
	const float Sway = g_Config.m_XcHatSway / 100.0f;
	if(!State.m_Valid || distance(State.m_Pos, Pos) > 200.0f)
	{
		State = SState();
		State.m_Valid = true;
		State.m_Pos = Pos;
		State.m_LastVel = Vel;
		return;
	}
	const float TargetTilt = std::clamp(Vel.x / 2400.0f, -0.25f, 0.25f) * Sway;
	State.m_Tilt = mix(State.m_Tilt, TargetTilt, SmoothFactor(8.0f, Dt));
	State.m_SpringVel += -(Vel.y - State.m_LastVel.y) * SPRING_IMPULSE * Sway;
	constexpr float STEP = 1.0f / 240.0f;
	for(float Remaining = Dt; Remaining > 0.0f; Remaining -= STEP)
	{
		const float h = std::min(Remaining, STEP);
		State.m_SpringVel += (-SPRING_STIFFNESS * State.m_Spring - SPRING_DAMPING * State.m_SpringVel) * h;
		State.m_Spring += State.m_SpringVel * h;
	}
	if(std::abs(State.m_Spring) > SPRING_MAX)
	{
		State.m_Spring = std::clamp(State.m_Spring, -SPRING_MAX, SPRING_MAX);
		State.m_SpringVel = 0.0f;
	}
	State.m_Pos = Pos;
	State.m_LastVel = Vel;
}

bool CXcHats::LooksLeft(int ClientId) const
{
	// the player you control looks where the mouse is, the others where the server says
	if(Client()->State() != IClient::STATE_DEMOPLAYBACK && ClientId == GameClient()->m_aLocalIds[g_Config.m_ClDummy])
		return GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy].x < 0.0f;
	const float Angle = GameClient()->m_aClients[ClientId].m_RenderCur.m_Angle / 256.0f;
	return std::cos(Angle) < 0.0f;
}

void CXcHats::HatRect(vec2 Pos, float TeeSize, bool LookingLeft, vec2 *pCenter, vec2 *pSize) const
{
	const float Scale = TeeSize / 64.0f;
	const bool Flip = LookingLeft && g_Config.m_XcHatFlip;
	const float Width = g_Config.m_XcHatSize * Scale;
	*pSize = vec2(Width, Width / std::max(m_Images.FrameAspect(), 0.05f));
	*pCenter = Pos + vec2((Flip ? -1.0f : 1.0f) * g_Config.m_XcHatX, g_Config.m_XcHatY) * Scale;
}

void CXcHats::RenderOnTee(vec2 Pos, float TeeSize, bool LookingLeft, float Tilt, float Spring, float Alpha) const
{
	if(!m_Images.HasFrames() || Alpha <= 0.0f)
		return;
	vec2 Center, Size;
	HatRect(Pos, TeeSize, LookingLeft, &Center, &Size);
	const bool Flip = LookingLeft && g_Config.m_XcHatFlip;
	Center.y += Spring * TeeSize / 64.0f;
	// the hat turns around the tee center with the tilt, like it sits on the head
	Center = Rotate(Center, Pos, Tilt);
	// pushed down = a little squashed, pulled up = stretched
	const float Squash = std::clamp(1.0f - Spring / SPRING_MAX * 0.15f, 0.85f, 1.15f);
	const float Rotation = (Flip ? -1.0f : 1.0f) * g_Config.m_XcHatRotation * pi / 180.0f + Tilt;
	m_Images.RenderQuad(Center, vec2(Size.x * (2.0f - Squash), Size.y * Squash), Rotation, Alpha * g_Config.m_XcHatAlpha / 100.0f, Flip);
}

void CXcHatsBehind::OnRender()
{
	if(g_Config.m_XcHatBehind)
		GameClient()->m_XcHats.RenderAll();
}

void CXcHats::OnRender()
{
	Update();
	if(!g_Config.m_XcHatBehind && !g_Config.m_XcHatsOverTiles)
		RenderAll();
}

void CXcOverTiles::OnRender()
{
	if(g_Config.m_XcFxOverTiles)
		GameClient()->m_FxRenderer.FlushWorld();
	if(g_Config.m_XcHatsOverTiles && !g_Config.m_XcHatBehind)
		GameClient()->m_XcHats.RenderAll();
}

void CXcHats::RenderAll()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(!g_Config.m_XcHat || !m_Images.HasFrames())
	{
		OnReset();
		return;
	}
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			!ShowFor(GameClient(), ClientId, g_Config.m_XcHatSelf, g_Config.m_XcHatDummy, g_Config.m_XcHatOthers))
		{
			State.m_Valid = false;
			continue;
		}
		const float Alpha = PlayerAlpha(GameClient(), ClientId);
		if(Alpha <= 0.0f)
		{
			State.m_Valid = false;
			continue;
		}
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		UpdateState(ClientId, Data.m_RenderPos, Dt);
		RenderOnTee(Data.m_RenderPos, Data.m_RenderInfo.m_Size, LooksLeft(ClientId), State.m_Tilt, State.m_Spring, Alpha);
	}
}
