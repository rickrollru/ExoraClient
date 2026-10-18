#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_MAP_INFO_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_MAP_INFO_H

#include <string>
#include <vector>

typedef struct _json_value json_value;

// What the Esc menu shows about the current map from ddnet.org (maps/?json=<map>).
struct SMapInfo
{
	struct SRank
	{
		int m_Rank = 0;
		std::string m_Name; // players of a team are joined with " & "
		float m_Time = 0.0f; // seconds
	};

	std::string m_Name;
	std::string m_Mapper;
	std::string m_Type; // server type: Novice, Moderate, ...
	int m_Points = 0;
	int m_Stars = 0; // difficulty 0..5
	long long m_Release = 0; // unix time, 0 = unknown
	int m_Finishes = 0;
	int m_Finishers = 0;
	float m_MedianTime = 0.0f;
	std::vector<SRank> m_vRanks; // best players, at most 5
	std::vector<SRank> m_vTeamRanks; // best teams, at most 3
};

// false when the answer is not a map (unknown map, error page)
bool ParseMapInfo(const json_value &Json, SMapInfo &Info);
// 75.3 -> "01:15.30", 3725 -> "1:02:05.00"
void FormatRaceTime(float Seconds, char *pBuf, int BufSize);

#endif
