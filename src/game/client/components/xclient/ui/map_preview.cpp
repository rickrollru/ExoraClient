#include "map_preview.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/engine.h>
#include <engine/image.h>
#include <engine/map.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <game/mapitems.h>

#include <algorithm>
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
		const int OutW = (AreaW + Step - 1) / Step * Scale;
		const int OutH = (AreaH + Step - 1) / Step * Scale;

		m_Image.m_Width = OutW;
		m_Image.m_Height = OutH;
		m_Image.m_Format = CImageInfo::FORMAT_RGBA;
		m_Image.Allocate();
		SPixel *pOut = reinterpret_cast<SPixel *>(m_Image.m_pData);
		std::memset(pOut, 0, (size_t)OutW * OutH * 4);
		for(int ty = 0; ty < (AreaH + Step - 1) / Step; ty++)
		{
			for(int tx = 0; tx < (AreaW + Step - 1) / Step; tx++)
			{
				// several blocks in one pixel: walls win, then the rest
				bool Found = false;
				SPixel Best = {0, 0, 0, 0};
				for(int sy = 0; sy < Step && !Found; sy++)
				{
					for(int sx = 0; sx < Step; sx++)
					{
						const int x = MinX + tx * Step + sx, y = MinY + ty * Step + sy;
						if(x > MaxX || y > MaxY)
							continue;
						const int i = y * Width + x;
						SPixel Tile;
						if(TileColor(pGame[i].m_Index, &Tile) || (pFront && TileColor(pFront[i].m_Index, &Tile)))
						{
							Best = Tile;
							if(pGame[i].m_Index == TILE_SOLID)
							{
								Found = true;
								break;
							}
						}
					}
				}
				if(Best.a == 0)
					continue;
				// walls: dark inside, light along the air, so the ways through the map draw as lines
				if(Found)
				{
					const auto &&IsWall = [&](int x, int y) {
						if(x < 0 || y < 0 || x >= Width || y >= Height)
							return true;
						const int Index = pGame[y * Width + x].m_Index;
						return Index == TILE_SOLID || Index == TILE_NOHOOK;
					};
					const int x = MinX + tx * Step, y = MinY + ty * Step;
					const bool Edge = !IsWall(x - Step, y) || !IsWall(x + Step, y) || !IsWall(x, y - Step) || !IsWall(x, y + Step);
					if(!Edge)
						Best = {58, 64, 80, 210};
				}
				for(int py = 0; py < Scale; py++)
				{
					for(int px = 0; px < Scale; px++)
					{
						SPixel Pixel = Best;
						// big blocks get a light top edge and a dark bottom one, so they read as blocks
						if(Scale >= 4 && (py == 0 || py == Scale - 1))
						{
							const int Shift = py == 0 ? 28 : -36;
							Pixel.r = (unsigned char)std::clamp(Pixel.r + Shift, 0, 255);
							Pixel.g = (unsigned char)std::clamp(Pixel.g + Shift, 0, 255);
							Pixel.b = (unsigned char)std::clamp(Pixel.b + Shift, 0, 255);
						}
						pOut[(size_t)(ty * Scale + py) * OutW + tx * Scale + px] = Pixel;
					}
				}
			}
		}
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
