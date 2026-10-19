#ifndef GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_EDITOR_STYLE_H
#define GAME_CLIENT_COMPONENTS_XCLIENT_EDITOR_EDITOR_STYLE_H

#include <base/color.h>

// The look of the map editor in the XClient style (xc_editor_style): flat dark panels like in
// Photoshop, buttons in neutral grays, the selection in the menu accent color.
namespace XcEditorStyle
{
bool Enabled();
ColorRGBA Accent(float Alpha = 1.0f);
ColorRGBA Window(); // menu bar and the area around the panels
ColorRGBA Panel(); // layer list, tool bar, status bar
ColorRGBA PanelLine(); // 1px separators between the panels
// button color for the editor's "Checked" states, see CEditor::GetButtonColor
ColorRGBA ButtonColor(int Checked, bool Hot);
// the transparency grid around and under the map
float CheckerSize();
float CheckerBrightness();
// the font of the editor (Manrope from the menu), empty when the classic font stays
const char *FontFace();
} // namespace XcEditorStyle

#endif
