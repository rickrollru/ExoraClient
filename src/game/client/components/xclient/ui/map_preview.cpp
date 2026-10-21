#include "map_preview.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/external/stb/stb_image.h>
#include <engine/image.h>
#include <engine/map.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/mapitems.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <cstring>

// the longer side of a picture, in pixels
static constexpr int MAX_PREVIEW_SIZE = 1024;
static constexpr int MAX_RUNNING_JOBS = 2;
static constexpr size_t MAX_PREVIEWS = 96;

struct SPixel
{
	unsigned char r, g, b, a;
};

// the colors of the scheme, close to the entities
static bool TileColor(int Index, SPixel *pColor)
{
	switch(Index)
	{
	case TILE_SOLID: *pColor = {178, 186, 202, 235}; return true;
	case TILE_NOHOOK: *pColor = {112, 118, 132, 255}; return true;
	case TILE_DEATH: *pColor = {255, 77, 77, 230}; return true;
	case TILE_FREEZE:
	case TILE_LFREEZE: *pColor = {79, 120, 220, 150}; return true;
	case TILE_DFREEZE: *pColor = {40, 52, 120, 210}; return true;
	case TILE_UNFREEZE:
	case TILE_DUNFREEZE:
	case TILE_LUNFREEZE: *pColor = {61, 214, 164, 110}; return true;
	case TILE_START: *pColor = {90, 220, 120, 200}; return true;
	case TILE_FINISH: *pColor = {255, 200, 60, 220}; return true;
	default: return false;
	}
}

class CXcMapPreviews::CJob : public IJob
{
public:
	IStorage *m_pStorage;
	std::string m_Path;
	CImageInfo m_Image;
	bool m_Success = false;

	CJob(IStorage *pStorage, std::string Path) :
		m_pStorage(pStorage), m_Path(std::move(Path))
	{
		Abortable(true);
	}


	// a tileset shrunk: for every tile Scale x Scale colors (straight alpha, 0..1); the whole picture's color too
	struct SImage
	{
		bool m_Valid = false;
		std::vector<float> m_vTiles;
		float m_aAverage[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	};

	void LoadImages(IMap *pMap, int Scale, std::vector<SImage> *pvImages)
	{
		int Start, Num;
		pMap->GetType(MAPITEMTYPE_IMAGE, &Start, &Num);
		pvImages->resize(Num);
		for(int i = 0; i < Num; i++)
		{
			const CMapItemImage *pItem = static_cast<const CMapItemImage *>(pMap->GetItem(Start + i));
			if(!pItem || pItem->m_Width <= 0 || pItem->m_Height <= 0)
				continue;
			int Width = pItem->m_Width, Height = pItem->m_Height;
			std::vector<unsigned char> vRgba;
			if(pItem->m_External)
			{
				// the mapres that come with the game
				const char *pName = static_cast<const char *>(pMap->GetData(pItem->m_ImageName));
				if(!pName || !pName[0])
					continue;
				char aPath[IO_MAX_PATH_LENGTH];
				str_format(aPath, sizeof(aPath), "mapres/%s.png", pName);
				void *pFile;
				unsigned FileSize;
				if(!m_pStorage->ReadFile(aPath, IStorage::TYPE_ALL, &pFile, &FileSize))
					continue;
				int Comp;
				unsigned char *pPixels = stbi_load_from_memory((const unsigned char *)pFile, (int)FileSize, &Width, &Height, &Comp, 4);
				free(pFile);
				if(!pPixels)
					continue;
				vRgba.assign(pPixels, pPixels + (size_t)Width * Height * 4);
				stbi_image_free(pPixels);
			}
			else
			{
				if((size_t)pMap->GetDataSize(pItem->m_ImageData) < (size_t)Width * Height * 4)
					continue;
				const unsigned char *pData = static_cast<const unsigned char *>(pMap->GetData(pItem->m_ImageData));
				vRgba.assign(pData, pData + (size_t)Width * Height * 4);
			}
			SImage &Image = (*pvImages)[i];
			// the average color, for quads
			double aSum[4] = {0, 0, 0, 0};
			for(size_t p = 0; p < (size_t)Width * Height; p++)
			{
				const double Alpha = vRgba[p * 4 + 3] / 255.0;
				for(int c = 0; c < 3; c++)
					aSum[c] += vRgba[p * 4 + c] / 255.0 * Alpha;
				aSum[3] += Alpha;
			}
			for(int c = 0; c < 3; c++)
				Image.m_aAverage[c] = aSum[3] > 0 ? (float)(aSum[c] / aSum[3]) : 1.0f;
			Image.m_aAverage[3] = (float)(aSum[3] / std::max<size_t>((size_t)Width * Height, 1));
			// tilesets are 16 x 16 tiles
			const int TileW = Width / 16, TileH = Height / 16;
			if(TileW <= 0 || TileH <= 0)
			{
				Image.m_Valid = false;
				continue;
			}
			Image.m_vTiles.assign((size_t)256 * Scale * Scale * 4, 0.0f);
			for(int t = 0; t < 256; t++)
			{
				const int X0 = (t % 16) * TileW, Y0 = (t / 16) * TileH;
				for(int sy = 0; sy < Scale; sy++)
				{
					for(int sx = 0; sx < Scale; sx++)
					{
						const int Ax = X0 + sx * TileW / Scale, Bx = std::max(Ax + 1, X0 + (sx + 1) * TileW / Scale);
						const int Ay = Y0 + sy * TileH / Scale, By = std::max(Ay + 1, Y0 + (sy + 1) * TileH / Scale);
						double R = 0, G = 0, B = 0, A = 0;
						int Count = 0;
						for(int y = Ay; y < By; y++)
						{
							for(int x = Ax; x < Bx; x++)
							{
								const unsigned char *pPixel = &vRgba[((size_t)y * Width + x) * 4];
								const double Alpha = pPixel[3] / 255.0;
								R += pPixel[0] / 255.0 * Alpha;
								G += pPixel[1] / 255.0 * Alpha;
								B += pPixel[2] / 255.0 * Alpha;
								A += Alpha;
								Count++;
							}
						}
						float *pDst = &Image.m_vTiles[((size_t)t * Scale * Scale + sy * Scale + sx) * 4];
						if(A > 0)
						{
							pDst[0] = (float)(R / A);
							pDst[1] = (float)(G / A);
							pDst[2] = (float)(B / A);
						}
						pDst[3] = (float)(A / std::max(Count, 1));
					}
				}
			}
			Image.m_Valid = true;
		}
	}

