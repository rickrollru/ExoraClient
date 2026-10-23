#include "world3d.h"

#include <base/color.h>
#include <base/math.h>
#include <base/time.h>

#include <engine/editor.h>
#include <engine/engine.h>
#include <engine/external/stb/stb_image.h>
#include <engine/graphics.h>
#include <engine/map.h>
#include <engine/shared/config.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>
#include <game/collision.h>
#include <game/map/map_renderer.h>
#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

// reads the map file again and colors every cell like the topmost design tile on it: the average color of that
// tile in its tileset times the color of the layer
class CXcWorld3D::CColorJob : public IJob
{
public:
	IStorage *m_pStorage;
	std::string m_Path;
	std::vector<uint8_t> m_vColors;
	int m_Width = 0;
	int m_Height = 0;
	bool m_Success = false;

	CColorJob(IStorage *pStorage, std::string Path) :
		m_pStorage(pStorage), m_Path(std::move(Path))
	{
		Abortable(true);
	}

protected:
	// the average color of each of the 256 tiles of a tileset (rgb, alpha coverage)
	bool TileColors(IMap *pMap, int ImageIndex, std::vector<float> &vOut)
	{
		int Start, Num;
		pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &Num);
		if(ImageIndex < 0 || ImageIndex >= Num)
			return false;
		const CMapItemImage *pItem = static_cast<const CMapItemImage *>(pMap->GetItem(Start + ImageIndex));
		if(!pItem || pItem->m_Width <= 0 || pItem->m_Height <= 0)
			return false;
		int Width = pItem->m_Width, Height = pItem->m_Height;
		std::vector<unsigned char> vRgba;
		if(pItem->m_External)
		{
			const char *pName = static_cast<const char *>(pMap->GetData(pItem->m_ImageName));
			if(!pName || !pName[0])
				return false;
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "mapres/%s.png", pName);
			void *pFile;
			unsigned FileSize;
			if(!m_pStorage->ReadFile(aPath, IStorage::TYPE_ALL, &pFile, &FileSize))
				return false;
			int Comp;
			unsigned char *pPixels = stbi_load_from_memory((const unsigned char *)pFile, (int)FileSize, &Width, &Height, &Comp, 4);
			free(pFile);
			if(!pPixels)
				return false;
			vRgba.assign(pPixels, pPixels + (size_t)Width * Height * 4);
			stbi_image_free(pPixels);
		}
		else
		{
			if((size_t)pMap->GetDataSize(pItem->m_ImageData) < (size_t)Width * Height * 4)
				return false;
			const unsigned char *pData = static_cast<const unsigned char *>(pMap->GetData(pItem->m_ImageData));
			vRgba.assign(pData, pData + (size_t)Width * Height * 4);
		}
		const int TileW = Width / 16, TileH = Height / 16;
		if(TileW <= 0 || TileH <= 0)
			return false;
		vOut.assign(256 * 4, 0.0f);
		for(int t = 0; t < 256; t++)
		{
			double R = 0, G = 0, B = 0, A = 0;
			const int X0 = (t % 16) * TileW, Y0 = (t / 16) * TileH;
			// every second pixel is plenty for an average
			for(int y = Y0; y < Y0 + TileH; y += 2)
			{
				for(int x = X0; x < X0 + TileW; x += 2)
				{
					const unsigned char *pPixel = &vRgba[((size_t)y * Width + x) * 4];
					const double Alpha = pPixel[3] / 255.0;
					R += pPixel[0] * Alpha;
					G += pPixel[1] * Alpha;
					B += pPixel[2] * Alpha;
					A += Alpha;
				}
			}
			const double Count = std::max(1.0, (double)((TileW + 1) / 2) * ((TileH + 1) / 2));
			if(A > 0)
			{
				vOut[t * 4 + 0] = (float)(R / A / 255.0);
				vOut[t * 4 + 1] = (float)(G / A / 255.0);
				vOut[t * 4 + 2] = (float)(B / A / 255.0);
			}
			vOut[t * 4 + 3] = (float)(A / Count);
		}
		return true;
	}

	void Run() override
	{
		std::unique_ptr<IMap> pMap = CreateMap();
		if(!pMap->Load(m_pStorage, m_Path.c_str(), IStorage::TYPE_ALL))
			return;
		int LayersStart, LayersNum, GroupsStart, GroupsNum;
		pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
		pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
		// the size of the game layer
		for(int i = 0; i < LayersNum; i++)
		{
			const CMapItemLayer *pLayer = static_cast<const CMapItemLayer *>(pMap->GetItem(LayersStart + i));
			if(pLayer && pLayer->m_Type == LAYERTYPE_TILES && (reinterpret_cast<const CMapItemLayerTilemap *>(pLayer)->m_Flags & TILESLAYERFLAG_GAME))
			{
				m_Width = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer)->m_Width;
				m_Height = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer)->m_Height;
				break;
			}
		}
		if(m_Width <= 0 || m_Height <= 0 || (size_t)m_Width * m_Height > 4000u * 4000u)
			return;
		m_vColors.assign((size_t)m_Width * m_Height * 4, 0);
		std::vector<std::vector<float>> vvTiles;
		std::vector<int> vTried;
		for(int g = 0; g < GroupsNum; g++)
		{
			const CMapItemGroup *pGroup = static_cast<const CMapItemGroup *>(pMap->GetItem(GroupsStart + g));
			// only what moves with the game
			if(!pGroup || pGroup->m_ParallaxX != 100 || pGroup->m_ParallaxY != 100)
				continue;
			const int OffsetX = (int)std::round(pGroup->m_OffsetX / 32.0f);
			const int OffsetY = (int)std::round(pGroup->m_OffsetY / 32.0f);
			for(int l = 0; l < pGroup->m_NumLayers; l++)
			{
				if(State() == IJob::STATE_ABORTED)
					return;
				const CMapItemLayer *pLayer = static_cast<const CMapItemLayer *>(pMap->GetItem(LayersStart + pGroup->m_StartLayer + l));
				if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
					continue;
				const CMapItemLayerTilemap *pTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
				if(pTilemap->m_Flags || pTilemap->m_Image < 0 || (pTilemap->m_Layer.m_Flags & LAYERFLAG_DETAIL))
					continue;
				if((size_t)pMap->GetDataSize(pTilemap->m_Data) < (size_t)pTilemap->m_Width * pTilemap->m_Height * sizeof(CTile))
					continue;
				if(pTilemap->m_Image >= (int)vvTiles.size())
				{
					vvTiles.resize(pTilemap->m_Image + 1);
					vTried.resize(pTilemap->m_Image + 1, 0);
				}
				if(!vTried[pTilemap->m_Image])
				{
					vTried[pTilemap->m_Image] = 1;
					TileColors(pMap.get(), pTilemap->m_Image, vvTiles[pTilemap->m_Image]);
				}
				const std::vector<float> &vTiles = vvTiles[pTilemap->m_Image];
				if(vTiles.empty())
					continue;
				const CTile *pTiles = static_cast<const CTile *>(pMap->GetData(pTilemap->m_Data));
				const float aLayer[4] = {pTilemap->m_Color.r / 255.0f, pTilemap->m_Color.g / 255.0f, pTilemap->m_Color.b / 255.0f, pTilemap->m_Color.a / 255.0f};
				for(int y = 0; y < pTilemap->m_Height; y++)
				{
					const int my = y + OffsetY;
					if(my < 0 || my >= m_Height)
						continue;
					for(int x = 0; x < pTilemap->m_Width; x++)
					{
						const int mx = x + OffsetX;
						const CTile &Tile = pTiles[(size_t)y * pTilemap->m_Width + x];
						if(mx < 0 || mx >= m_Width || Tile.m_Index == 0)
							continue;
						const float *pTile = &vTiles[Tile.m_Index * 4];
						// a mostly see-through tile (a shadow, grass on top) does not cover the block
						if(pTile[3] * aLayer[3] < 0.5f)
							continue;
						uint8_t *pDst = &m_vColors[((size_t)my * m_Width + mx) * 4];
						for(int c = 0; c < 3; c++)
							pDst[c] = (uint8_t)std::clamp((int)std::round(pTile[c] * aLayer[c] * 255.0f), 0, 255);
						pDst[3] = 1;
					}
				}
			}
		}
		m_Success = true;
	}
};

