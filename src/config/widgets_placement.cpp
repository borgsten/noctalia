#include "config/widgets_placement.h"

#include "config/widget_config.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace noctalia::config {

  namespace {

    template <typename Enum> struct NamedValue {
      Enum value;
      std::string_view name;
    };

    constexpr auto kPositionModeNames = std::to_array<NamedValue<DesktopWidgetPositionMode>>({
        {DesktopWidgetPositionMode::Absolute, "absolute"},
        {DesktopWidgetPositionMode::Relative, "relative"},
    });

    constexpr auto kAnchorNames = std::to_array<NamedValue<DesktopWidgetAnchor>>({
        {DesktopWidgetAnchor::TopLeft, "top_left"},
        {DesktopWidgetAnchor::Top, "top"},
        {DesktopWidgetAnchor::TopRight, "top_right"},
        {DesktopWidgetAnchor::Left, "left"},
        {DesktopWidgetAnchor::Center, "center"},
        {DesktopWidgetAnchor::Right, "right"},
        {DesktopWidgetAnchor::BottomLeft, "bottom_left"},
        {DesktopWidgetAnchor::Bottom, "bottom"},
        {DesktopWidgetAnchor::BottomRight, "bottom_right"},
    });

    constexpr auto kOutputLayoutNames = std::to_array<NamedValue<DesktopWidgetOutputLayout>>({
        {DesktopWidgetOutputLayout::Auto, "auto"},
        {DesktopWidgetOutputLayout::Shared, "shared"},
        {DesktopWidgetOutputLayout::Own, "own"},
    });

    template <typename Enum, std::size_t N>
    std::string_view nameOf(const std::array<NamedValue<Enum>, N>& names, Enum value) {
      const auto it = std::ranges::find(names, value, &NamedValue<Enum>::value);
      return it != names.end() ? it->name : std::string_view{};
    }

    template <typename Enum, std::size_t N>
    std::optional<Enum> valueOf(const std::array<NamedValue<Enum>, N>& names, std::string_view name) {
      const auto it = std::ranges::find(names, name, &NamedValue<Enum>::name);
      if (it == names.end()) {
        return std::nullopt;
      }
      return it->value;
    }

  } // namespace

  bool isPlacedWidgetKey(std::string_view key) { return std::ranges::contains(kPlacedWidgetKeys, key); }

  std::string_view positionModeName(DesktopWidgetPositionMode mode) { return nameOf(kPositionModeNames, mode); }

  std::optional<DesktopWidgetPositionMode> parsePositionMode(std::string_view name) {
    return valueOf(kPositionModeNames, name);
  }

  std::string_view anchorName(DesktopWidgetAnchor anchor) { return nameOf(kAnchorNames, anchor); }

  std::optional<DesktopWidgetAnchor> parseAnchor(std::string_view name) { return valueOf(kAnchorNames, name); }

  std::string_view outputLayoutName(DesktopWidgetOutputLayout layout) { return nameOf(kOutputLayoutNames, layout); }

  std::optional<DesktopWidgetOutputLayout> parseOutputLayout(std::string_view name) {
    return valueOf(kOutputLayoutNames, name);
  }

  toml::table placedWidgetTable(const DesktopWidgetState& widget, WidgetsPlacementStyle style) {
    const bool explicitDefaults = style == WidgetsPlacementStyle::Export;
    toml::table widgetTable;
    widgetTable.insert_or_assign("type", widget.type);
    widgetTable.insert_or_assign("output", widget.outputName);
    widgetTable.insert_or_assign("cx", static_cast<double>(widget.cx));
    widgetTable.insert_or_assign("cy", static_cast<double>(widget.cy));
    // A relative position does not depend on the output size, so it has no reference size to store.
    const bool relative = widget.positionMode == DesktopWidgetPositionMode::Relative;
    if (relative) {
      widgetTable.insert_or_assign("position", positionModeName(widget.positionMode));
    } else {
      widgetTable.insert_or_assign("placement_width", static_cast<double>(widget.placementWidth));
      widgetTable.insert_or_assign("placement_height", static_cast<double>(widget.placementHeight));
    }
    if (widget.anchor != DesktopWidgetAnchor::Center) {
      widgetTable.insert_or_assign("anchor", anchorName(widget.anchor));
    }
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
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts, WidgetsPlacementStyle style
  ) {
    toml::table gridTable;
    gridTable.insert_or_assign("visible", grid.visible);
    gridTable.insert_or_assign("cell_size", static_cast<std::int64_t>(grid.cellSize));
    gridTable.insert_or_assign("major_interval", static_cast<std::int64_t>(grid.majorInterval));
    section.insert_or_assign("grid", std::move(gridTable));

    // Every entry is written, including "auto": tables merge across config layers, so an explicit
    // "auto" is how an override resets a mode set by a lower layer.
    if (outputLayouts.empty()) {
      section.erase("output");
    } else {
      toml::table outputTable;
      for (const auto& [outputName, layout] : outputLayouts) {
        toml::table entry;
        entry.insert_or_assign("layout", outputLayoutName(layout));
        outputTable.insert_or_assign(outputName, std::move(entry));
      }
      section.insert_or_assign("output", std::move(outputTable));
    }

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
