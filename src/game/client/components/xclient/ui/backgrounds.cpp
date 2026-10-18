#include "backgrounds.h"

#include <base/fs.h>
#include <base/io.h>
#include <base/log.h>
#include <base/mem.h>
#include <base/str.h>

#include <engine/engine.h>
#include <engine/external/stb/stb_image.h>
#include <engine/external/stb/stb_image_xc.h>
#include <engine/gfx/image_manipulation.h>
#include <engine/image.h>
#include <engine/shared/jobs.h>
#include <engine/storage.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

static constexpr int THUMBNAIL_SIZE = 384;
static constexpr size_t GIF_MEMORY_BUDGET = 256 * 1024 * 1024;
static constexpr size_t MAX_GIF_FRAMES = 1000;
static constexpr int MAX_THUMBNAIL_JOBS = 2;
static constexpr size_t FRAMES_UPLOADED_PER_TICK = 6;

class CXcBackgrounds::CDecodeJob : public IJob
{
public:
	std::vector<unsigned char> m_vData;
	int m_MaxSize;
	bool m_FirstFrameOnly;

	std::vector<CImageInfo> m_vFrames;
	std::vector<int> m_vDelays;
	int m_SourceWidth = 0;
	int m_SourceHeight = 0;
	bool m_Success = false;

	CDecodeJob(std::vector<unsigned char> &&vData, int MaxSize, bool FirstFrameOnly) :
		m_vData(std::move(vData)), m_MaxSize(MaxSize), m_FirstFrameOnly(FirstFrameOnly)
	{
		Abortable(true);
	}

	~CDecodeJob() override
	{
		for(CImageInfo &Frame : m_vFrames)
			Frame.Free();
	}

protected:
	void Run() override
	{
		const int Size = (int)m_vData.size();
		const bool IsGif = Size >= 6 && mem_comp(m_vData.data(), "GIF8", 4) == 0;
		if(IsGif && !m_FirstFrameOnly)
		{
			XcStbiGifFrames(m_vData.data(), Size, FrameCallback, this);
		}
		else
		{
			int Width, Height, Comp;
			stbi_uc *pPixels = stbi_load_from_memory(m_vData.data(), Size, &Width, &Height, &Comp, 4);
			if(pPixels)
			{
				AddFrame(pPixels, Width, Height, 0);
				stbi_image_free(pPixels);
			}
		}
		m_Success = !m_vFrames.empty();
		m_vData.clear();
		m_vData.shrink_to_fit();
	}

private:
	size_t m_Memory = 0;

	static bool FrameCallback(void *pUser, const unsigned char *pRgba, int Width, int Height, int DelayMs)
	{
		return static_cast<CDecodeJob *>(pUser)->AddFrame(pRgba, Width, Height, DelayMs);
	}

	bool AddFrame(const unsigned char *pRgba, int Width, int Height, int DelayMs)
	{
		if(State() == IJob::STATE_ABORTED || Width <= 0 || Height <= 0)
			return false;
		m_SourceWidth = Width;
		m_SourceHeight = Height;
		const float Scale = std::min(1.0f, m_MaxSize / (float)std::max(Width, Height));
		const int NewWidth = std::max(1, (int)(Width * Scale));
		const int NewHeight = std::max(1, (int)(Height * Scale));

		CImageInfo Image;
		Image.m_Width = NewWidth;
		Image.m_Height = NewHeight;
		Image.m_Format = CImageInfo::FORMAT_RGBA;
		Image.Allocate();
		if(NewWidth == Width && NewHeight == Height)
		{
			mem_copy(Image.m_pData, pRgba, (size_t)Width * Height * 4);
		}
		else
		{
			uint8_t *pResized = ResizeImage(pRgba, Width, Height, NewWidth, NewHeight, 4);
			mem_copy(Image.m_pData, pResized, (size_t)NewWidth * NewHeight * 4);
			free(pResized);
		}
		m_vFrames.push_back(std::move(Image));
		// like browsers: tiny delays mean "as fast as the author forgot to set", play them at 10 fps
		m_vDelays.push_back(DelayMs <= 10 ? 100 : DelayMs);
		m_Memory += (size_t)NewWidth * NewHeight * 4;
		return m_Memory < GIF_MEMORY_BUDGET && m_vFrames.size() < MAX_GIF_FRAMES;
	}
};

