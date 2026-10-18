// Relative positions, anchors and the shared widget layout: the position math, the rule for which
// outputs show which widgets, and the config round trip of the new keys.

#include "config/config_service.h"
#include "config/config_types.h"
#include "config/config_validate.h"
#include "core/toml.h"
#include "shell/desktop/desktop_widget_layout.h"
#include "shell/desktop/shared_widget_layout.h"
#include "shell/lockscreen/lockscreen_login_box.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <string_view>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

  int g_failures = 0;

  void expect(bool condition, std::string_view message) {
    if (!condition) {
      std::println(stderr, "desktop_widget_placement: FAIL: {}", message);
      ++g_failures;
    }
  }

  bool near(float a, float b) { return std::abs(a - b) < 1e-3F; }

  void writeFile(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << content;
  }

  // Selection only reads output metadata; the proxy is never dereferenced.
  WaylandOutput makeOutput(std::string connector, std::string model = "Display", std::string serial = "") {
    static char outputToken;
    WaylandOutput output;
    output.connectorName = std::move(connector);
    output.description = "Acme " + model;
    output.make = "Acme";
    output.model = std::move(model);
    output.serialNumber = std::move(serial);
    output.output = reinterpret_cast<wl_output*>(&outputToken);
    output.done = true;
    output.logicalWidth = 1920;
    output.logicalHeight = 1080;
    return output;
  }

  DesktopWidgetState widget(std::string id, std::string output, std::string type = "clock") {
    DesktopWidgetState state;
    state.id = std::move(id);
    state.type = std::move(type);
    state.outputName = std::move(output);
    return state;
  }

  void testPositionMath() {
    using desktop_widgets::resolveCenter;
    using desktop_widgets::storeCenter;
    constexpr float kOutW = 1920.0F;
    constexpr float kOutH = 1080.0F;
    constexpr float kW = 200.0F;
    constexpr float kH = 80.0F;

    DesktopWidgetState absolute;
    absolute.cx = 300.0F;
    absolute.cy = 400.0F;
    const auto passthrough = resolveCenter(absolute, kOutW, kOutH, kW, kH);
    expect(passthrough.cx == 300.0F && passthrough.cy == 400.0F, "absolute center positions pass through");

    DesktopWidgetState absoluteTopRight = absolute;
    absoluteTopRight.anchor = DesktopWidgetAnchor::TopRight;
    const auto anchored = resolveCenter(absoluteTopRight, kOutW, kOutH, kW, kH);
    expect(
        near(anchored.cx, 300.0F - kW * 0.5F) && near(anchored.cy, 400.0F + kH * 0.5F),
        "an absolute position places the anchor point"
    );

    DesktopWidgetState topRight;
    topRight.positionMode = DesktopWidgetPositionMode::Relative;
    topRight.anchor = DesktopWidgetAnchor::TopRight;
    topRight.cx = 1.0F;
    topRight.cy = 0.0F;
    const auto flush = resolveCenter(topRight, kOutW, kOutH, kW, kH);
    expect(near(flush.cx, kOutW - kW * 0.5F) && near(flush.cy, kH * 0.5F), "top_right at (1, 0) is flush");

    constexpr std::array kAnchors = {
        DesktopWidgetAnchor::TopLeft,    DesktopWidgetAnchor::Top,    DesktopWidgetAnchor::TopRight,
        DesktopWidgetAnchor::Left,       DesktopWidgetAnchor::Center, DesktopWidgetAnchor::Right,
        DesktopWidgetAnchor::BottomLeft, DesktopWidgetAnchor::Bottom, DesktopWidgetAnchor::BottomRight,
    };
    for (const auto mode : {DesktopWidgetPositionMode::Absolute, DesktopWidgetPositionMode::Relative}) {
      for (const auto anchor : kAnchors) {
        for (const float rotation : {0.0F, 0.5236F, 1.5708F}) {
          DesktopWidgetState state;
          state.positionMode = mode;
          state.anchor = anchor;
          state.rotationRad = rotation;
          state.cx = mode == DesktopWidgetPositionMode::Relative ? 0.3F : 576.0F;
          state.cy = mode == DesktopWidgetPositionMode::Relative ? 0.7F : 756.0F;
          const auto center = resolveCenter(state, kOutW, kOutH, kW, kH);
          DesktopWidgetState stored = state;
          stored.cx = 0.0F;
          stored.cy = 0.0F;
          storeCenter(stored, kOutW, kOutH, kW, kH, center.cx, center.cy);
          const float tolerance = mode == DesktopWidgetPositionMode::Relative ? 1e-4F : 1e-2F;
          expect(
              std::abs(stored.cx - state.cx) < tolerance && std::abs(stored.cy - state.cy) < tolerance,
              "storeCenter inverts resolveCenter"
          );
        }
      }
    }
  }

  void testSharedLayout() {
    using desktop_widgets::SharedLayoutResolver;
    const std::vector<WaylandOutput> outputs = {makeOutput("DP-1"), makeOutput("DP-2"), makeOutput("HDMI-A-1")};

    // auto: an output shows the shared layout only while nothing enabled is bound to it.
    std::vector<DesktopWidgetState> widgets = {widget("shared", "*"), widget("own", "DP-1")};
    DesktopWidgetState disabled = widget("disabled", "DP-2");
    disabled.enabled = false;
    widgets.push_back(disabled);
    widgets.push_back(widget("box", "HDMI-A-1", "login_box"));
    std::unordered_map<std::string, DesktopWidgetOutputLayout> layouts;
    {
      const SharedLayoutResolver resolver(widgets, layouts, outputs);
      expect(!resolver.showsShared("DP-1"), "auto: a bound widget gives the output its own layout");
      expect(resolver.showsShared("DP-2"), "auto: a disabled bound widget does not count");
      expect(resolver.showsShared("HDMI-A-1"), "auto: a login box does not count");
      expect(resolver.outputsFor(widgets[0]) == std::vector<std::string>{"DP-2", "HDMI-A-1"}, "shared widget fans out");
      expect(resolver.outputsFor(widgets[1]) == std::vector<std::string>{"DP-1"}, "bound widget stays on its output");
    }

    layouts["DP-1"] = DesktopWidgetOutputLayout::Shared;
    layouts["DP-2"] = DesktopWidgetOutputLayout::Own;
    {
      const SharedLayoutResolver resolver(widgets, layouts, outputs);
      expect(resolver.showsShared("DP-1"), "shared: bound widgets are drawn on top of the shared layout");
      expect(!resolver.showsShared("DP-2"), "own: never shows the shared layout");
    }

    // A widget without an output belongs to the first output.
    const std::vector<DesktopWidgetState> legacy = {widget("legacy", "")};
    const std::unordered_map<std::string, DesktopWidgetOutputLayout> noLayouts;
    const SharedLayoutResolver legacyResolver(legacy, noLayouts, outputs);
    expect(!legacyResolver.showsShared("DP-1"), "a widget without an output binds to the first output");
    expect(legacyResolver.showsShared("DP-2"), "a widget without an output leaves other outputs alone");
  }

  void testSelectors() {
    using desktop_widgets::SharedLayoutResolver;
    const std::vector<WaylandOutput> outputs = {
        makeOutput("DP-1", "Landscape", "LAND1"),
        makeOutput("DP-2", "Portrait", "PORT2"),
    };
    const std::vector<DesktopWidgetState> widgets = {
        widget("shared", "*"),
        widget("portrait", "Portrait"),
        widget("serial", "PORT2"),
        widget("ambiguous", "Acme"),
    };
    std::unordered_map<std::string, DesktopWidgetOutputLayout> layouts;
    {
      const SharedLayoutResolver resolver(widgets, layouts, outputs);
      expect(resolver.outputsFor(widgets[1]) == std::vector<std::string>{"DP-2"}, "a description selector binds");
      expect(resolver.outputsFor(widgets[2]) == std::vector<std::string>{"DP-2"}, "a serial selector binds");
      expect(resolver.outputsFor(widgets[3]).empty(), "an ambiguous selector shows the widget nowhere");
      expect(!resolver.showsShared("DP-2"), "auto: a selector-bound widget gives the output its own layout");
      expect(resolver.showsShared("DP-1"), "auto: an ambiguous selector does not bind the output");
      expect(resolver.layoutKeyFor("DP-1") == "DP-1", "an output without a layout entry uses its own key");
    }

    const auto placements = desktop_widgets::resolveWidgetPlacements(outputs, widgets, layouts);
    const auto* placement = desktop_widgets::findPlacement(placements, "portrait", "DP-2");
    expect(placement != nullptr && placement->output == &outputs[1], "a placement resolves its selector to the output");

    layouts["Landscape"] = DesktopWidgetOutputLayout::Own;
    {
      const SharedLayoutResolver resolver(widgets, layouts, outputs);
      expect(!resolver.showsShared("DP-1"), "a layout table can be named by a selector");
      expect(resolver.layoutKeyFor("DP-1") == "Landscape", "the selector key applies to its output");
    }

    layouts["DP-1"] = DesktopWidgetOutputLayout::Shared;
    {
      const SharedLayoutResolver resolver(widgets, layouts, outputs);
      expect(resolver.showsShared("DP-1"), "the output's own key wins over a selector");
    }
  }

  void testDrop() {
    using desktop_widgets::SharedLayoutResolver;
    const std::vector<WaylandOutput> outputs = {
        makeOutput("DP-1", "Landscape"),
        makeOutput("DP-2", "Portrait"),
        makeOutput("DP-3", "Square"),
    };
    const std::vector<DesktopWidgetState> widgets = {
        widget("shared", "*"),
        widget("bound", "Landscape"),
        widget("box", "DP-1", "login_box"),
    };
    std::unordered_map<std::string, DesktopWidgetOutputLayout> layouts = {
        {"DP-3", DesktopWidgetOutputLayout::Own},
    };
    const SharedLayoutResolver resolver(widgets, layouts, outputs);

    const auto home = resolver.dropOn(widgets[1], "DP-1");
    expect(home.outputName == "Landscape" && !home.pinsShared, "a drop where the selector resolves keeps the selector");

    const auto bound = resolver.dropOn(widgets[1], "DP-2");
    expect(bound.outputName == "DP-2", "a drop on another output binds to it");
    expect(bound.pinsShared, "binding to an auto output showing the shared layout pins it");

    const auto own = resolver.dropOn(widgets[1], "DP-3");
    expect(own.outputName == "DP-3" && !own.pinsShared, "an own output has no shared layout to keep");

    const auto shared = resolver.dropOn(widgets[0], "DP-2");
    expect(shared.outputName == "*" && !shared.pinsShared, "a shared widget stays shared on a shared output");
    const auto sharedOnOwn = resolver.dropOn(widgets[0], "DP-3");
    expect(sharedOnOwn.outputName == "DP-3", "a shared widget binds on an output without the shared layout");

    const auto box = resolver.dropOn(widgets[2], "DP-2");
    expect(!box.pinsShared, "a login box does not hide the shared layout");
  }

  void testLoginBoxSelection() {
    using desktop_widgets::SharedLayoutResolver;
    const std::vector<WaylandOutput> outputs = {makeOutput("DP-1"), makeOutput("DP-2")};
    const std::vector<DesktopWidgetState> widgets = {
        widget("box-dp1", "DP-1", "login_box"),
        widget("box-shared", "*", "login_box"),
        widget("own", "DP-1"),
    };
    const std::unordered_map<std::string, DesktopWidgetOutputLayout> layouts;
    const SharedLayoutResolver resolver(widgets, layouts, outputs);
    const auto* dp1 = resolver.loginBoxFor("DP-1");
    const auto* dp2 = resolver.loginBoxFor("DP-2");
    expect(dp1 != nullptr && dp1->id == "box-dp1", "an output with its own layout uses its own login box");
    expect(dp2 != nullptr && dp2->id == "box-shared", "a shared output uses the shared login box");
    expect(
        resolver.outputsFor(widgets[1]) == std::vector<std::string>{"DP-2"},
        "the shared login box is only shown where it is used"
    );

    const std::vector<DesktopWidgetState> boundOnly = {widget("box-dp1", "DP-1", "login_box")};
    const SharedLayoutResolver boundResolver(boundOnly, layouts, outputs);
    expect(boundResolver.loginBoxFor("DP-2") == nullptr, "no login box when neither a shared nor a bound one exists");

    std::vector<DesktopWidgetState> disabled = {
        widget("box-dp1", "DP-1", "login_box"), widget("box-dp2", "DP-2", "login_box")
    };
    disabled[0].enabled = false;
    {
      const SharedLayoutResolver resolver(disabled, layouts, outputs);
      expect(!resolver.showsLoginBox("DP-1"), "a disabled login box is hidden while another output shows one");
      expect(resolver.showsLoginBox("DP-2"), "an enabled login box is shown");
    }
    disabled[1].enabled = false;
    {
      const SharedLayoutResolver resolver(disabled, layouts, outputs);
      expect(
          resolver.showsLoginBox("DP-1") && resolver.showsLoginBox("DP-2"),
          "every output shows its login box when none is enabled"
      );
    }
  }

  void testLoginBoxSelectors() {
    using desktop_widgets::SharedLayoutResolver;
    const std::vector<WaylandOutput> outputs = {makeOutput("DP-1", "Landscape"), makeOutput("DP-2", "Portrait")};
    const std::unordered_map<std::string, DesktopWidgetOutputLayout> noLayouts;

    std::vector<DesktopWidgetState> widgets = {
        widget("box-portrait", "Portrait", "login_box"),
        widget("own", "Portrait"),
        widget("box-shared", "*", "login_box"),
    };
    lockscreen_login_box::ensureWidgets(widgets, noLayouts, outputs);
    expect(widgets.size() == 3, "a selector-bound and a shared login box need no generated box");
    {
      const SharedLayoutResolver resolver(widgets, noLayouts, outputs);
      const auto* portrait = resolver.loginBoxFor("DP-2");
      const auto* landscape = resolver.loginBoxFor("DP-1");
      expect(portrait != nullptr && portrait->id == "box-portrait", "a selector-bound login box is found");
      expect(landscape != nullptr && landscape->id == "box-shared", "the other output uses the shared login box");
    }

    // ensureWidgets looks after a shared box too, even though it resolves to no single output.
    std::vector<DesktopWidgetState> shared = {widget("box-shared", "*", "login_box")};
    shared[0].rotationRad = 1.0F;
    shared[0].boxWidth = 5000.0F;
    shared[0].boxHeight = 10.0F;
    shared[0].enabled = false;
    lockscreen_login_box::ensureWidgets(shared, noLayouts, outputs);
    expect(shared.size() == 1, "a shared login box covers every output");
    expect(shared[0].rotationRad == 0.0F, "a shared login box is not rotated");
    expect(shared[0].boxWidth == 5000.0F, "a shared login box keeps its width for each screen to fit");
    expect(shared[0].enabled, "a shared login box in use is enabled when no other login box is");

    shared[0].boxWidth = 100.0F;
    lockscreen_login_box::ensureWidgets(shared, noLayouts, outputs);
    expect(
        shared[0].boxWidth
            == lockscreen_login_box::minPanelWidth(lockscreen_login_box::resolveStyle(shared[0].settings).layout),
        "a shared login box is at least as wide as its layout's minimum"
    );

    // A box no output uses can't stand in for the enabled one.
    std::vector<DesktopWidgetState> unused = {
        widget("box-shared", "*", "login_box"),
        widget("box-dp1", "DP-1", "login_box"),
    };
    unused[0].enabled = false;
    lockscreen_login_box::ensureWidgets(unused, noLayouts, outputs);
    expect(unused[0].enabled, "an unused per-output box does not count as the enabled login box");
  }

  constexpr std::string_view kConfig = R"(
