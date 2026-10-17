#pragma once

#include "config/config_types.h"
#include "core/toml.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

// TOML shape shared by [desktop_widgets] and [lockscreen_widgets]: a grid plus an ordered set of
// placed widgets.
namespace noctalia::config {

  inline constexpr std::array<std::string_view, 5> kWidgetsPlacementSectionKeys = {
      "enabled", "schema_version", "grid", "widget", "widget_order",
  };

  inline constexpr std::array<std::string_view, 3> kWidgetsPlacementGridKeys = {
      "visible",
      "cell_size",
      "major_interval",
  };

  inline constexpr std::array<std::string_view, 14> kPlacedWidgetKeys = {
      "id",        "type",       "output",   "cx",     "cy",     "placement_width", "placement_height",
      "box_width", "box_height", "rotation", "flip_x", "flip_y", "enabled",         "settings",
  };

  [[nodiscard]] bool isPlacedWidgetKey(std::string_view key);

  enum class WidgetsPlacementStyle : std::uint8_t {
    // settings.toml: default-valued `enabled`/`settings` are omitted, and widget/widget_order are always
    // written so an empty list overrides widgets from lower config layers.
    Overrides,
    // Full effective config (config export, plugin/template config snapshot): like the rest of the export,
    // defaults are written explicitly, so `enabled` and `settings` are always present. widget/widget_order
    // are omitted when there are no widgets, an explicit empty widget_order would drop the widgets of any
    // config layer it is merged over.
    Export,
  };

  [[nodiscard]] toml::table placedWidgetTable(const DesktopWidgetState& widget, WidgetsPlacementStyle style);

  // Writes grid, widget and widget_order into `section`, replacing any previous values.
  void writeWidgetsPlacement(
      toml::table& section, const DesktopWidgetsGridState& grid, const std::vector<DesktopWidgetState>& widgets,
      WidgetsPlacementStyle style
  );

} // namespace noctalia::config
