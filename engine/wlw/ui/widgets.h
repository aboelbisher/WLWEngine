#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "wlw/core/color.h"
#include "wlw/ui/canvas.h"
#include "wlw/ui/font.h"
#include "wlw/ui/icons.h"
#include "wlw/ui/ui_context.h"

// Immediate-mode widgets, drawn with the Canvas: call one every frame you want it shown. They take
// the UIContext for the pointer, for what they remember between frames (which one is held, a knob
// sliding) and for the Theme they're drawn with.
//
// Each kind of thing the player can do has its own widget, as in Material / Unreal's CommonUI, so a
// screen reads at a glance: Button = do something (one Primary per screen), Card = go somewhere
// (a mode, a level), Switch / SettingRow / IconToggle = a setting, IconButton = a small tool in a
// corner, Dialog = a group of choices over the game.
namespace wlw::ui {

// ---- Building blocks ----

// An id for a widget, from what identifies it on screen: its label and where it is (FNV-1a).
uint64_t WidgetId(std::string_view label, const Rect& rect);

// The pointer's effect on a pressable area this frame (like Dear ImGui's ButtonBehavior).
struct Press {
  bool clicked = false;  // pressed on it and released on it (sliding off cancels)
  bool held = false;     // down on it right now
  float amount = 0.0f;   // 0..1, eased: how far it's pushed in (for drawing)
};
Press Pressable(UIContext& ui, uint64_t id, const Rect& rect);

// A chunky "3D" slab, the look of casual-game buttons: a soft shadow, a darker lip under the face,
// the face (pushed down onto the lip by `press`), and a light band across its top. Returns the face.
Rect RaisedRect(Canvas& canvas, const Rect& rect, const core::Color& face, float radius, float depth, float press = 0.0f);

// `text` scaled down to fit `max_width` if it's wider at `size` (never up).
float FitTextSize(const Font& font, std::string_view text, float size, float max_width);

// Outlined text in the theme's ink (readable over anything).
TextStyle ThemedText(const Theme& theme, const core::Color& color, float alpha = 1.0f);

// ---- Buttons ----

enum class Variant { Primary, Secondary, Danger, Ghost };  // Ghost: no slab, just the label (a link)

// A labelled button, with an optional icon before the label. Returns true on the frame it's clicked.
bool Button(UIContext& ui, std::string_view label, const Rect& rect, Variant variant = Variant::Primary,
            Icon icon = Icon::None);

// A button with only an icon (round when the rect is square): corners, toolbars.
bool IconButton(UIContext& ui, Icon icon, const Rect& rect, Variant variant = Variant::Secondary);

// A round on/off icon button: off, it's faded and the icon is struck through (music, sound...).
// Flips `value` and returns true on the frame it's clicked.
bool IconToggle(UIContext& ui, Icon icon, bool& value, const Rect& rect);

// ---- Settings ----

// A switch: a pill with a knob that slides across. Flips `value`, returns true when clicked.
bool Switch(UIContext& ui, bool& value, const Rect& rect);

// A settings row: icon, label, and a Switch on the right; the whole row is tappable.
bool SettingRow(UIContext& ui, Icon icon, std::string_view label, bool& value, const Rect& rect);

// A row that leads somewhere (how to play, privacy...): icon, label and ">". Returns true when clicked.
bool LinkRow(UIContext& ui, Icon icon, std::string_view label, const Rect& rect);

// ---- Cards ----

struct CardResult {
  bool clicked = false;
  Rect art;  // the picture's area on the card (it moves with the press): the caller draws there
};
// A tile that leads somewhere, e.g. a game mode: coloured by `accent`, a picture area at the top for
// the caller to draw in, the title and a subtitle under it, an optional badge ("NEW") on its corner.
CardResult Card(UIContext& ui, std::string_view title, std::string_view subtitle, const Rect& rect,
                const core::Color& accent, std::string_view badge = {});

// ---- Containers ----

struct DialogResult {
  Rect content;        // the panel under the title: lay the dialog's widgets out in here
  bool close = false;  // the close button, or a tap outside the panel
};
// A centred dialog over a dimmed screen: the panel slides in the first frames it's shown, the title
// sits on top, and with `closable` there's a close button in its corner. Lay out the content in
// `content` (it moves with the panel).
DialogResult Dialog(UIContext& ui, std::string_view title, glm::vec2 size, bool closable = true);

// Tabs in a well: one of `labels` chosen; the marker slides between them. Returns true when changed.
bool Segmented(UIContext& ui, std::span<const std::string_view> labels, int& selected, const Rect& rect);

// ---- Display ----

// A small pill with an optional icon and a short text (a stat: "BEST 14,303"), centred on `center`.
// Returns where it was drawn.
Rect Chip(UIContext& ui, std::string_view text, glm::vec2 center, Icon icon = Icon::None, float height = 38.0f);
// The size Chip will have (to lay several out in a row).
glm::vec2 ChipSize(const UIContext& ui, std::string_view text, Icon icon = Icon::None, float height = 38.0f);

// `count` stars side by side centred on `center`, the first `filled` in the primary colour.
void Stars(UIContext& ui, glm::vec2 center, float size, int filled, int count = 3);

// ---- The first widgets (a font and a style per call, no theme) ----

// How a Button / Toggle looks.
struct ButtonStyle {
  core::Color fill{0.98f, 0.76f, 0.25f, 1.0f};
  core::Color text{0.10f, 0.08f, 0.16f, 1.0f};
  float text_size = 0.0f;  // 0 = 45% of the button height
  float corner_radius = 14.0f;
  core::Color shadow{0.0f, 0.0f, 0.0f, 0.35f};  // the soft shadow under the button; alpha 0 = none
};

// A button: returns true on the frame it was clicked (pressed on it and released on it - sliding
// off cancels). It sinks while held. Only responds while ui.interactive.
bool Button(UIContext& ui, const Font& font, std::string_view label, const Rect& rect, const ButtonStyle& style = {});

// An on/off switch drawn as a button reading "<label> ON" or "<label> OFF" (dimmed when off).
// Flips `value` and returns true on the frame it was clicked.
bool Toggle(UIContext& ui, const Font& font, std::string_view label, bool& value, const Rect& rect,
            const ButtonStyle& style = {});

// A rounded box with a soft shadow under it: the background of dialogs and panels (drawing only).
void Panel(Canvas& canvas, const Rect& rect, const core::Color& fill, float corner_radius = 24.0f,
           const core::Color& shadow = {0.0f, 0.0f, 0.0f, 0.35f});

}  // namespace wlw::ui
