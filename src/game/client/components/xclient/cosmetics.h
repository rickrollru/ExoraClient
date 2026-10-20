#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_COSMETICS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_COSMETICS_H

#include <base/color.h>
#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <string>
#include <vector>

// 3D cosmetics made of cubes (Minecraft Bedrock geometry, from the Flower client with permission):
// wings and backpacks on the back, hats on the head and pets flying next to the tee.
// The models are drawn by this client only, cube faces with simple light, sorted back to front;
// in the 2D view flat from the side or the front, in the 3D view turned and with perspective.
// Their keyframe animations play (wings flap, pets move). Only this client sees them.
class CXcCosmetics : public CComponent
{
public:
	enum
	{
		SLOT_BACK = 0,
		SLOT_HAT,
		SLOT_PET,
		NUM_SLOTS,
	};
	struct SFace
	{
		vec2 m_aPoints[4]; // model space, y up, the model is about 1 unit big
		float m_Depth;
		float m_Light; // 0..1
	};
	struct SModelInfo
	{
		std::string m_Id; // file name without .json
		std::string m_Name;
		int m_Slot;
		bool m_Wings; // shown from the front, spread behind the tee
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnReset() override;
	void OnRender() override; // hats and pets, in front of the tees

	// wings and backpacks, called by CXcCosmeticsBehind before the tees are drawn
	void RenderBehind();

	const std::vector<SModelInfo> &Models() const { return m_vInfos; }
	int FindModel(const char *pId) const;
	// faces of a model for previews: Yaw in degrees (0 = front), Time plays the idle animation
	void BuildFaces(int Model, float Yaw, float Pitch, bool Perspective, float Time, bool Moving, std::vector<SFace> &vFaces) const;
	ColorRGBA Color(int Slot, int ClientId) const;

private:
	struct SCube
	{
		vec3 m_Origin;
		vec3 m_Size;
		vec3 m_Rotation;
		vec3 m_Pivot;
		bool m_Rotated;
	};
	struct SBone
	{
		std::string m_Name;
		int m_Parent;
		vec3 m_Pivot;
		vec3 m_Rotation;
		std::vector<SCube> m_vCubes;
	};
	struct SKey
	{
		float m_Time;
		vec3 m_Value;
	};
	struct SBoneAnim
	{
		int m_Bone;
		std::vector<SKey> m_avKeys[3]; // rotation, position, scale
	};
	struct SAnimation
	{
		std::string m_Name;
		float m_Length;
		std::vector<SBoneAnim> m_vBones;
	};
	struct SModel
	{
		std::vector<SBone> m_vBones;
		std::vector<SAnimation> m_vAnimations;
		int m_IdleAnim = -1;
		int m_MoveAnim = -1;
		// rest pose, front view: center and size used to fit the model
		vec3 m_Center;
		float m_Size = 1.0f;
		float m_Bottom = 0.0f; // lowest point under the center
	};
	std::vector<SModel> m_vModels;
	std::vector<SModelInfo> m_vInfos;

	// per player: the pet follows on a spring, the view turns smoothly when the tee turns
	struct SState
	{
		bool m_Valid = false;
		vec2 m_PetPos;
		vec2 m_PetVel;
		float m_Turn = 1.0f; // +1 looking right, -1 left, eased
		float m_PetTurn = 1.0f;
		float m_Time = 0.0f;
	};
	SState m_aStates[MAX_CLIENTS];
	mutable std::vector<SFace> m_vFaces;
	int m_LastSelection[NUM_SLOTS] = {-2, -2, -2};

	bool LoadModel(const char *pFile, const char *pId);
	static int ListCallback(const char *pName, int IsDir, int StorageType, void *pUser);
	int Selected(int Slot) const;
	void RenderSlot(int Slot, int ClientId, float Alpha);
	// Cartoon: our 2D look, a thick outline around the shape and soft flat tones
	void DrawFaces(vec2 Pos, float Scale, ColorRGBA Color, float Alpha, bool Cartoon) const;
};

// The pass behind the tees, registered before the players
class CXcCosmeticsBehind : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
