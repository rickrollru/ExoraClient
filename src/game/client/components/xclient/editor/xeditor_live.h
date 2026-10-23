// Live map editing: while the map of a tab is played on our own local server, the changed blocks of the tab
// go to the server a few times a second and every player sees them at once, without loading the map again
// (see game/xc_live_edit.h). Only the blocks: new or moved layers need the map loaded again (play again).
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_LIVE_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_LIVE_H

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class CEditor;
class CEditorMap;
class CXcCoop;

class CXcLiveEdit
{
public:
	void Init(CEditor *pEditor, CXcCoop *pCoop)
	{
		m_pEditor = pEditor;
		m_pCoop = pCoop;
	}
	// the tab is played now on the local server, as the map MapName
	void Start(CEditorMap *pMap, uint32_t CoopMapId, const char *pMapName);
	void Stop();
	void Update();
	bool Active() const { return m_pMap != nullptr || m_CoopMapId != 0; }
	// layers were added, removed or resized since the start: their blocks cannot go live
	bool StructureChanged() const { return m_StructureChanged; }

private:
	struct SSnap
	{
		int m_Group;
		int m_Layer;
		int m_Width;
		int m_Height;
		int m_CellSize;
		int m_Flags; // what kind of layer, to notice when another layer took its place
		int m_Image;
		std::string m_Name;
		std::vector<uint8_t> m_vTiles; // CTile per cell, as last sent
		std::vector<uint8_t> m_vExtra; // the tele/speedup/switch/tune data per cell
	};

	CEditor *m_pEditor = nullptr;
	CXcCoop *m_pCoop = nullptr;
	CEditorMap *m_pMap = nullptr;
	uint32_t m_CoopMapId = 0;
	std::string m_MapName;
	std::vector<SSnap> m_vSnaps;
	std::deque<std::vector<uint8_t>> m_vQueue; // packed tiles, one message each
	float m_NextDiff = 0.0f;
	float m_DiffedModified = -1.0f; // the change time of the map at the last look
	bool m_StructureChanged = false;

	CEditorMap *CurrentMap();
	void TakeSnapshot(CEditorMap *pMap);
	void Diff(CEditorMap *pMap);
};

#endif
