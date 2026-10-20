#include "cosmetics.h"

#include "sprite_fx.h"
#include "xc_helpers.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/client.h>
#include <engine/external/json-parser/json.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/gameclient.h>

#include <algorithm>
#include <cmath>

using namespace XcHelpers;

namespace
{
const char *const FOLDER = "xclient/cosmetics";

// a 3x4 affine transform
struct SMat
{
	float m[3][4];
	static SMat Identity()
	{
		SMat r = {};
		r.m[0][0] = r.m[1][1] = r.m[2][2] = 1.0f;
		return r;
	}
	vec3 Apply(vec3 p) const
	{
		return vec3(m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
			m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
			m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]);
	}
	SMat operator*(const SMat &o) const
	{
		SMat r = {};
		for(int i = 0; i < 3; i++)
		{
			for(int j = 0; j < 4; j++)
			{
				float v = j == 3 ? m[i][3] : 0.0f;
				for(int k = 0; k < 3; k++)
					v += m[i][k] * o.m[k][j];
				r.m[i][j] = v;
			}
		}
		return r;
	}
};

// Bedrock rotations: x and y turn the other way than in a right handed system, applied x, then y, then z
SMat Rotation(vec3 Degrees)
{
	const float x = -Degrees.x * pi / 180.0f, y = -Degrees.y * pi / 180.0f, z = Degrees.z * pi / 180.0f;
	const float cx = std::cos(x), sx = std::sin(x), cy = std::cos(y), sy = std::sin(y), cz = std::cos(z), sz = std::sin(z);
	SMat Rx = SMat::Identity(), Ry = SMat::Identity(), Rz = SMat::Identity();
	Rx.m[1][1] = cx;
	Rx.m[1][2] = -sx;
	Rx.m[2][1] = sx;
	Rx.m[2][2] = cx;
	Ry.m[0][0] = cy;
	Ry.m[0][2] = sy;
	Ry.m[2][0] = -sy;
	Ry.m[2][2] = cy;
	Rz.m[0][0] = cz;
	Rz.m[0][1] = -sz;
	Rz.m[1][0] = sz;
	Rz.m[1][1] = cz;
	return Rz * Ry * Rx;
}

SMat Translation(vec3 t)
{
	SMat r = SMat::Identity();
	r.m[0][3] = t.x;
	r.m[1][3] = t.y;
	r.m[2][3] = t.z;
	return r;
}

SMat Scaling(vec3 s)
{
	SMat r = SMat::Identity();
	r.m[0][0] = s.x;
	r.m[1][1] = s.y;
	r.m[2][2] = s.z;
	return r;
}

vec3 ReadVec3(const json_value &Value, vec3 Default)
{
	if(Value.type == json_array && Value.u.array.length >= 3)
	{
		float aOut[3];
		for(int i = 0; i < 3; i++)
		{
			const json_value &Item = *Value.u.array.values[i];
			aOut[i] = Item.type == json_double ? (float)Item.u.dbl : (Item.type == json_integer ? (float)Item.u.integer : 0.0f);
		}
		return vec3(aOut[0], aOut[1], aOut[2]);
	}
	if(Value.type == json_double)
		return vec3((float)Value.u.dbl, (float)Value.u.dbl, (float)Value.u.dbl);
	if(Value.type == json_integer)
		return vec3((float)Value.u.integer, (float)Value.u.integer, (float)Value.u.integer);
	if(Value.type == json_object)
	{
		// {"post": [...]}, {"vector": [...]}, {"pre": [...], "post": [...]}
		const json_value &Post = Value["post"];
		if(Post.type != json_none)
			return ReadVec3(Post, Default);
		const json_value &Vector = Value["vector"];
		if(Vector.type != json_none)
			return ReadVec3(Vector, Default);
		return ReadVec3(Value["pre"], Default);
	}
	return Default;
}

bool NameHas(const std::string &Name, const char *pPart)
{
	return str_find_nocase(Name.c_str(), pPart) != nullptr;
}
} // namespace

int CXcCosmetics::ListCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CXcCosmetics *pSelf = static_cast<CXcCosmetics *>(pUser);
	if(IsDir || !str_endswith(pName, ".json"))
		return 0;
	char aId[128];
	str_truncate(aId, sizeof(aId), pName, str_length(pName) - 5);
	if(pSelf->FindModel(aId) >= 0)
		return 0; // the save folder may hold a copy of a shipped file
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "%s/%s", FOLDER, pName);
	pSelf->LoadModel(aPath, aId);
	return 0;
}

