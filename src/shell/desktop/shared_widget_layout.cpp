#include "shell/desktop/shared_widget_layout.h"

#include "shell/desktop/desktop_widget_layout.h"
#include "shell/lockscreen/lockscreen_login_box.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace desktop_widgets {

  namespace {

    std::vector<const WaylandOutput*> usableOutputs(const WaylandConnection& wayland) {
      std::vector<const WaylandOutput*> outputs;
      for (const auto& output : wayland.outputs()) {
        if (output.done && output.output != nullptr && output.hasUsableGeometry()) {
          outputs.push_back(&output);
        }
      }
      return outputs;
    }

  } // namespace

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
      bool shown = false;
      if (lockscreen_login_box::isLoginBoxWidget(widget)) {
        const DesktopWidgetState* loginBox = loginBoxFor(outputKey);
        shown = loginBox != nullptr && loginBox->id == widget.id;
      } else if (isSharedWidget(widget)) {
        shown = showsShared(outputKey);
      } else {
        shown = boundOutput(widget) == outputKey;
      }
      if (shown) {
        outputs.push_back(outputKey);
      }
    }
    return outputs;
  }

  const DesktopWidgetState* SharedLayoutResolver::loginBoxFor(std::string_view outputKey) const {
    if (showsShared(outputKey)) {
      const auto shared = std::ranges::find_if(m_widgets, [](const DesktopWidgetState& widget) {
        return lockscreen_login_box::isLoginBoxWidget(widget) && isSharedWidget(widget);
      });
      if (shared != m_widgets.end()) {
        return &*shared;
      }
    }
    const auto bound = std::ranges::find_if(m_widgets, [&](const DesktopWidgetState& widget) {
      return lockscreen_login_box::isLoginBoxWidget(widget) && widget.outputName == outputKey;
    });
    return bound != m_widgets.end() ? &*bound : nullptr;
  }

  bool SharedLayoutResolver::showsLoginBox(std::string_view outputKey) const {
    const auto shows = [this](std::string_view key) {
      const DesktopWidgetState* loginBox = loginBoxFor(key);
      return loginBox == nullptr || loginBox->enabled;
    };
    return shows(outputKey) || std::ranges::none_of(m_outputKeys, shows);
  }

  std::vector<std::string> usableOutputKeys(const WaylandConnection& wayland) {
    std::vector<std::string> keys;
    for (const auto* output : usableOutputs(wayland)) {
      keys.push_back(outputKey(*output));
    }
    return keys;
  }

  std::vector<WidgetPlacement> resolveWidgetPlacements(
      const WaylandConnection& wayland, const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts
  ) {
    const std::vector<const WaylandOutput*> outputs = usableOutputs(wayland);
    const std::vector<std::string> outputKeys = usableOutputKeys(wayland);

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