// MaxPictureSize / MaxGifSize: biggest side of a decoded picture; gifs are kept smaller, they have many frames
CXcBackgrounds::CXcBackgrounds(const char *pFolder, int MaxPictureSize, int MaxGifSize, bool AllowMaps) :
	m_pFolder(pFolder), m_MaxPictureSize(MaxPictureSize), m_MaxGifSize(MaxGifSize), m_AllowMaps(AllowMaps)
{
}
CXcBackgrounds::~CXcBackgrounds() = default;

void CXcBackgrounds::Init(IGraphics *pGraphics, IStorage *pStorage, IEngine *pEngine)
{
	m_pGraphics = pGraphics;
	m_pStorage = pStorage;
	m_pEngine = pEngine;
	m_pStorage->CreateFolder("xclient", IStorage::TYPE_SAVE);
	m_pStorage->CreateFolder(m_pFolder, IStorage::TYPE_SAVE);
}

void CXcBackgrounds::Shutdown()
{
	for(auto &[Name, pJob] : m_vThumbnailJobs)
		pJob->Abort();
	m_vThumbnailJobs.clear();
	for(SEntry &Entry : m_vEntries)
		m_pGraphics->UnloadTexture(&Entry.m_Thumbnail);
	m_vEntries.clear();
	UnloadActive();
}

bool CXcBackgrounds::IsImageFile(const char *pName)
{
	return str_endswith_nocase(pName, ".png") || str_endswith_nocase(pName, ".jpg") || str_endswith_nocase(pName, ".jpeg") ||
	       str_endswith_nocase(pName, ".gif") || str_endswith_nocase(pName, ".bmp") || str_endswith_nocase(pName, ".tga");
}

int CXcBackgrounds::ScanCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CXcBackgrounds *pSelf = static_cast<CXcBackgrounds *>(pUser);
	if(IsDir || !IsImageFile(pName))
		return 0;
	// the same name in data/ and in the save folder: the save folder wins
	for(SEntry &Other : pSelf->m_vEntries)
	{
		if(Other.m_Name == pName)
		{
			Other.m_Builtin = Other.m_Builtin && StorageType != IStorage::TYPE_SAVE;
			return 0;
		}
	}
	SEntry Entry;
	Entry.m_Name = pName;
	Entry.m_Builtin = StorageType != IStorage::TYPE_SAVE;
	Entry.m_Animated = str_endswith_nocase(pName, ".gif");
	pSelf->m_vEntries.push_back(std::move(Entry));
	return 0;
}

void CXcBackgrounds::Refresh()
{
	// keep the thumbnails of files that are still there
	std::vector<SEntry> vOld = std::move(m_vEntries);
	m_vEntries.clear();
	m_pStorage->ListDirectory(IStorage::TYPE_ALL, m_pFolder, ScanCallback, this);
	std::sort(m_vEntries.begin(), m_vEntries.end(), [](const SEntry &a, const SEntry &b) { return str_comp_nocase(a.m_Name.c_str(), b.m_Name.c_str()) < 0; });
	for(SEntry &Old : vOld)
	{
		auto It = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const SEntry &e) { return e.m_Name == Old.m_Name; });
		if(It != m_vEntries.end())
		{
			const bool Builtin = It->m_Builtin;
			*It = std::move(Old);
			It->m_Builtin = Builtin;
		}
		else
			m_pGraphics->UnloadTexture(&Old.m_Thumbnail);
	}
}

std::shared_ptr<CXcBackgrounds::CDecodeJob> CXcBackgrounds::StartJob(const char *pName, int MaxSize, bool FirstFrameOnly)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", m_pFolder, pName);
	void *pData;
	unsigned Size;
	if(!m_pStorage->ReadFile(aPath, IStorage::TYPE_ALL, &pData, &Size))
		return nullptr;
	std::vector<unsigned char> vData((unsigned char *)pData, (unsigned char *)pData + Size);
	free(pData);
	auto pJob = std::make_shared<CDecodeJob>(std::move(vData), MaxSize, FirstFrameOnly);
	m_pEngine->AddJob(pJob);
	return pJob;
}

