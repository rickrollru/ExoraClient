// Animated gifs in maps. The DDNet map format has no animated images, so the frames are packed into one
// image and every frame gets its own quad at the same place. A step color envelope turns the alpha of a
// quad on only for the time of its frame; all quads share it with different time offsets. Envelopes are a
// standard map feature, so the gif plays on every DDNet client and server.
#include "image_tools.h"

#include <base/fs.h>
#include <base/log.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/external/stb/stb_image_xc.h>
#include <engine/graphics.h>
#include <engine/storage.h>

#include <game/editor/editor.h>
#include <game/editor/mapitems/envelope.h>
#include <game/editor/mapitems/image.h>
#include <game/editor/mapitems/layer_group.h>
#include <game/editor/mapitems/layer_quads.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

namespace
{
constexpr int MAX_FRAMES = 96;
constexpr int MAX_ATLAS = 2048;
constexpr int PADDING = 1; // transparent border around every frame, keeps neighbors from bleeding in

struct SGifFrame
{
	std::vector<uint8_t> m_vPixels;
	int m_DelayMs;
};

struct SGifDecode
{
	int m_Width = 0;
	int m_Height = 0;
	std::vector<SGifFrame> m_vFrames;
	size_t m_Bytes = 0;
};

bool OnGifFrame(void *pUser, const unsigned char *pRgba, int Width, int Height, int DelayMs)
{
	SGifDecode *pDecode = static_cast<SGifDecode *>(pUser);
	pDecode->m_Width = Width;
	pDecode->m_Height = Height;
	SGifFrame Frame;
	Frame.m_vPixels.assign(pRgba, pRgba + (size_t)Width * Height * 4);
	// browsers treat tiny delays as 100 ms, gifs are made for that
	Frame.m_DelayMs = DelayMs <= 10 ? 100 : DelayMs;
	pDecode->m_Bytes += Frame.m_vPixels.size();
	pDecode->m_vFrames.push_back(std::move(Frame));
	return pDecode->m_Bytes < 512u * 1024u * 1024u;
}

// bilinear resize with premultiplied alpha
std::vector<uint8_t> Resize(const std::vector<uint8_t> &vSrc, int W, int H, int NewW, int NewH)
{
	std::vector<uint8_t> vDst((size_t)NewW * NewH * 4);
	for(int y = 0; y < NewH; y++)
	{
		const float sy = std::clamp((y + 0.5f) * H / NewH - 0.5f, 0.0f, H - 1.0f);
		const int y0 = (int)sy, y1 = std::min(y0 + 1, H - 1);
		const float fy = sy - y0;
		for(int x = 0; x < NewW; x++)
		{
			const float sx = std::clamp((x + 0.5f) * W / NewW - 0.5f, 0.0f, W - 1.0f);
			const int x0 = (int)sx, x1 = std::min(x0 + 1, W - 1);
			const float fx = sx - x0;
			float aSum[4] = {};
			const int aX[4] = {x0, x1, x0, x1};
			const int aY[4] = {y0, y0, y1, y1};
			const float aW[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
			for(int i = 0; i < 4; i++)
			{
				const uint8_t *pPx = &vSrc[((size_t)aY[i] * W + aX[i]) * 4];
				const float a = pPx[3] / 255.0f;
				for(int c = 0; c < 3; c++)
					aSum[c] += pPx[c] * a * aW[i];
				aSum[3] += a * aW[i];
			}
			uint8_t *pOut = &vDst[((size_t)y * NewW + x) * 4];
			for(int c = 0; c < 3; c++)
				pOut[c] = aSum[3] > 0.0f ? (uint8_t)std::clamp(aSum[c] / aSum[3], 0.0f, 255.0f) : 0;
			pOut[3] = (uint8_t)std::round(std::clamp(aSum[3], 0.0f, 1.0f) * 255.0f);
		}
	}
	return vDst;
}
} // namespace

bool CEditor::XcCallbackAddGif(const char *pFilename, int StorageType, void *pUser)
{
	CEditor *pEditor = static_cast<CEditor *>(pUser);
	CEditorMap *pMap = pEditor->Map();

	void *pFile;
	unsigned FileSize;
	if(!pEditor->Storage()->ReadFile(pFilename, StorageType, &pFile, &FileSize))
	{
		pEditor->ShowFileDialogError("Failed to read the file '%s'.", pFilename);
		return false;
	}
	SGifDecode Decode;
	XcStbiGifFrames(static_cast<const unsigned char *>(pFile), (int)FileSize, OnGifFrame, &Decode);
	free(pFile);
	if(Decode.m_vFrames.empty() || Decode.m_Width <= 0 || Decode.m_Height <= 0)
	{
		pEditor->ShowFileDialogError("'%s' is not an animated gif.", pFilename);
		return false;
	}
	if(pMap->m_vpImages.size() >= MAX_MAPIMAGES)
	{
		pEditor->ShowFileDialogError("The map already has the maximum number of images.");
		return false;
	}

	// too many frames: keep every n-th frame, the skipped time goes to the kept ones
	std::vector<SGifFrame> &vFrames = Decode.m_vFrames;
	if((int)vFrames.size() > MAX_FRAMES)
	{
		const float Step = vFrames.size() / (float)MAX_FRAMES;
		std::vector<SGifFrame> vKept;
		for(int i = 0; i < MAX_FRAMES; i++)
		{
			const size_t First = (size_t)(i * Step), Last = std::min((size_t)((i + 1) * Step), vFrames.size());
			SGifFrame Frame = std::move(vFrames[First]);
			for(size_t j = First + 1; j < Last; j++)
				Frame.m_DelayMs += vFrames[j].m_DelayMs;
			vKept.push_back(std::move(Frame));
		}
		vFrames = std::move(vKept);
	}
	const int NumFrames = (int)vFrames.size();

	// grid of frames, as square as possible, shrunk to fit the atlas limit
	int FrameW = Decode.m_Width, FrameH = Decode.m_Height;
	const int Columns = std::max(1, (int)std::ceil(std::sqrt((float)NumFrames * FrameH / FrameW)));
	const int Rows = (NumFrames + Columns - 1) / Columns;
	const float Fit = std::min(1.0f, std::min((MAX_ATLAS / (float)Columns - PADDING * 2) / FrameW, (MAX_ATLAS / (float)Rows - PADDING * 2) / FrameH));
	if(Fit < 1.0f)
	{
		const int NewW = std::max(1, (int)(FrameW * Fit)), NewH = std::max(1, (int)(FrameH * Fit));
		for(SGifFrame &Frame : vFrames)
			Frame.m_vPixels = Resize(Frame.m_vPixels, FrameW, FrameH, NewW, NewH);
		FrameW = NewW;
		FrameH = NewH;
	}
	const int CellW = FrameW + PADDING * 2, CellH = FrameH + PADDING * 2;

	std::shared_ptr<CEditorImage> pImg = std::make_shared<CEditorImage>(pMap);
	pImg->m_Width = (size_t)CellW * Columns;
	pImg->m_Height = (size_t)CellH * Rows;
	pImg->m_Format = CImageInfo::FORMAT_RGBA;
	pImg->AllocateFillZero();
	for(int i = 0; i < NumFrames; i++)
	{
		const int CellX = (i % Columns) * CellW + PADDING, CellY = (i / Columns) * CellH + PADDING;
		for(int y = 0; y < FrameH; y++)
			std::memcpy(pImg->m_pData + ((size_t)(CellY + y) * pImg->m_Width + CellX) * 4, vFrames[i].m_vPixels.data() + (size_t)y * FrameW * 4, (size_t)FrameW * 4);
	}

	// a free name for the image, the group and the envelopes
	char aName[64];
	fs_split_file_extension(fs_filename(pFilename), aName, sizeof(aName));
	char aImageName[sizeof(pImg->m_aName)];
	str_copy(aImageName, aName);
	for(int Suffix = 2; std::any_of(pMap->m_vpImages.begin(), pMap->m_vpImages.end(), [&](const auto &pOther) { return str_comp(pOther->m_aName, aImageName) == 0; }); Suffix++)
		str_format(aImageName, sizeof(aImageName), "%s_%d", aName, Suffix);
	str_copy(pImg->m_aName, aImageName);
	pImg->m_External = false;
	pImg->m_Texture = pEditor->Graphics()->LoadTextureRaw(*pImg, 0, aImageName);
	pMap->m_vpImages.push_back(pImg);
	pMap->SortImages();
	const int ImageIndex = (int)(std::find(pMap->m_vpImages.begin(), pMap->m_vpImages.end(), pImg) - pMap->m_vpImages.begin());

	// frame start times and the loop length
	std::vector<int> vStart(NumFrames);
	int Total = 0;
	for(int i = 0; i < NumFrames; i++)
	{
		vStart[i] = Total;
		Total += vFrames[i].m_DelayMs;
	}

	// one step envelope per distinct frame length: visible from 0 to the length, hidden until the loop ends
	std::map<int, int> EnvelopeOfDelay;
	if(NumFrames > 1)
	{
		const int One = f2fx(1.0f);
		for(const SGifFrame &Frame : vFrames)
		{
			if(EnvelopeOfDelay.count(Frame.m_DelayMs))
				continue;
			std::shared_ptr<CEnvelope> pEnv = std::make_shared<CEnvelope>(CEnvelope::EType::COLOR);
			str_format(pEnv->m_aName, sizeof(pEnv->m_aName), "gif %s %dms", aImageName, Frame.m_DelayMs);
			pEnv->AddPoint(CFixedTime(0), {One, One, One, One});
			pEnv->AddPoint(CFixedTime(Frame.m_DelayMs), {One, One, One, 0});
			if(Frame.m_DelayMs < Total)
				pEnv->AddPoint(CFixedTime(Total), {One, One, One, 0});
			for(auto &Point : pEnv->m_vPoints)
				Point.m_Curvetype = CURVETYPE_STEP;
			pEnv->m_Synchronized = true; // the same frame for everybody on the server
			pMap->m_vpEnvelopes.push_back(pEnv);
			EnvelopeOfDelay[Frame.m_DelayMs] = (int)pMap->m_vpEnvelopes.size() - 1;
		}
	}

	// the group and the layer with one quad per frame, all in the middle of the view
	std::shared_ptr<CLayerGroup> pGroup = pMap->NewGroup();
	str_copy(pGroup->m_aName, aImageName, sizeof(pGroup->m_aName));
	std::shared_ptr<CLayerQuads> pLayer = std::make_shared<CLayerQuads>(pMap);
	str_copy(pLayer->m_aName, "gif", sizeof(pLayer->m_aName));
	pLayer->m_Image = ImageIndex;
	pGroup->AddLayer(pLayer);

	const vec2 Center = pEditor->MapView()->GetWorldOffset();
	const float HalfW = Decode.m_Width / 2.0f, HalfH = Decode.m_Height / 2.0f;
	for(int i = 0; i < NumFrames; i++)
	{
		CQuad Quad;
		Quad.m_PosEnv = -1;
		Quad.m_PosEnvOffset = 0;
		Quad.m_ColorEnv = NumFrames > 1 ? EnvelopeOfDelay[vFrames[i].m_DelayMs] : -1;
		// the envelope starts the frame at time 0, shift it to the start of this frame
		Quad.m_ColorEnvOffset = NumFrames > 1 ? (Total - vStart[i]) % Total : 0;
		const float aX[4] = {Center.x - HalfW, Center.x + HalfW, Center.x - HalfW, Center.x + HalfW};
		const float aY[4] = {Center.y - HalfH, Center.y - HalfH, Center.y + HalfH, Center.y + HalfH};
		const float U0 = ((i % Columns) * CellW + PADDING) / (float)pImg->m_Width;
		const float V0 = ((i / Columns) * CellH + PADDING) / (float)pImg->m_Height;
		const float U1 = U0 + FrameW / (float)pImg->m_Width;
		const float V1 = V0 + FrameH / (float)pImg->m_Height;
		const float aU[4] = {U0, U1, U0, U1};
		const float aV[4] = {V0, V0, V1, V1};
		for(int c = 0; c < 4; c++)
		{
			Quad.m_aPoints[c].x = f2fx(aX[c]);
			Quad.m_aPoints[c].y = f2fx(aY[c]);
			Quad.m_aTexcoords[c].x = f2fx(aU[c]);
			Quad.m_aTexcoords[c].y = f2fx(aV[c]);
			Quad.m_aColors[c] = CColor{255, 255, 255, 255};
		}
		Quad.m_aPoints[4].x = f2fx(Center.x);
		Quad.m_aPoints[4].y = f2fx(Center.y);
		pLayer->m_vQuads.push_back(Quad);
	}

	pMap->OnModify();
	log_info("editor", "gif '%s': %d frames %dx%d, loop %d ms, image %dx%d, at %.0f %.0f", pFilename, NumFrames, FrameW, FrameH, Total, (int)pImg->m_Width, (int)pImg->m_Height, Center.x, Center.y);
	pEditor->OnDialogClose();
	return true;
}