	// the sky: the colors of the first quad of the map from top to bottom (most maps start with a full screen one)
	void DrawBackground(IMap *pMap, const std::vector<SImage> &vImages, int OutH, int OutW, std::vector<float> &vPixels)
	{
		float aTop[3] = {0.18f, 0.2f, 0.25f}, aBottom[3] = {0.18f, 0.2f, 0.25f};
		int Start, Num;
		pMap->GetType(MAPITEMTYPE_LAYER, &Start, &Num);
		for(int i = 0; i < Num; i++)
		{
			const CMapItemLayer *pLayer = static_cast<const CMapItemLayer *>(pMap->GetItem(Start + i));
			if(!pLayer || pLayer->m_Type != LAYERTYPE_QUADS)
				continue;
			const CMapItemLayerQuads *pQuads = reinterpret_cast<const CMapItemLayerQuads *>(pLayer);
			if(pQuads->m_NumQuads <= 0 || (size_t)pMap->GetDataSize(pQuads->m_Data) < sizeof(CQuad))
				continue;
			const CQuad *pQuad = static_cast<const CQuad *>(pMap->GetData(pQuads->m_Data));
			float aTint[3] = {1.0f, 1.0f, 1.0f};
			if(pQuads->m_Image >= 0 && pQuads->m_Image < (int)vImages.size())
				for(int c = 0; c < 3; c++)
					aTint[c] = vImages[pQuads->m_Image].m_aAverage[c];
			const auto Channel = [](const CColor &Color, int c) { return c == 0 ? Color.r : (c == 1 ? Color.g : Color.b); };
			for(int c = 0; c < 3; c++)
			{
				aTop[c] = (Channel(pQuad->m_aColors[0], c) + Channel(pQuad->m_aColors[1], c)) / 510.0f * aTint[c];
				aBottom[c] = (Channel(pQuad->m_aColors[2], c) + Channel(pQuad->m_aColors[3], c)) / 510.0f * aTint[c];
			}
			break;
		}
		for(int y = 0; y < OutH; y++)
		{
			const float t = OutH > 1 ? y / (float)(OutH - 1) : 0.0f;
			for(int x = 0; x < OutW; x++)
				for(int c = 0; c < 3; c++)
					vPixels[((size_t)y * OutW + x) * 3 + c] = aTop[c] + (aBottom[c] - aTop[c]) * t;
		}
	}

protected:
	void Run() override
	{
		std::unique_ptr<IMap> pMap = CreateMap();
		if(!pMap->Load(m_pStorage, m_Path.c_str(), IStorage::TYPE_ALL))
			return;
		// the game layer, and the front layer for the freeze that sits there
		const CTile *pGame = nullptr;
		const CTile *pFront = nullptr;
		int Width = 0, Height = 0;
		int LayersStart, LayersNum;
		pMap->GetType(MAPITEMTYPE_LAYER, &LayersStart, &LayersNum);
		for(int i = 0; i < LayersNum && !pGame; i++)
		{
			const CMapItemLayer *pLayer = static_cast<const CMapItemLayer *>(pMap->GetItem(LayersStart + i));
			if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
				continue;
			const CMapItemLayerTilemap *pTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
			if(!(pTilemap->m_Flags & TILESLAYERFLAG_GAME))
				continue;
			Width = pTilemap->m_Width;
			Height = pTilemap->m_Height;
			if(Width <= 0 || Height <= 0 || (size_t)pMap->GetDataSize(pTilemap->m_Data) < (size_t)Width * Height * sizeof(CTile))
				return;
			pGame = static_cast<const CTile *>(pMap->GetData(pTilemap->m_Data));
			// a DDNet game layer item carries the front layer
			if(pTilemap->m_Version >= 3 && pMap->GetItemSize(LayersStart + i) >= (int)sizeof(CMapItemLayerTilemap) && pTilemap->m_Front >= 0 &&
				(size_t)pMap->GetDataSize(pTilemap->m_Front) >= (size_t)Width * Height * sizeof(CTile))
				pFront = static_cast<const CTile *>(pMap->GetData(pTilemap->m_Front));
		}
		if(!pGame)
			return;

		// only the part with something in it
		int MinX = Width, MinY = Height, MaxX = -1, MaxY = -1;
		SPixel Color;
		for(int y = 0; y < Height; y++)
		{
			for(int x = 0; x < Width; x++)
			{
				const int i = y * Width + x;
				if(TileColor(pGame[i].m_Index, &Color) || (pFront && TileColor(pFront[i].m_Index, &Color)))
				{
					MinX = std::min(MinX, x);
					MinY = std::min(MinY, y);
					MaxX = std::max(MaxX, x);
					MaxY = std::max(MaxY, y);
				}
			}
		}
		if(MaxX < MinX)
			return;
		MinX = std::max(MinX - 2, 0);
		MinY = std::max(MinY - 2, 0);
		MaxX = std::min(MaxX + 2, Width - 1);
		MaxY = std::min(MaxY + 2, Height - 1);
		const int AreaW = MaxX - MinX + 1;
		const int AreaH = MaxY - MinY + 1;
		// a few pixels for a block on small maps, a block in a pixel (or less) on big ones
		const int Scale = std::clamp(MAX_PREVIEW_SIZE / std::max(AreaW, AreaH), 1, 8);
		const int Step = std::max(1, (std::max(AreaW, AreaH) + MAX_PREVIEW_SIZE - 1) / MAX_PREVIEW_SIZE);
		const int TilesW = (AreaW + Step - 1) / Step;
		const int TilesH = (AreaH + Step - 1) / Step;
		const int OutW = TilesW * Scale;
		const int OutH = TilesH * Scale;

		// the map as it looks: the background, then every design tile layer with its tiles shrunk
		std::vector<SImage> vImages;
		LoadImages(pMap.get(), Scale, &vImages);
		if(State() == IJob::STATE_ABORTED)
			return;
		std::vector<float> vPixels((size_t)OutW * OutH * 3);
		DrawBackground(pMap.get(), vImages, OutH, OutW, vPixels);

		int GroupsStart, GroupsNum;
		pMap->GetType(MAPITEMTYPE_GROUP, &GroupsStart, &GroupsNum);
		for(int g = 0; g < GroupsNum; g++)
		{
			const CMapItemGroup *pGroup = static_cast<const CMapItemGroup *>(pMap->GetItem(GroupsStart + g));
			// only what moves with the game: far backgrounds would land in wrong places
			if(!pGroup || pGroup->m_ParallaxX != 100 || pGroup->m_ParallaxY != 100)
				continue;
			const int OffsetX = (int)std::round(pGroup->m_OffsetX / 32.0f);
			const int OffsetY = (int)std::round(pGroup->m_OffsetY / 32.0f);
			for(int l = 0; l < pGroup->m_NumLayers; l++)
			{
				const CMapItemLayer *pLayer = static_cast<const CMapItemLayer *>(pMap->GetItem(LayersStart + pGroup->m_StartLayer + l));
				if(!pLayer || pLayer->m_Type != LAYERTYPE_TILES)
					continue;
				const CMapItemLayerTilemap *pTilemap = reinterpret_cast<const CMapItemLayerTilemap *>(pLayer);
				if(pTilemap->m_Flags & (TILESLAYERFLAG_GAME | TILESLAYERFLAG_TELE | TILESLAYERFLAG_SPEEDUP | TILESLAYERFLAG_FRONT | TILESLAYERFLAG_SWITCH | TILESLAYERFLAG_TUNE))
					continue;
				const int LayerW = pTilemap->m_Width, LayerH = pTilemap->m_Height;
				if(LayerW <= 0 || LayerH <= 0 || (size_t)pMap->GetDataSize(pTilemap->m_Data) < (size_t)LayerW * LayerH * sizeof(CTile))
					continue;
				const CTile *pTiles = static_cast<const CTile *>(pMap->GetData(pTilemap->m_Data));
				const SImage *pImage = pTilemap->m_Image >= 0 && pTilemap->m_Image < (int)vImages.size() && vImages[pTilemap->m_Image].m_Valid ? &vImages[pTilemap->m_Image] : nullptr;
				const float LayerColor[4] = {pTilemap->m_Color.r / 255.0f, pTilemap->m_Color.g / 255.0f, pTilemap->m_Color.b / 255.0f, pTilemap->m_Color.a / 255.0f};
				if(LayerColor[3] <= 0.0f)
					continue;
				for(int ty = 0; ty < TilesH; ty++)
				{
					for(int tx = 0; tx < TilesW; tx++)
					{
						const int x = MinX + tx * Step - OffsetX, y = MinY + ty * Step - OffsetY;
						if(x < 0 || y < 0 || x >= LayerW || y >= LayerH)
							continue;
						const CTile &Tile = pTiles[y * LayerW + x];
						if(Tile.m_Index == 0)
							continue;
						for(int py = 0; py < Scale; py++)
						{
							for(int px = 0; px < Scale; px++)
							{
								// the part of the tile under this pixel, turned and flipped like the tile
								int sx = px, sy = py;
								if(Tile.m_Flags & TILEFLAG_ROTATE)
								{
									const int Old = sx;
									sx = sy;
									sy = Scale - 1 - Old;
								}
								if(Tile.m_Flags & TILEFLAG_XFLIP)
									sx = Scale - 1 - sx;
								if(Tile.m_Flags & TILEFLAG_YFLIP)
									sy = Scale - 1 - sy;
								float r = 1.0f, gr = 1.0f, b = 1.0f, a = 1.0f;
								if(pImage)
								{
									const float *pSrc = &pImage->m_vTiles[((size_t)Tile.m_Index * Scale * Scale + sy * Scale + sx) * 4];
									r = pSrc[0];
									gr = pSrc[1];
									b = pSrc[2];
									a = pSrc[3];
								}
								a *= LayerColor[3];
								if(a <= 0.003f)
									continue;
								float *pDst = &vPixels[((size_t)(ty * Scale + py) * OutW + tx * Scale + px) * 3];
								pDst[0] += (r * LayerColor[0] - pDst[0]) * a;
								pDst[1] += (gr * LayerColor[1] - pDst[1]) * a;
								pDst[2] += (b * LayerColor[2] - pDst[2]) * a;
							}
						}
					}
				}
			}
		}

		m_Image.m_Width = OutW;
		m_Image.m_Height = OutH;
		m_Image.m_Format = CImageInfo::FORMAT_RGBA;
		m_Image.Allocate();
		SPixel *pOut = reinterpret_cast<SPixel *>(m_Image.m_pData);
		for(size_t i = 0; i < (size_t)OutW * OutH; i++)
			pOut[i] = {(unsigned char)std::clamp(vPixels[i * 3] * 255.0f, 0.0f, 255.0f), (unsigned char)std::clamp(vPixels[i * 3 + 1] * 255.0f, 0.0f, 255.0f), (unsigned char)std::clamp(vPixels[i * 3 + 2] * 255.0f, 0.0f, 255.0f), 255};
		m_Success = true;
	}
};

CXcMapPreviews::CXcMapPreviews() = default;

CXcMapPreviews::~CXcMapPreviews()
{
	for(auto &[Key, Entry] : m_Entries)
		if(Entry.m_pJob)
			Entry.m_pJob->Abort();
}

void CXcMapPreviews::Init(IGraphics *pGraphics, IStorage *pStorage, IEngine *pEngine)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	m_pEngine = pEngine;
}

