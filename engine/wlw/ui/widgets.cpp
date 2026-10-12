#include "wlw/ui/widgets.h"

#include <cstdint>
#include <string>

namespace wlw::ui {

bool Button(UIContext& ui, const Font& font, std::string_view label, const Rect& rect, const ButtonStyle& style) {
  // Immediate mode keeps no widget objects, so a button is recognised from frame to frame by an
  // id made from what identifies it: its label and where it is (FNV-1a hash).
  uint64_t id = 1469598103934665603ull;
  auto mix = [&id](uint64_t value) { id = (id ^ value) * 1099511628211ull; };
  for (char c : label) mix(static_cast<unsigned char>(c));
  mix(static_cast<uint64_t>(static_cast<int64_t>(rect.Center().x * 4.0f)));
  mix(static_cast<uint64_t>(static_cast<int64_t>(rect.Center().y * 4.0f)));
  if (id == 0) id = 1;

  const PointerState& pointer = ui.pointer;
  const glm::vec2 p = pointer.position;
  const bool inside = p.x >= rect.min.x && p.x <= rect.max.x && p.y >= rect.min.y && p.y <= rect.max.y;
  bool clicked = false;
  if (ui.interactive) {
    if (pointer.pressed && inside) ui.active_widget = id;  // went down on this button
    if (ui.active_widget == id && pointer.released) {     // ...and up: a click only if still on it
      clicked = inside;
      ui.active_widget = 0;
    } else if (ui.active_widget == id && !pointer.down) {
      ui.active_widget = 0;  // the press ended elsewhere without us seeing the release
    }
  }
  const bool held = ui.interactive && ui.active_widget == id && pointer.down && inside;

  // A darker slab under the face; held, the face sinks onto it and shrinks a little.
  Canvas& canvas = ui.canvas;
  const glm::vec2 depth(0.0f, 4.0f);
  if (style.shadow.a > 0.0f) canvas.FillRect({rect.min + depth, rect.max + depth}, style.shadow, style.corner_radius);
  Rect face = rect;
  if (held) {
    const glm::vec2 shrink = rect.Size() * 0.03f;
    face = {rect.min + shrink + depth * 0.6f, rect.max - shrink + depth * 0.6f};
  }
  core::Color fill = style.fill;
  if (held) fill = {fill.r * 0.85f, fill.g * 0.85f, fill.b * 0.85f, fill.a};
  canvas.FillRect(face, fill, style.corner_radius);
  const float text_size = style.text_size > 0.0f ? style.text_size : rect.Size().y * 0.45f;
  TextStyle text;
  text.color = style.text;
  canvas.Text(font, label, face.Center(), text_size, Align::Center, text);
  return clicked;
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

void Panel(Canvas& canvas, const Rect& rect, const core::Color& fill, float corner_radius, const core::Color& shadow) {
  if (shadow.a > 0.0f) {
    canvas.FillRect({rect.min + glm::vec2(0.0f, 6.0f), rect.max + glm::vec2(0.0f, 6.0f)}, shadow, corner_radius);
  }
  canvas.FillRect(rect, fill, corner_radius);
}

}  // namespace wlw::ui
