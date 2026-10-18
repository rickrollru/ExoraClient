#include "map_info.h"

#include <base/str.h>

#include <engine/external/json-parser/json.h>

#include <algorithm>
#include <cmath>

static double NumberOr0(const json_value &Value)
{
	if(Value.type == json_integer)
		return (double)Value.u.integer;
	if(Value.type == json_double)
		return Value.u.dbl;
	return 0.0;
}

static std::string StringOr(const json_value &Value, const char *pDefault = "")
{
	return Value.type == json_string ? std::string(Value.u.string.ptr) : std::string(pDefault);
}

bool ParseMapInfo(const json_value &Json, SMapInfo &Info)
{
	if(Json.type != json_object || Json["name"].type != json_string)
		return false;
	Info = SMapInfo();
	Info.m_Name = StringOr(Json["name"]);
	Info.m_Mapper = StringOr(Json["mapper"]);
	Info.m_Type = StringOr(Json["type"]);
	Info.m_Points = (int)NumberOr0(Json["points"]);
	Info.m_Stars = std::clamp((int)NumberOr0(Json["difficulty"]), 0, 5);
	Info.m_Release = (long long)NumberOr0(Json["release"]);
	Info.m_Finishes = (int)NumberOr0(Json["finishes"]);
	Info.m_Finishers = (int)NumberOr0(Json["finishers"]);
	Info.m_MedianTime = (float)NumberOr0(Json["median_time"]);

	const json_value &Ranks = Json["ranks"];
	if(Ranks.type == json_array)
	{
		for(unsigned i = 0; i < Ranks.u.array.length && Info.m_vRanks.size() < 5; i++)
		{
			const json_value &Rank = *Ranks.u.array.values[i];
			SMapInfo::SRank Entry;
			Entry.m_Rank = (int)NumberOr0(Rank["rank"]);
			Entry.m_Name = StringOr(Rank["player"], "?");
			Entry.m_Time = (float)NumberOr0(Rank["time"]);
			Info.m_vRanks.push_back(Entry);
		}
	}
	const json_value &TeamRanks = Json["team_ranks"];
	if(TeamRanks.type == json_array)
	{
		for(unsigned i = 0; i < TeamRanks.u.array.length && Info.m_vTeamRanks.size() < 3; i++)
		{
			const json_value &Rank = *TeamRanks.u.array.values[i];
			SMapInfo::SRank Entry;
			Entry.m_Rank = (int)NumberOr0(Rank["rank"]);
			Entry.m_Time = (float)NumberOr0(Rank["time"]);
			const json_value &Players = Rank["players"];
			if(Players.type == json_array)
			{
				for(unsigned p = 0; p < Players.u.array.length; p++)
				{
					if(p > 0)
						Entry.m_Name += " & ";
					Entry.m_Name += StringOr(*Players.u.array.values[p], "?");
				}
			}
			Info.m_vTeamRanks.push_back(Entry);
		}
	}
	return true;
}

void FormatRaceTime(float Seconds, char *pBuf, int BufSize)
{
	const int Centi = (int)std::round(Seconds * 100.0f);
	const int Hours = Centi / 360000;
	const int Minutes = Centi / 6000 % 60;
	const int Secs = Centi / 100 % 60;
	if(Hours > 0)
		str_format(pBuf, BufSize, "%d:%02d:%02d.%02d", Hours, Minutes, Secs, Centi % 100);
	else
		str_format(pBuf, BufSize, "%02d:%02d.%02d", Minutes, Secs, Centi % 100);
}