int CXcMapPreviews::ScanCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	(void)StorageType;
	CXcMapPreviews *pSelf = static_cast<CXcMapPreviews *>(pUser);
	if(IsDir || !str_endswith(pName, ".map"))
		return 0;
	char aName[128];
	str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
	int Crc = -1;
	if(str_comp(pSelf->m_pScanFolder, "downloadedmaps") == 0)
	{
		// name_sha256.map or name_crc.map: the name is everything before the last underscore
		const char *pUnderscore = str_rchr(aName, '_');
		if(!pUnderscore)
			return 0;
		const char *pHash = pUnderscore + 1;
		if(str_length(pHash) == 8)
			Crc = (int)strtoul(pHash, nullptr, 16);
		aName[pUnderscore - aName] = '\0';
	}
	char aLower[128];
	str_copy(aLower, aName);
	for(char *p = aLower; *p; p++)
		*p = (char)std::tolower((unsigned char)*p);
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", pSelf->m_pScanFolder, pName);
	pSelf->m_Files[aLower].push_back({aPath, Crc});
	return 0;
}

void CXcMapPreviews::Scan()
{
	m_Files.clear();
	m_pScanFolder = "downloadedmaps";
	m_pStorage->ListDirectory(IStorage::TYPE_ALL, "downloadedmaps", ScanCallback, this);
	m_pScanFolder = "maps";
	m_pStorage->ListDirectory(IStorage::TYPE_ALL, "maps", ScanCallback, this);
}