void CXcCosmetics::OnInit()
{
	m_vModels.clear();
	m_vInfos.clear();
	Storage()->ListDirectory(IStorage::TYPE_ALL, FOLDER, ListCallback, this);
	// back, hat, pet, each by name; models and infos stay in the same order
	std::vector<int> vOrder(m_vInfos.size());
	for(size_t i = 0; i < vOrder.size(); i++)
		vOrder[i] = (int)i;
	std::sort(vOrder.begin(), vOrder.end(), [&](int a, int b) {
		if(m_vInfos[a].m_Slot != m_vInfos[b].m_Slot)
			return m_vInfos[a].m_Slot < m_vInfos[b].m_Slot;
		return str_comp_nocase(m_vInfos[a].m_Name.c_str(), m_vInfos[b].m_Name.c_str()) < 0;
	});
	std::vector<SModel> vModels;
	std::vector<SModelInfo> vInfos;
	for(int i : vOrder)
	{
		vModels.push_back(std::move(m_vModels[i]));
		vInfos.push_back(std::move(m_vInfos[i]));
	}
	m_vModels = std::move(vModels);
	m_vInfos = std::move(vInfos);
}

void CXcCosmetics::OnReset()
{
	for(SState &State : m_aStates)
		State.m_Valid = false;
}

int CXcCosmetics::FindModel(const char *pId) const
{
	if(!pId[0])
		return -1;
	for(size_t i = 0; i < m_vInfos.size(); i++)
		if(m_vInfos[i].m_Id == pId)
			return (int)i;
	return -1;
}

