#include "shell/lockscreen/lockscreen_widgets_host.h"

#include "config/config_service.h"
#include "core/log.h"
#include "render/render_context.h"
#include "render/scene/node.h"
#include "scripting/plugin_id.h"
#include "shell/desktop/desktop_widget_layout.h"
#include "shell/desktop/shared_widget_layout.h"
#include "shell/lockscreen/lock_screen.h"
#include "shell/lockscreen/lock_surface.h"
#include "shell/lockscreen/lockscreen_login_box.h"
#include "time/time_format.h"
#include "ui/builders.h"
#include "wayland/wayland_connection.h"

#include <algorithm>
#include <string>
#include <wayland-client-protocol.h>

namespace {

  constexpr Logger kLog("lockscreen");

} // namespace

void LockscreenWidgetsHost::initialize(const DesktopWidgetServices& services) {
  m_wayland = &services.wayland;
  m_config = services.config;
  m_renderContext = services.renderContext;
  m_factory = std::make_unique<DesktopWidgetFactory>(services.runtime);
}

void LockscreenWidgetsHost::show(const LockscreenWidgetsSnapshot& snapshot, LockScreen& lockScreen) {
  m_snapshot = snapshot;
  m_visible = true;
  syncSurfaces(lockScreen);
}

void LockscreenWidgetsHost::hide() {
  for (auto& instance : m_instances) {
    if (instance->widget != nullptr) {
      instance->widget->setFrameTickRequestCallback(nullptr);
      instance->widget->setUpdateCallback(nullptr);
      instance->widget->setLayoutCallback(nullptr);
      instance->widget->setRedrawCallback(nullptr);
    }
    detachFromSurface(*instance);
  }
  m_visible = false;
  m_instances.clear();
}

void LockscreenWidgetsHost::rebuild(const LockscreenWidgetsSnapshot& snapshot, LockScreen& lockScreen) {
  m_snapshot = snapshot;
  if (!m_visible) {
    return;
  }
  syncSurfaces(lockScreen);
}

void LockscreenWidgetsHost::reloadPluginWidgets(LockScreen& lockScreen) {
  if (!m_visible) {
    return;
  }
  // Match on the id syntax, not the registry: a plugin disabled by this reload has
  // already left the registry, and its instances must go down with it.
  const auto before = m_instances.size();
  for (auto& instance : m_instances) {
    if (scripting::isValidPluginEntryId(instance->state.type)) {
      detachFromSurface(*instance);
    }
  }
  eraseDetachedInstances();
  if (m_instances.size() == before) {
    return;
  }
  syncSurfaces(lockScreen);
}

void LockscreenWidgetsHost::onOutputChange(LockScreen& lockScreen) {
  if (!m_visible) {
    return;
  }
  syncSurfaces(lockScreen);
}

void LockscreenWidgetsHost::onSecondTick() {
  if (!m_visible) {
    return;
  }

  const bool minuteBoundary = formatLocalTime("{:%S}") == "00";
  for (auto& instance : m_instances) {
    if (instance->surface == nullptr || instance->widget == nullptr) {
      continue;
    }
    if (instance->widget->wantsSecondTicks()) {
      instance->surface->requestUpdateOnly();
    } else if (minuteBoundary) {
      instance->surface->requestUpdate();
    }
  }
}

LockscreenWidgetsHost::WidgetInstance*
LockscreenWidgetsHost::findInstance(const std::string& id, const std::string& outputKey) {
  for (auto& instance : m_instances) {
    if (instance->state.id == id && instance->effectiveOutputName == outputKey) {
      return instance.get();
    }
  }
  return nullptr;
}

LockSurface* LockscreenWidgetsHost::findSurfaceForOutput(LockScreen& lockScreen, const std::string& outputKey) const {
  LockSurface* found = nullptr;
  lockScreen.forEachSurface([&](LockSurface& surface) {
    if (found != nullptr || m_wayland == nullptr) {
      return;
    }
    const WaylandOutput* output = m_wayland->findOutputByWl(surface.output());
    if (output == nullptr) {
      return;
    }
    if (desktop_widgets::outputKey(*output) == outputKey) {
      found = &surface;
    }
  });
  return found;
}

