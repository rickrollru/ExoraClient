// The map generator's bot: finds a way through a part of a map on the real DDNet physics (the character core
// of the game), running, jumping and hooking; touching freeze or death ends a try. Only in the editor, offline.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_SOLVER_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_XEDITOR_SOLVER_H

#include <base/vmath.h>

#include <game/mapitems.h>
#include <game/gamecore.h>

#include <atomic>
#include <string>
#include <memory>
#include <mutex>
#include <vector>

namespace XcSolver
{
struct SStep
{
	int m_Direction; // -1, 0, 1
	bool m_Jump; // pressed at the start of the step
	int m_Hook; // 0 released, 1 held, 2 pressed again
	vec2 m_Target; // where the mouse points (relative)
	int m_Ticks = 5; // how long the step lasts
};

struct SResult
{
	bool m_Found = false;
	std::vector<vec2> m_vPath; // positions of the tee along the way
	std::vector<SStep> m_vSteps; // the inputs, one per step
	int m_Ticks = 0; // how long the way takes (50 ticks a second)
	int m_Hooks = 0; // hooks used
	int m_Jumps = 0; // jumps used
	int m_Explored = 0; // tries looked at
	int m_HintsPassed = 0; // hints passed by the best way
	int m_Repairs = 0; // hints reached only by the precise search
	vec2 m_Closest = vec2(0.0f, 0.0f); // where it got closest to the goal when not found

	int m_Width = 0;
	int m_Height = 0;
	// the heat map: how hard each point of m_vPath is, 0..1 (the share of small mistakes there that end in freeze)
	std::vector<float> m_vDifficulty;
	int m_Restarts = 0; // places passed only after starting again a few hints before them
	// how much room the way has at each point: cells to the nearest wall or freeze (1: a one cell gap)
	std::vector<uint8_t> m_vRoom;
	// tower skips: cells of frozen tees the way stood on or hooked
	std::vector<vec2> m_vTowerSpots;
	// the way is made of pieces when the bot could not pass a place and went on after it: where each piece starts
	// in m_vPath, and the places it could not pass
	std::vector<int> m_vBreaks;
	std::vector<vec2> m_vStuck;
	// the hints set along the route when the mapper gave none
	std::vector<vec2> m_vAutoHints;
	// a text report of the search for the mapper to send: hints reached, why tries ended, the place it stopped
	std::string m_Log;
};

// how one search goes about it: the search is sensitive to these (one setting gets far on a map where another
// gets stuck early), so several searches with different settings run at once, one per processor core
struct SVariant
{
	int m_Cap = 100; // different states kept per cell
	int m_CapFreeze = 500; // the same near freeze, where the tee has to be precise
	int m_Aim = 6; // hooks aimed at the blocks that pull best towards the way ahead
	int m_AimFreeze = 8; // the same near freeze
	bool m_AimAround = true; // also one aimed hook in each of 8 directions (to brake and turn)
	bool m_WideFixed = true; // fixed hook directions also sideways, not only upwards
	float m_TickWeight = 5.0f; // how much being quick counts against getting closer
	int m_RingFreeze = 0; // near freeze also hooks all around, this many directions evenly (0: off)
	size_t m_MaxNodes = 6000000; // places kept, for the memory
};

// a search running in a thread: copies the tiles first, so the map can change meanwhile
class CJob
{
public:
	std::atomic<bool> m_Done = false;
	std::atomic<bool> m_Cancel = false;
	std::atomic<int> m_Progress = 0; // tries looked at so far
	// live view while it searches: the best way so far and the places it tried (under the mutex)
	std::mutex m_LiveMutex;
	std::vector<vec2> m_vLivePath;
	std::vector<vec2> m_vLiveTried;
	std::atomic<int> m_LiveTicks = 0; // game time of the best way so far
	std::atomic<int> m_LiveHints = 0; // hints passed by the best way so far
	SResult m_Result;
	bool m_HasStart = false;
	bool m_HasGoal = false;

	int m_Width = 0;
	int m_Height = 0;
	std::vector<CTile> m_vGame;
	std::vector<CTile> m_vFront;
	std::vector<CTeleTile> m_vTele; // empty without a tele layer
	std::vector<CTuneTile> m_vTune; // empty without a tune layer
	std::vector<CTuningParams> m_vTuning; // per tune zone (0: the whole map), empty: the default physics
	vec2 m_Start;
	std::vector<vec2> m_vHints; // points the way has to pass, in this order, before the finish
	int m_MaxTries = 200000; // tries without a new hint before giving up
	// tower skip check: frozen tees lie in freeze pits up to this many cells deep (0: off)
	int m_TowerDepth = 0;
	// a softer check: freeze counts only after the middle of the tee was in it for 3 ticks in a row
	bool m_Forgiving = false;
	// not a human run: finer inputs and any hook direction, the fastest way the physics allows
	bool m_Ideal = false;
	SVariant m_Variant;
};

// a search from the spawn (or start line) to the finish line of the layers; the tiles are copied
// the tune zones: the tune layer and the settings of the map ("tune gravity 0.3", "tune_zone 2 gravity 0.1")
std::shared_ptr<CJob> Start(const CTile *pGame, const CTile *pFront, const CTeleTile *pTele, int Width, int Height, int MaxTries, const std::vector<vec2> &vHints = {}, int TowerDepth = 0, bool Forgiving = false, bool Ideal = false, const CTuneTile *pTune = nullptr, const std::vector<std::string> &vSettings = {});

// the ideal route: no physics at all, the shortest line from the spawn through the hints (in their order) to the
// finish that keeps clear of walls, freeze and death, as if the tee could fly anywhere
struct SRoute
{
	bool m_HasStart = false;
	bool m_HasGoal = false;
	int m_FailedHint = -1; // the hint that cannot be reached (the hints count, then the finish), -1: none
	std::vector<vec2> m_vPath;
	float m_Length = 0.0f; // in pixels
};
SRoute IdealRoute(const CTile *pGame, const CTile *pFront, int Width, int Height, const std::vector<vec2> &vHints);

// how hard the map is on the KoG scale (Easy 1 .. Insane 3, each with 1 to 3 stars: easier, middle, harder), from
// the difficulty along the way the bot found; fitted to 19 ranked KoG maps (off by 0.7 of a tier on average)
struct SRating
{
	bool m_Valid = false;
	float m_Score = 0.0f; // 1 (Easy 1) .. 12 (Insane 3), the stars in between
	int m_Tier = 0; // 1..12
	int m_Stars = 0; // 1..3
	bool m_Partial = false; // the bot did not get through all of it: from the part it passed
};
SRating Rate(const SResult &Result);
// "Easy", "Main", "Hard", "Insane"
const char *TierGroup(int Tier);
} // namespace XcSolver

#endif
