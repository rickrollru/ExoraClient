#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_BACKGROUNDS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_BACKGROUNDS_H

#include <base/vmath.h>

#include <engine/graphics.h>

#include <memory>
#include <string>
#include <vector>

class IEngine;
class IStorage;

// A folder of pictures and gifs: own menu backgrounds (xclient/backgrounds) and own hats (xclient/hats).
// Files are decoded in a worker job, only the texture upload happens on the main thread.
// The save folder can add to files shipped in data/ with the same folder name, those cannot be deleted.
class CXcBackgrounds
{
public:
	static constexpr const char *FOLDER = "xclient/backgrounds";
	static constexpr const char *HATS_FOLDER = "xclient/hats";

	struct SEntry
	{
		std::string m_Name; // file name with extension
		bool m_Animated = false; // gif
		IGraphics::CTextureHandle m_Thumbnail;
		float m_Aspect = 16.0f / 9.0f; // width / height
		bool m_ThumbnailRequested = false;
		bool m_Broken = false; // the file could not be decoded
		bool m_Builtin = false; // shipped with the client, not in the save folder
	};

	enum EAddResult
	{
		ADD_FAILED = 0,
		ADD_IMAGE,
		ADD_MAP,
		ADD_UNSUPPORTED,
	};

	// AllowMaps: AddFile copies .map files into themes/ (menu backgrounds only)
	explicit CXcBackgrounds(const char *pFolder = FOLDER, int MaxPictureSize = 2560, int MaxGifSize = 960, bool AllowMaps = true);
	~CXcBackgrounds();

	const char *Folder() const { return m_pFolder; }

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
	// current frame and its width / height, for drawing it elsewhere
	IGraphics::CTextureHandle CurrentFrame() const;
	float FrameAspect() const { return m_FrameHeight > 0.0f ? m_FrameWidth / m_FrameHeight : 1.0f; }
	// draws the current frame as a rotated quad in the current screen mapping, Flip mirrors it
	void RenderQuad(vec2 Center, vec2 Size, float Angle, float Alpha, bool Flip) const;

private:
	class CDecodeJob;

	IGraphics *m_pGraphics = nullptr;
	IStorage *m_pStorage = nullptr;
	IEngine *m_pEngine = nullptr;
	const char *m_pFolder;
	int m_MaxPictureSize;
	int m_MaxGifSize;
	bool m_AllowMaps;

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