void CXcBackgrounds::UpdateThumbnails()
{
	// finish the jobs that are done
	for(auto It = m_vThumbnailJobs.begin(); It != m_vThumbnailJobs.end();)
	{
		if(!It->second->Done())
		{
			++It;
			continue;
		}
		auto EntryIt = std::find_if(m_vEntries.begin(), m_vEntries.end(), [&](const SEntry &e) { return e.m_Name == It->first; });
		CDecodeJob *pJob = It->second.get();
		if(EntryIt != m_vEntries.end())
		{
			if(pJob->State() == IJob::STATE_DONE && pJob->m_Success)
			{
				EntryIt->m_Aspect = pJob->m_SourceWidth / (float)std::max(pJob->m_SourceHeight, 1);
				EntryIt->m_Thumbnail = m_pGraphics->LoadTextureRawMove(pJob->m_vFrames[0], 0, EntryIt->m_Name.c_str());
			}
			else
				EntryIt->m_Broken = true;
		}
		It = m_vThumbnailJobs.erase(It);
	}
	// start new ones
	for(SEntry &Entry : m_vEntries)
	{
		if((int)m_vThumbnailJobs.size() >= MAX_THUMBNAIL_JOBS)
			break;
		if(Entry.m_ThumbnailRequested)
			continue;
		Entry.m_ThumbnailRequested = true;
		auto pJob = StartJob(Entry.m_Name.c_str(), THUMBNAIL_SIZE, true);
		if(pJob)
			m_vThumbnailJobs.emplace_back(Entry.m_Name, pJob);
		else
			Entry.m_Broken = true;
	}
}

CXcBackgrounds::EAddResult CXcBackgrounds::AddFile(const char *pPath, char *pAddedName, int AddedNameSize)
{
	const bool IsMap = m_AllowMaps && str_endswith_nocase(pPath, ".map");
	if(!IsMap && !IsImageFile(pPath))
		return ADD_UNSUPPORTED;

	void *pData;
	unsigned Size;
	if(!m_pStorage->ReadFile(pPath, IStorage::TYPE_ABSOLUTE, &pData, &Size))
	{
		log_error("xclient", "could not read '%s'", pPath);
		return ADD_FAILED;
	}
	if(!IsMap)
	{
		// only keep files that really are pictures
		int Width, Height, Comp;
		if(!stbi_info_from_memory((const stbi_uc *)pData, (int)Size, &Width, &Height, &Comp))
		{
			log_error("xclient", "'%s' is not a picture: %s", pPath, stbi_failure_reason());
			free(pData);
			return ADD_FAILED;
		}
	}

	char aName[96];
	char aExtension[16];
	fs_split_file_extension(fs_filename(pPath), aName, sizeof(aName), aExtension, sizeof(aExtension));
	// file names from other systems can carry characters the console config does not like
	for(char *p = aName; *p; p++)
	{
		if(*p == '"' || *p == ';' || *p == '\\' || *p == '/')
			*p = '_';
	}
	const char *pFolder = IsMap ? "themes" : m_pFolder;
	if(IsMap)
		m_pStorage->CreateFolder("themes", IStorage::TYPE_SAVE);

	char aFile[128];
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aFile, sizeof(aFile), "%s.%s", aName, aExtension);
	str_format(aPath, sizeof(aPath), "%s/%s", pFolder, aFile);
	for(int i = 2; m_pStorage->FileExists(aPath, IStorage::TYPE_SAVE) && i < 100; i++)
	{
		str_format(aFile, sizeof(aFile), "%s_%d.%s", aName, i, aExtension);
		str_format(aPath, sizeof(aPath), "%s/%s", pFolder, aFile);
	}

	IOHANDLE File = m_pStorage->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	bool Success = false;
	if(File)
	{
		Success = io_write(File, pData, Size) == Size;
		io_close(File);
	}
	free(pData);
	if(!Success)
	{
		log_error("xclient", "could not write '%s'", aPath);
		return ADD_FAILED;
	}

	if(IsMap)
		fs_split_file_extension(aFile, pAddedName, AddedNameSize); // themes are named without .map
	else
		str_copy(pAddedName, aFile, AddedNameSize);
	if(!IsMap)
		Refresh();
	log_info("xclient", "added '%s'", aPath);
	return IsMap ? ADD_MAP : ADD_IMAGE;
}

bool CXcBackgrounds::Remove(const char *pName)
{
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", m_pFolder, pName);
	if(!m_pStorage->RemoveFile(aPath, IStorage::TYPE_SAVE))
		return false;
	if(m_ActiveName == pName)
		UnloadActive();
	Refresh();
	return true;
}

void CXcBackgrounds::UnloadActive()
{
	if(m_pActiveJob)
		m_pActiveJob->Abort();
	m_pActiveJob = nullptr;
	for(IGraphics::CTextureHandle &Frame : m_vFrames)
		m_pGraphics->UnloadTexture(&Frame);
	m_vFrames.clear();
	m_vDelays.clear();
	m_UploadedFrames = 0;
	m_CurrentFrame = 0;
	m_FrameTime = 0.0f;
	m_ActiveName.clear();
	m_ActiveFailed = false;
}