[desktop_widgets]
widget_order = ["clock", "legacy"]

[desktop_widgets.output.DP-1]
layout = "shared"

[desktop_widgets.output.HDMI-A-1]
layout = "own"

[desktop_widgets.widget.clock]
type = "clock"
output = "*"
position = "relative"
anchor = "top_right"
cx = 0.98
cy = 0.03

[desktop_widgets.widget.legacy]
type = "clock"
output = "DP-1"
cx = 100.0
cy = 200.0
placement_width = 1920.0
placement_height = 1080.0
)";

  void testConfigRoundTrip(const std::filesystem::path& root) {
    writeFile(root / "config" / "noctalia" / "config.toml", kConfig);
    ::setenv("NOCTALIA_CONFIG_HOME", (root / "config").c_str(), 1);
    ::setenv("XDG_STATE_HOME", (root / "state").c_str(), 1);

    ConfigService config;
    const DesktopWidgetsConfig parsed = config.config().desktopWidgets;
    expect(parsed.widgets.size() == 2, "both widgets are parsed");
    if (parsed.widgets.size() == 2) {
      const auto& clock = parsed.widgets[0];
      expect(clock.positionMode == DesktopWidgetPositionMode::Relative, "position is parsed");
      expect(clock.anchor == DesktopWidgetAnchor::TopRight, "anchor is parsed");
      expect(clock.outputName == kSharedWidgetOutput, "shared output is kept");
      expect(parsed.widgets[1].positionMode == DesktopWidgetPositionMode::Absolute, "position defaults to absolute");
    }
    expect(parsed.outputLayouts.size() == 2, "output layouts are parsed");
    expect(
        parsed.outputLayouts.contains("HDMI-A-1")
            && parsed.outputLayouts.at("HDMI-A-1") == DesktopWidgetOutputLayout::Own,
        "own layout is parsed"
    );

    expect(config.setDesktopWidgetsState(parsed), "widget state is written");
    expect(config.config().desktopWidgets == parsed, "written widget state reads back unchanged");

    const toml::table written = toml::parse_file((root / "state" / "noctalia" / "settings.toml").string());
    const auto* section = written["desktop_widgets"].as_table();
    expect(section != nullptr, "settings.toml has [desktop_widgets]");
    if (section == nullptr) {
      return;
    }
    const auto clock = (*section)["widget"]["clock"];
    expect(clock["position"].value<std::string>() == "relative", "relative widget writes position");
    expect(clock["anchor"].value<std::string>() == "top_right", "non-center anchor is written");
    expect(!clock["placement_width"], "relative widget writes no placement size");
    const auto legacy = (*section)["widget"]["legacy"];
    expect(!legacy["position"] && !legacy["anchor"], "absolute center widget writes no new keys");
    expect(legacy["placement_width"].value<double>() == 1920.0, "absolute widget keeps its placement size");
    expect((*section)["output"]["DP-1"]["layout"].value<std::string>() == "shared", "output layout is written");

    // Widget tables merge key by key, so going back to a default must override the lower layer.
    if (parsed.widgets.size() == 2) {
      DesktopWidgetsConfig reset = parsed;
      reset.widgets[0].positionMode = DesktopWidgetPositionMode::Absolute;
      reset.widgets[0].anchor = DesktopWidgetAnchor::Center;
      reset.widgets[0].cx = 960.0F;
      expect(config.setDesktopWidgetsState(reset), "reset widget state is written");
      const auto& reread = config.config().desktopWidgets.widgets;
      expect(
          reread.size() == 2
              && reread[0].positionMode == DesktopWidgetPositionMode::Absolute
              && reread[0].anchor == DesktopWidgetAnchor::Center,
          "a default position and anchor override the lower config layer"
      );
      const toml::table rewritten = toml::parse_file((root / "state" / "noctalia" / "settings.toml").string());
      const auto rewrittenWidgets = rewritten["desktop_widgets"]["widget"];
      expect(rewrittenWidgets["clock"]["position"].value<std::string>() == "absolute", "default position is written");
      expect(rewrittenWidgets["clock"]["anchor"].value<std::string>() == "center", "default anchor is written");
      expect(
          !rewrittenWidgets["legacy"]["position"] && !rewrittenWidgets["legacy"]["anchor"],
          "keys no lower layer sets stay out of settings.toml"
      );
    }
  }

  void testValidationWarnings(const std::filesystem::path& root) {
    const auto path = root / "invalid.toml";
    writeFile(
        path,
        R"(
[desktop_widgets.output.DP-1]
layout = "sharred"

[desktop_widgets.widget.clock]
type = "clock"
position = "relativ"

[desktop_widgets.widget.anchored]
type = "clock"
position = "relative"
anchor = "middle"
cx = 960.0

[desktop_widgets.widget.hanging]
type = "clock"
position = "relative"
anchor = "top_left"
cx = -0.2
cy = 1.4

[desktop_widgets.widget.absolute]
type = "clock"
anchor = "top"
)"
    );
    const auto diagnostics = noctalia::config::validateConfigFile(path.string());
    const auto warned = [&](std::string_view entryPath) {
      return std::ranges::any_of(diagnostics.entries, [&](const auto& entry) { return entry.path == entryPath; });
    };
    expect(warned("desktop_widgets.output.DP-1.layout"), "unknown layout warns");
    expect(warned("desktop_widgets.widget.clock.position"), "unknown position warns");
    expect(warned("desktop_widgets.widget.anchored.anchor"), "unknown anchor warns");
    expect(warned("desktop_widgets.widget.anchored.cx"), "a pixel value in a relative position warns");
    expect(
        !warned("desktop_widgets.widget.hanging.cx") && !warned("desktop_widgets.widget.hanging.cy"),
        "the values the editor stores for a widget hanging off an edge do not warn"
    );
    expect(!warned("desktop_widgets.widget.absolute.anchor"), "an anchor on an absolute widget is valid");
  }

} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / ("noctalia-widget-placement-" + std::to_string(::getpid()));
  std::filesystem::remove_all(root);

  testPositionMath();
  testSharedLayout();
  testSelectors();
  testDrop();
  testLoginBoxSelection();
  testLoginBoxSelectors();
  testConfigRoundTrip(root);
  testValidationWarnings(root);

  std::filesystem::remove_all(root);
  if (g_failures > 0) {
    std::println(stderr, "desktop_widget_placement: {} failure(s)", g_failures);
    return 1;
  }
  return 0;
}
