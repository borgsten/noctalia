// Relative positions, anchors and the shared widget layout: the position math, the rule for which
// outputs show which widgets, and the config round trip of the new keys.

#include "config/config_service.h"
#include "config/config_types.h"
#include "config/config_validate.h"
#include "core/toml.h"
#include "shell/desktop/desktop_widget_layout.h"
#include "shell/desktop/shared_widget_layout.h"

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
    expect(passthrough.cx == 300.0F && passthrough.cy == 400.0F, "absolute positions pass through");

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
    for (const auto anchor : kAnchors) {
      for (const float rotation : {0.0F, 0.5236F, 1.5708F}) {
        DesktopWidgetState state;
        state.positionMode = DesktopWidgetPositionMode::Relative;
        state.anchor = anchor;
        state.rotationRad = rotation;
        state.cx = 0.3F;
        state.cy = 0.7F;
        const auto center = resolveCenter(state, kOutW, kOutH, kW, kH);
        DesktopWidgetState stored = state;
        stored.cx = 0.0F;
        stored.cy = 0.0F;
        storeCenter(stored, kOutW, kOutH, kW, kH, center.cx, center.cy);
        expect(near(stored.cx, state.cx) && near(stored.cy, state.cy), "storeCenter inverts resolveCenter");
      }
    }
  }

  void testSharedLayout() {
    using desktop_widgets::SharedLayoutResolver;
    const std::vector<std::string> outputs = {"DP-1", "DP-2", "HDMI-A-1"};

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

  void testLoginBoxSelection() {
    using desktop_widgets::SharedLayoutResolver;
    const std::vector<std::string> outputs = {"DP-1", "DP-2"};
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
cx = 1.5

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
    expect(warned("desktop_widgets.widget.anchored.cx"), "relative position outside 0..1 warns");
    expect(warned("desktop_widgets.widget.absolute.anchor"), "anchor on an absolute widget warns");
  }

} // namespace

int main() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / ("noctalia-widget-placement-" + std::to_string(::getpid()));
  std::filesystem::remove_all(root);

  testPositionMath();
  testSharedLayout();
  testLoginBoxSelection();
  testConfigRoundTrip(root);
  testValidationWarnings(root);

  std::filesystem::remove_all(root);
  if (g_failures > 0) {
    std::println(stderr, "desktop_widget_placement: {} failure(s)", g_failures);
    return 1;
  }
  return 0;
}
