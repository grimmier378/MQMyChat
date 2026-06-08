#include "Widgets.h"

#include <imgui/imgui_internal.h>
#include "imgui/imanim/im_anim.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace myui
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;

void RenderStar(ImDrawList* dl, ImVec2 center, float radius, float rot, ImU32 col)
{
	const int points = 5;
	ImVec2 pts[10];
	for (int i = 0; i < points * 2; ++i)
	{
		float r = (i & 1) ? radius * 0.45f : radius;
		float a = rot + (float)i * (kPi / points) - kPi * 0.5f;
		pts[i] = ImVec2(center.x + cosf(a) * r, center.y + sinf(a) * r);
	}
	for (int i = 0; i < points * 2; ++i)
	{
		dl->AddTriangleFilled(center, pts[i], pts[(i + 1) % (points * 2)], col);
	}
}

void RenderMoon(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, ImU32 carve)
{
	dl->AddCircleFilled(center, radius, col, 16);
	dl->AddCircleFilled(ImVec2(center.x + radius * 0.45f, center.y - radius * 0.2f), radius * 0.9f, carve, 16);
}

int g_globalToggleFlags = kDefaultToggleFlags;
ImVec2 g_globalToggleSize = ImVec2(0.0f, 0.0f);
} // namespace

void SetGlobalToggleStyle(int flags, ImVec2 size)
{
	g_globalToggleFlags = flags;
	g_globalToggleSize = size;
}

bool DrawToggle(const char* id, bool* value, int flags, ImVec2 size)
{
	if (!value)
	{
		return false;
	}

	if (flags < 0)
	{
		flags = g_globalToggleFlags;
	}
	if (size.x <= 0.0f && size.y <= 0.0f)
	{
		size = g_globalToggleSize;
	}

	char label[128] = { 0 };
	const char* sep = strstr(id, "##");
	size_t labelLen = sep ? (size_t)(sep - id) : strlen(id);
	if (labelLen >= sizeof(label))
	{
		labelLen = sizeof(label) - 1;
	}
	memcpy(label, id, labelLen);
	label[labelLen] = 0;

	float height = size.y > 0.0f ? size.y : ImGui::GetFrameHeight() * 0.85f;
	float width  = size.x > 0.0f ? size.x : height * 1.9f;
	float radius = height * 0.5f;

	const bool rightLabel = (flags & ToggleFlags_RightLabel) != 0;
	const bool hasLabel   = label[0] != 0;

	ImGui::BeginGroup();
	ImGui::PushID(id);

	if (hasLabel && !rightLabel)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine();
	}

	ImVec2 pos = ImGui::GetCursorScreenPos();
	float yOff = (ImGui::GetFrameHeight() - height) * 0.5f;
	pos.y += yOff > 0.0f ? yOff : 0.0f;

	ImGui::InvisibleButton("##knob", ImVec2(width, height));
	bool clicked = ImGui::IsItemClicked();
	bool hovered = ImGui::IsItemHovered();
	if (clicked)
	{
		*value = !*value;
	}

	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImVec4 offCol = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
	ImVec4 onCol  = ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive);

	float dt = ImGui::GetIO().DeltaTime;
	dt = (dt <= 0.0f) ? (1.0f / 60.0f) : (dt > 0.1f ? 0.1f : dt);
	const ImGuiID animId = ImGui::GetID("##knob");

	// Track color crossfades on<->off in OKLAB; the thumb slides with a slight
	// overshoot (out_back). imanim drives both; the flags below still apply.
	ImVec4 trackCol = iam_tween_color(animId, ImHashStr("rv_tgbg"), *value ? onCol : offCol, 0.20f,
		iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, iam_col_oklab, dt, *value ? onCol : offCol);
	dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(trackCol), radius);

	float t = iam_tween_float(animId, ImHashStr("rv_tgpos"), *value ? 1.0f : 0.0f, 0.25f,
		iam_ease_preset(iam_ease_out_back), iam_policy_crossfade, dt, *value ? 1.0f : 0.0f);
	ImVec2 knob(pos.x + radius + t * (width - height), pos.y + radius);

	const float time = (float)ImGui::GetTime();
	bool animate = (flags & ToggleFlags_AnimateKnob) || ((flags & ToggleFlags_AnimateOnHover) && hovered);

	ImVec4 knobCol(1.0f, 1.0f, 1.0f, 1.0f);
	if ((flags & ToggleFlags_PulseOnHover) && hovered)
	{
		float pulse = 0.6f + 0.4f * sinf(time * 4.0f);
		knobCol = ImVec4(pulse, pulse, pulse, 1.0f);
	}
	ImU32 knobU32 = ImGui::GetColorU32(knobCol);
	ImU32 trackU32 = ImGui::GetColorU32(trackCol);

	if ((flags & ToggleFlags_GlowOnHover) && hovered)
	{
		float breathe = 0.25f + 0.2f * sinf(time * 3.0f);
		for (int i = 3; i >= 1; --i)
		{
			ImVec4 g = *value ? onCol : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
			g.w = breathe / (float)i;
			dl->AddCircleFilled(knob, radius * (0.8f + 0.35f * i), ImGui::GetColorU32(g), 20);
		}
	}

	// Soft drop shadow under the thumb (imanim toggle detail).
	dl->AddCircleFilled(ImVec2(knob.x + 1.0f, knob.y + 2.0f), radius * 0.8f, IM_COL32(0, 0, 0, 35), 20);

	if (flags & ToggleFlags_StarKnob)
	{
		if (*value)
		{
			RenderStar(dl, knob, radius * 0.8f, animate ? time * 1.5f : 0.0f, knobU32);
		}
		else
		{
			RenderMoon(dl, knob, radius * 0.7f, knobU32, trackU32);
		}
	}
	else
	{
		dl->AddCircleFilled(knob, radius * 0.8f, knobU32, 20);
		if (flags & ToggleFlags_KnobBorder)
		{
			dl->AddCircle(knob, radius * 0.8f, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.6f)), 20, 1.0f);
		}
	}

	if (hasLabel && rightLabel)
	{
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
	}

	ImGui::PopID();
	ImGui::EndGroup();
	return clicked;
}

