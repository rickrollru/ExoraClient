#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_MAP_PREVIEW_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_MAP_PREVIEW_H

#include <engine/graphics.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

class IEngine;
class IStorage;

// Small pictures of maps for the server browser: the game layer of a map that is already on the
// disk (downloadedmaps/ or maps/), drawn in a worker thread as a flat scheme (walls, freeze, death,
// start and finish). Maps that are not downloaded have no picture.
class CXcMapPreviews
{
public:
	struct SPreview
	{
		IGraphics::CTextureHandle m_Texture;
		int m_Width = 0;
		int m_Height = 0;
	};

	CXcMapPreviews();
	~CXcMapPreviews();
	void Init(IGraphics *pGraphics, IStorage *pStorage, IEngine *pEngine);
	// call once a frame while previews are shown: finishes jobs and uploads the pictures
	void Update();
	// the picture of the map, nullptr while it is made or when the map is not on the disk
	const SPreview *Find(const char *pMap, int Crc);
	void Clear();

private:
	class CJob;
	struct SEntry
	{
		int m_State = 0; // 0 queued, 1 running, 2 ready, 3 none
		std::shared_ptr<CJob> m_pJob;
		SPreview m_Preview;
	};

	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;
	IEngine *m_pEngine = nullptr;
	std::map<std::string, SEntry> m_Entries;
	int m_Running = 0;

	// the maps on the disk: lowercase name -> paths, with the crc when the file name has one
	struct SFile
	{
		std::string m_Path;
		int m_Crc; // -1 unknown
	};
	std::map<std::string, std::vector<SFile>> m_Files;
	float m_ScanTime = -1000.0f;
	void Scan();
	static int ScanCallback(const char *pName, int IsDir, int StorageType, void *pUser);
	const char *m_pScanFolder = nullptr;
	bool FindFile(const char *pMap, int Crc, std::string *pPath);
};

#endif
