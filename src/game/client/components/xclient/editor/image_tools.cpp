#include "image_tools.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/external/stb/stb_image.h>
#include <engine/image.h>
#include <engine/storage.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace XcImageTools
{
// the biggest side after upscaling, DDNet maps store raw RGBA
static constexpr int MAX_SIDE = 8192;

bool IsSupportedImage(const char *pFilename)
{
	static const char *const s_apExtensions[] = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".psd"};
	for(const char *pExtension : s_apExtensions)
		if(str_endswith_nocase(pFilename, pExtension))
			return true;
	return false;
}

bool LoadAnyImage(IStorage *pStorage, const char *pFilename, int StorageType, CImageInfo &Out)
{
	void *pFile;
	unsigned FileSize;
	if(!pStorage->ReadFile(pFilename, StorageType, &pFile, &FileSize))
		return false;
	int Width, Height, Channels;
	stbi_uc *pPixels = stbi_load_from_memory(static_cast<const stbi_uc *>(pFile), (int)FileSize, &Width, &Height, &Channels, 4);
	free(pFile);
	if(!pPixels)
		return false;
	if(Width <= 0 || Height <= 0 || Width > MAX_SIDE || Height > MAX_SIDE)
	{
		stbi_image_free(pPixels);
		return false;
	}
	Out.Free();
	Out.m_Width = Width;
	Out.m_Height = Height;
	Out.m_Format = CImageInfo::FORMAT_RGBA;
	Out.Allocate();
	std::memcpy(Out.m_pData, pPixels, (size_t)Width * Height * 4);
	stbi_image_free(pPixels);
	return true;
}

static bool SamePixel(const uint8_t *pA, const uint8_t *pB)
{
	return std::memcmp(pA, pB, 4) == 0;
}

// Scale2x (EPX): every pixel becomes 2x2, corners take the neighbor color when two neighbors agree
static void Scale2x(const uint8_t *pSrc, int W, int H, uint8_t *pDst)
{
	const auto Px = [&](int x, int y) {
		x = std::clamp(x, 0, W - 1);
		y = std::clamp(y, 0, H - 1);
		return pSrc + ((size_t)y * W + x) * 4;
	};
	const int DstW = W * 2;
	for(int y = 0; y < H; y++)
	{
		for(int x = 0; x < W; x++)
		{
			const uint8_t *pP = Px(x, y);
			const uint8_t *pA = Px(x, y - 1);
			const uint8_t *pB = Px(x + 1, y);
			const uint8_t *pC = Px(x - 1, y);
			const uint8_t *pD = Px(x, y + 1);
			const uint8_t *apOut[4] = {pP, pP, pP, pP};
			if(!SamePixel(pA, pD) && !SamePixel(pC, pB))
			{
				if(SamePixel(pC, pA))
					apOut[0] = pA;
				if(SamePixel(pA, pB))
					apOut[1] = pB;
				if(SamePixel(pD, pC))
					apOut[2] = pC;
				if(SamePixel(pB, pD))
					apOut[3] = pD;
			}
			uint8_t *pRow0 = pDst + ((size_t)(y * 2) * DstW + x * 2) * 4;
			uint8_t *pRow1 = pRow0 + (size_t)DstW * 4;
			std::memcpy(pRow0, apOut[0], 4);
			std::memcpy(pRow0 + 4, apOut[1], 4);
			std::memcpy(pRow1, apOut[2], 4);
			std::memcpy(pRow1 + 4, apOut[3], 4);
		}
	}
}

static float CatmullRom(float t, int i)
{
	// weights of the 4 taps for the fraction t
	const float t2 = t * t, t3 = t2 * t;
	switch(i)
	{
	case 0: return -0.5f * t3 + t2 - 0.5f * t;
	case 1: return 1.5f * t3 - 2.5f * t2 + 1.0f;
	case 2: return -1.5f * t3 + 2.0f * t2 + 0.5f * t;
	default: return 0.5f * t3 - 0.5f * t2;
	}
}

static void SmoothScale(const uint8_t *pSrc, int W, int H, int Factor, uint8_t *pDst)
{
	// premultiplied alpha keeps transparent pixels from bleeding dark edges
	std::vector<float> vPremul((size_t)W * H * 4);
	for(size_t i = 0; i < (size_t)W * H; i++)
	{
		const float a = pSrc[i * 4 + 3] / 255.0f;
		for(int c = 0; c < 3; c++)
			vPremul[i * 4 + c] = pSrc[i * 4 + c] / 255.0f * a;
		vPremul[i * 4 + 3] = a;
	}
	const int DstW = W * Factor, DstH = H * Factor;
	for(int y = 0; y < DstH; y++)
	{
		const float sy = (y + 0.5f) / Factor - 0.5f;
		const int y0 = (int)std::floor(sy);
		const float fy = sy - y0;
		for(int x = 0; x < DstW; x++)
		{
			const float sx = (x + 0.5f) / Factor - 0.5f;
			const int x0 = (int)std::floor(sx);
			const float fx = sx - x0;
			float aSum[4] = {0, 0, 0, 0};
			for(int j = 0; j < 4; j++)
			{
				const int yy = std::clamp(y0 - 1 + j, 0, H - 1);
				const float wy = CatmullRom(fy, j);
				for(int i = 0; i < 4; i++)
				{
					const int xx = std::clamp(x0 - 1 + i, 0, W - 1);
					const float w = wy * CatmullRom(fx, i);
					const float *pPx = &vPremul[((size_t)yy * W + xx) * 4];
					for(int c = 0; c < 4; c++)
						aSum[c] += pPx[c] * w;
				}
			}
			const float a = std::clamp(aSum[3], 0.0f, 1.0f);
			uint8_t *pOut = pDst + ((size_t)y * DstW + x) * 4;
			for(int c = 0; c < 3; c++)
				pOut[c] = a > 0.0f ? (uint8_t)std::round(std::clamp(aSum[c] / a, 0.0f, 1.0f) * 255.0f) : 0;
			pOut[3] = (uint8_t)std::round(a * 255.0f);
		}
	}
}

bool Upscale(CImageInfo &Image, int Factor, EUpscale Mode)
{
	if(Image.m_Format != CImageInfo::FORMAT_RGBA || (Factor != 2 && Factor != 4))
		return false;
	const int W = (int)Image.m_Width, H = (int)Image.m_Height;
	if(W * Factor > MAX_SIDE || H * Factor > MAX_SIDE)
		return false;

	CImageInfo Result;
	Result.m_Width = (size_t)W * Factor;
	Result.m_Height = (size_t)H * Factor;
	Result.m_Format = CImageInfo::FORMAT_RGBA;
	Result.Allocate();
	if(Mode == EUpscale::SHARP)
	{
		if(Factor == 2)
			Scale2x(Image.m_pData, W, H, Result.m_pData);
		else
		{
			// Scale4x = Scale2x twice
			std::vector<uint8_t> vTwice((size_t)W * H * 16);
			Scale2x(Image.m_pData, W, H, vTwice.data());
			Scale2x(vTwice.data(), W * 2, H * 2, Result.m_pData);
		}
	}
	else
		SmoothScale(Image.m_pData, W, H, Factor, Result.m_pData);
	Image.Free(); // the move below does not free the old pixels
	Image = std::move(Result);
	return true;
}
} // namespace XcImageTools