// ============================================================================
// imanim-driven style widgets — pills + animated tree/header reveals.
// Motion values exported from demos/style-playground.html. Colors are pulled
// live from the active ImGui theme (ImGuiCol_Header / FrameBg / Text / ...) so
// ThemeZ and DB themes restyle them for free. imanim runs on MQ2Main's shared
// default context, which the host drives every frame, so we just call
// iam_tween_* — no init and no per-frame plumbing in this plugin.
// ============================================================================
namespace
{
namespace style
{
inline iam_ease_desc TabPillEase()      { return iam_ease_spring_desc(1.65f, 170.0f, 22.0f, 0.0f); }
inline iam_ease_desc ListPillEase()     { return iam_ease_preset(iam_ease_out_cubic); }
inline iam_ease_desc TreeRevealEase()   { return iam_ease_preset(iam_ease_out_cubic); }
inline iam_ease_desc HeaderRevealEase() { return iam_ease_preset(iam_ease_out_cubic); }

constexpr float kTabPillDur      = 0.28f;
constexpr float kTreeRevealDur   = 0.31f;
constexpr float kHeaderRevealDur = 0.26f;
constexpr float kListPillDur     = 0.50f;

constexpr float kPillRounding = 1.0f;  // fraction of (rowHeight * 0.5)
constexpr float kPadX         = 18.0f;
constexpr float kRowHeight    = 26.0f;
constexpr float kHoverAlpha   = 0.22f;
} // namespace style

float SafeDeltaTime()
{
	float dt = ImGui::GetIO().DeltaTime;
	if (dt <= 0.0f)
	{
		dt = 1.0f / 60.0f;
	}
	if (dt > 0.1f)
	{
		dt = 0.1f;
	}
	return dt;
}

void DrawChevron(ImDrawList* dl, ImVec2 center, float size, float angleDeg, ImU32 col)
{
	const float a = angleDeg * (kPi / 180.0f);
	const float ca = cosf(a);
	const float sa = sinf(a);
	const ImVec2 base[3] = { ImVec2(0.5f * size, 0.0f), ImVec2(-0.35f * size, -0.5f * size), ImVec2(-0.35f * size, 0.5f * size) };
	ImVec2 p[3];
	for (int i = 0; i < 3; ++i)
	{
		p[i] = ImVec2(center.x + base[i].x * ca - base[i].y * sa, center.y + base[i].x * sa + base[i].y * ca);
	}
	dl->AddTriangleFilled(p[0], p[1], p[2], col);
}

struct RevealFrame
{
	ImGuiStorage* parentStorage;
	ImGuiID fullKey;
};
ImVector<RevealFrame> g_revealStack;

// Header row + animated height-reveal body. The header (chevron + label) always
// renders; the body goes into a clipped child whose height tweens to the last
// measured content height. Returns true when the body should render (open or
// mid-collapse) — caller must then call EndReveal().
// Caller-driven reveal: *openPtr holds the open state (toggled on click here),
// so callers can coordinate exclusivity (accordion) across several nodes.
bool RevealCore(const char* label, bool* openPtr, bool headerStyle)
{
	ImGuiStorage* st = ImGui::GetStateStorage();
	const ImGuiID id = ImGui::GetID(label);
	const ImGuiID fullKey = ImHashStr("##rv_full", 0, id);
	bool open = *openPtr;

	const float dt = SafeDeltaTime();
	const float rowH = ImGui::GetFrameHeight();
	const float availW = ImGui::GetContentRegionAvail().x;
	const ImVec2 pos = ImGui::GetCursorScreenPos();

	ImGui::InvisibleButton(label, ImVec2(availW, rowH));
	const bool hovered = ImGui::IsItemHovered();
	if (ImGui::IsItemClicked())
	{
		open = !open;
	}
	*openPtr = open;

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float dur = headerStyle ? style::kHeaderRevealDur : style::kTreeRevealDur;
	const iam_ease_desc ease = headerStyle ? style::HeaderRevealEase() : style::TreeRevealEase();
	const float chevT = iam_tween_float(id, ImHashStr("rv_chev"), open ? 1.0f : 0.0f, dur, ease, iam_policy_crossfade, dt, open ? 1.0f : 0.0f);
	const float fullH = st->GetFloat(fullKey, 0.0f);
	const float t = iam_tween_float(id, ImHashStr("rv_h"), open ? 1.0f : 0.0f, dur, ease, iam_policy_crossfade, dt, open ? 1.0f : 0.0f);
	const float bodyH = fullH * t;

	if (headerStyle)
	{
		// Collapsed -> a full pill. Expanding -> the pill morphs into a
		// rounded-top header strip with a border framing the revealed body,
		// then collapses back up into the pill.
		const float pillR = rowH * 0.5f;
		const float boxR = ImMax(ImGui::GetStyle().FrameRounding, 4.0f);
		const float radius = ImLerp(pillR, boxR, ImClamp(t, 0.0f, 1.0f));
		const ImVec2 cmax = ImVec2(pos.x + availW, pos.y + rowH + bodyH);

		if (t > 0.02f)
		{
			dl->AddRect(pos, cmax, ImGui::GetColorU32(ImGuiCol_Header), radius, ImDrawFlags_RoundCornersAll, 1.5f);
		}
		const ImDrawFlags hdrCorners = (t < 0.02f) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop;
		ImVec4 hdr = ImGui::GetStyleColorVec4(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header);
		hdr.w = 1.0f;
		dl->AddRectFilled(pos, ImVec2(cmax.x, pos.y + rowH), ImGui::GetColorU32(hdr), radius, hdrCorners);
	}
	else if (hovered)
	{
		ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered);
		bg.w *= 0.5f;
		dl->AddRectFilled(pos, ImVec2(pos.x + availW, pos.y + rowH), ImGui::GetColorU32(bg), rowH * 0.5f);
	}

