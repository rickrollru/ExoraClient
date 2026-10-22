#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_WORLD3D_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_WORLD3D_H

#include <game/client/component.h>

#include <cstdint>
#include <memory>
#include <vector>

// 3D view (xc_3d): the world is seen by a tilted perspective camera and the blocks of the map get depth.
// Everything DDNet draws between this component and m_End goes through the tilted camera (IGraphics::XcSetTilt);
// the sides of the blocks are drawn by the render module under the layers of the game group.
// Only the picture changes: the camera, the input and the physics stay as they are. OpenGL 3.3 only.
class CXcWorld3D : public CComponent
{
public:
	// the end of the tilted part, right before the screen overlays and the HUD
	class CEnd : public CComponent
	{
	public:
		CXcWorld3D *m_pOwner = nullptr;
		int Sizeof() const override { return sizeof(*this); }
		void OnRender() override;
	};
	CEnd m_End;

	// tilts what is drawn inside it (the crosshair, drawn by the HUD)
	class CTiltScope
	{
		CXcWorld3D *m_pWorld;
		bool m_On;

	public:
		explicit CTiltScope(CXcWorld3D *pWorld);
		~CTiltScope();
	};

	CXcWorld3D() { m_End.m_pOwner = this; }
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnMapLoad() override;

	bool Active() const;

private:
	friend class CEnd;
	friend class CTiltScope;
	void Tilt(bool On);
	void DrawBlocks();

	bool m_FrameOn = false; // the tilted part of this frame is being drawn
	bool m_BlocksDrawn = false;
	float m_Yaw = 0.0f; // eased, radians
	float m_Dir = 1.0f; // eased direction of motion
	bool m_HasDir = false;
	float m_LastCenterX = 0.0f;
	int64_t m_LastTime = 0;
	std::vector<uint8_t> m_vGrid;

	// the color of every block of the map, from the design tiles in front of it (worked out in a job)
	class CColorJob;
	std::shared_ptr<CColorJob> m_pColorJob;
	std::vector<uint8_t> m_vColors; // rgba per cell, a: the color is set
	int m_ColorsW = 0;
	int m_ColorsH = 0;
	bool m_ColorsWanted = false;
	void UpdateColors();
};

#endif