void CXcWorld3D::OnMapLoad()
{
	m_HasDir = false;
	if(m_pColorJob)
		m_pColorJob->Abort();
	m_pColorJob = nullptr;
	m_vColors.clear();
	m_ColorsW = m_ColorsH = 0;
	m_ColorsWanted = true;
}

void CXcWorld3D::UpdateColors()
{
	if(m_ColorsWanted && !m_pColorJob && g_Config.m_Xc3dMapColors)
	{
		m_ColorsWanted = false;
		const char *pPath = GameClient()->Map()->Path();
		if(pPath && pPath[0])
		{
			m_pColorJob = std::make_shared<CColorJob>(Storage(), pPath);
			Engine()->AddJob(m_pColorJob);
		}
	}
	if(m_pColorJob && m_pColorJob->Done())
	{
		if(m_pColorJob->m_Success)
		{
			m_vColors = std::move(m_pColorJob->m_vColors);
			m_ColorsW = m_pColorJob->m_Width;
			m_ColorsH = m_pColorJob->m_Height;
		}
		m_pColorJob = nullptr;
	}
}

void CXcWorld3D::OnInit()
{
	// first person: a sprite is seen when no block stands between it and the eye
	// and it is not a part of your own tee right at the camera
	Graphics()->XcSetVisibilityTest([this](vec2 Eye, vec2 Pos) {
		return distance(Eye, Pos) > 56.0f && !Collision()->IntersectLine(Eye, Pos, nullptr, nullptr);
	});
	// the map renderer calls this when it starts the group with the game layer
	CMapRenderer::ms_fnXcGameGroup = [this]() {
		if(m_FrameOn && !m_BlocksDrawn)
		{
			m_BlocksDrawn = true;
			DrawBlocks();
		}
	};
}