	DrawChevron(dl, ImVec2(pos.x + rowH * 0.5f, pos.y + rowH * 0.5f), rowH * 0.42f, chevT * 90.0f, ImGui::GetColorU32(ImGuiCol_Text));

	const char* textEnd = ImGui::FindRenderedTextEnd(label);
	const ImVec2 ts = ImGui::CalcTextSize(label, textEnd);
	dl->AddText(ImVec2(pos.x + rowH, pos.y + (rowH - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label, textEnd);

	const bool visible = open || t > 0.001f;
	if (!visible)
	{
		return false;
	}

	const bool settled = open && t >= 0.999f;
	const ImGuiID childId = ImHashStr("##rv_body", 0, id);
	if (settled)
	{
		ImGui::BeginChild(childId, ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	}
	else
	{
		ImGui::BeginChild(childId, ImVec2(0.0f, ImMax(fullH * t, 1.0f)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	}

	// Fade the contents in/out with the reveal (matches imanim's accordion).
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * ImClamp(t, 0.0f, 1.0f));
	g_revealStack.push_back({ st, fullKey });
	return true;
}

// Self-managed reveal: open state persists in the window's ImGuiStorage. Used
// for the bar-editor headers, which open/close independently of each other.
bool BeginRevealSelf(const char* label, bool defaultOpen, bool headerStyle)
{
	ImGuiStorage* st = ImGui::GetStateStorage();
	const ImGuiID openKey = ImHashStr("##rv_open", 0, ImGui::GetID(label));
	bool open = st->GetBool(openKey, defaultOpen);
	const bool visible = RevealCore(label, &open, headerStyle);
	st->SetBool(openKey, open);
	return visible;
}

void EndReveal()
{
	IM_ASSERT(!g_revealStack.empty());
	RevealFrame rf = g_revealStack.back();
	g_revealStack.pop_back();
	const float contentH = ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y;
	rf.parentStorage->SetFloat(rf.fullKey, contentH);
	ImGui::PopStyleVar(); // reveal-content alpha
	ImGui::EndChild();
}
} // namespace

int PillTabBar(const char* id, const char* const labels[], int count, int current)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	ImGui::PushID(id);

	const float padX = style::kPadX;
	const float outer = 3.0f;
	const float height = ImGui::GetFrameHeight() + 4.0f;

	float widths[16] = {};
	float positions[16] = {};
	float total = 0.0f;
	if (count > 16)
	{
		count = 16;
	}
	for (int i = 0; i < count; ++i)
	{
		const char* te = ImGui::FindRenderedTextEnd(labels[i]);
		widths[i] = ImGui::CalcTextSize(labels[i], te).x + padX * 2.0f;
		positions[i] = total;
		total += widths[i];
	}

	int sel = (current < 0 || current >= count) ? 0 : current;
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float dt = SafeDeltaTime();

	dl->AddRectFilled(pos, ImVec2(pos.x + total + outer * 2.0f, pos.y + height + outer * 2.0f),
		ImGui::GetColorU32(ImGuiCol_FrameBg), (height + outer * 2.0f) * 0.5f);

	const ImGuiID pillId = ImGui::GetID("##pill");
	const float pillX = iam_tween_float(pillId, ImHashStr("x"), positions[sel], style::kTabPillDur, style::TabPillEase(), iam_policy_crossfade, dt, positions[sel]);
	const float pillW = iam_tween_float(pillId, ImHashStr("w"), widths[sel], style::kTabPillDur, style::TabPillEase(), iam_policy_crossfade, dt, widths[sel]);

	ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_Header);
	accent.w = 1.0f;
	dl->AddRectFilled(ImVec2(pos.x + outer + pillX, pos.y + outer),
		ImVec2(pos.x + outer + pillX + pillW, pos.y + outer + height),
		ImGui::GetColorU32(accent), height * 0.5f);

	for (int i = 0; i < count; ++i)
	{
		const ImVec2 tabPos(pos.x + outer + positions[i], pos.y + outer);
		ImGui::SetCursorScreenPos(tabPos);
		ImGui::PushID(i);
		const ImGuiID tabId = ImGui::GetID("##tab");
		if (ImGui::InvisibleButton("##tab", ImVec2(widths[i], height)))
		{
			sel = i;
		}
		const bool hov = ImGui::IsItemHovered();
		ImGui::PopID();

		// Smoothly brighten the label on hover / selection (idle -> hover -> active).
		const float targetA = (i == sel) ? 1.0f : (hov ? 0.85f : 0.55f);
		const float a = iam_tween_float(tabId, ImHashStr("rv_talpha"), targetA, 0.15f, style::ListPillEase(), iam_policy_crossfade, dt, targetA);

		const char* te = ImGui::FindRenderedTextEnd(labels[i]);
		const ImVec2 ts = ImGui::CalcTextSize(labels[i], te);
		ImVec4 tcol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		tcol.w *= a;
		dl->AddText(ImVec2(tabPos.x + (widths[i] - ts.x) * 0.5f, tabPos.y + (height - ts.y) * 0.5f), ImGui::GetColorU32(tcol), labels[i], te);
	}

	ImGui::SetCursorScreenPos(pos);
	ImGui::Dummy(ImVec2(total + outer * 2.0f, height + outer * 2.0f));
	ImGui::PopID();
	return sel;
}

bool PillSelectable(const char* label, bool selected, float width)
{
	const float height = ImMax(ImGui::GetFrameHeight(), style::kRowHeight);
	if (width <= 0.0f)
	{
		width = ImGui::GetContentRegionAvail().x;
	}

	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton(label, ImVec2(width, height));
	const bool clicked = ImGui::IsItemClicked();
	const bool hovered = ImGui::IsItemHovered();

	const float target = selected ? 1.0f : (hovered ? style::kHoverAlpha : 0.0f);
	const float t = iam_tween_float(ImGui::GetID(label), ImHashStr("fill"), target, style::kListPillDur, style::ListPillEase(), iam_policy_crossfade, SafeDeltaTime(), 0.0f);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float rounding = (height * 0.5f) * style::kPillRounding;
	if (t > 0.001f)
	{
		ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_Header);
		accent.w = t;
		dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(accent), rounding);
	}

	const char* textEnd = ImGui::FindRenderedTextEnd(label);
	const ImVec2 ts = ImGui::CalcTextSize(label, textEnd);
	const ImVec4 textCol = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), ImGui::GetStyleColorVec4(ImGuiCol_Text), ImClamp(t, 0.0f, 1.0f));
	const float inset = ImMax(rounding, 8.0f);
	dl->PushClipRect(pos, ImVec2(pos.x + width, pos.y + height), true);
	dl->AddText(ImVec2(pos.x + inset, pos.y + (height - ts.y) * 0.5f), ImGui::GetColorU32(textCol), label, textEnd);
	dl->PopClipRect();

	return clicked;
}

