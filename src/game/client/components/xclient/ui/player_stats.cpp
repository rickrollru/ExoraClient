#include "player_stats.h"

#include <engine/external/json-parser/json.h>

static int IntOr0(const json_value &Value)
{
	return Value.type == json_integer ? (int)Value.u.integer : 0;
}

bool ParsePlayerStats(const json_value &Json, SPlayerStats &Stats)
{
	const json_value &Points = Json["points"];
	if(Points.type != json_object)
		return false;
	Stats = SPlayerStats();
	Stats.m_Points = IntOr0(Points["points"]);
	Stats.m_MaxPoints = IntOr0(Points["total"]);
	Stats.m_Rank = IntOr0(Points["rank"]);
	Stats.m_Hours = IntOr0(Json["hours_played_past_365_days"]);

	// maps are grouped by server type: count the finished ones and all of them
	const json_value &Types = Json["types"];
	if(Types.type != json_object)
		return true;
	for(unsigned t = 0; t < Types.u.object.length; t++)
	{
		const json_value &Maps = (*Types.u.object.values[t].value)["maps"];
		if(Maps.type != json_object)
			continue;
		for(unsigned m = 0; m < Maps.u.object.length; m++)
		{
			Stats.m_Maps++;
			if(IntOr0((*Maps.u.object.values[m].value)["finishes"]) > 0)
				Stats.m_Finished++;
		}
	}
	return true;
}
