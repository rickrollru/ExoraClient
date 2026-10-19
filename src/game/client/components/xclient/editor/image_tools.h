#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_IMAGE_TOOLS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_IMAGE_TOOLS_H

class CImageInfo;
class IStorage;

// Picture helpers for the map editor: other formats become RGBA like a png, and upscaling.
namespace XcImageTools
{
// png, jpg, bmp, gif (first frame), tga, psd
bool IsSupportedImage(const char *pFilename);
// false when the file can not be read or decoded; Out becomes RGBA
bool LoadAnyImage(IStorage *pStorage, const char *pFilename, int StorageType, CImageInfo &Out);

enum class EUpscale
{
	SHARP, // pixel art: Scale2x per step, edges stay crisp, no new colors
	SMOOTH, // drawings and photos: Catmull-Rom filtering with premultiplied alpha
};
// Factor 2 or 4; the image must be RGBA. Returns false when the result would be too big.
bool Upscale(CImageInfo &Image, int Factor, EUpscale Mode);
} // namespace XcImageTools

#endif