bool BeginAnimatedTreeNode(const char* label, bool* open)
{
	return RevealCore(label, open, false);
}

void EndAnimatedTreeNode()
{
	EndReveal();
}

bool BeginAnimatedHeader(const char* label, bool defaultOpen)
{
	return BeginRevealSelf(label, defaultOpen, true);
}

void EndAnimatedHeader()
{
	EndReveal();
}

namespace
{
// One slider edit popup is ever open at a time, so a single scratch buffer is fine.
char g_sliderEditBuf[64] = {};
ImVec2 g_sliderEditPos = ImVec2(0.0f, 0.0f);

// imanim-style slider visual: thin glowing rail, accent fill, white thumb with an
// accent ring that scales up on hover/drag, and the value text on the right.
void DrawAnimatedSlider(ImGuiID id, ImVec2 pos, float w, float h, float trackT, bool excited, const char* valueText)
{
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float dt = SafeDeltaTime();
	const float r = h * 0.36f;
	const float th = h * 0.26f;
	const float cy = pos.y + h * 0.5f;
	const float ty0 = cy - th * 0.5f;
	const float ty1 = cy + th * 0.5f;
	trackT = ImClamp(trackT, 0.0f, 1.0f);
	const float thumbX = pos.x + r + trackT * (w - 2.0f * r);

	ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_Header);
	accent.w = 1.0f;
	const ImU32 accentU = ImGui::GetColorU32(accent);