bool CXcMapPreviews::FindFile(const char *pMap, int Crc, std::string *pPath)
{
	char aLower[128];
	str_copy(aLower, pMap);
	for(char *p = aLower; *p; p++)
		*p = (char)std::tolower((unsigned char)*p);
	auto It = m_Files.find(aLower);
	if(It == m_Files.end() || It->second.empty())
		return false;
	// the exact version first, any version of the map else (the scheme hardly changes)
	for(const SFile &File : It->second)
	{
		if(File.m_Crc == Crc)
		{
			*pPath = File.m_Path;
			return true;
		}
	}
	*pPath = It->second.front().m_Path;
	return true;
}

void CXcMapPreviews::Update()
{
	if(!m_pGraphics)
		return;
	// new downloads show up after a while
	const float Now = (float)(time_get() / (double)time_freq());
	if(Now - m_ScanTime > 20.0f)
	{
		m_ScanTime = Now;
		Scan();
		// maps that were missing may be there now
		for(auto It = m_Entries.begin(); It != m_Entries.end();)
		{
			if(It->second.m_State == 3)
				It = m_Entries.erase(It);
			else
				++It;
		}
	}
	for(auto &[Key, Entry] : m_Entries)
	{
		if(Entry.m_State != 1 || !Entry.m_pJob || !Entry.m_pJob->Done())
			continue;
		m_Running--;
		if(Entry.m_pJob->m_Success)
		{
			Entry.m_Preview.m_Width = Entry.m_pJob->m_Image.m_Width;
			Entry.m_Preview.m_Height = Entry.m_pJob->m_Image.m_Height;
			Entry.m_Preview.m_Texture = m_pGraphics->LoadTextureRawMove(Entry.m_pJob->m_Image, 0, "xclient/map_preview");
			Entry.m_State = 2;
		}
		else
			Entry.m_State = 3;
		Entry.m_pJob = nullptr;
	}
	// start the waiting ones
	for(auto &[Key, Entry] : m_Entries)
	{
		if(m_Running >= MAX_RUNNING_JOBS)
			break;
		if(Entry.m_State != 0 || !Entry.m_pJob)
			continue;
		m_pEngine->AddJob(Entry.m_pJob);
		Entry.m_State = 1;
		m_Running++;
	}
}

const CXcMapPreviews::SPreview *CXcMapPreviews::Find(const char *pMap, int Crc)
{
	if(!m_pGraphics || !pMap[0])
		return nullptr;
	char aKey[160];
	str_format(aKey, sizeof(aKey), "%s#%08x", pMap, Crc);
	auto It = m_Entries.find(aKey);
	if(It != m_Entries.end())
		return It->second.m_State == 2 ? &It->second.m_Preview : nullptr;
	if(m_Entries.size() >= MAX_PREVIEWS)
		Clear();
	SEntry &Entry = m_Entries[aKey];
	std::string Path;
	if(!FindFile(pMap, Crc, &Path))
	{
		Entry.m_State = 3;
		return nullptr;
	}
	Entry.m_pJob = std::make_shared<CJob>(m_pStorage, Path);
	return nullptr;
}

void CXcMapPreviews::Clear()
{
	for(auto &[Key, Entry] : m_Entries)
	{
		if(Entry.m_pJob)
			Entry.m_pJob->Abort();
		if(Entry.m_State == 2)
			m_pGraphics->UnloadTexture(&Entry.m_Preview.m_Texture);
	}
	m_Entries.clear();
	m_Running = 0;
}
