#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_CURSOR_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_CURSOR_H

#include <base/vmath.h>

class IGraphics;

// The mouse cursor of the menus (new and classic), the HUD editor and the wheels: an arrow in the accent
// color by default, a dot, a triangle or an own picture (xc_cursor*). CRenderTools::RenderCursor asks here first.
namespace XcCursor
{
	enum
	{
		CURSOR_STANDARD = 0, // the cursor of the game skin
		CURSOR_ARROW,
		CURSOR_DOT,
		CURSOR_TRIANGLE,
		CURSOR_PICTURE, // xclient/cursors/<xc_cursor_file>
		NUM_CURSORS,
	};
	// Pos is where the cursor points (the top left corner of the classic cursor); true when it was drawn
	bool Render(IGraphics *pGraphics, vec2 Pos, float Size, float Alpha);
}

#endif