	dl->AddRectFilled(ImVec2(pos.x, ty0), ImVec2(pos.x + w, ty1), ImGui::GetColorU32(ImGuiCol_FrameBg), th * 0.5f);

	ImVec4 glow = accent;
	glow.w = 0.30f;
	dl->AddRectFilled(ImVec2(pos.x, ty0 - 2.0f), ImVec2(thumbX, ty1 + 2.0f), ImGui::GetColorU32(glow), (th + 4.0f) * 0.5f);
	dl->AddRectFilled(ImVec2(pos.x, ty0), ImVec2(thumbX, ty1), accentU, th * 0.5f);

	const float target = excited ? 1.25f : 1.0f;
	const float scale = iam_tween_float(id, ImHashStr("rv_thumb"), target, 0.15f, style::ListPillEase(), iam_policy_crossfade, dt, 1.0f);
	if (excited)
	{
		ImVec4 g2 = accent;
		g2.w = 0.22f;
		dl->AddCircleFilled(ImVec2(thumbX, cy), r * scale * 1.6f, ImGui::GetColorU32(g2));
	}
	dl->AddCircleFilled(ImVec2(thumbX, cy), r * scale, IM_COL32(255, 255, 255, 255));
	dl->AddCircle(ImVec2(thumbX, cy), r * scale, accentU, 0, 2.0f);

