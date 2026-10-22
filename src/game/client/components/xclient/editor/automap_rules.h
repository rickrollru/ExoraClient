// Automapper rules as data: read and write the DDNet .rules text, run them on a small grid for previews, and
// build a whole rule set from "roles" (which tile is the top edge, which one an inner corner...). The editor's
// automapper builder works on this; the saved file is a plain DDNet rules file.
#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_AUTOMAP_RULES_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_AUTOMAP_RULES_H

#include <string>
#include <vector>

namespace XcAutomap
{
	// tile flags as in the map (TILEFLAG_XFLIP, TILEFLAG_YFLIP, TILEFLAG_ROTATE)
	enum
	{
		FLAG_XFLIP = 1,
		FLAG_YFLIP = 2,
		FLAG_ROTATE = 8,
	};

	struct SIndex
	{
		int m_Id = 0;
		int m_Flags = 0;
		bool m_TestFlags = false;
	};

	struct SPos
	{
		enum
		{
			EMPTY = 0, // INDEX 0
			FULL, // NOTINDEX 0
			INDEX,
			NOTINDEX,
		};
		int m_X = 0;
		int m_Y = 0;
		int m_Type = EMPTY;
		std::vector<SIndex> m_vIndices; // INDEX / NOTINDEX
	};

	struct SRule
	{
		int m_Id = 1;
		int m_Flags = 0;
		std::vector<SPos> m_vPos;
		float m_Chance = 1.0f; // 0..1, "Random"
		bool m_NoDefault = false; // "NoDefaultRule": the rule may also put a tile on empty cells
		std::vector<std::vector<int>> m_vModulo; // "Modulo x y ox oy"
	};

	struct SRun
	{
		std::vector<SRule> m_vRules;
		bool m_Copy = true; // false: "NoLayerCopy", rules see the changes of the same run
	};

	struct SConfig
	{
		std::string m_Name;
		std::vector<SRun> m_vRuns;
		std::vector<std::string> m_vComments; // "# ..." lines right after the name (the builder keeps its roles there)
	};

	// the whole file; unknown lines are skipped like DDNet does
	void Parse(const char *pText, std::vector<SConfig> &vConfigs);
	std::string Write(const std::vector<SConfig> &vConfigs);

	// runs a rule set on a grid of tile indices (0 empty); pFlags gets the tile flags (may be null)
	// pvWriter (may be null): which rule put the tile there, run * 10000 + rule, -1 none
	void Apply(const SConfig &Config, int Width, int Height, std::vector<int> &vIndices, std::vector<int> *pvFlags, unsigned Seed, std::vector<int> *pvWriter = nullptr);

	// screen corner order of a tile: top left, top right, bottom right, bottom left. The texture coordinates
	// (0 or 1 within the tile) of each corner for the given flags, exactly as DDNet draws tiles
	void TileUvs(int Flags, float aU[4], float aV[4]);

	// --- building from roles
	enum
	{
		ROLE_INSIDE = 0,
		ROLE_TOP,
		ROLE_BOTTOM,
		ROLE_LEFT,
		ROLE_RIGHT,
		ROLE_OUTER_TL,
		ROLE_OUTER_TR,
		ROLE_OUTER_BL,
		ROLE_OUTER_BR,
		ROLE_INNER_TL,
		ROLE_INNER_TR,
		ROLE_INNER_BL,
		ROLE_INNER_BR,
		ROLE_PLATFORM, // one block high: empty above and below
		ROLE_COLUMN, // one block wide: empty left and right
		ROLE_SINGLE, // empty on all four sides
		NUM_ROLES,
	};
	const char *RoleName(int Role); // English, for XcUi::Tr
	// the 8 neighbors (dx, dy) of a role: 1 must be a block, -1 must be empty, 0 does not matter; index (dy+1)*3+(dx+1)
	const int *RolePattern(int Role);

	struct STile
	{
		int m_Id = 0;
		int m_Flags = 0;
	};
	struct SRoles
	{
		std::vector<STile> m_avTiles[NUM_ROLES]; // several tiles: they are mixed randomly
	};
	// a missing role gets a picked tile turned or mirrored, when one fits; true when it found one
	bool DeriveRole(const SRoles &Roles, int Role, STile *pOut);
	// the rule set from roles; Derive: missing roles from turned tiles
	SConfig Build(const SRoles &Roles, const char *pName, bool Derive);
	// guesses the roles from the pictures of the tiles: which sides of a tile are see-through (RGBA, 16x16 tiles);
	// returns how many roles got a tile
	int GuessRoles(const unsigned char *pRgba, int Width, int Height, SRoles &Roles);
	// the roles saved in a config built by Build (from its comments), false when it was not
	bool ReadRoles(const SConfig &Config, SRoles &Roles, bool *pDerive);
} // namespace XcAutomap

#endif
