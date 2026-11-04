#include "perf_detail.h"

#include <base/str.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/xc_render.h>

#include <game/client/component.h>

#include <algorithm>
#include <string>
#include <typeinfo>
#include <vector>

namespace
{
	struct SEntry
	{
		std::string m_Name;
		double m_CpuMs = 0.0;
	};
	std::vector<SEntry> s_vEntries;
	int s_Frames = 0;
	int64_t s_WindowStart = 0;

	// "class CPlayers" (MSVC), "8CPlayers" (GCC, Clang): the bare class name
	std::string ClassName(const CComponent *pComponent)
	{
		const char *pName = typeid(*pComponent).name();
		if(str_startswith(pName, "class "))
			pName += 6;
		else if(str_startswith(pName, "struct "))
			pName += 7;
		while(*pName >= '0' && *pName <= '9')
			pName++;
		return pName;
	}
}

bool XcPerfDetail::Active()
{
	return g_Config.m_XcPerfLog && g_Config.m_XcPerfDetail;
}

int64_t XcPerfDetail::Begin(IGraphics *pGraphics, size_t Index)
{
	pGraphics->XcPerfMark((int)Index);
	return time_get_nanoseconds().count();
}

void XcPerfDetail::End(const CComponent *pComponent, size_t Index, int64_t Start)
{
	const int64_t Now = time_get_nanoseconds().count();
	if(s_vEntries.size() <= Index)
		s_vEntries.resize(Index + 1);
	SEntry &Entry = s_vEntries[Index];
	if(Entry.m_Name.empty())
		Entry.m_Name = ClassName(pComponent);
	Entry.m_CpuMs += (Now - Start) / 1000000.0;
}

void XcPerfDetail::FrameEnd(IGraphics *pGraphics, IConsole *pConsole)
{
	pGraphics->XcPerfMark(-1);
	s_Frames++;
	const int64_t Now = time_get_nanoseconds().count();
	if(s_WindowStart == 0)
		s_WindowStart = Now;
	if(Now - s_WindowStart < 5000000000LL || s_Frames == 0)
		return;
	s_WindowStart = Now;

	std::vector<double> vGpu(XcRender::MAX_PERF_SCOPES, 0.0);
	const int GpuFrames = XcRender::PerfTake(vGpu.data(), (int)vGpu.size());
	struct SLine
	{
		size_t m_Index;
		double m_Cpu, m_Gpu;
	};
	std::vector<SLine> vLines;
	double TotalCpu = 0.0, TotalGpu = 0.0;
	for(size_t i = 0; i < s_vEntries.size(); i++)
	{
		const double Cpu = s_vEntries[i].m_CpuMs / s_Frames;
		const double Gpu = GpuFrames > 0 && i < vGpu.size() ? vGpu[i] / GpuFrames : 0.0;
		TotalCpu += Cpu;
		TotalGpu += Gpu;
		vLines.push_back({i, Cpu, Gpu});
		s_vEntries[i].m_CpuMs = 0.0;
	}
	std::sort(vLines.begin(), vLines.end(), [](const SLine &a, const SLine &b) { return std::max(a.m_Cpu, a.m_Gpu) > std::max(b.m_Cpu, b.m_Gpu); });
	char aBuf[256];
	if(GpuFrames > 0)
		str_format(aBuf, sizeof(aBuf), "per frame: cpu %.2f ms, gpu %.2f ms (the heaviest parts below)", TotalCpu, TotalGpu);
	else
		str_format(aBuf, sizeof(aBuf), "per frame: cpu %.2f ms, gpu not measured (needs the render module, OpenGL 3.3)", TotalCpu);
	pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "xc_perf_detail", aBuf);
	for(size_t i = 0; i < vLines.size() && i < 12; i++)
	{
		const SLine &Line = vLines[i];
		if(std::max(Line.m_Cpu, Line.m_Gpu) < 0.02)
			break;
		if(GpuFrames > 0)
			str_format(aBuf, sizeof(aBuf), "  %-28s cpu %6.2f ms  gpu %6.2f ms", s_vEntries[Line.m_Index].m_Name.c_str(), Line.m_Cpu, Line.m_Gpu);
		else
			str_format(aBuf, sizeof(aBuf), "  %-28s cpu %6.2f ms", s_vEntries[Line.m_Index].m_Name.c_str(), Line.m_Cpu);
		pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "xc_perf_detail", aBuf);
	}
	s_Frames = 0;
}