	if (valueText && valueText[0])
	{
		const ImVec2 tsz = ImGui::CalcTextSize(valueText);
		dl->AddText(ImVec2(pos.x + w - tsz.x - 4.0f, cy - tsz.y * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), valueText);
	}
}

// Ctrl+click edit popup at the cursor: an input + Apply/Cancel. Returns true and
// writes the parsed value to *out when accepted.
bool SliderEditPopup(const char* popupId, bool asInt, double* out)
{
	bool accepted = false;
	ImGui::SetNextWindowPos(g_sliderEditPos, ImGuiCond_Appearing);
	if (ImGui::BeginPopup(popupId))
	{
		const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
			| (asInt ? ImGuiInputTextFlags_CharsDecimal : ImGuiInputTextFlags_CharsScientific);
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(110.0f);
		const bool enter = ImGui::InputText("##val", g_sliderEditBuf, sizeof(g_sliderEditBuf), flags);
		const bool apply = ImGui::Button("Apply");
		ImGui::SameLine();
		const bool cancel = ImGui::Button("Cancel");
		if (enter || apply)
		{
			*out = atof(g_sliderEditBuf);
			accepted = true;
			ImGui::CloseCurrentPopup();
		}
		else if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	return accepted;
}

void SliderLabel(const char* label)
{
	const char* end = ImGui::FindRenderedTextEnd(label);
	if (end != label)
	{
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::TextUnformatted(label, end);
	}
}
} // namespace

