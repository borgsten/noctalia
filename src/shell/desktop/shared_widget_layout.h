#pragma once

#include "config/config_types.h"
#include "shell/desktop/desktop_widget_layout.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace desktop_widgets {

  // Decides which outputs show which widgets: the single place the shared-layout rule lives.
  // Holds references to `widgets` and `outputLayouts`, which must outlive it.
  class SharedLayoutResolver {
  public:
    // `outputKeys` are the available outputs; widgets without an output belong to the first one.
    SharedLayoutResolver(
        const std::vector<DesktopWidgetState>& widgets,
        const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts,
        std::vector<std::string> outputKeys
    );

    // Whether the output shows the widgets with output = "*".
    [[nodiscard]] bool showsShared(std::string_view outputKey) const;

    // The outputs `widget` is shown on, in output order. Ignores `enabled`: a disabled widget still
    // has a place, it is just not drawn.
    [[nodiscard]] std::vector<std::string> outputsFor(const DesktopWidgetState& widget) const;

    // The login box the output uses: the shared one if the output shows the shared layout and one
    // exists, otherwise the one bound to the output. nullptr if there is none.
    [[nodiscard]] const DesktopWidgetState* loginBoxFor(std::string_view outputKey) const;

    // Whether the output shows a login box: if its box is enabled or it has none (the default box), or
    // if no output would show one, so the screen can always be unlocked.
    [[nodiscard]] bool showsLoginBox(std::string_view outputKey) const;

  private:
    [[nodiscard]] std::string_view boundOutput(const DesktopWidgetState& widget) const;

    const std::vector<DesktopWidgetState>& m_widgets;
    const std::unordered_map<std::string, DesktopWidgetOutputLayout>& m_outputLayouts;
    std::vector<std::string> m_outputKeys;
  };

  // The keys of the outputs widgets can be shown on, in output order.
  [[nodiscard]] std::vector<std::string> usableOutputKeys(const WaylandConnection& wayland);

  // The enabled widgets to show and the outputs to show them on, in config order: a bound widget on
  // its output (left out while that output is unavailable), a shared widget once per output that
  // shows the shared layout.
  [[nodiscard]] std::vector<WidgetPlacement> resolveWidgetPlacements(
      const WaylandConnection& wayland, const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts
  );

} // namespace desktop_widgets