bool CXcWorld3D::Active() const
{
	if(!g_Config.m_Xc3d || !Graphics()->XcRenderAvailable())
		return false;
	return Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
}

void CXcWorld3D::Tilt(bool On)
{
	if(!On)
	{
		Graphics()->XcSetPlaneZ(0.0f);
		Graphics()->XcSetTilt(false);
		return;
	}
	// side view: the tees, items and effects stand in the middle of the blocks' depth, on the floor and not
	// in front of it; the map layers stay on the front faces (the map renderer puts them back to 0)
	Graphics()->XcSetPlaneZ(m_FirstPerson ? 0.0f : std::max(g_Config.m_Xc3dDepth, 0) / 100.0f * 32.0f * 0.5f);
	if(m_FirstPerson)
	{
		Graphics()->XcSetTilt(true, m_FpPitch, m_Yaw, std::clamp(g_Config.m_Xc3dFpFov, 50, 120) * pi / 180.0f, true, m_FpEye, m_FrameOn);
		return;
	}
	const float Pitch = -g_Config.m_Xc3dPitch * pi / 180.0f; // positive setting: the camera above, looking down
	const float Fov = std::clamp(g_Config.m_Xc3dFov, 20, 100) * pi / 180.0f;
	Graphics()->XcSetTilt(true, Pitch, m_Yaw, Fov);
}

void CXcWorld3D::OnRender()
{
	m_FrameOn = false;
	m_BlocksDrawn = false;
	if(!Active())
		return;

	// the camera looks from behind the direction of motion, turning around softly
	const int64_t Now = time_get();
	const float Dt = m_LastTime ? std::clamp((Now - m_LastTime) / (float)time_freq(), 0.0f, 0.1f) : 0.0f;
	m_LastTime = Now;
	const float CenterX = GameClient()->m_Camera.m_Center.x;
	if(!m_HasDir)
	{
		m_LastCenterX = CenterX;
		m_HasDir = true;
	}
	if(Dt > 0.0f)
	{
		const float Speed = (CenterX - m_LastCenterX) / Dt;
		if(std::abs(Speed) > 250.0f) // a few blocks per second: not just a small step
			m_Dir += ((Speed > 0.0f ? 1.0f : -1.0f) - m_Dir) * (1.0f - std::exp(-Dt * 1.5f));
	}
	m_LastCenterX = CenterX;

	m_FirstPerson = g_Config.m_Xc3dMode == 1;
	float TargetYaw;
	if(m_FirstPerson)
	{
		// the camera in the head of the tee looks where you aim: the crosshair stays in the middle of the screen
		const bool Playing = Client()->State() != IClient::STATE_DEMOPLAYBACK && GameClient()->m_Snap.m_pLocalCharacter;
		const vec2 Tee = Playing ? GameClient()->m_LocalCharacterPos : GameClient()->m_Camera.m_Center;
		vec2 Aim = Playing ? GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy] : vec2(m_Dir * 100.0f, 0.0f);
		if(length(Aim) < 1.0f)
			Aim = vec2(m_Dir, 0.0f);
		if(Playing && std::abs(Aim.x) > 4.0f)
			m_Dir = Aim.x > 0.0f ? 1.0f : -1.0f;
		m_FpEye = Tee + vec2(0.0f, -8.0f);
		m_FpPitch = std::clamp(std::atan2(-Aim.y, std::max(std::abs(Aim.x), 1.0f)), -1.4f, 1.4f);
		TargetYaw = pi * 0.5f * m_Dir;
	}
	else
	{
		const float Side = g_Config.m_Xc3dFollow ? m_Dir : 1.0f;
		TargetYaw = g_Config.m_Xc3dYaw * pi / 180.0f * Side;
	}
	// turning around: quick in first person, softer from the side
	const float TurnSpeed = m_FirstPerson ? 14.0f : 6.0f;
	m_Yaw = Dt > 0.0f ? m_Yaw + (TargetYaw - m_Yaw) * (1.0f - std::exp(-Dt * TurnSpeed)) : TargetYaw;

	UpdateColors();
	m_FrameOn = true;
	Tilt(true);
}