bool CXcCosmetics::LoadModel(const char *pFile, const char *pId)
{
	char *pText = Storage()->ReadFileStr(pFile, IStorage::TYPE_ALL);
	if(!pText)
		return false;
	json_value *pJson = json_parse(pText, str_length(pText));
	free(pText);
	if(!pJson)
		return false;
	const json_value &Root = *pJson;
	SModel Model;
	SModelInfo Info;
	Info.m_Id = pId;
	Info.m_Name = Root["name"].type == json_string ? Root["name"].u.string.ptr : pId;
	const char *pSlot = Root["slot"].type == json_string ? Root["slot"].u.string.ptr : "back";
	Info.m_Slot = str_comp(pSlot, "hat") == 0 ? SLOT_HAT : (str_comp(pSlot, "pet") == 0 ? SLOT_PET : SLOT_BACK);
	Info.m_Wings = Info.m_Slot == SLOT_BACK && NameHas(Info.m_Name, "wing");

	const json_value &Geometry = Root["model"]["minecraft:geometry"];
	const json_value &Bones = Geometry.type == json_array && Geometry.u.array.length > 0 ? (*Geometry.u.array.values[0])["bones"] : json_value_none;
	if(Bones.type == json_array)
	{
		for(unsigned i = 0; i < Bones.u.array.length; i++)
		{
			const json_value &Bone = *Bones.u.array.values[i];
			SBone Out;
			Out.m_Name = Bone["name"].type == json_string ? Bone["name"].u.string.ptr : "";
			Out.m_Parent = -1;
			Out.m_Pivot = ReadVec3(Bone["pivot"], vec3(0, 0, 0));
			Out.m_Rotation = ReadVec3(Bone["rotation"], vec3(0, 0, 0));
			const json_value &Cubes = Bone["cubes"];
			for(unsigned c = 0; Cubes.type == json_array && c < Cubes.u.array.length; c++)
			{
				const json_value &Cube = *Cubes.u.array.values[c];
				SCube Box;
				const float Inflate = Cube["inflate"].type == json_double ? (float)Cube["inflate"].u.dbl : (Cube["inflate"].type == json_integer ? (float)Cube["inflate"].u.integer : 0.0f);
				Box.m_Origin = ReadVec3(Cube["origin"], vec3(0, 0, 0)) - vec3(Inflate, Inflate, Inflate);
				Box.m_Size = ReadVec3(Cube["size"], vec3(0, 0, 0)) + vec3(Inflate, Inflate, Inflate) * 2.0f;
				Box.m_Rotated = Cube["rotation"].type == json_array;
				Box.m_Rotation = ReadVec3(Cube["rotation"], vec3(0, 0, 0));
				Box.m_Pivot = ReadVec3(Cube["pivot"], vec3(0, 0, 0));
				Out.m_vCubes.push_back(Box);
			}
			Model.m_vBones.push_back(Out);
		}
		// parents by name
		for(unsigned i = 0; i < Bones.u.array.length; i++)
		{
			const json_value &Parent = (*Bones.u.array.values[i])["parent"];
			if(Parent.type != json_string)
				continue;
			for(size_t p = 0; p < Model.m_vBones.size(); p++)
				if(Model.m_vBones[p].m_Name == Parent.u.string.ptr && p != i)
					Model.m_vBones[i].m_Parent = (int)p;
		}
	}

	const json_value &Animations = Root["animations"];
	for(unsigned a = 0; Animations.type == json_object && a < Animations.u.object.length; a++)
	{
		const json_value &Anim = *Animations.u.object.values[a].value;
		SAnimation Out;
		Out.m_Name = Animations.u.object.values[a].name;
		Out.m_Length = Anim["animation_length"].type == json_double ? (float)Anim["animation_length"].u.dbl : (Anim["animation_length"].type == json_integer ? (float)Anim["animation_length"].u.integer : 0.0f);
		float LastKey = 0.0f;
		const json_value &AnimBones = Anim["bones"];
		for(unsigned b = 0; AnimBones.type == json_object && b < AnimBones.u.object.length; b++)
		{
			SBoneAnim BoneAnim;
			BoneAnim.m_Bone = -1;
			for(size_t i = 0; i < Model.m_vBones.size(); i++)
				if(Model.m_vBones[i].m_Name == AnimBones.u.object.values[b].name)
					BoneAnim.m_Bone = (int)i;
			if(BoneAnim.m_Bone < 0)
				continue;
			const json_value &Channels = *AnimBones.u.object.values[b].value;
			static const char *const s_apChannels[3] = {"rotation", "position", "scale"};
			for(int c = 0; c < 3; c++)
			{
				const json_value &Channel = Channels[s_apChannels[c]];
				const vec3 Default = c == 2 ? vec3(1, 1, 1) : vec3(0, 0, 0);
				std::vector<SKey> &vKeys = BoneAnim.m_avKeys[c];
				if(Channel.type == json_object && Channel["post"].type == json_none && Channel["vector"].type == json_none && Channel["pre"].type == json_none)
				{
					// keyframes: "time": value
					for(unsigned k = 0; k < Channel.u.object.length; k++)
					{
						const float Time = str_tofloat(Channel.u.object.values[k].name);
						vKeys.push_back({Time, ReadVec3(*Channel.u.object.values[k].value, Default)});
						LastKey = std::max(LastKey, Time);
					}
					std::sort(vKeys.begin(), vKeys.end(), [](const SKey &x, const SKey &y) { return x.m_Time < y.m_Time; });
				}
				else if(Channel.type != json_none)
					vKeys.push_back({0.0f, ReadVec3(Channel, Default)}); // a constant
			}
			Out.m_vBones.push_back(BoneAnim);
		}
		if(Out.m_Length <= 0.0f)
			Out.m_Length = LastKey;
		Model.m_vAnimations.push_back(Out);
	}
	json_value_free(pJson);

	// idle and moving animations by their names
	const auto Skip = [](const std::string &Name) {
		return NameHas(Name, ".water") || NameHas(Name, ".sneak") || NameHas(Name, "gui") || NameHas(Name, "elytra") || NameHas(Name, "swim") || NameHas(Name, "afk") || NameHas(Name, "water");
	};
	for(size_t i = 0; i < Model.m_vAnimations.size(); i++)
	{
		const std::string &Name = Model.m_vAnimations[i].m_Name;
		if(Skip(Name))
			continue;
		const bool Moving = NameHas(Name, "walk") || NameHas(Name, "moving") || NameHas(Name, "run") || NameHas(Name, "flying");
		if(Moving && Model.m_MoveAnim < 0)
			Model.m_MoveAnim = (int)i;
		else if(!Moving && Model.m_IdleAnim < 0)
			Model.m_IdleAnim = (int)i;
	}
	if(Model.m_IdleAnim < 0)
		Model.m_IdleAnim = Model.m_MoveAnim;
	if(Model.m_MoveAnim < 0)
		Model.m_MoveAnim = Model.m_IdleAnim;

	// fit: the rest pose seen from the front
	Model.m_Size = 1.0f;
	Model.m_Center = vec3(0, 0, 0);
	m_vModels.push_back(Model);
	m_vInfos.push_back(Info);
	std::vector<SFace> vFaces;
	const int Index = (int)m_vModels.size() - 1;
	vec2 Min(1e9f, 1e9f), Max(-1e9f, -1e9f);
	bool Any = false;
	// the rest pose and a few moments of the animations: wings may open much wider than they rest
	const SModel &Fit = m_vModels.back();
	for(int Sample = -1; Sample < 12; Sample++)
	{
		const bool Moving = Sample >= 6;
		const int Anim = Moving ? Fit.m_MoveAnim : Fit.m_IdleAnim;
		if(Sample >= 0 && Anim < 0)
			break;
		const float Time = Sample < 0 ? -1.0f : Fit.m_vAnimations[Anim].m_Length * (Sample % 6) / 6.0f;
		BuildFaces(Index, 0.0f, 0.0f, false, Time, Moving, vFaces);
		for(const SFace &Face : vFaces)
		{
			for(const vec2 &p : Face.m_aPoints)
			{
				Min = vec2(std::min(Min.x, p.x), std::min(Min.y, p.y));
				Max = vec2(std::max(Max.x, p.x), std::max(Max.y, p.y));
			}
			Any = true;
		}
	}
	if(!Any)
	{
		m_vModels.pop_back();
		m_vInfos.pop_back();
		return false;
	}
	SModel &Stored = m_vModels.back();
	Stored.m_Center = vec3((Min.x + Max.x) * 0.5f, (Min.y + Max.y) * 0.5f, 0.0f);
	Stored.m_Size = std::max(std::max(Max.x - Min.x, Max.y - Min.y), 0.001f);
	Stored.m_Bottom = (Min.y - Stored.m_Center.y) / Stored.m_Size;
	return true;
}

