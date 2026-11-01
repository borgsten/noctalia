#pragma once

#include "config/config_types.h"
#include "shell/desktop/desktop_widget_layout.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace desktop_widgets {

  // Where a widget dropped on an output ends up.
  struct WidgetDrop {
    std::string outputName;
    // The output has to be pinned to the shared layout, which it would otherwise stop showing.
    bool pinsShared = false;
  };

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

    // Whether the widget's output, which can be a selector, resolves to this output.
    [[nodiscard]] bool isBoundTo(const DesktopWidgetState& widget, std::string_view outputKey) const;

    // Whether binding a widget to the output would make an auto output stop showing the shared layout.
    [[nodiscard]] bool bindingHidesShared(std::string_view outputKey) const;

    // Dropping `widget` on the output: a login box stays where it is, since each output has exactly one. A shared
    // widget stays shared while the output shows the shared layout, a bound widget keeps its selector while that
    // resolves to the output, otherwise it binds.
    [[nodiscard]] WidgetDrop dropOn(const DesktopWidgetState& widget, std::string_view outputKey) const;

    // The login box the output uses: the shared one if the output shows the shared layout and one
    // exists, otherwise the one bound to the output. nullptr if there is none.
    [[nodiscard]] const DesktopWidgetState* loginBoxFor(std::string_view outputKey) const;

    // Whether the output shows a login box: if its box is enabled or it has none (the default box), or if no output
    // the lock screen is interactive on would show one, so the screen can always be unlocked. `lockMonitors` is
    // lockscreen.monitors; empty means every output.
    [[nodiscard]] bool showsLoginBox(std::string_view outputKey, std::span<const std::string> lockMonitors = {}) const;

  private:
    const std::vector<DesktopWidgetState>& m_widgets;
    const std::unordered_map<std::string, DesktopWidgetOutputLayout>& m_outputLayouts;
    std::span<const WaylandOutput> m_outputs;
    std::vector<std::string> m_outputKeys;
  };

  // The keys of the outputs widgets can be shown on, in output order.
  // The enabled widgets to show and the outputs to show them on, in config order: a bound widget on
  // its output (left out while that output is unavailable or its selector is ambiguous), a shared widget
  // once per output that shows the shared layout.
  [[nodiscard]] std::vector<WidgetPlacement> resolveWidgetPlacements(
      std::span<const WaylandOutput> outputs, const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts
  );

} // namespace desktop_widgets
