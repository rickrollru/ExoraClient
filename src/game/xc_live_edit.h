// XClient: live map editing on our own server. The editor sends changed tiles, the server puts them into its
// map and physics and sends them on to all players; the clients put them into their map, physics and drawing.
// Only between Exora Client and its own server (sv_xc_live_edit 1); DDNet ignores the unknown messages.
#ifndef GAME_XC_LIVE_EDIT_H
#define GAME_XC_LIVE_EDIT_H

#include <cstdint>
#include <vector>

class CLayers;
class CPacker;
class CUnpacker;

namespace XcLiveEdit
{
// the raw data of one message stays under the network chunk limit (1023 bytes with the header)
constexpr int MAX_MESSAGE_DATA = 760;

// one run of changed cells of a tile layer: CTile, then the tele/speedup/switch/tune data of the layer
struct SRun
{
	int m_Start;
	int m_Count;
	const uint8_t *m_pCells;
};

// a changed tile, for the side effects (entities)
struct SChange
{
	int m_X;
	int m_Y;
	int m_OldIndex; // the game/front index, or the switch type
	int m_NewIndex;
	bool m_Switch; // m_OldIndex/m_NewIndex are switch types
	bool m_Front;
};

// message layout after the message id: [group][layer][cell size][runs] then per run [start][count][raw cells]
bool Read(CUnpacker *pUnpacker, int *pGroup, int *pLayer, int *pCellSize, std::vector<SRun> *pvRuns);
void Write(CPacker *pPacker, int Group, int Layer, int CellSize, const std::vector<SRun> &vRuns);

// the size of the extra data of a physics layer (0 for a normal tile layer), -1 when the layer is not a tile layer
int ExtraSize(CLayers *pLayers, int Group, int Layer);
// puts the cells into the map data in place; false when the layer does not fit (another map)
bool Apply(CLayers *pLayers, int Group, int Layer, int CellSize, const std::vector<SRun> &vRuns, std::vector<SChange> *pvChanges);
} // namespace XcLiveEdit

#endif
