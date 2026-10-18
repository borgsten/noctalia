#pragma once

#include "config/config_types.h"
#include "core/toml.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// TOML shape shared by [desktop_widgets] and [lockscreen_widgets]: a grid plus an ordered set of
// placed widgets.
namespace noctalia::config {

  inline constexpr auto kWidgetsPlacementSectionKeys =
      std::to_array<std::string_view>({"enabled", "schema_version", "grid", "widget", "widget_order", "output"});

  inline constexpr auto kWidgetsPlacementGridKeys =
      std::to_array<std::string_view>({"visible", "cell_size", "major_interval"});

  inline constexpr auto kPlacedWidgetKeys = std::to_array<std::string_view>(
      {"id", "type", "output", "cx", "cy", "placement_width", "placement_height", "box_width", "box_height", "rotation",
       "flip_x", "flip_y", "enabled", "position", "anchor", "settings"}
  );

  // Keys of one `[<section>.output.<name>]` table.
  inline constexpr auto kWidgetsOutputKeys = std::to_array<std::string_view>({"layout"});

  [[nodiscard]] bool isPlacedWidgetKey(std::string_view key);

  [[nodiscard]] std::string_view positionModeName(DesktopWidgetPositionMode mode);
  [[nodiscard]] std::optional<DesktopWidgetPositionMode> parsePositionMode(std::string_view name);
  [[nodiscard]] std::string_view anchorName(DesktopWidgetAnchor anchor);
  [[nodiscard]] std::optional<DesktopWidgetAnchor> parseAnchor(std::string_view name);
  [[nodiscard]] std::string_view outputLayoutName(DesktopWidgetOutputLayout layout);
  [[nodiscard]] std::optional<DesktopWidgetOutputLayout> parseOutputLayout(std::string_view name);

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

  // Writes grid, widget, widget_order and output into `section`, replacing any previous values.
  void writeWidgetsPlacement(
      toml::table& section, const DesktopWidgetsGridState& grid, const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts, WidgetsPlacementStyle style
  );

} // namespace noctalia::config
