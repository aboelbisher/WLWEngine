#include "wlw/ui/widgets.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "wlw/anim/ease.h"
#include "wlw/ui/layout.h"

namespace wlw::ui {

namespace {

using core::Color;

Color Shade(const Color& c, float k) { return {c.r * k, c.g * k, c.b * k, c.a}; }
Color Mix(const Color& a, const Color& b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color WithAlpha(const Color& c, float a) { return {c.r, c.g, c.b, c.a * a}; }

float Approach(float value, float target, float step) {
  return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

bool Inside(const Rect& rect, glm::vec2 p) {
  return p.x >= rect.min.x && p.x <= rect.max.x && p.y >= rect.min.y && p.y <= rect.max.y;
}

// Ids for what a widget remembers besides being held (salted so they don't meet WidgetId's).
uint64_t Salted(uint64_t id, uint64_t salt) { return (id ^ (salt * 0x9E3779B97F4A7C15ull)) | 1u; }

// The colours of a variant's slab and of what's written on it.
struct Look {
  Color face;
  Color content;
};
Look LookOf(const Theme& theme, Variant variant) {
  switch (variant) {
    case Variant::Primary:
      return {theme.primary, theme.on_primary};
    case Variant::Danger:
      return {theme.danger, theme.on_danger};
    case Variant::Ghost:
      return {{0.0f, 0.0f, 0.0f, 0.0f}, theme.text_muted};
    case Variant::Secondary:
      break;
  }
  return {theme.secondary, theme.on_secondary};
}

// Text on a slab: plain on the light primary, outlined (game style) on the darker ones.
TextStyle SlabText(const Theme& theme, Variant variant, const Color& color) {
  if (variant == Variant::Primary) {
    TextStyle style;
    style.color = color;
    return style;
  }
  return ThemedText(theme, color);
}

// A switch's look: the knob at `on` (0..1) across the track.
void DrawSwitch(Canvas& canvas, const Theme& theme, const Rect& rect, float on) {
  const float h = rect.Size().y;
  const float e = anim::ease::InOutQuad(std::clamp(on, 0.0f, 1.0f));
  canvas.FillRect({rect.min + glm::vec2(0.0f, 2.0f), rect.max + glm::vec2(0.0f, 2.0f)}, {0.0f, 0.0f, 0.0f, 0.25f},
                  h * 0.5f);
  canvas.FillRect(rect, Mix(theme.track, theme.on, e), h * 0.5f);
  const float r = h * 0.5f - 3.0f;
  const glm::vec2 knob(rect.min.x + h * 0.5f + (rect.Size().x - h) * e, rect.Center().y);
  canvas.Circle(knob + glm::vec2(0.0f, 2.0f), r, {0.0f, 0.0f, 0.0f, 0.3f});
  canvas.Circle(knob, r, {1.0f, 1.0f, 1.0f, 1.0f});
}

// The icon in a rounded square at the start of a row; returns where the row's label starts.
float RowStart(Canvas& canvas, const Theme& theme, Icon icon, const Rect& face) {
  const float h = face.Size().y;
  const float pad = h * 0.16f;
  const Rect bubble{face.min + glm::vec2(pad), face.min + glm::vec2(h - pad)};
  canvas.FillRect(bubble, theme.secondary, theme.radius_small);
  DrawIcon(canvas, icon, Inset(bubble, bubble.Size().x * 0.2f), theme.on_secondary);
  return bubble.max.x + pad * 1.3f;
}

}  // namespace

// ---- Building blocks ----

uint64_t WidgetId(std::string_view label, const Rect& rect) {
  uint64_t id = 1469598103934665603ull;
  auto mix = [&id](uint64_t value) { id = (id ^ value) * 1099511628211ull; };
  for (char c : label) mix(static_cast<unsigned char>(c));
  mix(static_cast<uint64_t>(static_cast<int64_t>(rect.Center().x * 4.0f)));
  mix(static_cast<uint64_t>(static_cast<int64_t>(rect.Center().y * 4.0f)));
  return id == 0 ? 1 : id;
}

Press Pressable(UIContext& ui, uint64_t id, const Rect& rect) {
  // Immediate mode keeps no widget objects: the one the pointer went down on is remembered by its
  // id, and a click is a release on that same one.
  const PointerState& pointer = ui.pointer;
  const bool inside = Inside(rect, pointer.position);
  Press press;
  if (ui.interactive) {
    if (pointer.pressed && inside) ui.active_widget = id;  // went down on this one
    if (ui.active_widget == id && pointer.released) {     // ...and up: a click only if still on it
      press.clicked = inside;
      ui.active_widget = 0;
    } else if (ui.active_widget == id && !pointer.down) {
      ui.active_widget = 0;  // the press ended elsewhere without us seeing the release
    }
  }
  press.held = ui.interactive && ui.active_widget == id && pointer.down && inside;
  // Pushed in fast, springs back a little slower; a click shows at least a dip.
  float& amount = ui.Memory(Salted(id, 1));
  amount = Approach(amount, press.held ? 1.0f : 0.0f, ui.dt * (press.held ? 16.0f : 7.0f));
  if (press.held || press.clicked) amount = std::max(amount, 0.5f);
  press.amount = amount;
  return press;
}

Rect RaisedRect(Canvas& canvas, const Rect& rect, const Color& face, float radius, float depth, float press) {
  const glm::vec2 down(0.0f, depth);
  if (depth > 0.0f) {
    const glm::vec2 shadow(0.0f, depth + 3.0f);
    canvas.FillRect({rect.min + shadow, rect.max + shadow}, {0.0f, 0.0f, 0.0f, 0.25f * face.a}, radius);
    canvas.FillRect({rect.min + down, rect.max + down}, Shade(face, 0.62f), radius);  // the lip
  }
  const glm::vec2 sink = down * std::clamp(press, 0.0f, 1.0f) * 0.8f;
  const Rect f{rect.min + sink, rect.max + sink};
  canvas.FillRect(f, face, radius);
  // Light from above: a soft band across the top of the face.
  const float h = f.Size().y;
  const Rect band{f.min + glm::vec2(std::min(radius * 0.7f, f.Size().x * 0.2f), h * 0.08f),
                  {f.max.x - std::min(radius * 0.7f, f.Size().x * 0.2f), f.min.y + h * 0.38f}};
  if (radius < h * 0.45f && band.Size().x > 0.0f && band.Size().y > 0.0f) {  // not on round things
    canvas.FillRect(band, {1.0f, 1.0f, 1.0f, 0.12f * face.a}, std::min(radius * 0.7f, band.Size().y * 0.5f));
  }
  return f;
}

float FitTextSize(const Font& font, std::string_view text, float size, float max_width) {
  const float width = font.Measure(text, size);
  return width > max_width && width > 0.0f ? size * max_width / width : size;
}

TextStyle ThemedText(const Theme& theme, const Color& color, float alpha) {
  TextStyle style = TextStyle::Outlined(color, theme.ink, alpha);
  style.outline = theme.outline;
  return style;
}

// ---- Buttons ----

bool Button(UIContext& ui, std::string_view label, const Rect& rect, Variant variant, Icon icon) {
  const Theme& theme = ui.theme;
  Canvas& canvas = ui.canvas;
  const Press press = Pressable(ui, WidgetId(label, rect), rect);
  const Look look = LookOf(theme, variant);
  Rect face = rect;
  if (variant != Variant::Ghost) {
    face = RaisedRect(canvas, rect, look.face, std::min(theme.radius, rect.Size().y * 0.5f), theme.depth, press.amount);
  } else {
    face = Centered(rect.Center(), rect.Size() * (1.0f - 0.06f * press.amount));
  }
  if (!theme.font) return press.clicked;
  const float h = face.Size().y;
  const float icon_size = icon == Icon::None ? 0.0f : h * 0.44f;
  const float gap = icon == Icon::None ? 0.0f : h * 0.16f;
  const float text_size = FitTextSize(*theme.font, label, h * 0.42f, face.Size().x - h * 0.5f - icon_size - gap);
  const float text_width = theme.font->Measure(label, text_size);
  float x = face.Center().x - (icon_size + gap + text_width) * 0.5f;
  if (icon != Icon::None) {
    DrawIcon(canvas, icon, Centered({x + icon_size * 0.5f, face.Center().y}, glm::vec2(icon_size)), look.content);
    x += icon_size + gap;
  }
  canvas.Text(*theme.font, label, {x, face.Center().y}, text_size, Align::Left, SlabText(theme, variant, look.content));
  return press.clicked;
}

bool IconButton(UIContext& ui, Icon icon, const Rect& rect, Variant variant) {
  const Theme& theme = ui.theme;
  const Press press = Pressable(ui, WidgetId("icon", rect) ^ static_cast<uint64_t>(icon), rect);
  const Look look = LookOf(theme, variant);
  const float size = std::min(rect.Size().x, rect.Size().y);
  Rect face = rect;
  if (variant != Variant::Ghost) {
    face = RaisedRect(ui.canvas, rect, look.face, size * 0.5f, theme.depth, press.amount);
  }
  DrawIcon(ui.canvas, icon, Centered(face.Center(), glm::vec2(size * 0.5f)), look.content);
  return press.clicked;
}

bool IconToggle(UIContext& ui, Icon icon, bool& value, const Rect& rect) {
  const Theme& theme = ui.theme;
  const Press press = Pressable(ui, WidgetId("toggle", rect) ^ static_cast<uint64_t>(icon), rect);
  const float size = std::min(rect.Size().x, rect.Size().y);
  const Color face = value ? theme.secondary : Mix(theme.secondary, theme.track, 0.55f);
  const Rect f = RaisedRect(ui.canvas, rect, face, size * 0.5f, theme.depth, press.amount);
  const Rect box = Centered(f.Center(), glm::vec2(size * 0.5f));
  DrawIcon(ui.canvas, icon, box, value ? theme.on_secondary : theme.text_muted);
  if (!value) {  // struck through
    ui.canvas.Line(box.min + glm::vec2(-1.0f), box.max + glm::vec2(1.0f), size * 0.12f, face);
    ui.canvas.Line(box.min, box.max, size * 0.07f, theme.danger);
  }
  if (!press.clicked) return false;
  value = !value;
  return true;
}

// ---- Settings ----

bool Switch(UIContext& ui, bool& value, const Rect& rect) {
  const uint64_t id = WidgetId("switch", rect);
  const bool clicked = Pressable(ui, id, rect).clicked;
  if (clicked) value = !value;
  float& knob = ui.Memory(Salted(id, 2), value ? 1.0f : 0.0f);
  knob = Approach(knob, value ? 1.0f : 0.0f, ui.dt * 7.0f);
  DrawSwitch(ui.canvas, ui.theme, rect, knob);
  return clicked;
}

bool SettingRow(UIContext& ui, Icon icon, std::string_view label, bool& value, const Rect& rect) {
  const Theme& theme = ui.theme;
  Canvas& canvas = ui.canvas;
  const uint64_t id = WidgetId(label, rect);
  const Press press = Pressable(ui, id, rect);
  if (press.clicked) value = !value;
  const Rect face = RaisedRect(canvas, rect, theme.surface_raised, theme.radius_small, 3.0f, press.amount);
  const float x = RowStart(canvas, theme, icon, face);
  const float h = face.Size().y;
  const Rect track = Centered({face.max.x - h * 0.2f - h * 0.5f, face.Center().y}, {h, h * 0.54f});
  if (theme.font) {
    const float size = FitTextSize(*theme.font, label, h * 0.36f, track.min.x - x - 8.0f);
    canvas.Text(*theme.font, label, {x, face.Center().y}, size, Align::Left, ThemedText(theme, theme.text));
  }
  float& knob = ui.Memory(Salted(id, 2), value ? 1.0f : 0.0f);
  knob = Approach(knob, value ? 1.0f : 0.0f, ui.dt * 7.0f);
  DrawSwitch(canvas, theme, track, knob);
  return press.clicked;
}

bool LinkRow(UIContext& ui, Icon icon, std::string_view label, const Rect& rect) {
  const Theme& theme = ui.theme;
  Canvas& canvas = ui.canvas;
  const Press press = Pressable(ui, WidgetId(label, rect), rect);
  const Rect face = RaisedRect(canvas, rect, theme.surface_raised, theme.radius_small, 3.0f, press.amount);
  const float x = RowStart(canvas, theme, icon, face);
  const float h = face.Size().y;
  const glm::vec2 chevron(face.max.x - h * 0.42f, face.Center().y);
  DrawIcon(canvas, Icon::Next, Centered(chevron, glm::vec2(h * 0.3f)), theme.text_muted);
  if (theme.font) {
    const float size = FitTextSize(*theme.font, label, h * 0.36f, chevron.x - h * 0.3f - x);
    canvas.Text(*theme.font, label, {x, face.Center().y}, size, Align::Left, ThemedText(theme, theme.text));
  }
  return press.clicked;
}

// ---- Cards ----

CardResult Card(UIContext& ui, std::string_view title, std::string_view subtitle, const Rect& rect,
                const Color& accent, std::string_view badge) {
  const Theme& theme = ui.theme;
  Canvas& canvas = ui.canvas;
  const Press press = Pressable(ui, WidgetId(title, rect), rect);
  const Rect face = RaisedRect(canvas, rect, accent, theme.radius, theme.depth, press.amount);
  const float h = face.Size().y;
  const float pad = std::min(8.0f, face.Size().x * 0.07f);
  CardResult result;
  result.clicked = press.clicked;
  result.art = {face.min + glm::vec2(pad), {face.max.x - pad, face.min.y + h * 0.58f}};
  canvas.FillRect(result.art, Shade(accent, 0.7f), theme.radius_small);
  if (theme.font) {
    const float below = face.max.y - result.art.max.y;
    const float width = face.Size().x - 2.0f * pad;
    const float title_size = FitTextSize(*theme.font, title, h * 0.15f, width);
    const float sub_size = FitTextSize(*theme.font, subtitle, std::max(h * 0.12f, 18.0f), width);
    canvas.Text(*theme.font, title, {face.Center().x, result.art.max.y + below * 0.36f}, title_size, Align::Center,
                ThemedText(theme, theme.text));
    canvas.Text(*theme.font, subtitle, {face.Center().x, result.art.max.y + below * 0.74f}, sub_size, Align::Center,
                ThemedText(theme, theme.text, 0.85f));
    if (!badge.empty()) {
      const float size = 16.0f;
      const glm::vec2 pill(theme.font->Measure(badge, size) + 18.0f, 26.0f);
      const Rect at = Centered({face.max.x - pill.x * 0.5f + 6.0f, face.min.y + 2.0f}, pill);
      canvas.FillRect({at.min + glm::vec2(0.0f, 2.0f), at.max + glm::vec2(0.0f, 2.0f)}, Shade(theme.danger, 0.6f),
                      pill.y * 0.5f);
      canvas.FillRect(at, theme.danger, pill.y * 0.5f);
      canvas.Text(*theme.font, badge, at.Center(), size, Align::Center, ThemedText(theme, theme.on_danger));
    }
  }
  return result;
}

// ---- Containers ----

DialogResult Dialog(UIContext& ui, std::string_view title, glm::vec2 size, bool closable) {
  const Theme& theme = ui.theme;
  Canvas& canvas = ui.canvas;
  const uint64_t id = WidgetId(title, {{0.0f, 0.0f}, size});
  // Comes in over ~0.3 s: the screen dims, the panel rises with a little overshoot.
  float& shown = ui.Memory(Salted(id, 3));
  shown = std::min(shown + ui.dt / 0.3f, 1.0f);
  canvas.FillRect({{0.0f, 0.0f}, canvas.Size()}, WithAlpha(theme.scrim, std::min(shown / 0.6f, 1.0f)));
  const float rise = (1.0f - anim::ease::OutBack(shown)) * 70.0f;
  const Rect panel = Centered(ui.safe_area.Center() + glm::vec2(0.0f, rise), size);
  Panel(canvas, panel, theme.surface, theme.radius_large);
  canvas.FillRect({panel.min + glm::vec2(10.0f, 8.0f), {panel.max.x - 10.0f, panel.min.y + 64.0f}},
                  {1.0f, 1.0f, 1.0f, 0.04f}, theme.radius_large - 8.0f);  // a lighter title band
  if (theme.font) {
    const float title_size = FitTextSize(*theme.font, title, 36.0f, size.x - 120.0f);
    canvas.Text(*theme.font, title, {panel.Center().x, panel.min.y + 38.0f}, title_size, Align::Center,
                ThemedText(theme, theme.primary));
  }
  DialogResult result;
  result.content = {panel.min + glm::vec2(20.0f, 82.0f), panel.max - glm::vec2(20.0f, 22.0f)};
  if (!closable) return result;
  if (IconButton(ui, Icon::Close, Centered({panel.max.x - 14.0f, panel.min.y + 14.0f}, {50.0f, 50.0f}),
                 Variant::Danger)) {
    result.close = true;
  }
  // A tap outside the panel (down and up outside) closes it too.
  float& outside = ui.Memory(Salted(id, 4));
  if (ui.interactive) {
    const bool in = Inside(panel, ui.pointer.position);
    if (ui.pointer.pressed) outside = in ? 0.0f : 1.0f;
    if (ui.pointer.released) {
      if (outside > 0.0f && !in && shown >= 1.0f) result.close = true;
      outside = 0.0f;
    }
  }
  return result;
}

bool Segmented(UIContext& ui, std::span<const std::string_view> labels, int& selected, const Rect& rect) {
  const Theme& theme = ui.theme;
  Canvas& canvas = ui.canvas;
  const int count = static_cast<int>(labels.size());
  if (count == 0) return false;
  const float h = rect.Size().y;
  canvas.FillRect(rect, theme.track, h * 0.5f);
  const Rect inner = Inset(rect, 4.0f);
  bool changed = false;
  for (int i = 0; i < count; ++i) {
    if (Pressable(ui, WidgetId(labels[i], Column(inner, i, count, 0.0f)), Column(inner, i, count, 0.0f)).clicked &&
        i != selected) {
      selected = i;
      changed = true;
    }
  }
  float& marker = ui.Memory(Salted(WidgetId("tabs", rect), 5), static_cast<float>(selected));
  marker = Approach(marker, static_cast<float>(selected), ui.dt * 9.0f);
  const float width = inner.Size().x / static_cast<float>(count);
  const Rect pill{{inner.min.x + marker * width, inner.min.y}, {inner.min.x + (marker + 1.0f) * width, inner.max.y - 3.0f}};
  RaisedRect(canvas, pill, theme.primary, pill.Size().y * 0.5f, 3.0f);
  if (theme.font) {
    for (int i = 0; i < count; ++i) {
      const Rect cell = Column(inner, i, count, 0.0f);
      const float size = FitTextSize(*theme.font, labels[i], h * 0.42f, width - 12.0f);
      const bool on = i == selected;
      const glm::vec2 at = cell.Center() - glm::vec2(0.0f, 1.5f);
      if (on) {
        TextStyle style;
        style.color = theme.on_primary;
        canvas.Text(*theme.font, labels[i], at, size, Align::Center, style);
      } else {
        canvas.Text(*theme.font, labels[i], at, size, Align::Center, ThemedText(theme, theme.text_muted));
      }
    }
  }
  return changed;
}

// ---- Display ----

glm::vec2 ChipSize(const UIContext& ui, std::string_view text, Icon icon, float height) {
  const float icon_part = icon == Icon::None ? 0.0f : height * 0.75f;  // the icon and the gap after it
  const float text_width = ui.theme.font ? ui.theme.font->Measure(text, height * 0.5f) : 0.0f;
  return {height * 0.84f + icon_part + text_width, height};
}

Rect Chip(UIContext& ui, std::string_view text, glm::vec2 center, Icon icon, float height) {
  const Theme& theme = ui.theme;
  Canvas& canvas = ui.canvas;
  const float text_size = height * 0.5f;
  const float icon_size = icon == Icon::None ? 0.0f : height * 0.55f;
  const float gap = icon == Icon::None ? 0.0f : height * 0.2f;
  const float pad = height * 0.42f;
  const Rect rect = Centered(center, ChipSize(ui, text, icon, height));
  canvas.FillRect(rect, WithAlpha(theme.track, 0.85f), height * 0.5f);
  float x = rect.min.x + pad;
  if (icon != Icon::None) {
    DrawIcon(canvas, icon, Centered({x + icon_size * 0.5f, center.y}, glm::vec2(icon_size)), theme.primary);
    x += icon_size + gap;
  }
  if (theme.font) canvas.Text(*theme.font, text, {x, center.y}, text_size, Align::Left, ThemedText(theme, theme.text));
  return rect;
}

void Stars(UIContext& ui, glm::vec2 center, float size, int filled, int count) {
  const Theme& theme = ui.theme;
  for (int i = 0; i < count; ++i) {
    const glm::vec2 at = center + glm::vec2((static_cast<float>(i) - (count - 1) * 0.5f) * size * 1.15f, 0.0f);
    DrawIcon(ui.canvas, Icon::Star, Centered(at + glm::vec2(0.0f, 2.0f), glm::vec2(size * 1.18f)), theme.ink);
    DrawIcon(ui.canvas, Icon::Star, Centered(at, glm::vec2(size)), i < filled ? theme.primary : theme.surface_raised);
  }
}

// ---- The first widgets ----

bool Button(UIContext& ui, const Font& font, std::string_view label, const Rect& rect, const ButtonStyle& style) {
  const Press press = Pressable(ui, WidgetId(label, rect), rect);
  const float depth = style.shadow.a > 0.0f ? 4.0f : 0.0f;
  const Rect face = RaisedRect(ui.canvas, rect, style.fill, style.corner_radius, depth, press.amount);
  const float text_size = style.text_size > 0.0f ? style.text_size : rect.Size().y * 0.45f;
  TextStyle text;
  text.color = style.text;
  ui.canvas.Text(font, label, face.Center(), FitTextSize(font, label, text_size, rect.Size().x - 12.0f), Align::Center,
                 text);
  return press.clicked;
}

bool Toggle(UIContext& ui, const Font& font, std::string_view label, bool& value, const Rect& rect,
            const ButtonStyle& style) {
  ButtonStyle look = style;
  if (!value) {  // off: the same button, faded
    look.fill = {style.fill.r * 0.55f, style.fill.g * 0.55f, style.fill.b * 0.55f, style.fill.a};
    look.text = {style.text.r, style.text.g, style.text.b, style.text.a * 0.6f};
  }
  std::string text(label);
  text += value ? " ON" : " OFF";
  if (!Button(ui, font, text, rect, look)) return false;
  value = !value;
  return true;
}

void Panel(Canvas& canvas, const Rect& rect, const Color& fill, float corner_radius, const Color& shadow) {
  if (shadow.a > 0.0f) {
    canvas.FillRect({rect.min + glm::vec2(0.0f, 6.0f), rect.max + glm::vec2(0.0f, 6.0f)}, shadow, corner_radius);
  }
  canvas.FillRect(rect, fill, corner_radius);
}

}  // namespace wlw::ui
