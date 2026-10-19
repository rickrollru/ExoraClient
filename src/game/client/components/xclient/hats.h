#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_HATS_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_HATS_H

#include "ui/backgrounds.h"

#include <base/vmath.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>

#include <map>
#include <string>

// Own hats: any picture or gif from xclient/hats is worn on top of the tee. Position, size and
// rotation are set in the cosmetics editor (Visuals > Cosmetics). Only this client sees the hat.
class CXcHats : public CComponent
{
public:
	CXcHats();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnReset() override;
	void OnMapLoad() override;

	CXcBackgrounds &Images() { return m_Images; }
	// puts on another hat: the current one keeps its place, the new one gets its own (or a good default)
	void SelectHat(const char *pName);
	void ResetPlacement();
	// loads the chosen hat; called by the game and by the menu so the editor works in the main menu too
	void Update();
	bool HasHat() const { return m_Images.HasFrames(); }
	// draws the hat on a tee at Pos with the given tee size, in the current screen mapping
	void RenderOnTee(vec2 Pos, float TeeSize, bool LookingLeft, float Tilt, float Spring, float Alpha) const;
	// center and size of the hat for a tee at Pos (without sway), for the editor frame
	void HatRect(vec2 Pos, float TeeSize, bool LookingLeft, vec2 *pCenter, vec2 *pSize) const;

private:
	struct SState
	{
		bool m_Valid = false;
		vec2 m_Pos = vec2(0.0f, 0.0f);
		vec2 m_LastVel = vec2(0.0f, 0.0f);
		float m_Tilt = 0.0f;
		float m_Spring = 0.0f;
		float m_SpringVel = 0.0f;
	};
	SState m_aStates[MAX_CLIENTS];
	CXcBackgrounds m_Images;

	// every hat remembers where it sits: xclient/hat_placements.txt, "x y size rotation name" per line
	struct SPlacement
	{
		int m_X, m_Y, m_Size, m_Rotation;
		int m_Behind = 0;
	};
	std::map<std::string, SPlacement> m_Placements;
	static SPlacement DefaultPlacement(const char *pName);
	void StoreCurrentPlacement();
	void LoadPlacements();
	void SavePlacements();
	float m_LastUpdate = -1.0f;

	void UpdateState(int ClientId, vec2 Pos, float Dt);
	friend class CXcHatsBehind;
	friend class CXcOverTiles;
	// draws the hats of all players; called once a frame, behind or in front of the tees (xc_hat_behind)
	void RenderAll();
	bool LooksLeft(int ClientId) const;
};

// The pass behind the tees (wings, capes), registered before the players.
class CXcHatsBehind : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

// Registered right after the front map layer: with xc_fx_over_tiles / xc_hats_over_tiles the queued
// effects and the hats are drawn here, over the blocks, instead of under them.
class CXcOverTiles : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnRender() override;
};

#endif
