#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_UI_PLAYER_STATS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_UI_PLAYER_STATS_H

typedef struct _json_value json_value;

// What the home page shows from a ddnet.org player answer (players/?json2=<name>).
struct SPlayerStats
{
	int m_Points = 0;
	int m_MaxPoints = 0;
	int m_Rank = 0;
	int m_Hours = 0; // played in the last 365 days
	int m_Finished = 0; // maps with at least one finish
	int m_Maps = 0; // all maps listed
};

// false when the answer has no points: the player never finished a map
bool ParsePlayerStats(const json_value &Json, SPlayerStats &Stats);

#endif
