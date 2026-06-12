#include "Widgets.h"

#include <imgui/imgui_internal.h>
#include "imgui/imanim/im_anim.h"
#include "imgui/fonts/IconsFontAwesome.h"
#include "mq/imgui/AlphaMask.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

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

// Crescent moon as a clean single silhouette: a full disc with an offset disc
// carved out via a subtractive alpha mask (full circle layer EXCEPT the carve
// layer). Unlike the old stacked-circle carve it needs no track color behind
// it, so it reads correctly on any theme. rot rocks the crescent on transition.
void RenderMoon(ImDrawList* dl, ImVec2 center, float radius, float rot, ImU32 col)
{
	const float ca = cosf(rot);
	const float sa = sinf(rot);
	const float ox = radius * 0.42f;
	const float oy = -radius * 0.18f;
	const ImVec2 carve(center.x + ox * ca - oy * sa, center.y + ox * sa + oy * ca);

	mq::imgui::CreateMaskLayer(dl);
	dl->AddCircleFilled(center, radius, IM_COL32_WHITE, 24);
	mq::imgui::CreateMaskLayer(dl);
	dl->AddCircleFilled(carve, radius * 0.88f, IM_COL32_WHITE, 24);
	mq::imgui::BeginMaskedDraw(dl, mq::imgui::AlphaMaskOp::Subtract);
	dl->AddCircleFilled(center, radius, col, 24);
	mq::imgui::EndMaskedDraw(dl);
}

int g_globalToggleFlags = kDefaultToggleFlags;
ImVec2 g_globalToggleSize = ImVec2(0.0f, 0.0f);
bool g_animationsEnabled = true;

// Gated tween wrappers: when the global animation switch is off they snap to the
// target (no motion) so every styled widget — pills, reveals, sliders, combos,
// checkbox, button, toggle — honors the "Animated Widgets" setting. Bars are NOT
// routed through here; they keep their own per-bar shimmer/glow settings.
float AnimFloat(ImGuiID id, ImGuiID ch, float target, float dur, const iam_ease_desc& ez, int policy, float dt, float init)
{
	if (!g_animationsEnabled)
	{
		return target;
	}
	return iam_tween_float(id, ch, target, dur, ez, policy, dt, init);
}

ImVec4 AnimColor(ImGuiID id, ImGuiID ch, ImVec4 target, float dur, const iam_ease_desc& ez, int policy, int colorSpace, float dt, ImVec4 init)
{
	if (!g_animationsEnabled)
	{
		return target;
	}
	return iam_tween_color(id, ch, target, dur, ez, policy, colorSpace, dt, init);
}
} // namespace

void SetGlobalToggleStyle(int flags, ImVec2 size)
{
	g_globalToggleFlags = flags;
	g_globalToggleSize = size;
}

void SetAnimationsEnabled(bool enabled)
{
	g_animationsEnabled = enabled;
}

bool AnimationsEnabled()
{
	return g_animationsEnabled;
}

ImVec4 Soften(const ImVec4& accent)
{
	ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
	bg.w = 1.0f;
	ImVec4 r = ImLerp(accent, bg, 0.35f);
	r.w = accent.w;
	return r;
}

void SoftGlowRoundRect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, ImVec4 col, float intensity)
{
	if (intensity <= 0.001f)
	{
		return;
	}
	for (int i = 3; i >= 1; --i)
	{
		const float spread = (float)i * 3.0f;
		ImVec4 g = col;
		g.w = col.w * intensity * (0.22f / (float)i);
		dl->AddRectFilled(ImVec2(p0.x - spread, p0.y - spread), ImVec2(p1.x + spread, p1.y + spread),
			ImGui::GetColorU32(g), rounding + spread);
	}
}

float DirectionRingRadius(const char* distText, const RingStyle& style)
{
	// Always auto-fit to a fixed 3-digit text width, so the ring is a constant size
	// regardless of the actual distance (which DrawDirectionRing centers inside) while
	// still scaling with the font. Size to the text half-WIDTH (not the diagonal) so the
	// track hugs the number tightly. distText is unused now that the width is fixed.
	(void)distText;
	const float w3 = ImGui::CalcTextSize("000").x;
	return w3 * 0.5f + style.thickness * 0.5f + 2.0f;
}

void PushWindowClip(ImDrawList* dl)
{
	if (!dl)
	{
		return;
	}
	const ImVec2 wmin = ImGui::GetWindowPos();
	const ImVec2 wsz = ImGui::GetWindowSize();
	dl->PushClipRect(wmin, ImVec2(wmin.x + wsz.x, wmin.y + wsz.y), false);
}