void CXcWorld3D::CEnd::OnRender()
{
	// editing together: the cursors of the friends in the editor, over the world
	if(GameClient()->Editor() && (Client()->State() == IClient::STATE_ONLINE))
		GameClient()->Editor()->XcRenderInGame();
	if(!m_pOwner->m_FrameOn)
		return;
	m_pOwner->m_FrameOn = false;
	m_pOwner->Tilt(false);
}

CXcWorld3D::CTiltScope::CTiltScope(CXcWorld3D *pWorld) :
	m_pWorld(pWorld), m_On(pWorld->Active() && pWorld->m_LastTime != 0)
{
	if(m_On)
		m_pWorld->Tilt(true);
}

CXcWorld3D::CTiltScope::~CTiltScope()
{
	if(m_On)
		m_pWorld->Tilt(false);
}

void CXcWorld3D::DrawBlocks()
{
	CCollision *pCollision = Collision();
	const int MapW = pCollision->GetWidth(), MapH = pCollision->GetHeight();
	if(MapW <= 0 || MapH <= 0 || g_Config.m_Xc3dDepth <= 0)
		return;
	// everything the tilted camera sees, one cell more on each side for the neighbors
	const CScreenRect Screen = Graphics()->GetScreen();
	const int x0 = (int)std::floor(Screen.m_TopLeft.x / 32.0f) - 1;
	const int y0 = (int)std::floor(Screen.m_TopLeft.y / 32.0f) - 1;
	const int x1 = (int)std::ceil(Screen.m_BottomRight.x / 32.0f) + 1;
	const int y1 = (int)std::ceil(Screen.m_BottomRight.y / 32.0f) + 1;
	const int W = std::clamp(x1 - x0, 1, 512), H = std::clamp(y1 - y0, 1, 512);
	m_vGrid.resize((size_t)W * H * 4);
	const bool Colors = g_Config.m_Xc3dMapColors && m_ColorsW == MapW && m_ColorsH == MapH;
	const bool Freeze = g_Config.m_Xc3dFreeze;
	for(int y = 0; y < H; y++)
	{
		// outside the map the border blocks go on, like DDNet draws them
		const int my = std::clamp(y0 + y, 0, MapH - 1);
		for(int x = 0; x < W; x++)
		{
			const int mx = std::clamp(x0 + x, 0, MapW - 1);
			const int Index = my * MapW + mx;
			const int Tile = pCollision->GetTileIndex(Index);
			const int Front = pCollision->GetFrontTileIndex(Index);
			const auto &&IsFreeze = [](int t) { return t == TILE_FREEZE || t == TILE_DFREEZE || t == TILE_LFREEZE; };
			uint8_t *pCell = &m_vGrid[((size_t)y * W + x) * 4];
			if(Tile == TILE_SOLID || Front == TILE_SOLID)
				pCell[3] = 1;
			else if(Tile == TILE_NOHOOK || Front == TILE_NOHOOK)
				pCell[3] = 2;
			else if(Freeze && (IsFreeze(Tile) || IsFreeze(Front)))
				pCell[3] = 3;
			else
				pCell[3] = 0;
			if(Colors && m_vColors[(size_t)Index * 4 + 3])
			{
				pCell[0] = m_vColors[(size_t)Index * 4 + 0];
				pCell[1] = m_vColors[(size_t)Index * 4 + 1];
				pCell[2] = m_vColors[(size_t)Index * 4 + 2];
				pCell[3] |= 4;
			}
			else
				pCell[0] = pCell[1] = pCell[2] = 0;
		}
	}
	const ColorRGBA Color = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_Xc3dColor));
	const ColorRGBA Color2 = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_Xc3dColor2));
	const float aColor[4] = {Color.r, Color.g, Color.b, 1.0f};
	const float aColor2[4] = {Color2.r, Color2.g, Color2.b, 1.0f};
	const ColorRGBA Color3 = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_Xc3dFreezeColor));
	const float aColor3[4] = {Color3.r, Color3.g, Color3.b, g_Config.m_Xc3dFreezeAlpha / 100.0f};
	const float aFog[4] = {0.03f, 0.035f, 0.05f, g_Config.m_Xc3dFog / 100.0f};
	// first person: a dark sky behind the blocks, the far blocks fade into it
	const float aSky[4] = {0.05f, 0.06f, 0.09f, m_FirstPerson ? 1.0f : 0.0f};
	const float aFpFog[4] = {aSky[0], aSky[1], aSky[2], std::max(aFog[3], 0.3f)};
	Graphics()->XcDrawBlocks3D(m_vGrid.data(), x0, y0, W, H, g_Config.m_Xc3dDepth / 100.0f * 32.0f, aColor, aColor2, aColor3, m_FirstPerson ? aFpFog : aFog, aSky);
}
