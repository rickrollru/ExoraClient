#ifndef GAME_MAP_MAP_RENDERER_H
#define GAME_MAP_MAP_RENDERER_H

#include <engine/map.h>

#include <game/layers.h>
#include <game/map/render_component.h>
#include <game/map/render_layer.h>

typedef std::function<void(int GroupId, int NumGroups, int LayerId, int NumLayers)> FCallbackMapRendererInit;

class CMapRenderer : public CRenderComponent
{
public:
	CMapRenderer() = default;

	void Clear();
	void Load(ERenderType Type, CLayers *pLayers, IMapImages *pMapImages, const IEnvelopeEval *pEnvelopeEval, std::optional<FCallbackMapRendererInit> CallbackMapRendererInitOptional);
	void Render(const CRenderLayerParams &Params);
	void XcReloadLayer(int Group, int Layer); // XClient: live map editing
	// XClient: 3D view, called when the group with the game layer starts (block sides under its layers)
	static std::function<void()> ms_fnXcGameGroup;

private:
	int GetLayerType(const CMapItemLayer *pLayer) const;

	std::vector<std::unique_ptr<CRenderLayer>> m_vpRenderLayers;
	int m_XcGameGroup = -1; // XClient
};

#endif