bool StyledSliderFloat(const char* label, float* v, float vmin, float vmax, const char* fmt, ImGuiSliderFlags flags)
{
	(void)flags;
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float w = ImGui::CalcItemWidth();
	const float h = ImGui::GetFrameHeight();
	const float r = h * 0.36f;

	ImGui::InvisibleButton(label, ImVec2(w, h));
	const ImGuiID id = ImGui::GetID(label);
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();
	ImGuiIO& io = ImGui::GetIO();

	char popupId[20];
	ImFormatString(popupId, sizeof(popupId), "##se%08X", id);
	bool changed = false;

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyCtrl)
	{
		ImFormatString(g_sliderEditBuf, sizeof(g_sliderEditBuf), fmt, *v);
		g_sliderEditPos = io.MousePos;
		ImGui::OpenPopup(popupId);
	}
	else if (active && !io.KeyCtrl)
	{
		const float tnorm = ImClamp((io.MousePos.x - (pos.x + r)) / ImMax(w - 2.0f * r, 1.0f), 0.0f, 1.0f);
		const float nv = vmin + tnorm * (vmax - vmin);
		if (nv != *v) { *v = nv; changed = true; }
	}

	double parsed = 0.0;
	if (SliderEditPopup(popupId, false, &parsed))
	{
		*v = ImClamp((float)parsed, vmin, vmax);
		changed = true;
	}

	const float denom = vmax - vmin;
	const float trackT = denom != 0.0f ? (*v - vmin) / denom : 0.0f;
	char value[32];
	ImFormatString(value, sizeof(value), fmt, *v);
	DrawAnimatedSlider(id, pos, w, h, trackT, hovered || active, value);
	SliderLabel(label);
	return changed;
}

bool StyledSliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt, ImGuiSliderFlags flags)
{
	(void)flags;
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float w = ImGui::CalcItemWidth();
	const float h = ImGui::GetFrameHeight();
	const float r = h * 0.36f;

	ImGui::InvisibleButton(label, ImVec2(w, h));
	const ImGuiID id = ImGui::GetID(label);
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();
	ImGuiIO& io = ImGui::GetIO();

	char popupId[20];
	ImFormatString(popupId, sizeof(popupId), "##se%08X", id);
	bool changed = false;

	if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyCtrl)
	{
		ImFormatString(g_sliderEditBuf, sizeof(g_sliderEditBuf), fmt, *v);
		g_sliderEditPos = io.MousePos;
		ImGui::OpenPopup(popupId);
	}
	else if (active && !io.KeyCtrl)
	{
		const float tnorm = ImClamp((io.MousePos.x - (pos.x + r)) / ImMax(w - 2.0f * r, 1.0f), 0.0f, 1.0f);
		const int nv = vmin + (int)(tnorm * (float)(vmax - vmin) + 0.5f);
		if (nv != *v) { *v = nv; changed = true; }
	}

	double parsed = 0.0;
	if (SliderEditPopup(popupId, true, &parsed))
	{
		const int nv = (int)(parsed >= 0.0 ? parsed + 0.5 : parsed - 0.5);
		*v = ImClamp(nv, vmin, vmax);
		changed = true;
	}

	const float denom = (float)(vmax - vmin);
	const float trackT = denom != 0.0f ? (float)(*v - vmin) / denom : 0.0f;
	char value[32];
	ImFormatString(value, sizeof(value), fmt, *v);
	DrawAnimatedSlider(id, pos, w, h, trackT, hovered || active, value);
	SliderLabel(label);
	return changed;
}

bool StyledBeginCombo(const char* label, const char* preview, ImGuiComboFlags flags)
{
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const float w = ImGui::CalcItemWidth();
	const float h = ImGui::GetFrameHeight();
	const ImGuiID id = ImGui::GetID(label);

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, h * 0.5f);
	const bool open = ImGui::BeginCombo(label, preview, flags | ImGuiComboFlags_NoArrowButton);
	ImGui::PopStyleVar();

	// Animated chevron (down when closed, up when open) drawn over the frame.
	const float chevT = iam_tween_float(id, ImHashStr("rv_combochev"), open ? 1.0f : 0.0f,
		style::kListPillDur * 0.4f, style::ListPillEase(), iam_policy_crossfade, SafeDeltaTime(), open ? 1.0f : 0.0f);
	ImDrawList* dl = ImGui::GetWindowDrawList();
	DrawChevron(dl, ImVec2(pos.x + w - h * 0.5f, pos.y + h * 0.5f), h * 0.30f, 90.0f + chevT * 180.0f, ImGui::GetColorU32(ImGuiCol_Text));
	return open;
}

void StyledEndCombo()
{
	ImGui::EndCombo();
}