void CXcCosmetics::BuildFaces(int ModelIndex, float Yaw, float Pitch, bool Perspective, float Time, bool Moving, std::vector<SFace> &vFaces) const
{
	vFaces.clear();
	if(ModelIndex < 0 || ModelIndex >= (int)m_vModels.size())
		return;
	const SModel &Model = m_vModels[ModelIndex];
	const int NumBones = (int)Model.m_vBones.size();

	// the pose: rest rotation plus the animation at this time (Time < 0: rest pose)
	std::vector<vec3> vRot(NumBones), vPos(NumBones, vec3(0, 0, 0)), vScale(NumBones, vec3(1, 1, 1));
	for(int b = 0; b < NumBones; b++)
		vRot[b] = Model.m_vBones[b].m_Rotation;
	const int AnimIndex = Moving ? Model.m_MoveAnim : Model.m_IdleAnim;
	if(Time >= 0.0f && AnimIndex >= 0)
	{
		const SAnimation &Anim = Model.m_vAnimations[AnimIndex];
		const float t = Anim.m_Length > 0.0f ? std::fmod(Time, Anim.m_Length) : 0.0f;
		for(const SBoneAnim &BoneAnim : Anim.m_vBones)
		{
			for(int c = 0; c < 3; c++)
			{
				const std::vector<SKey> &vKeys = BoneAnim.m_avKeys[c];
				if(vKeys.empty())
					continue;
				vec3 Value = vKeys.back().m_Value;
				if(t <= vKeys.front().m_Time)
					Value = vKeys.front().m_Value;
				else
				{
					for(size_t k = 0; k + 1 < vKeys.size(); k++)
					{
						if(t >= vKeys[k].m_Time && t <= vKeys[k + 1].m_Time)
						{
							const float Span = vKeys[k + 1].m_Time - vKeys[k].m_Time;
							const float f = Span > 0.0f ? (t - vKeys[k].m_Time) / Span : 0.0f;
							Value = vKeys[k].m_Value + (vKeys[k + 1].m_Value - vKeys[k].m_Value) * f;
							break;
						}
					}
				}
				if(c == 0)
					vRot[BoneAnim.m_Bone] += Value;
				else if(c == 1)
					vPos[BoneAnim.m_Bone] = Value;
				else
					vScale[BoneAnim.m_Bone] = Value;
			}
		}
	}

	// bone transforms, parents first (a parent can come after its child in the file)
	std::vector<SMat> vWorld(NumBones);
	std::vector<char> vDone(NumBones, 0);
	for(int Pass = 0; Pass < NumBones; Pass++)
	{
		bool Progress = false;
		for(int b = 0; b < NumBones; b++)
		{
			if(vDone[b])
				continue;
			const int Parent = Model.m_vBones[b].m_Parent;
			if(Parent >= 0 && !vDone[Parent])
				continue;
			const vec3 Pivot = Model.m_vBones[b].m_Pivot;
			const vec3 Offset(-vPos[b].x, vPos[b].y, vPos[b].z);
			const SMat Local = Translation(Pivot + Offset) * Rotation(vRot[b]) * Scaling(vScale[b]) * Translation(-Pivot);
			vWorld[b] = Parent >= 0 ? vWorld[Parent] * Local : Local;
			vDone[b] = 1;
			Progress = true;
		}
		if(!Progress)
			break;
	}

	// view: turned around y (0 = front), then tilted
	const SMat View = Rotation(vec3(-Pitch, -Yaw, 0.0f));
	const float Distance = 3.2f; // the camera of the perspective, in model sizes
	const vec3 Light = normalize(vec3(0.35f, 0.8f, 0.5f));
	static const int s_aaQuads[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
	for(int b = 0; b < NumBones; b++)
	{
		if(!vDone[b])
			continue;
		for(const SCube &Cube : Model.m_vBones[b].m_vCubes)
		{
			const SMat CubeMat = Cube.m_Rotated ? Translation(Cube.m_Pivot) * Rotation(Cube.m_Rotation) * Translation(-Cube.m_Pivot) : SMat::Identity();
			const SMat Full = vWorld[b] * CubeMat;
			vec3 aCorners[8];
			vec3 Center(0, 0, 0);
			for(int i = 0; i < 8; i++)
			{
				const vec3 Local = Cube.m_Origin + vec3(Cube.m_Size.x * (i & 1), Cube.m_Size.y * ((i >> 1) & 1), Cube.m_Size.z * ((i >> 2) & 1));
				vec3 p = Full.Apply(Local);
				p.x = -p.x; // the Bedrock x axis points the other way
				p = (p - Model.m_Center) / Model.m_Size;
				aCorners[i] = View.Apply(p);
				Center += aCorners[i] / 8.0f;
			}
			for(const auto &aQuad : s_aaQuads)
			{
				const vec3 &a = aCorners[aQuad[0]], &b2 = aCorners[aQuad[1]], &c = aCorners[aQuad[2]], &d = aCorners[aQuad[3]];
				const vec3 FaceCenter = (a + b2 + c + d) / 4.0f;
				vec3 Normal = cross(b2 - a, d - a);
				const float NormalLength = length(Normal);
				if(NormalLength < 1e-9f)
					continue;
				Normal = Normal / NormalLength;
				// point the normal out of the cube; a flat cube shows its side that faces the camera
				const vec3 Out = FaceCenter - Center;
				if(length(Out) > 1e-6f)
				{
					if(dot(Normal, Out) < 0.0f)
						Normal = -Normal;
				}
				else if(Normal.z < 0.0f)
					Normal = -Normal;
				const vec3 ToCamera = Perspective ? vec3(0.0f, 0.0f, Distance) - FaceCenter : vec3(0.0f, 0.0f, 1.0f);
				if(dot(Normal, ToCamera) <= 0.0f)
					continue;
				SFace Face;
				for(int i = 0; i < 4; i++)
				{
					const vec3 &p = aCorners[aQuad[i]];
					const float f = Perspective ? Distance / std::max(Distance - p.z, 0.2f) : 1.0f;
					Face.m_aPoints[i] = vec2(p.x * f, p.y * f);
				}
				Face.m_Depth = FaceCenter.z;
				Face.m_Light = 0.42f + 0.58f * std::max(0.0f, dot(Normal, Light));
				vFaces.push_back(Face);
			}
		}
	}
	// far ones first
	std::sort(vFaces.begin(), vFaces.end(), [](const SFace &x, const SFace &y) { return x.m_Depth < y.m_Depth; });
}

ColorRGBA CXcCosmetics::Color(int Slot, int ClientId) const
{
	switch(Slot)
	{
	case SLOT_BACK: return EffectColor(GameClient(), g_Config.m_XcCosmeticBackColorMode, g_Config.m_XcCosmeticBackColor, ClientId);
	case SLOT_HAT: return EffectColor(GameClient(), g_Config.m_XcCosmeticHatColorMode, g_Config.m_XcCosmeticHatColor, ClientId, 0.33f);
	default: return EffectColor(GameClient(), g_Config.m_XcCosmeticPetColorMode, g_Config.m_XcCosmeticPetColor, ClientId, 0.66f);
	}
}

int CXcCosmetics::Selected(int Slot) const
{
	switch(Slot)
	{
	case SLOT_BACK: return FindModel(g_Config.m_XcCosmeticBack);
	case SLOT_HAT: return FindModel(g_Config.m_XcCosmeticHat);
	default: return FindModel(g_Config.m_XcCosmeticPet);
	}
}

CXcCosmetics::SOutline CXcCosmetics::Outline(int Slot, ColorRGBA Color) const
{
	int *apOn[NUM_SLOTS] = {&g_Config.m_XcCosmeticBackOutline, &g_Config.m_XcCosmeticHatOutline, &g_Config.m_XcCosmeticPetOutline};
	int *apWidth[NUM_SLOTS] = {&g_Config.m_XcCosmeticBackOutlineWidth, &g_Config.m_XcCosmeticHatOutlineWidth, &g_Config.m_XcCosmeticPetOutlineWidth};
	int *apAuto[NUM_SLOTS] = {&g_Config.m_XcCosmeticBackOutlineAuto, &g_Config.m_XcCosmeticHatOutlineAuto, &g_Config.m_XcCosmeticPetOutlineAuto};
	unsigned *apColor[NUM_SLOTS] = {&g_Config.m_XcCosmeticBackOutlineColor, &g_Config.m_XcCosmeticHatOutlineColor, &g_Config.m_XcCosmeticPetOutlineColor};
	const int s = std::clamp(Slot, 0, NUM_SLOTS - 1);
	SOutline Out;
	Out.m_Enabled = *apOn[s] != 0;
	Out.m_Width = *apWidth[s] / 100.0f;
	Out.m_Auto = *apAuto[s] != 0;
	// automatic: a much darker shade of the model
	Out.m_Color = Out.m_Auto ? ColorRGBA(Color.r * 0.22f, Color.g * 0.22f, Color.b * 0.25f, 1.0f) : HslConfigColor(*apColor[s]);
	return Out;
}

void CXcCosmetics::DrawFaces(vec2 Pos, float Scale, ColorRGBA Color, float Alpha, bool Cartoon, const SOutline &Line) const
{
	const auto ToWorld = [&](vec2 p) { return Pos + vec2(p.x, -p.y) * Scale; };
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	if(Cartoon && Line.m_Enabled)
	{
		// our 2D look, like the tees: one thick outline around the whole shape first...
		const float Outline = std::max(Scale * 0.035f, 2.0f) * Line.m_Width;
		Graphics()->SetColor(Line.m_Color.WithAlpha(Alpha));
		for(const SFace &Face : m_vFaces)
		{
			vec2 Center(0.0f, 0.0f);
			vec2 aP[4];
			for(int i = 0; i < 4; i++)
			{
				aP[i] = ToWorld(Face.m_aPoints[i]);
				Center += aP[i] * 0.25f;
			}
			for(vec2 &p : aP)
			{
				const vec2 Out = p - Center;
				const float Length = length(Out);
				if(Length > 0.001f)
					p += Out / Length * Outline;
			}
			const IGraphics::CFreeformItem Item(aP[0].x, aP[0].y, aP[1].x, aP[1].y, aP[3].x, aP[3].y, aP[2].x, aP[2].y);
			Graphics()->QuadsDrawFreeform(&Item, 1);
		}
	}
	for(const SFace &Face : m_vFaces)
	{
		vec2 aP[4];
		for(int i = 0; i < 4; i++)
			aP[i] = ToWorld(Face.m_aPoints[i]);
		if(Cartoon)
		{
			// ...then soft flat tones: the light in a few steps, a little lighter at the top
			const float Tone = Face.m_Light > 0.85f ? 1.0f : (Face.m_Light > 0.6f ? 0.82f : 0.66f);
			const auto Shade = [&](const vec2 &p, float Extra) {
				const float l = std::min(Tone + Extra, 1.15f);
				return ColorRGBA(std::min(Color.r * l, 1.0f), std::min(Color.g * l, 1.0f), std::min(Color.b * l, 1.0f), Alpha);
			};
			// lighter on the upper corners of the face
			float Top = aP[0].y, Bottom = aP[0].y;
			for(const vec2 &p : aP)
			{
				Top = std::min(Top, p.y);
				Bottom = std::max(Bottom, p.y);
			}
			const float Height = std::max(Bottom - Top, 0.001f);
			ColorRGBA aColors[4];
			for(int i = 0; i < 4; i++)
				aColors[i] = Shade(aP[i], 0.12f * (1.0f - (aP[i].y - Top) / Height));
			Graphics()->SetColor4(aColors[0], aColors[1], aColors[3], aColors[2]);
		}
		else
		{
			const float l = Face.m_Light;
			Graphics()->SetColor(Color.r * l, Color.g * l, Color.b * l, Alpha);
		}
		const IGraphics::CFreeformItem Item(aP[0].x, aP[0].y, aP[1].x, aP[1].y, aP[3].x, aP[3].y, aP[2].x, aP[2].y);
		Graphics()->QuadsDrawFreeform(&Item, 1);
	}
	Graphics()->QuadsEnd();
	if(Cartoon || !Line.m_Enabled)
		return;
	// 3D: thin edges keep the cubes apart; automatic ones are a darker shade of each face
	Graphics()->LinesBegin();
	for(const SFace &Face : m_vFaces)
	{
		const float l = Face.m_Light * 0.55f;
		if(Line.m_Auto)
			Graphics()->SetColor(Color.r * l, Color.g * l, Color.b * l, Alpha * 0.9f);
		else
			Graphics()->SetColor(Line.m_Color.WithAlpha(Alpha * 0.9f));
		IGraphics::CLineItem aLines[4];
		for(int i = 0; i < 4; i++)
		{
			const vec2 a = ToWorld(Face.m_aPoints[i]);
			const vec2 b = ToWorld(Face.m_aPoints[(i + 1) % 4]);
			aLines[i] = IGraphics::CLineItem(a.x, a.y, b.x, b.y);
		}
		Graphics()->LinesDraw(aLines, 4);
	}
	Graphics()->LinesEnd();
}

void CXcCosmetics::RenderSlot(int Slot, int ClientId, float Alpha)
{
	const int ModelIndex = Selected(Slot);
	if(ModelIndex < 0)
		return;
	const SModel &Model = m_vModels[ModelIndex];
	const SModelInfo &Info = m_vInfos[ModelIndex];
	SState &State = m_aStates[ClientId];
	const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
	const float TeeSize = Data.m_RenderInfo.m_Size;
	const vec2 TeePos = Data.m_RenderPos;
	const bool View3D = g_Config.m_XcCosmetic3d != 0;
	const float Turn = Slot == SLOT_PET ? State.m_PetTurn : State.m_Turn;
	const float Pitch = View3D ? 12.0f : 0.0f;
	const vec2 Vel = CharacterVelocity(GameClient(), ClientId);
	const bool Moving = length(Vel) > 200.0f;
	const float Time = g_Config.m_XcCosmeticAnim ? State.m_Time : -1.0f;

	float Yaw, Fit;
	vec2 Pos;
	if(Slot == SLOT_BACK && Info.m_Wings)
	{
		// spread behind the tee; turned a little to its back in 3D
		Yaw = View3D ? -25.0f * Turn : 0.0f;
		Fit = 1.7f;
		Pos = TeePos + vec2(-Turn * TeeSize * 0.14f, -TeeSize * 0.32f);
	}
	else if(Slot == SLOT_BACK)
	{
		// a backpack seen from the side, on the back of the tee
		Yaw = View3D ? -50.0f * Turn : -90.0f * Turn;
		Fit = 0.75f;
		Pos = TeePos + vec2(-Turn * TeeSize * 0.42f, TeeSize * 0.02f);
	}
	else if(Slot == SLOT_HAT)
	{
		Yaw = View3D ? -50.0f * Turn : -90.0f * Turn;
		// Minecraft hats are made for a whole head: smaller and sitting on top of the tee
		Fit = 0.72f;
		Pos = TeePos;
	}
	else
	{
		Yaw = View3D ? -50.0f * Turn : -90.0f * Turn;
		Fit = 0.55f;
		Pos = State.m_PetPos;
	}
	const float Scale = TeeSize * Fit * g_Config.m_XcCosmeticSize / 100.0f;
	if(Slot == SLOT_HAT)
	{
		// the bottom of the model sits a little into the top of the head
		const float HeadTop = TeePos.y - TeeSize * 0.36f;
		Pos.y = HeadTop + Model.m_Bottom * Scale + TeeSize * 0.1f;
	}

	BuildFaces(ModelIndex, Yaw, Pitch, View3D, Time, Moving, m_vFaces);
	const ColorRGBA Main = Color(Slot, ClientId);
	if(g_Config.m_XcCosmeticGlow && GameClient()->m_XcSprites.Loaded())
	{
		CXcSprites &Sprites = GameClient()->m_XcSprites;
		Sprites.Begin(CXcSprites::SPRITE_GLOW, true);
		Sprites.Draw(Pos, Scale * 1.8f, 0.0f, Main.WithAlpha(0.35f * Alpha));
		Sprites.End();
	}
	DrawFaces(Pos, Scale, Main, Alpha, !View3D, Outline(Slot, Main));
}

void CXcCosmetics::RenderBehind()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(Selected(SLOT_BACK) < 0)
		return;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!m_aStates[ClientId].m_Valid)
			continue;
		const float Alpha = PlayerAlpha(GameClient(), ClientId);
		if(Alpha > 0.0f)
			RenderSlot(SLOT_BACK, ClientId, Alpha);
	}
}

