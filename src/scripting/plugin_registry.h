#pragma once

#include "scripting/plugin_manifest.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scripting {

  // A single entry resolved against the catalog: its owning manifest, the entry
  // declaration, and the absolute path to its `.luau` source.
  struct ResolvedPluginEntry {
    const PluginManifest* manifest = nullptr;
    const PluginEntry* entry = nullptr;
    std::filesystem::path sourcePath;

    [[nodiscard]] std::string fullId() const; // "author/plugin:entry"
  };

  // Scans bundled + user plugin directories and exposes their entries by id.
  // P0: bundled and user plugins are implicitly active; distribution (sources,
  // enable/disable) is a later phase.
  class PluginRegistry {
  public:
    // Process-wide registry. Plugins are global app state; both the widget
    // factory and the settings GUI read the same catalog.
    static PluginRegistry& instance();

    // Scan once on first use (idempotent). Call scan() directly to force a rescan.
    void ensureScanned();

    // Rescan the bundled (assets/plugins) and user ($XDG_DATA_HOME/noctalia/plugins)
    // directories. Pointers from prior resolve()/entriesOfKind() calls are
    // invalidated.
    void scan();

    // Resolve "author/plugin:entry" to its manifest, entry, and source path.
    [[nodiscard]] std::optional<ResolvedPluginEntry> resolve(std::string_view fullEntryId) const;

    // Whether `id` names a registered entry ("author/plugin:entry").
    [[nodiscard]] bool hasEntry(std::string_view fullEntryId) const;

    // All entries of one kind (e.g. every [[widget]]) across active plugins.
    [[nodiscard]] std::vector<ResolvedPluginEntry> entriesOfKind(PluginEntryKind kind) const;

  private:
    struct LoadedPlugin {
      PluginManifest manifest;
      std::filesystem::path dir;
    };

    void scanDir(const std::filesystem::path& dir);
    [[nodiscard]] const LoadedPlugin* findPlugin(std::string_view pluginId) const;

    std::vector<LoadedPlugin> m_plugins;
    bool m_scanned = false;
  };

} // namespace scripting
