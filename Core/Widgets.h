#pragma once

#include <mq/Plugin.h>

#include <string>

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

// Master switch for the animated widget effects (button glow/press/shimmer,
// toggle glow/pulse/rock, toolbar hover glow). When off, widgets still render
// from the theme but draw their resting state with no tweens. Pushed once per
// frame from MQMyUI.cpp before RenderAll, driven by the per-char setting.
void SetAnimationsEnabled(bool enabled);
bool AnimationsEnabled();

// Soft outer halo behind a rounded rect (a few expanding rounded rects with
// falling alpha), scaled by intensity 0..1. Shared by the styled button and the
// toolbar hover so their glows match. Colors come from the caller (theme-sourced).
void SoftGlowRoundRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, ImVec4 col, float intensity);

// --- Reusable animated-control primitives -----------------------------------
// Theme-neutral building blocks for custom framed/animated controls (used by the
// styled button + toolbar; available to any host). All honor SetAnimationsEnabled
// and key their motion off the caller-supplied ImGuiID (use ImGui::GetID(...)).

// Eased hover intensity 0..1 for an item. Snaps to 0/1 when animations are off.
float HoverIntensity(ImGuiID id, bool hovered);

// Spring press scale around 1.0: shrinks to pressedScale while active, then a
// lightly-damped spring overshoots past 1.0 and settles on release. Returns 1.0
// when animations are off. Apply to the *drawn* rect, not the layout rect.
float PressScale(ImGuiID id, bool active, float pressedScale = 0.86f);

// Diagonal white hover sheen swept across a rounded rect, stencil-masked to the
// shape so it never spills into the corners. No-op when animations are off or
// intensity <= 0.
void DrawHoverShimmer(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding, float intensity);

enum ButtonFlags
{
	ButtonFlags_None        = 0,
	ButtonFlags_GlowOnHover = 1 << 0,
	ButtonFlags_PressSink   = 1 << 1, // label/fill sinks slightly while held
	ButtonFlags_Shimmer     = 1 << 2, // subtle hover sheen sweep
};

constexpr int kDefaultButtonFlags = ButtonFlags_GlowOnHover | ButtonFlags_PressSink | ButtonFlags_Shimmer;
constexpr int kUseGlobalButtonFlags = -1;

// Drop-in replacements for ImGui::Button / ImGui::SmallButton. Sized like ImGui
// (text + frame padding; small = no vertical pad), colored live from the theme
// (ImGuiCol_Button / ButtonHovered / ButtonActive / Border / Text) so they fit
// any ThemeZ/DB theme, with imanim hover/press motion that respects the global
// animation switch above. Pass flags = -1 to use kDefaultButtonFlags.
bool StyledButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f), int flags = kUseGlobalButtonFlags);
bool StyledSmallButton(const char* label, int flags = kUseGlobalButtonFlags);

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

// (?) help marker: a TextDisabled("(?)") that shows a wrapped tooltip after a
// half-second hover. Pair with ImGui::SameLine() to trail a control.
void HelpMarker(const char* desc);

// Cursor-anchored single-line input popup with Accept/Cancel. Call
// OpenInputPopup(id, initial) to open it at the mouse, then each frame call
// InputPopup(id, out) which returns true (and fills out) only when accepted.
// Only one such popup is open at a time (shared internal buffer).
void OpenInputPopup(const char* popupId, const char* initialText);
bool InputPopup(const char* popupId, std::string& out, const char* hint = nullptr);

// Click-to-edit field: a rounded, read-only display of the current text with a
// pencil affordance; clicking it opens the cursor-anchored InputPopup (Accept/
// Cancel) to edit. Returns true and writes the value only when accepted. Width
// follows SetNextItemWidth/CalcItemWidth; the visible label (text after "##") is
// drawn after the field; the optional hint shows when the value is empty.
// Drop-in for a plain ImGui::InputText on persistent value fields.
bool StyledEditField(const char* label, std::string* value, const char* hint = nullptr);
bool StyledEditField(const char* label, char* buf, size_t bufSize, const char* hint = nullptr);
}