void CXcCosmetics::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	const float Dt = std::min(Client()->RenderFrameTime(), 0.1f);
	const bool Any = Selected(SLOT_BACK) >= 0 || Selected(SLOT_HAT) >= 0 || Selected(SLOT_PET) >= 0;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		SState &State = m_aStates[ClientId];
		if(!Any || !GameClient()->m_Snap.m_aCharacters[ClientId].m_Active ||
			!ShowFor(GameClient(), ClientId, g_Config.m_XcCosmeticSelf, g_Config.m_XcCosmeticDummy, g_Config.m_XcCosmeticOthers))
		{
			State.m_Valid = false;
			continue;
		}
		const CGameClient::CClientData &Data = GameClient()->m_aClients[ClientId];
		const float TeeSize = Data.m_RenderInfo.m_Size;
		const vec2 TeePos = Data.m_RenderPos;
		const bool LooksLeft = direction(Data.m_RenderCur.m_Angle / 256.0f).x < 0.0f;
		const vec2 PetTarget = TeePos + vec2((LooksLeft ? 1.0f : -1.0f) * TeeSize * 0.95f, -TeeSize * 0.75f + std::sin(State.m_Time * 2.2f) * TeeSize * 0.08f);
		if(!State.m_Valid || distance(State.m_PetPos, TeePos) > TeeSize * 12.0f)
		{
			State = SState();
			State.m_Valid = true;
			State.m_PetPos = PetTarget;
			State.m_PetVel = vec2(0, 0);
			State.m_Turn = State.m_PetTurn = LooksLeft ? -1.0f : 1.0f;
		}
		State.m_Time += Dt;
		// turning around is a short animation, not a jump
		const float TurnTarget = LooksLeft ? -1.0f : 1.0f;
		State.m_Turn += (TurnTarget - State.m_Turn) * SmoothFactor(9.0f, Dt);
		// the pet follows on a soft spring and looks where it flies
		const vec2 Pull = PetTarget - State.m_PetPos;
		State.m_PetVel += Pull * (28.0f * Dt);
		State.m_PetVel *= std::pow(0.02f, Dt);
		State.m_PetPos += State.m_PetVel * Dt;
		const float PetTurnTarget = std::abs(State.m_PetVel.x) > 25.0f ? (State.m_PetVel.x < 0.0f ? -1.0f : 1.0f) : TurnTarget;
		State.m_PetTurn += (PetTurnTarget - State.m_PetTurn) * SmoothFactor(6.0f, Dt);
	}
	if(!Any)
		return;
	for(int ClientId = 0; ClientId < MAX_CLIENTS; ClientId++)
	{
		if(!m_aStates[ClientId].m_Valid)
			continue;
		const float Alpha = PlayerAlpha(GameClient(), ClientId);
		if(Alpha <= 0.0f)
			continue;
		RenderSlot(SLOT_HAT, ClientId, Alpha);
		RenderSlot(SLOT_PET, ClientId, Alpha);
	}
}

void CXcCosmeticsBehind::OnRender()
{
	GameClient()->m_XcCosmetics.RenderBehind();
}
