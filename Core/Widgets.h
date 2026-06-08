#pragma once

#include <mq/Plugin.h>

namespace myui
{
enum ToggleFlags
{
	ToggleFlags_None          = 0,
	ToggleFlags_StarKnob      = 1 << 0,
	ToggleFlags_RightLabel    = 1 << 1,
	ToggleFlags_AnimateKnob   = 1 << 2,
	ToggleFlags_GlowOnHover   = 1 << 3,
	ToggleFlags_AnimateOnHover = 1 << 4,
	ToggleFlags_PulseOnHover  = 1 << 5,
	ToggleFlags_KnobBorder    = 1 << 6,
};

constexpr int kDefaultToggleFlags =
	ToggleFlags_RightLabel | ToggleFlags_GlowOnHover | ToggleFlags_PulseOnHover;

constexpr int kUseGlobalToggleFlags = -1;

void SetGlobalToggleStyle(int flags, ImVec2 size = ImVec2(0.0f, 0.0f));

bool DrawToggle(const char* id, bool* value, int flags = kUseGlobalToggleFlags, ImVec2 size = ImVec2(0.0f, 0.0f));

// imanim-driven style widgets (see Widgets.cpp). Colors come from the active
// ImGui theme; motion values are tuned in demos/style-playground.html.

// Pill tab bar with a sliding (spring) selection. Returns the selected index.
int PillTabBar(const char* id, const char* const labels[], int count, int current);

// Pill selectable row: animated hover/selected fill. width <= 0 = content width.
bool PillSelectable(const char* label, bool selected, float width = 0.0f);

// Animated expand/collapse tree node (height reveal + rotating chevron). The
// caller owns the open state via *open (toggled here on click) so several nodes
// can be coordinated into an exclusive accordion. Render children between
// Begin/End only when Begin returns true:
//   bool open = (m_sel == name);
//   if (BeginAnimatedTreeNode("Name", &open)) { ...; EndAnimatedTreeNode(); }
bool BeginAnimatedTreeNode(const char* label, bool* open);
void EndAnimatedTreeNode();

// Animated collapsing header (filled header row + height reveal + chevron). Same
// Begin/End pattern as the tree node above.
bool BeginAnimatedHeader(const char* label, bool defaultOpen = false);
void EndAnimatedHeader();

// Custom-styled slider: we draw the pill track / accent fill / grab knob from
// theme colors, while ImGui handles all interaction underneath a transparent
// frame — so Ctrl+click direct text entry and keyboard input still work.
bool StyledSliderFloat(const char* label, float* v, float vmin, float vmax, const char* fmt = "%.2f", ImGuiSliderFlags flags = 0);
bool StyledSliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt = "%d", ImGuiSliderFlags flags = 0);

// Custom-styled combo: rounded frame, animated chevron, pill-highlighted items.
// StyledCombo wraps the simple array form; StyledBeginCombo/EndCombo wrap custom
// item loops (use PillSelectable for the items between Begin/End).
bool StyledCombo(const char* label, int* currentItem, const char* const items[], int count);
bool StyledBeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0);
void StyledEndCombo();

// Animated checkbox: rounded box, accent fill that scales in, and a checkmark
// that draws itself in (out_back), all theme-colored. Drop-in for ImGui::Checkbox.
bool StyledCheckbox(const char* label, bool* v);
}
