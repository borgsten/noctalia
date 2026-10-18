#include "shell/desktop/shared_widget_layout.h"

#include "shell/desktop/desktop_widget_layout.h"
#include "shell/lockscreen/lockscreen_login_box.h"

#include <algorithm>
#include <utility>

namespace desktop_widgets {

  namespace {

    bool isUsable(const WaylandOutput& output) {
      return output.done && output.output != nullptr && output.hasUsableGeometry();
    }

  } // namespace

  SharedLayoutResolver::SharedLayoutResolver(
      const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts,
      std::span<const WaylandOutput> outputs
  )
      : m_widgets(widgets), m_outputLayouts(outputLayouts), m_outputs(outputs) {
    for (const auto& output : outputs) {
      if (isUsable(output)) {
        m_outputKeys.push_back(outputKey(output));
      }
    }
  }

  bool SharedLayoutResolver::isBoundTo(const DesktopWidgetState& widget, std::string_view key) const {
    const WaylandOutput* output = selectOutput(m_outputs, widget.outputName).output;
    return output != nullptr && outputKey(*output) == key;
  }

  std::string SharedLayoutResolver::layoutKeyFor(std::string_view key) const {
    std::string outputName(key);
    if (m_outputLayouts.contains(outputName)) {
      return outputName;
    }
    // The map has no order, so take the smallest key to pick the same selector every time.
    const std::string* selected = nullptr;
    for (const auto& [selector, layout] : m_outputLayouts) {
      (void)layout;
      const WaylandOutput* output = selector.empty() ? nullptr : selectOutput(m_outputs, selector).output;
      if (output != nullptr && outputKey(*output) == key && (selected == nullptr || selector < *selected)) {
        selected = &selector;
      }
    }
    return selected != nullptr ? *selected : outputName;
  }

  bool SharedLayoutResolver::showsShared(std::string_view key) const {
    const auto it = m_outputLayouts.find(layoutKeyFor(key));
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
          && isBoundTo(widget, key);
    });
  }

  bool SharedLayoutResolver::bindingHidesShared(std::string_view key) const {
    const auto it = m_outputLayouts.find(layoutKeyFor(key));
    return (it == m_outputLayouts.end() || it->second == DesktopWidgetOutputLayout::Auto)
        && std::ranges::any_of(m_widgets, isSharedWidget)
        && showsShared(key);
  }

  WidgetDrop SharedLayoutResolver::dropOn(const DesktopWidgetState& widget, std::string_view key) const {
    const bool stays = isSharedWidget(widget) ? showsShared(key) : isBoundTo(widget, key);
    if (stays) {
      return {.outputName = widget.outputName};
    }
    return {
        .outputName = std::string(key),
        .pinsShared = !lockscreen_login_box::isLoginBoxWidget(widget) && bindingHidesShared(key),
    };
  }

  std::vector<std::string> SharedLayoutResolver::outputsFor(const DesktopWidgetState& widget) const {
    std::vector<std::string> outputs;
    for (const auto& key : m_outputKeys) {
      bool shown = false;
      if (lockscreen_login_box::isLoginBoxWidget(widget)) {
        const DesktopWidgetState* loginBox = loginBoxFor(key);
        shown = loginBox != nullptr && loginBox->id == widget.id;
      } else if (isSharedWidget(widget)) {
        shown = showsShared(key);
      } else {
        shown = isBoundTo(widget, key);
      }
      if (shown) {
        outputs.push_back(key);
      }
    }
    return outputs;
  }

  const DesktopWidgetState* SharedLayoutResolver::loginBoxFor(std::string_view key) const {
    if (showsShared(key)) {
      const auto shared = std::ranges::find_if(m_widgets, [](const DesktopWidgetState& widget) {
        return lockscreen_login_box::isLoginBoxWidget(widget) && isSharedWidget(widget);
      });
      if (shared != m_widgets.end()) {
        return &*shared;
      }
    }
    return lockscreen_login_box::findForOutput(m_widgets, m_outputs, key);
  }

  bool SharedLayoutResolver::showsLoginBox(std::string_view outputKey) const {
    const auto shows = [this](std::string_view key) {
      const DesktopWidgetState* loginBox = loginBoxFor(key);
      return loginBox == nullptr || loginBox->enabled;
    };
    return shows(outputKey) || std::ranges::none_of(m_outputKeys, shows);
  }

  std::vector<WidgetPlacement> resolveWidgetPlacements(
      std::span<const WaylandOutput> outputs, const std::vector<DesktopWidgetState>& widgets,
      const std::unordered_map<std::string, DesktopWidgetOutputLayout>& outputLayouts
  ) {
    const SharedLayoutResolver resolver(widgets, outputLayouts, outputs);
    std::vector<WidgetPlacement> placements;
    for (const auto& widget : widgets) {
      if (!widget.enabled) {
        continue;
      }
      for (auto& key : resolver.outputsFor(widget)) {
        const auto output = std::ranges::find_if(outputs, [&key](const WaylandOutput& candidate) {
          return isUsable(candidate) && outputKey(candidate) == key;
        });
        placements.push_back(WidgetPlacement{.state = &widget, .output = &*output, .outputKey = std::move(key)});
      }
    }
    return placements;
  }

} // namespace desktop_widgets