ImVec4 ResolveColorSource(ImGuiID id, ImU32 animSeed, const ColorSource& cs,
	float distance, float distMin, float distMax, bool los, ImGuiCol themeFallback, float dt)
{
	if (cs.mode == 1) // Distance: tween near->far across [distMin, distMax]
	{
		float span = distMax - distMin;
		float t = span > 0.001f ? (distance - distMin) / span : 0.0f;
		t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
		ImVec4 target = ImLerp(ImVec4(cs.distNear.ToImColor()), ImVec4(cs.distFar.ToImColor()), t);
		return AnimColor(id, animSeed, target, 0.25f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, iam_col_oklab, dt, target);
	}
	if (cs.mode == 2) // Visibility: tween between los/no-los colors
	{
		ImVec4 target = los ? ImVec4(cs.losColor.ToImColor()) : ImVec4(cs.noLosColor.ToImColor());
		return AnimColor(id, animSeed, target, 0.25f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, iam_col_oklab, dt, target);
	}
	return cs.custom.Alpha == 0 ? ImGui::GetStyleColorVec4(themeFallback) : ImVec4(cs.custom.ToImColor());
}

void DrawDirectionRing(ImDrawList* dl, ImGuiID id, ImVec2 center, float targetBearingDeg,
	float distance, bool los, const char* distText, const RingStyle& style)
{
	if (!dl)
	{
		return;
	}

	const float dt = ImGui::GetIO().DeltaTime;

	// Shortest-arc angle smoothing toward the (possibly stale) target bearing. Kept
	// in our own per-id state so wrapping past +/-180 never sweeps the long way.
	static std::unordered_map<ImGuiID, float> s_angle;
	float drawAngle = targetBearingDeg;
	if (g_animationsEnabled)
	{
		auto it = s_angle.find(id);
		float cur = (it != s_angle.end()) ? it->second : targetBearingDeg;
		float diff = targetBearingDeg - cur;
		while (diff > 180.0f)  { diff -= 360.0f; }
		while (diff < -180.0f) { diff += 360.0f; }
		const float k = 1.0f - expf(-dt / 0.10f); // ~0.1s smoothing time constant
		cur += diff * k;
		s_angle[id] = cur;
		drawAngle = cur;
	}
	else
	{
		s_angle[id] = targetBearingDeg;
	}

	// radius 0 = auto-fit: smallest radius whose inner edge clears the distance
	// text's bounding circle, plus the track half-thickness and a little padding,
	// so the text never touches the track (DirectionRingRadius owns the formula).
	const float radius = DirectionRingRadius(distText, style);

	// Background fill clipped to the inner edge of the track, behind the text.
	if (style.bgOn)
	{
		const ImVec4 bg = ResolveColorSource(id, ImHashStr("ring_bg"), style.bg,
			distance, style.distMin, style.distMax, los, ImGuiCol_ChildBg, dt);
		dl->AddCircleFilled(center, radius - style.thickness * 0.5f, ImGui::GetColorU32(bg), 64);
	}

	const ImVec4 track = ResolveColorSource(id, ImHashStr("ring_track"), style.track,
		distance, style.distMin, style.distMax, los, ImGuiCol_FrameBg, dt);
	dl->AddCircle(center, radius, ImGui::GetColorU32(track), 64, style.thickness);

	// Marker on the track: 0 deg = top, clockwise positive (ImGui Y is down).
	const float rad = drawAngle * (kPi / 180.0f);
	const ImVec2 mark(center.x + sinf(rad) * radius, center.y - cosf(rad) * radius);

	const ImVec4 indic = style.indicColor.Alpha == 0
		? ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab) : ImVec4(style.indicColor.ToImColor());

	if (style.glowOn && style.glowAlpha > 0.001f)
	{
		ImVec4 glow = style.glowColor.Alpha == 0 ? indic : ImVec4(style.glowColor.ToImColor());
		const float s = style.indicSize;
		SoftGlowRoundRect(dl, ImVec2(mark.x - s, mark.y - s), ImVec2(mark.x + s, mark.y + s),
			s, glow, style.glowAlpha);
	}

	dl->AddCircleFilled(mark, style.indicSize, ImGui::GetColorU32(indic), 16);

	if (distText && distText[0])
	{
		const ImVec2 ts = ImGui::CalcTextSize(distText);
		const ImVec2 tp(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f);
		if (style.textShadow)
		{
			dl->AddText(ImVec2(tp.x + 1.0f, tp.y + 1.0f), IM_COL32(0, 0, 0, 160), distText);
		}
		dl->AddText(tp, ImGui::GetColorU32(ImGuiCol_Text), distText);
	}
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

	const char* labelEnd = ImGui::FindRenderedTextEnd(id);

	const bool rightLabel = (flags & ToggleFlags_RightLabel) != 0;
	const bool hasLabel   = labelEnd != id;

	// Scale the switch to the label's text height (falls back to the line height
	// for unlabeled toggles) so it stays proportional to surrounding text rather
	// than to the padded frame height.
	const float textH = hasLabel ? ImGui::CalcTextSize(id, labelEnd).y : ImGui::GetTextLineHeight();
	float height = size.y > 0.0f ? size.y : ImMax(textH, 1.0f);
	float width  = size.x > 0.0f ? size.x : height * 1.9f;
	float radius = height * 0.5f;

	ImGui::BeginGroup();
	ImGui::PushID(id);

	if (hasLabel && !rightLabel)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(id, labelEnd);
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

	const bool anim = g_animationsEnabled;
	float dt = ImGui::GetIO().DeltaTime;
	dt = (dt <= 0.0f) ? (1.0f / 60.0f) : (dt > 0.1f ? 0.1f : dt);
	const ImGuiID animId = ImGui::GetID("##knob");
	const float targetT = *value ? 1.0f : 0.0f;

	// Track color crossfades on<->off in OKLAB; the thumb slides with a slight
	// overshoot (out_back). When animations are off both snap to the resting
	// state. A separate hover-intensity tween ramps the glow/pulse in and out so
	// they breathe rather than popping when the cursor enters or leaves.
	ImVec4 trackCol;
	float t;
	float hoverI;
	if (anim)
	{
		trackCol = AnimColor(animId, ImHashStr("rv_tgbg"), *value ? onCol : offCol, 0.20f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, iam_col_oklab, dt, *value ? onCol : offCol);
		t = AnimFloat(animId, ImHashStr("rv_tgpos"), targetT, 0.25f,
			iam_ease_preset(iam_ease_out_back), iam_policy_crossfade, dt, targetT);
		hoverI = AnimFloat(animId, ImHashStr("rv_tghov"), hovered ? 1.0f : 0.0f, 0.18f,
			iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt, hovered ? 1.0f : 0.0f);
	}
	else
	{
		trackCol = *value ? onCol : offCol;
		t = targetT;
		hoverI = 0.0f;
	}
	dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(trackCol), radius);

	ImVec2 knob(pos.x + radius + t * (width - height), pos.y + radius);

	// Rock the icon toward its direction of travel, leaning out at the start of
	// the slide and settling upright (proportional to the remaining distance).
	const float rock = anim ? (targetT - t) * 0.55f : 0.0f;

	const float time = (float)ImGui::GetTime();
	bool animate = (flags & ToggleFlags_AnimateKnob) || ((flags & ToggleFlags_AnimateOnHover) && hovered);

	ImVec4 knobCol(1.0f, 1.0f, 1.0f, 1.0f);
	if (anim && (flags & ToggleFlags_PulseOnHover) && hoverI > 0.001f)
	{
		const float wave = 0.78f + 0.22f * sinf(time * 2.4f);
		const float b = ImLerp(1.0f, wave, hoverI);
		knobCol = ImVec4(b, b, b, 1.0f);
	}
	ImU32 knobU32 = ImGui::GetColorU32(knobCol);

	if (anim && (flags & ToggleFlags_GlowOnHover) && hoverI > 0.001f)
	{
		const float breathe = (0.32f + 0.18f * sinf(time * 2.2f)) * hoverI;
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
			RenderStar(dl, knob, radius * 0.8f, (animate ? time * 1.5f : 0.0f) + rock, knobU32);
		}
		else
		{
			RenderMoon(dl, knob, radius * 0.7f, rock, knobU32);
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
		ImGui::TextUnformatted(id, labelEnd);
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
	const float chevT = AnimFloat(id, ImHashStr("rv_chev"), open ? 1.0f : 0.0f, dur, ease, iam_policy_crossfade, dt, open ? 1.0f : 0.0f);
	const float fullH = st->GetFloat(fullKey, 0.0f);
	const float t = AnimFloat(id, ImHashStr("rv_h"), open ? 1.0f : 0.0f, dur, ease, iam_policy_crossfade, dt, open ? 1.0f : 0.0f);
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
			dl->AddRect(pos, cmax, ImGui::GetColorU32(Soften(ImGui::GetStyleColorVec4(ImGuiCol_Header))), radius, ImDrawFlags_RoundCornersAll, 1.5f);
		}
		const ImDrawFlags hdrCorners = (t < 0.02f) ? ImDrawFlags_RoundCornersAll : ImDrawFlags_RoundCornersTop;
		ImVec4 hdr = Soften(ImGui::GetStyleColorVec4(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header));
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
	const float pillX = AnimFloat(pillId, ImHashStr("x"), positions[sel], style::kTabPillDur, style::TabPillEase(), iam_policy_crossfade, dt, positions[sel]);
	const float pillW = AnimFloat(pillId, ImHashStr("w"), widths[sel], style::kTabPillDur, style::TabPillEase(), iam_policy_crossfade, dt, widths[sel]);

	ImVec4 accent = Soften(ImGui::GetStyleColorVec4(ImGuiCol_Header));
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
		const float a = AnimFloat(tabId, ImHashStr("rv_talpha"), targetA, 0.15f, style::ListPillEase(), iam_policy_crossfade, dt, targetA);

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
	const float t = AnimFloat(ImGui::GetID(label), ImHashStr("fill"), target, style::kListPillDur, style::ListPillEase(), iam_policy_crossfade, SafeDeltaTime(), 0.0f);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float rounding = (height * 0.5f) * style::kPillRounding;
	if (t > 0.001f)
	{
		ImVec4 accent = Soften(ImGui::GetStyleColorVec4(ImGuiCol_Header));
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
// One edit popup is open at a time, so a single scratch buffer / position is shared
// by the slider Ctrl+click popup and the cursor-anchored InputPopup.
char g_editPopupBuf[256] = {};
ImVec2 g_editPopupPos = ImVec2(0.0f, 0.0f);
ImGuiID g_editPopupOwner = 0;

// Shared cursor-anchored edit-popup body: a focused input + styled Accept/Cancel
// buttons, Enter accepts / Esc cancels. Returns 1 = accepted, -1 = cancelled,
// 0 = nothing yet. The caller seeds g_editPopupBuf / g_editPopupPos before opening.
int EditPopupRun(const char* popupId, float width, ImGuiInputTextFlags extraFlags, const char* hint)
{
	int result = 0;
	const ImGuiID ownerId = ImGui::GetID(popupId);
	ImGui::SetNextWindowPos(g_editPopupPos, ImGuiCond_Appearing);
	if (ImGui::BeginPopup(popupId))
	{
		if (g_editPopupOwner != ownerId)
		{
			ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			return 0;
		}
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(width);
		const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll | extraFlags;
		bool enter = false;
		if (hint && hint[0] != '\0')
		{
			enter = ImGui::InputTextWithHint("##in", hint, g_editPopupBuf, sizeof(g_editPopupBuf), flags);
		}
		else
		{
			enter = ImGui::InputText("##in", g_editPopupBuf, sizeof(g_editPopupBuf), flags);
		}
		const bool accept = StyledButton("Accept");
		ImGui::SameLine();
		const bool cancel = StyledButton("Cancel");
		if (enter || accept)
		{
			result = 1;
			ImGui::CloseCurrentPopup();
		}
		else if (cancel || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			result = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	return result;
}

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

	ImVec4 accent = Soften(ImGui::GetStyleColorVec4(ImGuiCol_Header));
	accent.w = 1.0f;
	const ImU32 accentU = ImGui::GetColorU32(accent);

	dl->AddRectFilled(ImVec2(pos.x, ty0), ImVec2(pos.x + w, ty1), ImGui::GetColorU32(ImGuiCol_FrameBg), th * 0.5f);

	ImVec4 glow = accent;
	glow.w = 0.30f;
	dl->AddRectFilled(ImVec2(pos.x, ty0 - 2.0f), ImVec2(thumbX, ty1 + 2.0f), ImGui::GetColorU32(glow), (th + 4.0f) * 0.5f);
	dl->AddRectFilled(ImVec2(pos.x, ty0), ImVec2(thumbX, ty1), accentU, th * 0.5f);

	const float target = excited ? 1.25f : 1.0f;
	const float scale = AnimFloat(id, ImHashStr("rv_thumb"), target, 0.15f, style::ListPillEase(), iam_policy_crossfade, dt, 1.0f);
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
	const ImGuiInputTextFlags extra = asInt ? ImGuiInputTextFlags_CharsDecimal : ImGuiInputTextFlags_CharsScientific;
	if (EditPopupRun(popupId, 110.0f, extra, nullptr) == 1)
	{
		*out = atof(g_editPopupBuf);
		return true;
	}
	return false;
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

// Common scaffold for the styled float/int sliders: lays out the invisible
// button, derives the per-slider edit-popup id, and handles the Ctrl+click open
// + edit-popup parse. The two type-specific bits — the value text, the active-
// drag value mapping, and the trackT — stay in the wrappers so the float path
// keeps doing exact float arithmetic and the int path keeps integer rounding
// (mixing them through one double core would shift rounding/precision and was
// not behavior-preserving). `pos`/`w`/`h`/`r`/`id`/`hovered`/`active`/`popupId`
// are returned for the wrapper to finish with. `seedText` is what Ctrl+click
// preloads into the edit popup; `parsedOut` receives the popup result.
struct SliderFrame
{
	ImVec2  pos;
	float   w;
	float   h;
	float   r;
	ImGuiID id;
	bool    hovered;
	bool    active;
	char    popupId[20];
};

SliderFrame SliderBegin(const char* label)
{
	SliderFrame f{};
	f.pos = ImGui::GetCursorScreenPos();
	f.w = ImGui::CalcItemWidth();
	f.h = ImGui::GetFrameHeight();
	f.r = f.h * 0.36f;

	ImGui::InvisibleButton(label, ImVec2(f.w, f.h));
	f.id = ImGui::GetID(label);
	f.hovered = ImGui::IsItemHovered();
	f.active = ImGui::IsItemActive();
	ImFormatString(f.popupId, sizeof(f.popupId), "##se%08X", f.id);
	return f;
}

// Handles the Ctrl+click-to-open (seeding the popup with `seedText`) and the
// edit-popup run. Returns true when the popup was accepted, with the parsed
// value in *parsedOut.
bool SliderEdit(const SliderFrame& f, bool asInt, const char* seedText, double* parsedOut)
{
	ImGuiIO& io = ImGui::GetIO();
	if (f.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyCtrl)
	{
		ImFormatString(g_editPopupBuf, sizeof(g_editPopupBuf), "%s", seedText);
		g_editPopupPos = io.MousePos;
		g_editPopupOwner = ImGui::GetID(f.popupId);
		ImGui::OpenPopup(f.popupId);
	}
	return SliderEditPopup(f.popupId, asInt, parsedOut);
}
} // namespace

bool StyledSliderFloat(const char* label, float* v, float vmin, float vmax, const char* fmt, ImGuiSliderFlags flags)
{
	(void)flags;
	const SliderFrame f = SliderBegin(label);
	ImGuiIO& io = ImGui::GetIO();
	bool changed = false;

	char seed[32];
	ImFormatString(seed, sizeof(seed), fmt, *v);

	if (f.active && !io.KeyCtrl)
	{
		const float tnorm = ImClamp((io.MousePos.x - (f.pos.x + f.r)) / ImMax(f.w - 2.0f * f.r, 1.0f), 0.0f, 1.0f);
		const float nv = vmin + tnorm * (vmax - vmin);
		if (nv != *v) { *v = nv; changed = true; }
	}

	double parsed = 0.0;
	if (SliderEdit(f, false, seed, &parsed))
	{
		*v = ImClamp((float)parsed, vmin, vmax);
		changed = true;
	}

	const float denom = vmax - vmin;
	const float trackT = denom != 0.0f ? (*v - vmin) / denom : 0.0f;
	char value[32];
	ImFormatString(value, sizeof(value), fmt, *v);
	DrawAnimatedSlider(f.id, f.pos, f.w, f.h, trackT, f.hovered || f.active, value);
	SliderLabel(label);
	return changed;
}

bool StyledSliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt, ImGuiSliderFlags flags)
{
	(void)flags;
	const SliderFrame f = SliderBegin(label);
	ImGuiIO& io = ImGui::GetIO();
	bool changed = false;

	char seed[32];
	ImFormatString(seed, sizeof(seed), fmt, *v);

	if (f.active && !io.KeyCtrl)
	{
		const float tnorm = ImClamp((io.MousePos.x - (f.pos.x + f.r)) / ImMax(f.w - 2.0f * f.r, 1.0f), 0.0f, 1.0f);
		const int nv = vmin + (int)(tnorm * (float)(vmax - vmin) + 0.5f);
		if (nv != *v) { *v = nv; changed = true; }
	}

	double parsed = 0.0;
	if (SliderEdit(f, true, seed, &parsed))
	{
		const int nv = (int)(parsed >= 0.0 ? parsed + 0.5 : parsed - 0.5);
		*v = ImClamp(nv, vmin, vmax);
		changed = true;
	}

	const float denom = (float)(vmax - vmin);
	const float trackT = denom != 0.0f ? (float)(*v - vmin) / denom : 0.0f;
	char value[32];
	ImFormatString(value, sizeof(value), fmt, *v);
	DrawAnimatedSlider(f.id, f.pos, f.w, f.h, trackT, f.hovered || f.active, value);
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
	const float chevT = AnimFloat(id, ImHashStr("rv_combochev"), open ? 1.0f : 0.0f,
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
	const float fillT = ImClamp(AnimFloat(id, ImHashStr("rv_chkfill"), *v ? 1.0f : 0.0f, 0.20f,
		iam_ease_preset(iam_ease_out_back), iam_policy_crossfade, dt, *v ? 1.0f : 0.0f), 0.0f, 1.0f);
	const float drawT = ImClamp(AnimFloat(id, ImHashStr("rv_chkdraw"), *v ? 1.0f : 0.0f, 0.22f,
		iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, dt, *v ? 1.0f : 0.0f), 0.0f, 1.0f);

	const ImVec2 bmin = pos;
	const ImVec2 bmax = ImVec2(pos.x + sz, pos.y + sz);
	const float rounding = sz * 0.28f;
	const ImVec4 bg = ImGui::GetStyleColorVec4(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
	dl->AddRectFilled(bmin, bmax, ImGui::GetColorU32(bg), rounding);

	if (fillT > 0.001f)
	{
		ImVec4 acc = Soften(ImGui::GetStyleColorVec4(ImGuiCol_Header));
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

// ============================================================================
// Styled buttons — drop-in for ImGui::Button / SmallButton. ImGui-style sizing,
// theme-sourced colors (ImGuiCol_Button*), and imanim hover/press motion gated
// by the global animation switch.
// ============================================================================
namespace
{
// Gradient parallelogram (per-vertex alpha) used for the diagonal hover sheen.
void AddGradientQuad(ImDrawList* dl, const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d,
	ImU32 colA, ImU32 colB, ImU32 colC, ImU32 colD)
{
	const ImVec2 uv = dl->_Data->TexUvWhitePixel;
	dl->PrimReserve(6, 4);
	const unsigned int idx = dl->_VtxCurrentIdx;
	dl->PrimWriteIdx((ImDrawIdx)idx);
	dl->PrimWriteIdx((ImDrawIdx)(idx + 1));
	dl->PrimWriteIdx((ImDrawIdx)(idx + 2));
	dl->PrimWriteIdx((ImDrawIdx)idx);
	dl->PrimWriteIdx((ImDrawIdx)(idx + 2));
	dl->PrimWriteIdx((ImDrawIdx)(idx + 3));
	dl->PrimWriteVtx(a, uv, colA);
	dl->PrimWriteVtx(b, uv, colB);
	dl->PrimWriteVtx(c, uv, colC);
	dl->PrimWriteVtx(d, uv, colD);
}

bool DrawButtonCore(const char* label, ImVec2 size, int flags, bool smallButton)
{
	if (flags < 0)
	{
		flags = kDefaultButtonFlags;
	}

	ImGuiStyle& style = ImGui::GetStyle();
	const char* end = ImGui::FindRenderedTextEnd(label);
	const ImVec2 ts = ImGui::CalcTextSize(label, end);

	ImVec2 pad = style.FramePadding;
	if (smallButton)
	{
		pad.y = 0.0f;
	}
	// Match ImGui::Button sizing: 0 = auto (text + padding), negative = fill the
	// available region by that amount (e.g. -FLT_MIN for full width).
	ImVec2 sz = size;
	if (sz.x < 0.0f)
	{
		sz.x = ImMax(ImGui::GetContentRegionAvail().x + sz.x, 1.0f);
	}
	else if (sz.x == 0.0f)
	{
		sz.x = ts.x + pad.x * 2.0f;
	}
	if (sz.y < 0.0f)
	{
		sz.y = ImMax(ImGui::GetContentRegionAvail().y + sz.y, 1.0f);
	}
	else if (sz.y == 0.0f)
	{
		sz.y = ts.y + pad.y * 2.0f;
	}
	sz.x = ImMax(sz.x, 1.0f);
	sz.y = ImMax(sz.y, 1.0f);

	const ImVec2 pos = ImGui::GetCursorScreenPos();
	// InvisibleButton returns true on release-within-bounds, matching ImGui::Button.
	const bool clicked = ImGui::InvisibleButton(label, sz);
	const ImGuiID id = ImGui::GetID(label);
	const bool hovered = ImGui::IsItemHovered();
	const bool active = ImGui::IsItemActive();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 p0 = pos;
	const ImVec2 p1 = ImVec2(pos.x + sz.x, pos.y + sz.y);
	// Full pill, matching the toggle / pill-tab vocabulary so the styled button
	// reads as a distinct themed widget rather than a stock rectangle.
	const float rounding = sz.y * 0.5f;

	const ImVec4 base = Soften(ImGui::GetStyleColorVec4(ImGuiCol_Button));
	const ImVec4 hov  = Soften(ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
	const ImVec4 act  = Soften(ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

	// Fill crossfades idle→hover→active (snaps when animations are off). Hover
	// intensity, press-spring scale and the diagonal sheen are the shared
	// primitives, so the toolbar icons animate identically.
	const ImVec4 target = active ? act : (hovered ? hov : base);
	const ImVec4 fill = AnimColor(id, ImHashStr("rv_btnfill"), target, 0.18f,
		iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, iam_col_oklab, SafeDeltaTime(), target);
	const float hoverI = HoverIntensity(id, hovered);
	const float scale = (flags & ButtonFlags_PressSink) ? PressScale(id, active) : 1.0f;

	// Scale the drawn pill around its center without disturbing layout.
	const ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
	const float hx = sz.x * 0.5f * scale;
	const float hy = sz.y * 0.5f * scale;
	const ImVec2 q0(c.x - hx, c.y - hy);
	const ImVec2 q1(c.x + hx, c.y + hy);
	const float round2 = hy; // full pill on the scaled rect

	if ((flags & ButtonFlags_GlowOnHover) && AnimationsEnabled() && hoverI > 0.001f)
	{
		ImVec4 glowCol = hov;
		glowCol.w = 1.0f;
		SoftGlowRoundRect(dl, q0, q1, round2, glowCol, hoverI);
	}

	// Force the fill near-opaque so the glow behind doesn't bleed through a
	// translucent theme Button color and wash out the label.
	ImVec4 fillOpaque = fill;
	fillOpaque.w = ImMax(fillOpaque.w, 0.92f);
	dl->AddRectFilled(q0, q1, ImGui::GetColorU32(fillOpaque), round2);

	// Subtle top sheen so the pill reads as a styled, lit surface (always on).
	{
		ImVec4 gloss(1.0f, 1.0f, 1.0f, 0.05f + 0.04f * hoverI);
		dl->AddRectFilled(q0, ImVec2(q1.x, c.y), ImGui::GetColorU32(gloss),
			round2, ImDrawFlags_RoundCornersTop);
	}

	if (flags & ButtonFlags_Shimmer)
	{
		DrawHoverShimmer(dl, q0, q1, round2, hoverI);
	}

	// Always-visible theme-accent rim (ButtonActive = the theme's accent) so the
	// pill outline reads even on themes whose ImGuiCol_Border is transparent;
	// it brightens from a soft rest alpha up to full on hover.
	const float borderSize = ImMax(style.FrameBorderSize, 1.0f);
	ImVec4 rim = Soften(ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	rim.w = 0.40f + 0.60f * ImClamp(hoverI, 0.0f, 1.0f);
	dl->AddRect(q0, q1, ImGui::GetColorU32(rim), round2, 0, borderSize);

	// Label with a soft drop shadow so it stays readable over the glow/sheen.
	const ImVec4 textCol = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	const ImVec2 tp(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
	dl->PushClipRect(q0, q1, true);
	dl->AddText(ImVec2(tp.x, tp.y + 1.0f), IM_COL32(0, 0, 0, 130), label, end);
	dl->AddText(tp, ImGui::GetColorU32(textCol), label, end);
	dl->PopClipRect();

	return clicked;
}
} // namespace

float HoverIntensity(ImGuiID id, bool hovered)
{
	if (!g_animationsEnabled)
	{
		return hovered ? 1.0f : 0.0f;
	}
	return iam_tween_float(id, ImHashStr("rv_hoverI"), hovered ? 1.0f : 0.0f, 0.18f,
		iam_ease_preset(iam_ease_out_cubic), iam_policy_crossfade, SafeDeltaTime(), hovered ? 1.0f : 0.0f);
}

float PressScale(ImGuiID id, bool active, float pressedScale)
{
	if (!g_animationsEnabled)
	{
		return 1.0f;
	}
	return iam_tween_float(id, ImHashStr("rv_press"), active ? pressedScale : 1.0f, 0.50f,
		iam_ease_spring_desc(1.0f, 260.0f, 8.0f, 0.0f), iam_policy_crossfade, SafeDeltaTime(), 1.0f);
}

void DrawHoverShimmer(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1, float rounding, float intensity)
{
	if (!g_animationsEnabled || intensity <= 0.01f)
	{
		return;
	}
	const float phase = fmodf((float)ImGui::GetTime() * 0.7f, 1.0f);
	const float w = p1.x - p0.x;
	const float band = ImMax(w * 0.16f, 10.0f);
	// Negative slant leans the sheen top-left -> bottom-right (leads with the foot).
	const float slant = -((p1.y - p0.y) * 0.5f);
	const float xc = ImLerp(p0.x - band, p1.x + band, phase);
	const float a = 0.22f * intensity;
	const ImU32 cPeak = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, a));
	const ImU32 cZero = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
	// Clip the slanted sheen to the rounded shape so it never spills into corners.
	mq::imgui::CreateMaskLayer(dl);
	dl->AddRectFilled(p0, p1, IM_COL32_WHITE, rounding);
	mq::imgui::BeginMaskedDraw(dl, mq::imgui::AlphaMaskOp::Union);
	AddGradientQuad(dl,
		ImVec2(xc - band + slant, p0.y), ImVec2(xc + slant, p0.y),
		ImVec2(xc - slant, p1.y), ImVec2(xc - band - slant, p1.y),
		cZero, cPeak, cPeak, cZero);
	AddGradientQuad(dl,
		ImVec2(xc + slant, p0.y), ImVec2(xc + band + slant, p0.y),
		ImVec2(xc + band - slant, p1.y), ImVec2(xc - slant, p1.y),
		cPeak, cZero, cZero, cPeak);
	mq::imgui::EndMaskedDraw(dl);
}

bool StyledButton(const char* label, ImVec2 size, int flags)
{
	return DrawButtonCore(label, size, flags, false);
}

bool StyledSmallButton(const char* label, int flags)
{
	return DrawButtonCore(label, ImVec2(0.0f, 0.0f), flags, true);
}

void HelpMarker(const char* desc)
{
	ImGui::TextDisabled("(?)");

	// Temporarily widen the shared hover delay to ~0.5s just for this check so
	// the tooltip waits half a second without altering MQ's other tooltips.
	ImGuiStyle& st = ImGui::GetStyle();
	const float prevDelay = st.HoverDelayNormal;
	st.HoverDelayNormal = 0.5f;
	const bool show = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay);
	st.HoverDelayNormal = prevDelay;

	if (show && ImGui::BeginTooltip())
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
		ImGui::TextUnformatted(desc);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}
}

void OpenInputPopup(const char* popupId, const char* initialText)
{
	strncpy_s(g_editPopupBuf, sizeof(g_editPopupBuf), initialText ? initialText : "", _TRUNCATE);
	g_editPopupPos = ImGui::GetIO().MousePos;
	g_editPopupOwner = ImGui::GetID(popupId);
	ImGui::OpenPopup(popupId);
}

bool InputPopup(const char* popupId, std::string& out, const char* hint)
{
	if (EditPopupRun(popupId, 200.0f, 0, hint) == 1)
	{
		out = g_editPopupBuf;
		return true;
	}
	return false;
}

namespace
{
// Read-only framed display of `current`; clicking it opens the shared InputPopup
// to edit. Returns true (with `out` set) only when the popup is accepted.
bool EditFieldCore(const char* label, const char* current, std::string& out, const char* hint)
{
	ImGui::PushID(label);
	const float w = ImGui::CalcItemWidth();
	const float h = ImGui::GetFrameHeight();
	const ImVec2 p0 = ImGui::GetCursorScreenPos();
	const ImVec2 p1(p0.x + w, p0.y + h);

	const bool clicked = ImGui::InvisibleButton("##field", ImVec2(w, h));
	const bool hovered = ImGui::IsItemHovered();
	const ImGuiID fid = ImGui::GetID("##field");

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float rounding = ImMax(ImGui::GetStyle().FrameRounding, h * 0.30f);
	const float hi = HoverIntensity(fid, hovered);

	const ImVec4 bg = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg),
		ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered), ImClamp(hi, 0.0f, 1.0f));
	dl->AddRectFilled(p0, p1, ImGui::GetColorU32(bg), rounding);

	ImVec4 rim = Soften(ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	rim.w = 0.30f + 0.50f * ImClamp(hi, 0.0f, 1.0f);
	dl->AddRect(p0, p1, ImGui::GetColorU32(rim), rounding, 0, ImMax(ImGui::GetStyle().FrameBorderSize, 1.0f));

	const bool empty = (current == nullptr || current[0] == '\0');
	const char* shown = empty ? (hint && hint[0] ? hint : "") : current;
	const ImVec4 tcol = ImGui::GetStyleColorVec4(empty ? ImGuiCol_TextDisabled : ImGuiCol_Text);
	const float pad = ImMax(rounding, 6.0f);
	const float ty = p0.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
	dl->PushClipRect(p0, ImVec2(p1.x - h * 0.85f, p1.y), true);
	dl->AddText(ImVec2(p0.x + pad, ty), ImGui::GetColorU32(tcol), shown);
	dl->PopClipRect();

	// Pencil affordance on the right, brightening on hover.
	ImVec4 pen = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	pen.w *= 0.45f + 0.55f * ImClamp(hi, 0.0f, 1.0f);
	const ImVec2 penSz = ImGui::CalcTextSize(ICON_FA_PENCIL);
	dl->AddText(ImVec2(p1.x - penSz.x - pad, ty), ImGui::GetColorU32(pen), ICON_FA_PENCIL);

	char popupKey[24];
	ImFormatString(popupKey, sizeof(popupKey), "##ef%08X", fid);
	if (clicked)
	{
		OpenInputPopup(popupKey, current ? current : "");
	}
	const bool changed = InputPopup(popupKey, out, hint);

	ImGui::PopID();
	SliderLabel(label); // visible label (text before "##") drawn after the field
	return changed;
}
} // namespace

bool StyledEditField(const char* label, std::string* value, const char* hint)
{
	if (!value)
	{
		return false;
	}
	std::string out;
	if (EditFieldCore(label, value->c_str(), out, hint))
	{
		*value = out;
		return true;
	}
	return false;
}

bool StyledEditField(const char* label, char* buf, size_t bufSize, const char* hint)
{
	if (!buf || bufSize == 0)
	{
		return false;
	}
	std::string out;
	if (EditFieldCore(label, buf, out, hint))
	{
		strncpy_s(buf, bufSize, out.c_str(), _TRUNCATE);
		return true;
	}
	return false;
}
} // namespace myui