bool CXcBackgrounds::IsLoading() const
{
	return m_pActiveJob != nullptr;
}

void CXcBackgrounds::Update(const char *pName, float Dt, float Speed)
{
	if(m_ActiveName != pName)
	{
		UnloadActive();
		m_ActiveName = pName;
		if(pName[0])
		{
			const bool Gif = str_endswith_nocase(pName, ".gif");
			m_pActiveJob = StartJob(pName, Gif ? m_MaxGifSize : m_MaxPictureSize, false);
			m_ActiveFailed = m_pActiveJob == nullptr;
		}
	}

	// upload a few frames per tick so a long gif does not freeze the menu
	if(m_pActiveJob && m_pActiveJob->Done())
	{
		CDecodeJob *pJob = m_pActiveJob.get();
		if(pJob->State() != IJob::STATE_DONE || !pJob->m_Success)
		{
			m_ActiveFailed = true;
			m_pActiveJob = nullptr;
		}
		else
		{
			if(m_UploadedFrames == 0)
			{
				m_FrameWidth = (float)pJob->m_vFrames[0].m_Width;
				m_FrameHeight = (float)pJob->m_vFrames[0].m_Height;
				m_vDelays = pJob->m_vDelays;
			}
			const size_t End = std::min(m_UploadedFrames + FRAMES_UPLOADED_PER_TICK, pJob->m_vFrames.size());
			for(; m_UploadedFrames < End; m_UploadedFrames++)
				m_vFrames.push_back(m_pGraphics->LoadTextureRawMove(pJob->m_vFrames[m_UploadedFrames], 0, m_ActiveName.c_str()));
			if(m_UploadedFrames == pJob->m_vFrames.size())
				m_pActiveJob = nullptr;
		}
	}

	// gif playback, only over the frames that are already uploaded
	if(m_vFrames.size() > 1 && !m_pActiveJob)
	{
		m_FrameTime += Dt * 1000.0f * Speed;
		// a long hitch should not spin through many frames
		m_FrameTime = std::min(m_FrameTime, 2000.0f);
		while(m_FrameTime >= m_vDelays[m_CurrentFrame])
		{
			m_FrameTime -= m_vDelays[m_CurrentFrame];
			m_CurrentFrame = (m_CurrentFrame + 1) % m_vFrames.size();
		}
	}
}

bool CXcBackgrounds::Render(float Width, float Height, float Alpha)
{
	if(m_vFrames.empty() || m_CurrentFrame >= m_vFrames.size())
		return false;
	// cover: fill the screen, crop what sticks out, keep the aspect ratio
	const float Scale = std::max(Width / m_FrameWidth, Height / m_FrameHeight);
	const float U = Width / (m_FrameWidth * Scale);
	const float V = Height / (m_FrameHeight * Scale);
	m_pGraphics->MapScreen(CScreenRect(0.0f, 0.0f, Width, Height));
	m_pGraphics->TextureSet(m_vFrames[m_CurrentFrame]);
	m_pGraphics->QuadsBegin();
	m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	m_pGraphics->QuadsSetSubset((1.0f - U) * 0.5f, (1.0f - V) * 0.5f, (1.0f + U) * 0.5f, (1.0f + V) * 0.5f);
	const IGraphics::CQuadItem Item(0.0f, 0.0f, Width, Height);
	m_pGraphics->QuadsDrawTL(&Item, 1);
	m_pGraphics->QuadsEnd();
	return true;
}

IGraphics::CTextureHandle CXcBackgrounds::CurrentFrame() const
{
	if(m_CurrentFrame >= m_vFrames.size())
		return IGraphics::CTextureHandle();
	return m_vFrames[m_CurrentFrame];
}

void CXcBackgrounds::RenderQuad(vec2 Center, vec2 Size, float Angle, float Alpha, bool Flip) const
{
	if(m_CurrentFrame >= m_vFrames.size())
		return;
	m_pGraphics->TextureSet(m_vFrames[m_CurrentFrame]);
	m_pGraphics->WrapClamp();
	m_pGraphics->QuadsBegin();
	m_pGraphics->SetColor(1.0f, 1.0f, 1.0f, Alpha);
	if(Flip)
		m_pGraphics->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
	m_pGraphics->QuadsSetRotation(Angle);
	IGraphics::CQuadItem Item(Center.x, Center.y, Size.x, Size.y);
	m_pGraphics->QuadsDraw(&Item, 1);
	m_pGraphics->QuadsEnd();
	m_pGraphics->QuadsSetRotation(0.0f);
	m_pGraphics->WrapNormal();
}
