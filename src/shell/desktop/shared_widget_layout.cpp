#include "shell/desktop/shared_widget_layout.h"

#include "shell/desktop/desktop_widget_layout.h"
#include "shell/lockscreen/lockscreen_login_box.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace desktop_widgets {

  SharedLayoutResolver::SharedLayoutResolver(
      const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts,
      std::vector<std::string> outputKeys
  )
      : m_widgets(widgets), m_outputLayouts(outputLayouts), m_outputKeys(std::move(outputKeys)) {}

  std::string_view SharedLayoutResolver::boundOutput(const DesktopWidgetState& widget) const {
    if (widget.outputName.empty() && !m_outputKeys.empty()) {
      return m_outputKeys.front();
    }
    return widget.outputName;
  }

  bool SharedLayoutResolver::showsShared(std::string_view outputKey) const {
    const auto it = m_outputLayouts.find(std::string(outputKey));
    const DesktopWidgetOutputLayout layout = it != m_outputLayouts.end() ? it->second : DesktopWidgetOutputLayout::Auto;
    switch (layout) {
    case DesktopWidgetOutputLayout::Shared:
      return true;
    case DesktopWidgetOutputLayout::Own:
      return false;
    case DesktopWidgetOutputLayout::Auto:
      break;
    }
    // Every output has a login box, so login boxes do not count as the output's own layout.
    return std::ranges::none_of(m_widgets, [&](const DesktopWidgetState& widget) {
      return widget.enabled
          && !lockscreen_login_box::isLoginBoxWidget(widget)
          && !isSharedWidget(widget)
          && boundOutput(widget) == outputKey;
    });
  }

  std::vector<std::string> SharedLayoutResolver::outputsFor(const DesktopWidgetState& widget) const {
    std::vector<std::string> outputs;
    for (const auto& outputKey : m_outputKeys) {
      if (isSharedWidget(widget) ? showsShared(outputKey) : boundOutput(widget) == outputKey) {
        outputs.push_back(outputKey);
      }
    }
    return outputs;
  }

  std::vector<WidgetPlacement> resolveWidgetPlacements(
      const WaylandConnection& wayland, const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts
  ) {
    std::vector<const WaylandOutput*> outputs;
    std::vector<std::string> outputKeys;
    for (const auto& output : wayland.outputs()) {
      if (!output.done || output.output == nullptr || !output.hasUsableGeometry()) {
        continue;
      }
      outputs.push_back(&output);
      outputKeys.push_back(outputKey(output));
    }

    const SharedLayoutResolver resolver(widgets, outputLayouts, outputKeys);
    std::vector<WidgetPlacement> placements;
    for (const auto& widget : widgets) {
      if (!widget.enabled) {
        continue;
      }
      for (auto& key : resolver.outputsFor(widget)) {
        const auto index = static_cast<std::size_t>(std::ranges::find(outputKeys, key) - outputKeys.begin());
        placements.push_back(WidgetPlacement{.state = &widget, .output = outputs[index], .outputKey = std::move(key)});
      }
    }
    return placements;
  }

} // namespace desktop_widgets
