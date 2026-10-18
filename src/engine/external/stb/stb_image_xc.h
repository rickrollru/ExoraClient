#ifndef ENGINE_EXTERNAL_STB_STB_IMAGE_XC_H
#define ENGINE_EXTERNAL_STB_STB_IMAGE_XC_H

// XClient addition to stb_image: frame by frame gif decoding.
// The callback gets each composed RGBA frame and its delay; return false to stop.
typedef bool (*XC_STBI_GIF_FRAME_CALLBACK)(void *pUser, const unsigned char *pRgba, int Width, int Height, int DelayMs);
int XcStbiGifFrames(const unsigned char *pData, int Size, XC_STBI_GIF_FRAME_CALLBACK pfnFrame, void *pUser);

#endif
