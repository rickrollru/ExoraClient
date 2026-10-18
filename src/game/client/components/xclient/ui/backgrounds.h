#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_BACKGROUNDS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_BACKGROUNDS_H

#include <engine/graphics.h>

#include <memory>
#include <string>
#include <vector>

class IEngine;
class IStorage;

// Own menu backgrounds: pictures and gifs in xclient/backgrounds (save folder).
// Files are decoded in a worker job, only the texture upload happens on the main thread.
class CXcBackgrounds
{
public:
	static constexpr const char *FOLDER = "xclient/backgrounds";

	struct SEntry
	{
		std::string m_Name; // file name with extension
		bool m_Animated = false; // gif
		IGraphics::CTextureHandle m_Thumbnail;
		float m_Aspect = 16.0f / 9.0f; // width / height
		bool m_ThumbnailRequested = false;
		bool m_Broken = false; // the file could not be decoded
	};

	enum EAddResult
	{
		ADD_FAILED = 0,
		ADD_IMAGE,
		ADD_MAP,
		ADD_UNSUPPORTED,
	};

	CXcBackgrounds();
	~CXcBackgrounds();

	void Init(IGraphics *pGraphics, IStorage *pStorage, IEngine *pEngine);
	void Shutdown();

	// scans the folder again
	void Refresh();
	std::vector<SEntry> &Entries() { return m_vEntries; }
	// call every frame while the gallery is visible: starts and finishes thumbnail jobs
	void UpdateThumbnails();

	// copies a file from anywhere into the backgrounds folder (pictures, gifs) or into themes (maps)
	EAddResult AddFile(const char *pPath, char *pAddedName, int AddedNameSize);
	bool Remove(const char *pName);
	static bool IsImageFile(const char *pName);

	// the active background is (re)loaded when the name changes, an empty name unloads it
	void Update(const char *pName, float Dt, float Speed);
	bool IsLoading() const;
	bool IsFailed() const { return m_ActiveFailed; }
	bool HasFrames() const { return !m_vFrames.empty(); }
	// draws the active background over the whole screen (cover fit), false when nothing is loaded
	bool Render(float Width, float Height, float Alpha);

private:
	class CDecodeJob;

	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;
	IEngine *m_pEngine = nullptr;

	std::vector<SEntry> m_vEntries;
	std::vector<std::pair<std::string, std::shared_ptr<CDecodeJob>>> m_vThumbnailJobs;

	// active background
	std::string m_ActiveName;
	std::shared_ptr<CDecodeJob> m_pActiveJob;
	size_t m_UploadedFrames = 0;
	std::vector<IGraphics::CTextureHandle> m_vFrames;
	std::vector<int> m_vDelays; // milliseconds
	float m_FrameWidth = 0.0f;
	float m_FrameHeight = 0.0f;
	size_t m_CurrentFrame = 0;
	float m_FrameTime = 0.0f; // milliseconds into the current frame
	bool m_ActiveFailed = false;

	std::shared_ptr<CDecodeJob> StartJob(const char *pName, int MaxSize, bool FirstFrameOnly);
	void UnloadActive();
	static int ScanCallback(const char *pName, int IsDir, int StorageType, void *pUser);
};

#endif