void LockscreenWidgetsHost::syncSurfaces(LockScreen& lockScreen) {
  if (!m_visible || m_wayland == nullptr || m_renderContext == nullptr || m_factory == nullptr) {
    return;
  }

  const auto placements =
      desktop_widgets::resolveWidgetPlacements(*m_wayland, m_snapshot.widgets, m_snapshot.outputLayouts);

  // Detach first and erase afterwards: detachFromSurface() walks m_instances, which must not be
  // mid-erase at that point.
  for (auto& instance : m_instances) {
    const auto* placement =
        desktop_widgets::findPlacement(placements, instance->state.id, instance->effectiveOutputName);
    const bool keep = placement != nullptr
        && !desktop_widgets::widgetDefinitionChanged(instance->state, *placement->state)
        && instance->surface == findSurfaceForOutput(lockScreen, instance->effectiveOutputName);
    if (!keep) {
      detachFromSurface(*instance);
    }
  }
  eraseDetachedInstances();

  for (const auto& placement : placements) {
    if (lockscreen_login_box::isLoginBoxWidget(*placement.state)) {
      continue;
    }

    LockSurface* surface = findSurfaceForOutput(lockScreen, placement.outputKey);
    if (surface == nullptr) {
      continue;
    }

    WidgetInstance* existing = findInstance(placement.state->id, placement.outputKey);
    if (existing == nullptr) {
      createInstance(*placement.state, *surface, *placement.output);
      continue;
    }

    if (!(existing->state == *placement.state)) {
      existing->state = *placement.state;
      existing->surface->requestLayout();
    }
  }

  lockScreen.forEachSurface([&](LockSurface& surface) { surface.setWidgetsHost(this); });
}

void LockscreenWidgetsHost::createInstance(
    const DesktopWidgetState& state, LockSurface& surface, const WaylandOutput& output
) {
  if (m_factory == nullptr || m_renderContext == nullptr) {
    return;
  }

  const float baseUiScale = m_config != nullptr ? m_config->config().accessibility.uiScale : 1.0F;
  auto widget = m_factory->create(state.type, state.settings, desktop_widgets::widgetContentScale(baseUiScale));
  if (widget == nullptr) {
    return;
  }

  widget->create();
  widget->setBox(state.boxWidth, state.boxHeight);

  if (surface.renderTarget().isReady()) {
    m_renderContext->makeCurrent(surface.renderTarget());
    Renderer& renderer = surface.renderTarget().renderer();
    widget->update(renderer);
    widget->layout(renderer);
  }

  const float intrinsicWidth = std::max(1.0F, widget->intrinsicWidth());
  const float intrinsicHeight = std::max(1.0F, widget->intrinsicHeight());

  const WidgetTransformClampResult center =
      desktop_widgets::clampCenterToOutput(state, output, intrinsicWidth, intrinsicHeight);

  auto instance = std::make_unique<WidgetInstance>();
  instance->state = state;
  instance->effectiveOutputName = desktop_widgets::outputKey(output);
  instance->surface = &surface;
  instance->resolvedCx = center.cx;
  instance->resolvedCy = center.cy;
  instance->widget = std::move(widget);
  instance->intrinsicWidth = intrinsicWidth;
  instance->intrinsicHeight = intrinsicHeight;

  auto* rawInstance = instance.get();
  instance->widget->setAnimationManager(&instance->animations);
  instance->widget->setUpdateCallback([rawInstance]() {
    if (rawInstance->surface != nullptr) {
      rawInstance->surface->requestUpdateOnly();
    }
  });
  instance->widget->setLayoutCallback([rawInstance]() {
    if (rawInstance->surface != nullptr) {
      rawInstance->surface->requestUpdate();
    }
  });
  instance->widget->setRedrawCallback([rawInstance]() {
    if (rawInstance->surface != nullptr) {
      rawInstance->surface->requestRedraw();
    }
  });
  instance->widget->setFrameTickRequestCallback([this, rawInstance]() {
    if (rawInstance->surface != nullptr) {
      syncSurfaceFrameTick(rawInstance->surface);
      rawInstance->surface->requestFrameTick();
    }
  });

  attachToSurface(*instance);
  m_instances.push_back(std::move(instance));
}

void LockscreenWidgetsHost::attachToSurface(WidgetInstance& instance) {
  if (instance.surface == nullptr || instance.widget == nullptr) {
    return;
  }

  Node* layer = instance.surface->widgetLayer();
  if (layer == nullptr) {
    return;
  }

  auto transformNode = ui::node({});
  transformNode->setAnimationManager(&instance.animations);
  instance.transformNode = layer->addChild(std::move(transformNode));
  instance.transformNode->addChild(instance.widget->releaseRoot());

  syncSurfaceFrameTick(instance.surface);
  instance.surface->requestLayout();
}