bool StyledCombo(const char* label, int* currentItem, const char* const items[], int count)
{
	const char* preview = (*currentItem >= 0 && *currentItem < count) ? items[*currentItem] : "";
	bool changed = false;
	if (StyledBeginCombo(label, preview))
	{
		for (int i = 0; i < count; ++i)
		{
			if (PillSelectable(items[i], i == *currentItem))
			{
				*currentItem = i;
				changed = true;
				ImGui::CloseCurrentPopup();
			}
		}
		StyledEndCombo();
	}
	return changed;
}

bool StyledCheckbox(const char* label, bool* v)
{
	ImGuiStyle& style = ImGui::GetStyle();
	const float sz = ImGui::GetFrameHeight();
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	const char* end = ImGui::FindRenderedTextEnd(label);
	const ImVec2 lsz = ImGui::CalcTextSize(label, end);
	const float totalW = sz + (lsz.x > 0.0f ? style.ItemInnerSpacing.x + lsz.x : 0.0f);

	ImGui::InvisibleButton(label, ImVec2(totalW, sz));
	const ImGuiID id = ImGui::GetID(label);
	const bool hovered = ImGui::IsItemHovered();
	bool changed = false;
	if (ImGui::IsItemClicked())
	{
		*v = !*v;
		changed = true;
	}

	const float dt = SafeDeltaTime();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	// Separate tweens: the box fill scales in (bouncy), the checkmark strokes in.
	const float fillT = ImClamp(iam_tween_float(id, ImHashStr("rv_chkfill"), *v ? 1.0f : 0.0f, 0.20f,
		iam_ease_preset(iam_ease_out_back), iam_policy_crossfade, dt, *v ? 1.0f : 0.0f), 0.0f, 1.0f);
	const float drawT = ImClamp(iam_tween_float(id, ImHashStr("rv_chkdraw"), *v ? 1.0f : 0.0f, 0.22f,
		iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt, *v ? 1.0f : 0.0f), 0.0f, 1.0f);

	const ImVec2 bmin = pos;
	const ImVec2 bmax = ImVec2(pos.x + sz, pos.y + sz);
	const float rounding = sz * 0.28f;
	const ImVec4 bg = ImGui::GetStyleColorVec4(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
	dl->AddRectFilled(bmin, bmax, ImGui::GetColorU32(bg), rounding);

	if (fillT > 0.001f)
	{
		ImVec4 acc = ImGui::GetStyleColorVec4(ImGuiCol_Header);
		acc.w = 1.0f;
		const float pad = (sz * 0.5f) * (1.0f - fillT);
		dl->AddRectFilled(ImVec2(bmin.x + pad, bmin.y + pad), ImVec2(bmax.x - pad, bmax.y - pad), ImGui::GetColorU32(acc), rounding);
	}

	// Checkmark strokes itself in over drawT (short leg, then long leg). White for
	// contrast against the accent fill.
	if (drawT > 0.001f)
	{
		const ImU32 ck = IM_COL32(255, 255, 255, 255);
		const float th = ImMax(sz * 0.13f, 2.0f);
		const ImVec2 p1(pos.x + sz * 0.26f, pos.y + sz * 0.52f);
		const ImVec2 p2(pos.x + sz * 0.43f, pos.y + sz * 0.69f);
		const ImVec2 p3(pos.x + sz * 0.76f, pos.y + sz * 0.30f);
		const float split = 0.34f;
		if (drawT <= split)
		{
			dl->AddLine(p1, ImLerp(p1, p2, drawT / split), ck, th);
		}
		else
		{
			dl->AddLine(p1, p2, ck, th);
			dl->AddLine(p2, ImLerp(p2, p3, (drawT - split) / (1.0f - split)), ck, th);
		}
	}

	if (end != label)
	{
		dl->AddText(ImVec2(pos.x + sz + style.ItemInnerSpacing.x, pos.y + (sz - lsz.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label, end);
	}
	return changed;
}
} // namespace myui
