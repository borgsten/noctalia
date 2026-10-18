#pragma once

#include "config/config_types.h"
#include "shell/desktop/desktop_widget_layout.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace desktop_widgets {

  // Decides which outputs show which widgets: the single place the shared-layout rule lives. Outputs are
  // named by their output key; a widget's output and the layout table names can also be monitor selectors.
  // Holds references to `widgets`, `outputLayouts` and `outputs`, which must outlive it.
  class SharedLayoutResolver {
  public:
    // A widget without an output belongs to the first usable output.
    SharedLayoutResolver(
        const std::vector<DesktopWidgetState>& widgets,
        const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts,
        std::span<const WaylandOutput> outputs
    );

    // Whether the output shows the widgets with output = "*".
    [[nodiscard]] bool showsShared(std::string_view outputKey) const;

    // The outputs `widget` is shown on, in output order. Ignores `enabled`: a disabled widget still
    // has a place, it is just not drawn.
    [[nodiscard]] std::vector<std::string> outputsFor(const DesktopWidgetState& widget) const;

    // The outputLayouts key that applies to the output: its own key if present, otherwise the first selector
    // that resolves to it, otherwise the output key itself.
    [[nodiscard]] std::string layoutKeyFor(std::string_view outputKey) const;

  private:
    [[nodiscard]] bool isBoundTo(const DesktopWidgetState& widget, std::string_view outputKey) const;

    const std::vector<DesktopWidgetState>& m_widgets;
    const std::unordered_map<std::string, DesktopWidgetOutputLayout>& m_outputLayouts;
    std::span<const WaylandOutput> m_outputs;
    std::vector<std::string> m_outputKeys;
  };

  // The enabled widgets to show and the outputs to show them on, in config order: a bound widget on
  // its output (left out while that output is unavailable or its selector is ambiguous), a shared widget
  // once per output that shows the shared layout.
  [[nodiscard]] std::vector<WidgetPlacement> resolveWidgetPlacements(
      std::span<const WaylandOutput> outputs, const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts
  );

} // namespace desktop_widgets
