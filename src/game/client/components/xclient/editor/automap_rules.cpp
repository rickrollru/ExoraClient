#include "automap_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace XcAutomap
{
	static std::vector<std::string> Words(const std::string &Line)
	{
		std::vector<std::string> vWords;
		std::istringstream Stream(Line);
		std::string Word;
		while(Stream >> Word)
			vWords.push_back(Word);
		return vWords;
	}

	static int FlagOf(const std::string &Word)
	{
		if(Word == "XFLIP")
			return FLAG_XFLIP;
		if(Word == "YFLIP")
			return FLAG_YFLIP;
		if(Word == "ROTATE")
			return FLAG_ROTATE;
		return 0;
	}

	void Parse(const char *pText, std::vector<SConfig> &vConfigs)
	{
		vConfigs.clear();
		std::istringstream Stream(pText ? pText : "");
		std::string Line;
		SConfig *pConfig = nullptr;
		SRule *pRule = nullptr;
		while(std::getline(Stream, Line))
		{
			if(!Line.empty() && Line.back() == '\r')
				Line.pop_back();
			if(Line.empty() || Line[0] == ' ' || Line[0] == '\t')
				continue;
			if(Line[0] == '#')
			{
				// comments right after the name belong to the config (before its first rule)
				if(pConfig && !pRule)
				{
					size_t Start = 1;
					while(Start < Line.size() && Line[Start] == ' ')
						Start++;
					pConfig->m_vComments.push_back(Line.substr(Start));
				}
				continue;
			}
			if(Line[0] == '[')
			{
				vConfigs.emplace_back();
				pConfig = &vConfigs.back();
				const size_t End = Line.find(']');
				pConfig->m_Name = Line.substr(1, End == std::string::npos ? std::string::npos : End - 1);
				pConfig->m_vRuns.emplace_back();
				pRule = nullptr;
				continue;
			}
			if(!pConfig)
				continue;
			const std::vector<std::string> vWords = Words(Line);
			if(vWords.empty())
				continue;
			SRun &Run = pConfig->m_vRuns.back();
			if(vWords[0] == "NewRun")
			{
				pConfig->m_vRuns.emplace_back();
				pRule = nullptr;
			}
			else if(vWords[0] == "NoLayerCopy")
				Run.m_Copy = false;
			else if(vWords[0] == "Index" && vWords.size() >= 2)
			{
				Run.m_vRules.emplace_back();
				pRule = &Run.m_vRules.back();
				pRule->m_Id = std::atoi(vWords[1].c_str());
				for(size_t i = 2; i < vWords.size(); i++)
					pRule->m_Flags |= FlagOf(vWords[i]);
			}
			else if(vWords[0] == "Pos" && pRule && vWords.size() >= 4)
			{
				SPos Pos;
				Pos.m_X = std::atoi(vWords[1].c_str());
				Pos.m_Y = std::atoi(vWords[2].c_str());
				const std::string &Type = vWords[3];
				if(Type == "EMPTY")
					Pos.m_Type = SPos::EMPTY;
				else if(Type == "FULL")
					Pos.m_Type = SPos::FULL;
				else if(Type == "INDEX" || Type == "NOTINDEX")
				{
					Pos.m_Type = Type == "INDEX" ? SPos::INDEX : SPos::NOTINDEX;
					// id [flags...] OR id [flags...] ...
					size_t i = 4;
					while(i < vWords.size())
					{
						SIndex Index;
						Index.m_Id = std::atoi(vWords[i].c_str());
						i++;
						while(i < vWords.size() && vWords[i] != "OR")
						{
							if(vWords[i] == "NONE" || FlagOf(vWords[i]))
							{
								Index.m_TestFlags = true;
								Index.m_Flags |= FlagOf(vWords[i]);
							}
							i++;
						}
						Pos.m_vIndices.push_back(Index);
						if(i < vWords.size())
							i++; // OR
					}
					if(Pos.m_vIndices.empty())
						continue;
				}
				else
					continue;
				pRule->m_vPos.push_back(Pos);
			}
			else if(vWords[0] == "Random" && pRule && vWords.size() >= 2)
			{
				const std::string &Value = vWords[1];
				const float Number = (float)std::atof(Value.c_str());
				if(!Value.empty() && Value.back() == '%')
					pRule->m_Chance = std::clamp(Number / 100.0f, 0.0f, 1.0f);
				else if(Number > 0.0f)
					pRule->m_Chance = std::clamp(1.0f / Number, 0.0f, 1.0f);
			}
			else if(vWords[0] == "NoDefaultRule" && pRule)
				pRule->m_NoDefault = true;
			else if(vWords[0] == "Modulo" && pRule && vWords.size() >= 5)
			{
				std::vector<int> vModulo;
				for(int i = 1; i <= 4; i++)
					vModulo.push_back(std::atoi(vWords[i].c_str()));
				pRule->m_vModulo.push_back(vModulo);
			}
		}
	}

	static void WriteFlags(std::string &Out, int Flags, bool None)
	{
		if(Flags & FLAG_XFLIP)
			Out += " XFLIP";
		if(Flags & FLAG_YFLIP)
			Out += " YFLIP";
		if(Flags & FLAG_ROTATE)
			Out += " ROTATE";
		if(!Flags && None)
			Out += " NONE";
	}

	std::string Write(const std::vector<SConfig> &vConfigs)
	{
		std::string Out;
		char aBuf[128];
		for(const SConfig &Config : vConfigs)
		{
			if(!Out.empty())
				Out += "\n";
			Out += "[" + Config.m_Name + "]\n";
			for(const std::string &Comment : Config.m_vComments)
				Out += "# " + Comment + "\n";
			for(size_t r = 0; r < Config.m_vRuns.size(); r++)
			{
				const SRun &Run = Config.m_vRuns[r];
				if(r > 0)
					Out += "\nNewRun\n";
				if(!Run.m_Copy)
					Out += "NoLayerCopy\n";
				for(const SRule &Rule : Run.m_vRules)
				{
					Out += "\n";
					std::snprintf(aBuf, sizeof(aBuf), "Index %d", Rule.m_Id);
					Out += aBuf;
					WriteFlags(Out, Rule.m_Flags, false);
					Out += "\n";
					for(const SPos &Pos : Rule.m_vPos)
					{
						std::snprintf(aBuf, sizeof(aBuf), "Pos %d %d ", Pos.m_X, Pos.m_Y);
						Out += aBuf;
						if(Pos.m_Type == SPos::EMPTY)
							Out += "EMPTY";
						else if(Pos.m_Type == SPos::FULL)
							Out += "FULL";
						else
						{
							Out += Pos.m_Type == SPos::INDEX ? "INDEX" : "NOTINDEX";
							for(size_t i = 0; i < Pos.m_vIndices.size(); i++)
							{
								std::snprintf(aBuf, sizeof(aBuf), "%s %d", i ? " OR" : "", Pos.m_vIndices[i].m_Id);
								Out += aBuf;
								if(Pos.m_vIndices[i].m_TestFlags)
									WriteFlags(Out, Pos.m_vIndices[i].m_Flags, true);
							}
						}
						Out += "\n";
					}
					if(Rule.m_Chance < 1.0f)
					{
						std::snprintf(aBuf, sizeof(aBuf), "Random %g%%\n", std::round(Rule.m_Chance * 10000.0f) / 100.0f);
						Out += aBuf;
					}
					for(const std::vector<int> &vModulo : Rule.m_vModulo)
					{
						std::snprintf(aBuf, sizeof(aBuf), "Modulo %d %d %d %d\n", vModulo[0], vModulo[1], vModulo[2], vModulo[3]);
						Out += aBuf;
					}
					if(Rule.m_NoDefault)
						Out += "NoDefaultRule\n";
				}
			}
		}
		return Out;
	}

	// the same random numbers as the DDNet automapper (triple32inc)
	static uint32_t HashUInt32(uint32_t Num)
	{
		Num++;
		Num ^= Num >> 17;
		Num *= 0xed5ad4bbu;
		Num ^= Num >> 11;
		Num *= 0xac4c1b51u;
		Num ^= Num >> 15;
		Num *= 0x31848babu;
		Num ^= Num >> 14;
		return Num;
	}

	static int HashLocation(uint32_t Seed, uint32_t Run, uint32_t Rule, uint32_t X, uint32_t Y)
	{
		const uint32_t Prime = 31;
		uint32_t Hash = 1;
		Hash = Hash * Prime + HashUInt32(Seed);
		Hash = Hash * Prime + HashUInt32(Run);
		Hash = Hash * Prime + HashUInt32(Rule);
		Hash = Hash * Prime + HashUInt32(X);
		Hash = Hash * Prime + HashUInt32(Y);
		Hash = HashUInt32(Hash * Prime);
		return Hash % 65536;
	}

	static bool Matches(const SPos &Pos, int Index, int Flags)
	{
		switch(Pos.m_Type)
		{
		case SPos::EMPTY: return Index == 0;
		case SPos::FULL: return Index != 0;
		default:
			for(const SIndex &Wanted : Pos.m_vIndices)
				if(Index == Wanted.m_Id && (!Wanted.m_TestFlags || Flags == Wanted.m_Flags))
					return Pos.m_Type == SPos::INDEX;
			return Pos.m_Type == SPos::NOTINDEX;
		}
	}

	void Apply(const SConfig &Config, int Width, int Height, std::vector<int> &vIndices, std::vector<int> *pvFlags, unsigned Seed, std::vector<int> *pvWriter)
	{
		const size_t Cells = (size_t)Width * Height;
		if(pvWriter)
			pvWriter->assign(Cells, -1);
		vIndices.resize(Cells, 0);
		std::vector<int> vOwnFlags;
		std::vector<int> &vFlags = pvFlags ? *pvFlags : vOwnFlags;
		vFlags.resize(Cells, 0);
		std::vector<int> vReadIndices, vReadFlags;
		for(size_t r = 0; r < Config.m_vRuns.size(); r++)
		{
			const SRun &Run = Config.m_vRuns[r];
			if(Run.m_Copy)
			{
				vReadIndices = vIndices;
				vReadFlags = vFlags;
			}
			const std::vector<int> &vRead = Run.m_Copy ? vReadIndices : vIndices;
			const std::vector<int> &vReadF = Run.m_Copy ? vReadFlags : vFlags;
			for(int y = 0; y < Height; y++)
			{
				for(int x = 0; x < Width; x++)
				{
					const size_t Cell = (size_t)y * Width + x;
					for(size_t i = 0; i < Run.m_vRules.size(); i++)
					{
						const SRule &Rule = Run.m_vRules[i];
						// the default rule: only on cells with a block, unless the rule says otherwise
						bool Default = !Rule.m_NoDefault;
						for(const SPos &Pos : Rule.m_vPos)
							if(Pos.m_X == 0 && Pos.m_Y == 0 && (Pos.m_Type == SPos::EMPTY || (Pos.m_Type == SPos::INDEX && std::any_of(Pos.m_vIndices.begin(), Pos.m_vIndices.end(), [](const SIndex &Index) { return Index.m_Id == 0; }))))
								Default = false;
						if(Default && vRead[Cell] == 0)
							continue;
						bool Ok = true;
						for(const SPos &Pos : Rule.m_vPos)
						{
							const int cx = x + Pos.m_X, cy = y + Pos.m_Y;
							int Index = -1, Flags = 0;
							if(cx >= 0 && cy >= 0 && cx < Width && cy < Height)
							{
								Index = vRead[(size_t)cy * Width + cx];
								Flags = vReadF[(size_t)cy * Width + cx] & (FLAG_XFLIP | FLAG_YFLIP | FLAG_ROTATE);
							}
							if(!Matches(Pos, Index, Flags))
							{
								Ok = false;
								break;
							}
						}
						if(!Ok)
							continue;
						if(!Rule.m_vModulo.empty() && std::none_of(Rule.m_vModulo.begin(), Rule.m_vModulo.end(), [&](const std::vector<int> &vM) {
							   const int Mx = vM[0] ? vM[0] : 1, My = vM[1] ? vM[1] : 1;
							   return (x + vM[2]) % Mx == 0 && (y + vM[3]) % My == 0;
						   }))
							continue;
						if(Rule.m_Chance < 1.0f && HashLocation(Seed, (uint32_t)r, (uint32_t)i, x, y) >= 65536 * Rule.m_Chance)
							continue;
						vIndices[Cell] = Rule.m_Id;
						vFlags[Cell] = Rule.m_Flags;
						if(pvWriter)
							(*pvWriter)[Cell] = (int)(r * 10000 + i);
					}
				}
			}
		}
	}

	void TileUvs(int Flags, float aU[4], float aV[4])
	{
		// the same steps as the DDNet tile renderer
		float x0 = 0, y0 = 0, x1 = 1, y1 = 0, x2 = 1, y2 = 1, x3 = 0, y3 = 1;
		if(Flags & FLAG_XFLIP)
		{
			x0 = x2;
			x1 = x3;
			x2 = x3;
			x3 = x0;
		}
		if(Flags & FLAG_YFLIP)
		{
			y0 = y3;
			y2 = y1;
			y3 = y1;
			y1 = y0;
		}
		if(Flags & FLAG_ROTATE)
		{
			float Tmp = x0;
			x0 = x3;
			x3 = x2;
			x2 = x1;
			x1 = Tmp;
			Tmp = y0;
			y0 = y3;
			y3 = y2;
			y2 = y1;
			y1 = Tmp;
		}
		aU[0] = x0;
		aV[0] = y0;
		aU[1] = x1;
		aV[1] = y1;
		aU[2] = x2;
		aV[2] = y2;
		aU[3] = x3;
		aV[3] = y3;
	}

	// --- roles

	static const char *const s_apRoleNames[NUM_ROLES] = {
		"Inside",
		"Top edge",
		"Bottom edge",
		"Left edge",
		"Right edge",
		"Outer corner top left",
		"Outer corner top right",
		"Outer corner bottom left",
		"Outer corner bottom right",
		"Inner corner top left",
		"Inner corner top right",
		"Inner corner bottom left",
		"Inner corner bottom right",
		"Platform (1 high)",
		"Column (1 wide)",
		"Single block",
	};

	// neighbors: index (dy+1)*3+(dx+1); 1 block, -1 empty, 0 any
	static const int s_aaPatterns[NUM_ROLES][9] = {
		{0, 0, 0, 0, 1, 0, 0, 0, 0}, // inside
		{0, -1, 0, 0, 1, 0, 0, 0, 0}, // top
		{0, 0, 0, 0, 1, 0, 0, -1, 0}, // bottom
		{0, 0, 0, -1, 1, 0, 0, 0, 0}, // left
		{0, 0, 0, 0, 1, -1, 0, 0, 0}, // right
		{0, -1, 0, -1, 1, 0, 0, 0, 0}, // outer top left
		{0, -1, 0, 0, 1, -1, 0, 0, 0}, // outer top right
		{0, 0, 0, -1, 1, 0, 0, -1, 0}, // outer bottom left
		{0, 0, 0, 0, 1, -1, 0, -1, 0}, // outer bottom right
		{-1, 1, 0, 1, 1, 0, 0, 0, 0}, // inner top left
		{0, 1, -1, 0, 1, 1, 0, 0, 0}, // inner top right
		{0, 0, 0, 1, 1, 0, -1, 1, 0}, // inner bottom left
		{0, 0, 0, 0, 1, 1, 0, 1, -1}, // inner bottom right
		{0, -1, 0, 0, 1, 0, 0, -1, 0}, // platform
		{0, 0, 0, -1, 1, -1, 0, 0, 0}, // column
		{0, -1, 0, -1, 1, -1, 0, -1, 0}, // single
	};

	const char *RoleName(int Role) { return Role >= 0 && Role < NUM_ROLES ? s_apRoleNames[Role] : ""; }
	const int *RolePattern(int Role) { return s_aaPatterns[std::clamp(Role, 0, NUM_ROLES - 1)]; }

	// where a direction of the screen lands in the texture of a tile drawn with Flags
	static void ScreenToTexture(int Flags, int dx, int dy, int *pTx, int *pTy)
	{
		float aU[4], aV[4];
		TileUvs(Flags, aU, aV);
		// the texture side under a screen side: both its corners share that edge of the texture
		const auto &&Side = [&](int a, int b, int *pSx, int *pSy) {
			*pSx = 0;
			*pSy = 0;
			if(aU[a] == aU[b])
				*pSx = aU[a] > 0.5f ? 1 : -1;
			else
				*pSy = aV[a] > 0.5f ? 1 : -1;
		};
		int Rx, Ry, Bx, By;
		Side(1, 2, &Rx, &Ry); // screen right
		Side(2, 3, &Bx, &By); // screen bottom
		*pTx = dx * Rx + dy * Bx;
		*pTy = dx * Ry + dy * By;
	}

	bool DeriveRole(const SRoles &Roles, int Role, STile *pOut)
	{
		static const int s_aFlags[] = {FLAG_XFLIP, FLAG_YFLIP, FLAG_XFLIP | FLAG_YFLIP, FLAG_ROTATE, FLAG_ROTATE | FLAG_XFLIP, FLAG_ROTATE | FLAG_YFLIP, FLAG_ROTATE | FLAG_XFLIP | FLAG_YFLIP};
		const int *pTarget = RolePattern(Role);
		for(int Flags : s_aFlags)
		{
			for(int Source = 0; Source < NUM_ROLES; Source++)
			{
				if(Source == Role || Roles.m_avTiles[Source].empty() || Roles.m_avTiles[Source][0].m_Flags != 0)
					continue;
				const int *pSource = RolePattern(Source);
				bool Same = true;
				for(int dy = -1; dy <= 1 && Same; dy++)
				{
					for(int dx = -1; dx <= 1 && Same; dx++)
					{
						int tx, ty;
						ScreenToTexture(Flags, dx, dy, &tx, &ty);
						Same = pTarget[(dy + 1) * 3 + dx + 1] == pSource[(ty + 1) * 3 + tx + 1];
					}
				}
				if(Same)
				{
					pOut->m_Id = Roles.m_avTiles[Source][0].m_Id;
					pOut->m_Flags = Flags;
					return true;
				}
			}
		}
		return false;
	}

	SConfig Build(const SRoles &Roles, const char *pName, bool Derive)
	{
		SConfig Config;
		Config.m_Name = pName;
		Config.m_vRuns.emplace_back();
		char aBuf[128];
		std::snprintf(aBuf, sizeof(aBuf), "exora-builder derive=%d", Derive ? 1 : 0);
		Config.m_vComments.push_back(aBuf);
		for(int Role = 0; Role < NUM_ROLES; Role++)
		{
			for(const STile &Tile : Roles.m_avTiles[Role])
			{
				std::snprintf(aBuf, sizeof(aBuf), "role %d %d %d", Role, Tile.m_Id, Tile.m_Flags);
				Config.m_vComments.push_back(aBuf);
			}
		}

		// later rules win: the base, inner corners, edges, outer corners, then the thin shapes
		static const int s_aOrder[] = {ROLE_INSIDE, ROLE_INNER_TL, ROLE_INNER_TR, ROLE_INNER_BL, ROLE_INNER_BR, ROLE_TOP, ROLE_BOTTOM, ROLE_LEFT, ROLE_RIGHT,
			ROLE_OUTER_TL, ROLE_OUTER_TR, ROLE_OUTER_BL, ROLE_OUTER_BR, ROLE_PLATFORM, ROLE_COLUMN, ROLE_SINGLE};
		for(int Role : s_aOrder)
		{
			std::vector<STile> vTiles = Roles.m_avTiles[Role];
			STile Derived;
			if(vTiles.empty() && Derive && DeriveRole(Roles, Role, &Derived))
				vTiles.push_back(Derived);
			const int *pPattern = RolePattern(Role);
			for(size_t v = 0; v < vTiles.size(); v++)
			{
				SRule Rule;
				Rule.m_Id = vTiles[v].m_Id;
				Rule.m_Flags = vTiles[v].m_Flags;
				for(int i = 0; i < 9; i++)
				{
					if(i == 4 || pPattern[i] == 0)
						continue;
					SPos Pos;
					Pos.m_X = i % 3 - 1;
					Pos.m_Y = i / 3 - 1;
					Pos.m_Type = pPattern[i] > 0 ? SPos::FULL : SPos::EMPTY;
					Rule.m_vPos.push_back(Pos);
				}
				// the n-th of several tiles replaces the earlier ones with 1/n: all of them equally often
				if(v > 0)
					Rule.m_Chance = 1.0f / (float)(v + 1);
				Config.m_vRuns[0].m_vRules.push_back(Rule);
			}
		}
		return Config;
	}

	int GuessRoles(const unsigned char *pRgba, int Width, int Height, SRoles &Roles)
	{
		Roles = SRoles();
		const int TileW = Width / 16, TileH = Height / 16;
		if(!pRgba || TileW < 4 || TileH < 4)
			return 0;
		// a part of a tile: how solid it is (average alpha) and its color
		struct SArea
		{
			float m_Alpha = 0.0f;
			float m_aColor[3] = {0.0f, 0.0f, 0.0f};
		};
		const auto &&Area = [&](int Tile, float x0, float y0, float x1, float y1) {
			const int Bx = (Tile % 16) * TileW, By = (Tile / 16) * TileH;
			const int Ax0 = Bx + (int)(x0 * TileW), Ax1 = std::max(Ax0 + 1, Bx + (int)(x1 * TileW));
			const int Ay0 = By + (int)(y0 * TileH), Ay1 = std::max(Ay0 + 1, By + (int)(y1 * TileH));
			double Alpha = 0.0, aColor[3] = {0.0, 0.0, 0.0};
			int Count = 0;
			for(int y = Ay0; y < Ay1; y++)
			{
				for(int x = Ax0; x < Ax1; x++, Count++)
				{
					const unsigned char *pPixel = &pRgba[((size_t)y * Width + x) * 4];
					const double a = pPixel[3] / 255.0;
					Alpha += a;
					for(int c = 0; c < 3; c++)
						aColor[c] += pPixel[c] / 255.0 * a;
				}
			}
			SArea Result;
			Result.m_Alpha = Count ? (float)(Alpha / Count) : 0.0f;
			for(int c = 0; c < 3; c++)
				Result.m_aColor[c] = Alpha > 0.0 ? (float)(aColor[c] / Alpha) : 0.0f;
			return Result;
		};
		const auto &&Distance = [](const float *pA, const float *pB) {
			return std::sqrt((pA[0] - pB[0]) * (pA[0] - pB[0]) + (pA[1] - pB[1]) * (pA[1] - pB[1]) + (pA[2] - pB[2]) * (pA[2] - pB[2]));
		};

		// the parts measured: 4 sides (their middle, rounded corners do not count) and 4 corners
		const float Strip = 0.14f;
		struct STileInfo
		{
			float m_Fill;
			SArea m_All;
			SArea m_aSide[4]; // top, bottom, left, right
			SArea m_aCorner[4]; // top left, top right, bottom left, bottom right
		};
		std::vector<STileInfo> vInfo(256);
		for(int Tile = 1; Tile < 256; Tile++)
		{
			STileInfo &Info = vInfo[Tile];
			Info.m_All = Area(Tile, 0.0f, 0.0f, 1.0f, 1.0f);
			Info.m_Fill = Info.m_All.m_Alpha;
			Info.m_aSide[0] = Area(Tile, 0.25f, 0.0f, 0.75f, Strip);
			Info.m_aSide[1] = Area(Tile, 0.25f, 1.0f - Strip, 0.75f, 1.0f);
			Info.m_aSide[2] = Area(Tile, 0.0f, 0.25f, Strip, 0.75f);
			Info.m_aSide[3] = Area(Tile, 1.0f - Strip, 0.25f, 1.0f, 0.75f);
			Info.m_aCorner[0] = Area(Tile, 0.0f, 0.0f, Strip, Strip);
			Info.m_aCorner[1] = Area(Tile, 1.0f - Strip, 0.0f, 1.0f, Strip);
			Info.m_aCorner[2] = Area(Tile, 0.0f, 1.0f - Strip, Strip, 1.0f);
			Info.m_aCorner[3] = Area(Tile, 1.0f - Strip, 1.0f - Strip, 1.0f, 1.0f);
		}

		// the color of the inside: the first solid tile that looks the same at all its sides and corners
		// (a fill, not an edge); mappers put the main fill early in the tileset
		float aInside[3] = {0.0f, 0.0f, 0.0f};
		bool HasInside = false;
		for(int Tile = 1; Tile < 256 && !HasInside; Tile++)
		{
			const STileInfo &Info = vInfo[Tile];
			if(Info.m_Fill < 0.97f)
				continue;
			float Spread = 0.0f;
			for(int i = 0; i < 4; i++)
			{
				Spread = std::max(Spread, Distance(Info.m_aSide[i].m_aColor, Info.m_All.m_aColor));
				Spread = std::max(Spread, Distance(Info.m_aCorner[i].m_aColor, Info.m_All.m_aColor));
			}
			if(Spread < 0.1f)
			{
				std::copy(Info.m_All.m_aColor, Info.m_All.m_aColor + 3, aInside);
				HasInside = true;
			}
		}
		// a part is "open" when it is see-through, or (with an opaque tileset) when it does not look like the inside
		const auto &&Open = [&](const SArea &Part) {
			float Value = 1.0f - Part.m_Alpha;
			if(HasInside && Part.m_Alpha > 0.5f)
				Value = std::max(Value, std::clamp((Distance(Part.m_aColor, aInside) - 0.06f) / 0.18f, 0.0f, 1.0f));
			return Value;
		};

		struct SCandidate
		{
			float m_Score;
			int m_Tile;
		};
		std::vector<SCandidate> avCandidates[NUM_ROLES];
		for(int Tile = 1; Tile < 256; Tile++)
		{
			const STileInfo &Info = vInfo[Tile];
			if(Info.m_Fill < 0.15f)
				continue;
			float aOpen[4], aCorner[4];
			for(int i = 0; i < 4; i++)
			{
				aOpen[i] = Open(Info.m_aSide[i]);
				aCorner[i] = Open(Info.m_aCorner[i]);
			}
			for(int Role = 0; Role < NUM_ROLES; Role++)
			{
				const int *pPattern = RolePattern(Role);
				// a side must be open where the role has an empty neighbor, closed otherwise
				const int aSide[4] = {pPattern[1], pPattern[7], pPattern[3], pPattern[5]};
				float Score = 0.0f;
				for(int i = 0; i < 4; i++)
				{
					const float Want = aSide[i] < 0 ? 1.0f : 0.0f;
					Score += (aOpen[i] - Want) * (aOpen[i] - Want);
				}
				// the corners: an inner corner is open only at its diagonal; the inside has no open corner
				const int aDiagonal[4] = {pPattern[0], pPattern[2], pPattern[6], pPattern[8]};
				const bool Inner = Role >= ROLE_INNER_TL && Role <= ROLE_INNER_BR;
				if(Inner || Role == ROLE_INSIDE)
				{
					for(int i = 0; i < 4; i++)
					{
						const float Want = aDiagonal[i] < 0 ? 1.0f : 0.0f;
						Score += (aCorner[i] - Want) * (aCorner[i] - Want);
					}
				}
				if(Role == ROLE_INSIDE)
					Score += (1.0f - Info.m_Fill) * 2.0f + (HasInside ? Distance(Info.m_All.m_aColor, aInside) : 0.0f);
				if(Score < 0.4f)
					avCandidates[Role].push_back({Score, Tile});
			}
		}
		int Found = 0;
		for(int Role = 0; Role < NUM_ROLES; Role++)
		{
			std::vector<SCandidate> &vCandidates = avCandidates[Role];
			std::stable_sort(vCandidates.begin(), vCandidates.end(), [](const SCandidate &a, const SCandidate &b) { return a.m_Score < b.m_Score; });
			// the best few that are about as good as the best
			for(const SCandidate &Candidate : vCandidates)
			{
				if((int)Roles.m_avTiles[Role].size() >= 3 || Candidate.m_Score > vCandidates[0].m_Score + 0.04f)
					break;
				Roles.m_avTiles[Role].push_back({Candidate.m_Tile, 0});
			}
			if(!Roles.m_avTiles[Role].empty())
				Found++;
		}
		return Found;
	}

	bool ReadRoles(const SConfig &Config, SRoles &Roles, bool *pDerive)
	{
		bool Ours = false;
		Roles = SRoles();
		for(const std::string &Comment : Config.m_vComments)
		{
			int Value;
			if(std::sscanf(Comment.c_str(), "exora-builder derive=%d", &Value) == 1)
			{
				Ours = true;
				if(pDerive)
					*pDerive = Value != 0;
				continue;
			}
			int Role, Id, Flags;
			if(std::sscanf(Comment.c_str(), "role %d %d %d", &Role, &Id, &Flags) == 3 && Role >= 0 && Role < NUM_ROLES)
				Roles.m_avTiles[Role].push_back({Id, Flags});
		}
		return Ours;
	}
} // namespace XcAutomap