void LockscreenWidgetsHost::syncSurfaceFrameTick(LockSurface* surfacePtr) {
  if (surfacePtr == nullptr) {
    return;
  }

  const bool hasWidgets = std::ranges::any_of(m_instances, [&](const auto& instance) {
    return instance->surface == surfacePtr && instance->widget != nullptr;
  });
  if (!hasWidgets) {
    surfacePtr->setFrameTickCallback(nullptr);
    return;
  }

  auto* host = this;
  surfacePtr->setFrameTickCallback([host, surfacePtr](float deltaMs) {
    if (host->m_renderContext == nullptr) {
      return;
    }
    host->m_renderContext->makeCurrent(surfacePtr->renderTarget());
    Renderer& renderer = surfacePtr->renderTarget().renderer();

    bool needsRedraw = false;
    for (auto& instance : host->m_instances) {
      if (instance->surface != surfacePtr) {
        continue;
      }
      instance->animations.tick(deltaMs);
      needsRedraw = needsRedraw || instance->animations.hasActive();
    }

    bool needsContinuousRedraw = needsRedraw;
    for (auto& instance : host->m_instances) {
      if (instance->surface != surfacePtr || instance->widget == nullptr || !instance->widget->needsFrameTick()) {
        continue;
      }
      instance->widget->onFrameTick(deltaMs, renderer);
      needsContinuousRedraw = true;
    }

    if (needsContinuousRedraw) {
      surfacePtr->requestRedraw();
    }
  });
}

void LockscreenWidgetsHost::detachFromSurface(WidgetInstance& instance) {
  LockSurface* surface = instance.surface;

  if (instance.transformNode != nullptr && surface != nullptr) {
    if (Node* layer = surface->widgetLayer(); layer != nullptr) {
      (void)layer->removeChild(instance.transformNode);
    }
    instance.transformNode = nullptr;
  }
  instance.surface = nullptr;

  if (surface != nullptr) {
    syncSurfaceFrameTick(surface);
  }
}

void LockscreenWidgetsHost::eraseDetachedInstances() {
  std::erase_if(m_instances, [](const auto& instance) { return instance->surface == nullptr; });
}

void LockscreenWidgetsHost::prepareFrame(LockSurface& surface, bool needsUpdate, bool needsLayout) {
  if (!m_visible || m_renderContext == nullptr) {
    return;
  }

  m_renderContext->makeCurrent(surface.renderTarget());
  Renderer& renderer = surface.renderTarget().renderer();

  const float baseUiScale = m_config != nullptr ? m_config->config().accessibility.uiScale : 1.0F;
  const auto surfaceW = static_cast<float>(surface.width());
  const auto surfaceH = static_cast<float>(surface.height());

  const WaylandOutput* output = m_wayland != nullptr ? m_wayland->findOutputByWl(surface.output()) : nullptr;

  Node* layer = surface.widgetLayer();
  if (layer != nullptr) {
    layer->setPosition(0.0F, 0.0F);
    layer->setSize(surfaceW, surfaceH);
  }

  for (auto& instance : m_instances) {
    if (instance->surface != &surface || instance->widget == nullptr) {
      continue;
    }

    instance->widget->setContentScale(desktop_widgets::widgetContentScale(baseUiScale));
    instance->widget->setBox(instance->state.boxWidth, instance->state.boxHeight);

    if (needsUpdate) {
      instance->widget->update(renderer);
    }
    if (needsLayout) {
      instance->widget->layout(renderer);
      instance->intrinsicWidth = std::max(1.0F, instance->widget->intrinsicWidth());
      instance->intrinsicHeight = std::max(1.0F, instance->widget->intrinsicHeight());
    }

    if (output != nullptr) {
      const WidgetTransformClampResult center = desktop_widgets::clampCenterToOutput(
          instance->state, *output, instance->intrinsicWidth, instance->intrinsicHeight
      );
      instance->resolvedCx = center.cx;
      instance->resolvedCy = center.cy;
    }

    if (instance->transformNode == nullptr) {
      continue;
    }

    instance->transformNode->setFrameSize(instance->intrinsicWidth, instance->intrinsicHeight);
    instance->transformNode->setPosition(
        instance->resolvedCx - instance->intrinsicWidth * 0.5F, instance->resolvedCy - instance->intrinsicHeight * 0.5F
    );
    instance->transformNode->setRotation(instance->state.rotationRad);
    float flipScaleX = 1.0F;
    float flipScaleY = 1.0F;
    desktop_widgets::widgetNodeScale(instance->state, flipScaleX, flipScaleY);
    instance->transformNode->setScale(flipScaleX, flipScaleY);
  }

  // Mirror the desktop host: widgets like sysmon drive updates from frame ticks.
  const bool needsFrameTick = std::ranges::any_of(m_instances, [&surface](const auto& instance) {
    return instance != nullptr
        && instance->surface == &surface
        && instance->widget != nullptr
        && instance->widget->needsFrameTick();
  });
  if (needsFrameTick) {
    surface.requestFrameTick();
  }
}
