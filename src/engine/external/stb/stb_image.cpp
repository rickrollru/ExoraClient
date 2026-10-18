// stb_image v2.30 (public domain / MIT, https://github.com/nothings/stb), used by XClient for custom menu backgrounds.
// Only the formats the menu accepts, decoding from memory.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"

#include "stb_image_xc.h"

#include <cstring>
#include <vector>

// XClient: decodes the frames of a gif one by one and hands each one to the callback, so a big gif
// never has to sit in memory at full size with all its frames.
int XcStbiGifFrames(const unsigned char *pData, int Size, XC_STBI_GIF_FRAME_CALLBACK pfnFrame, void *pUser)
{
	stbi__context Context;
	stbi__start_mem(&Context, pData, Size);
	if(!stbi__gif_test(&Context))
		return 0;

	stbi__gif Gif;
	memset(&Gif, 0, sizeof(Gif));
	// "restore to previous" disposal needs the frame from two steps back
	std::vector<stbi_uc> aHistory[2];
	int Frames = 0;
	int Comp;
	while(true)
	{
		stbi_uc *pFrame = stbi__gif_load_next(&Context, &Gif, &Comp, 4, Frames >= 2 ? aHistory[Frames % 2].data() : nullptr);
		if(pFrame == nullptr || pFrame == (stbi_uc *)&Context)
			break;
		const size_t Stride = (size_t)Gif.w * Gif.h * 4;
		Frames++;
		if(!pfnFrame(pUser, pFrame, Gif.w, Gif.h, Gif.delay))
			break;
		// frame k is kept in aHistory[k % 2], so frame n - 2 is in aHistory[n % 2] when frame n is decoded
		aHistory[(Frames - 1) % 2].assign(pFrame, pFrame + Stride);
	}
	STBI_FREE(Gif.out);
	STBI_FREE(Gif.history);
	STBI_FREE(Gif.background);
	return Frames;
}
