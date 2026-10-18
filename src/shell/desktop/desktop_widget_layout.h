#pragma once

#include "config/config_types.h"
#include "shell/desktop/widget_transform.h"
#include "wayland/wayland_connection.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace desktop_widgets {

  inline float widgetContentScale(float baseUiScale) { return std::max(0.01F, baseUiScale); }

  inline void widgetNodeScale(const DesktopWidgetState& state, float& outScaleX, float& outScaleY) {
    outScaleX = state.flipX ? -1.0F : 1.0F;
    outScaleY = state.flipY ? -1.0F : 1.0F;
  }

  inline std::string outputKey(const WaylandOutput& output) {
    if (!output.connectorName.empty()) {
      return output.connectorName;
    }
    return std::to_string(output.name);
  }

  inline const WaylandOutput* findOutputByKey(const WaylandConnection& wayland, const std::string& key) {
    if (key.empty()) {
      return nullptr;
    }
    for (const auto& output : wayland.outputs()) {
      if (!output.done || output.output == nullptr || !output.hasUsableGeometry()) {
        continue;
      }
      if (outputKey(output) == key) {
        return &output;
      }
    }
    return nullptr;
  }

  inline const WaylandOutput*
  resolveEffectiveOutput(const WaylandConnection& wayland, const std::string& requestedOutput) {
    const auto& outputs = wayland.outputs();
    const WaylandOutput* primary = nullptr;
    for (const auto& output : outputs) {
      if (!output.done || output.output == nullptr || !output.hasUsableGeometry()) {
        continue;
      }
      if (primary == nullptr) {
        primary = &output;
      }
      if (!requestedOutput.empty() && outputKey(output) == requestedOutput) {
        return &output;
      }
    }
    if (!requestedOutput.empty()) {
      return nullptr;
    }
    return primary;
  }

  inline const WaylandOutput* resolveStateOutput(const WaylandConnection& wayland, const DesktopWidgetState& state) {
    if (state.outputName.empty()) {
      return resolveEffectiveOutput(wayland, state.outputName);
    }
    return findOutputByKey(wayland, state.outputName);
  }

  inline float outputLogicalWidth(const WaylandOutput& output) {
    return static_cast<float>(std::max(1, output.effectiveLogicalWidth()));
  }

  inline float outputLogicalHeight(const WaylandOutput& output) {
    return static_cast<float>(std::max(1, output.effectiveLogicalHeight()));
  }

  inline bool isSharedWidget(const DesktopWidgetState& state) { return state.outputName == kSharedWidgetOutput; }

  inline bool isRelativeWidget(const DesktopWidgetState& state) {
    return state.positionMode == DesktopWidgetPositionMode::Relative;
  }

  class PlacementMapper {
  public:
    [[nodiscard]] bool
    remapForOutputChange(const WaylandConnection& wayland, std::vector<DesktopWidgetState>& widgets) const {
      bool changed = false;
      for (auto& widget : widgets) {
        // Relative positions already follow the output size. Shared widgets have no single output.
        if (isRelativeWidget(widget) || isSharedWidget(widget)) {
          continue;
        }
        const WaylandOutput* output = resolveStateOutput(wayland, widget);
        if (output == nullptr) {
          continue;
        }

        const float width = outputLogicalWidth(*output);
        const float height = outputLogicalHeight(*output);
        if (widget.placementWidth <= 0.0F || widget.placementHeight <= 0.0F) {
          widget.placementWidth = width;
          widget.placementHeight = height;
          changed = true;
          continue;
        }

        if (widget.placementWidth == width && widget.placementHeight == height) {
          continue;
        }

        widget.cx *= width / widget.placementWidth;
        widget.cy *= height / widget.placementHeight;
        widget.placementWidth = width;
        widget.placementHeight = height;
        changed = true;
      }
      return changed;
    }

    void rebaseForCurrentOutputs(const WaylandConnection& wayland, std::vector<DesktopWidgetState>& widgets) const {
      for (auto& widget : widgets) {
        if (isRelativeWidget(widget) || isSharedWidget(widget)) {
          continue;
        }
        const WaylandOutput* output = resolveStateOutput(wayland, widget);
        if (output == nullptr) {
          continue;
        }
        widget.placementWidth = outputLogicalWidth(*output);
        widget.placementHeight = outputLogicalHeight(*output);
      }
    }
  };

  // Offset of the anchor point from the widget's center, as a fraction of its rotated AABB.
  struct AnchorFactors {
    float x = 0.0F;
    float y = 0.0F;
  };

  inline AnchorFactors anchorFactors(DesktopWidgetAnchor anchor) {
    switch (anchor) {
    case DesktopWidgetAnchor::TopLeft:
      return {-0.5F, -0.5F};
    case DesktopWidgetAnchor::Top:
      return {0.0F, -0.5F};
    case DesktopWidgetAnchor::TopRight:
      return {0.5F, -0.5F};
    case DesktopWidgetAnchor::Left:
      return {-0.5F, 0.0F};
    case DesktopWidgetAnchor::Center:
      return {0.0F, 0.0F};
    case DesktopWidgetAnchor::Right:
      return {0.5F, 0.0F};
    case DesktopWidgetAnchor::BottomLeft:
      return {-0.5F, 0.5F};
    case DesktopWidgetAnchor::Bottom:
      return {0.0F, 0.5F};
    case DesktopWidgetAnchor::BottomRight:
      return {0.5F, 0.5F};
    }
    return {};
  }

  // The widget's center in logical px on an output of the given size. The position places the anchor
  // point of the rotated AABB: in logical px for absolute widgets, as a fraction of the output for
  // relative ones.
  inline WidgetTransformClampResult resolveCenter(
      const DesktopWidgetState& state, float outputWidth, float outputHeight, float intrinsicWidth,
      float intrinsicHeight
  ) {
    const bool relative = isRelativeWidget(state);
    const float anchorX = relative ? state.cx * outputWidth : state.cx;
    const float anchorY = relative ? state.cy * outputHeight : state.cy;
    if (state.anchor == DesktopWidgetAnchor::Center) {
      return {anchorX, anchorY};
    }
    const WidgetTransformBounds bounds =
        computeWidgetTransformBounds(0.0F, 0.0F, intrinsicWidth, intrinsicHeight, 1.0F, state.rotationRad);
    const AnchorFactors factors = anchorFactors(state.anchor);
    return {anchorX - factors.x * bounds.aabbWidth, anchorY - factors.y * bounds.aabbHeight};
  }

  // Inverse of resolveCenter: stores a center in logical px into the state's own position form.
  inline void storeCenter(
      DesktopWidgetState& state, float outputWidth, float outputHeight, float intrinsicWidth, float intrinsicHeight,
      float centerX, float centerY
  ) {
    float anchorX = centerX;
    float anchorY = centerY;
    if (state.anchor != DesktopWidgetAnchor::Center) {
      const WidgetTransformBounds bounds =
          computeWidgetTransformBounds(0.0F, 0.0F, intrinsicWidth, intrinsicHeight, 1.0F, state.rotationRad);
      const AnchorFactors factors = anchorFactors(state.anchor);
      anchorX += factors.x * bounds.aabbWidth;
      anchorY += factors.y * bounds.aabbHeight;
    }
    if (!isRelativeWidget(state)) {
      state.cx = anchorX;
      state.cy = anchorY;
      return;
    }
    state.cx = anchorX / std::max(1.0F, outputWidth);
    state.cy = anchorY / std::max(1.0F, outputHeight);
  }

  // Single source of truth for desktop widget coordinate clamping. Edit mode, default mode, and
  // snapshot normalization all route through here so the visibility rule stays identical. Returns the
  // center to draw the widget at on `output`: the resolved center constrained so that at least
  // kDesktopWidgetMinVisibleFraction of the widget's rotated AABB remains on screen. The caller
  // passes the widget's current box size as intrinsicWidth/intrinsicHeight, so no extra scale
  // multiplier is applied here.
  inline WidgetTransformClampResult clampCenterToOutput(
      const DesktopWidgetState& state, const WaylandOutput& output, float intrinsicWidth, float intrinsicHeight
  ) {
    const float outputWidth = outputLogicalWidth(output);
    const float outputHeight = outputLogicalHeight(output);
    const WidgetTransformClampResult center =
        resolveCenter(state, outputWidth, outputHeight, intrinsicWidth, intrinsicHeight);
    return clampWidgetCenterToOutput(
        center.cx, center.cy, intrinsicWidth, intrinsicHeight, 1.0F, state.rotationRad, outputWidth, outputHeight,
        kDesktopWidgetMinVisibleFraction
    );
  }

  // Resolves the widget's effective output and writes the clamped center back into the state, in the
  // state's own position form.
  inline const WaylandOutput* clampStateToOutput(
      const WaylandConnection& wayland, DesktopWidgetState& state, float intrinsicWidth, float intrinsicHeight
  ) {
    const WaylandOutput* output = resolveStateOutput(wayland, state);
    if (output == nullptr) {
      return nullptr;
    }
    const WidgetTransformClampResult clamped = clampCenterToOutput(state, *output, intrinsicWidth, intrinsicHeight);
    storeCenter(
        state, outputLogicalWidth(*output), outputLogicalHeight(*output), intrinsicWidth, intrinsicHeight, clamped.cx,
        clamped.cy
    );
    return output;
  }

  // One widget instance a host should show: the widget's config and the output it is shown on.
  struct WidgetPlacement {
    const DesktopWidgetState* state = nullptr;
    const WaylandOutput* output = nullptr;
    std::string outputKey;
  };

  inline const WidgetPlacement*
  findPlacement(const std::vector<WidgetPlacement>& placements, std::string_view id, std::string_view outputKey) {
    for (const auto& placement : placements) {
      if (placement.state->id == id && placement.outputKey == outputKey) {
        return &placement;
      }
    }
    return nullptr;
  }

  // A change the live widget cannot pick up in place: the instance has to be recreated.
  inline bool widgetDefinitionChanged(const DesktopWidgetState& current, const DesktopWidgetState& next) {
    return current.type != next.type || current.settings != next.settings;
  }

} // namespace desktop_widgets
