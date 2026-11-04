#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_PERF_DETAIL_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_PERF_DETAIL_H

#include <cstddef>
#include <cstdint>

class CComponent;
class IConsole;
class IGraphics;

// What each part of the game costs per frame (xc_perf_detail with xc_perf_log): the CPU time of every component's
// OnRender and, with the render module, the GPU time of what it drew (timer queries between marks). Every 5 seconds
// the heaviest ones go to the console as "xc_perf_detail" lines.
namespace XcPerfDetail
{
	bool Active();
	// before a component draws: the GPU mark and the CPU start time
	int64_t Begin(IGraphics *pGraphics, size_t Index);
	void End(const CComponent *pComponent, size_t Index, int64_t Start);
	// after all components: the closing mark, and the report when its time has come
	void FrameEnd(IGraphics *pGraphics, IConsole *pConsole);
}

#endif
