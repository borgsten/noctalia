#include "config/widgets_placement.h"

#include "config/widget_config.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace noctalia::config {

  bool isPlacedWidgetKey(std::string_view key) { return std::ranges::contains(kPlacedWidgetKeys, key); }

  toml::table placedWidgetTable(const DesktopWidgetState& widget, WidgetsPlacementStyle style) {
    const bool explicitDefaults = style == WidgetsPlacementStyle::Export;
    toml::table widgetTable;
    widgetTable.insert_or_assign("type", widget.type);
    widgetTable.insert_or_assign("output", widget.outputName);
    widgetTable.insert_or_assign("cx", static_cast<double>(widget.cx));
    widgetTable.insert_or_assign("cy", static_cast<double>(widget.cy));
    widgetTable.insert_or_assign("placement_width", static_cast<double>(widget.placementWidth));
    widgetTable.insert_or_assign("placement_height", static_cast<double>(widget.placementHeight));
    widgetTable.insert_or_assign("box_width", static_cast<double>(widget.boxWidth));
    widgetTable.insert_or_assign("box_height", static_cast<double>(widget.boxHeight));
    widgetTable.insert_or_assign("rotation", static_cast<double>(widget.rotationRad));
    if (widget.flipX) {
      widgetTable.insert_or_assign("flip_x", true);
    }
    if (widget.flipY) {
      widgetTable.insert_or_assign("flip_y", true);
    }
    if (explicitDefaults || !widget.enabled) {
      widgetTable.insert_or_assign("enabled", widget.enabled);
    }
    if (explicitDefaults || !widget.settings.empty()) {
      toml::table settingsTable;
      for (const auto& [key, value] : widget.settings) {
        insertWidgetSettingValue(settingsTable, key, value);
      }
      widgetTable.insert_or_assign("settings", std::move(settingsTable));
    }
    return widgetTable;
  }

  void writeWidgetsPlacement(
      toml::table& section, const DesktopWidgetsGridState& grid, const std::vector<DesktopWidgetState>& widgets,
      WidgetsPlacementStyle style
  ) {
    toml::table gridTable;
    gridTable.insert_or_assign("visible", grid.visible);
    gridTable.insert_or_assign("cell_size", static_cast<std::int64_t>(grid.cellSize));
    gridTable.insert_or_assign("major_interval", static_cast<std::int64_t>(grid.majorInterval));
    section.insert_or_assign("grid", std::move(gridTable));

    toml::table widgetTable;
    toml::array widgetOrder;
    for (const auto& widget : widgets) {
      if (widget.id.empty() || widget.type.empty()) {
        continue;
      }
      widgetTable.insert_or_assign(widget.id, placedWidgetTable(widget, style));
      widgetOrder.push_back(widget.id);
    }
    if (style == WidgetsPlacementStyle::Export && widgetOrder.empty()) {
      return;
    }
    section.insert_or_assign("widget", std::move(widgetTable));
    section.insert_or_assign("widget_order", std::move(widgetOrder));
  }

} // namespace noctalia::config
