/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_MAPLAYERS_H
#define GAME_CLIENT_COMPONENTS_MAPLAYERS_H

#include <utility>
#include <vector>

#include <game/client/component.h>
#include <game/client/components/envelope_state.h>
#include <game/map/map_renderer.h>

class CCamera;
class CLayers;
class CMapImages;
class ColorRGBA;

class CMapLayers : public CComponent
{
	// TClient
	friend class CScriptRunner;
	friend class COutlines;

	friend class CBackground;
	friend class CMenuBackground;

	CLayers *m_pLayers;
	CMapImages *m_pImages;
	ERenderType m_Type;
	bool m_OnlineOnly;

public:
	CMapLayers(ERenderType Type, bool OnlineOnly = true);
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnMapLoad() override;

	virtual CCamera *GetCurCamera();

	CEnvelopeState &EnvEvaluator() { return m_EnvEvaluator; }
	// XClient: live map editing changed this layer; built again a few times a second at most
	void XcMarkDirty(int Group, int Layer) { m_vXcDirty.emplace_back(Group, Layer); }

private:
	CRenderLayerParams m_Params;
	CMapRenderer m_MapRenderer;
	CEnvelopeState m_EnvEvaluator;
	std::vector<std::pair<int, int>> m_vXcDirty; // XClient
	int64_t m_XcLastReload = 0;
};

#endif
