#include "Host.hpp"
#include "PluginMetadata.hpp"
#include "DesktopUi.hpp"
#include "GakuPluginMetadata.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <windows.h>

// Production PluginHost is linked, but no game, windows or dialogs are started.
namespace GakuLoader::DesktopUi {
bool Initialize() { return true; }
void Shutdown() {}
const GakuDesktopUiService* GetService() { return nullptr; }
const GakuDesktopWindowService* GetWindowService() { return nullptr; }
const GakuAppearanceService* GetAppearanceService() { return nullptr; }
GakuStatus SetFileDropTarget(const char*, const GakuFileDropTarget*) { return GAKU_E_UNAVAILABLE; }
GakuStatus PollFileDrop(const char*, uint32_t, GakuFileDropResult*, uint32_t, char*) { return GAKU_E_UNAVAILABLE; }
GakuStatus PeekFileDrag(const char*, uint32_t, GakuFileDragState*, uint32_t, char*) { return GAKU_E_UNAVAILABLE; }
GakuStatus RequestShowPanel(const char*) { return GAKU_E_UNAVAILABLE; }
void ToggleManager() {}
bool WantsTextInput() { return false; }
}
namespace GakuLoader::DesktopDialogs {
GakuFileDialogService ServiceForOwner(void*) { return {}; }
void CancelOwner(void*) {}
}

namespace {

using namespace GakuLoader::Metadata;

SemVer MustParseSemVer(std::string_view text, bool allowTwoPart = false) {
    SemVer ver;
    assert(ParseSemVer(text, ver, allowTwoPart));
    assert(ver.valid);
    return ver;
}

bool MatchesRange(std::string_view version, std::string_view rangeExpr, bool bareIsMinimum = false) {
    SemVerRange range;
    assert(ParseSemVerRange(rangeExpr, range, bareIsMinimum));
    bool verValid = false;
    return SatisfiesSemVerRange(version, range, verValid);
}

void SemVerChecks() {
    const std::vector<std::string> ordered{
        "0.2.0-alpha.1",
        "0.2.0-beta.1",
        "0.2.0-beta.2",
        "0.2.0-beta.10",
        "0.2.0",
        "0.2.1",
    };
    for (size_t i = 0; i < ordered.size(); ++i) {
        const SemVer a = MustParseSemVer(ordered[i]);
        assert(CompareSemVer(a, a) == 0);
        for (size_t j = i + 1; j < ordered.size(); ++j) {
            const SemVer b = MustParseSemVer(ordered[j]);
            assert(CompareSemVer(a, b) < 0);
            assert(CompareSemVer(b, a) > 0);
        }
    }

    // Numeric identifiers have lower precedence than non-numeric; shorter < longer.
    assert(CompareSemVer(MustParseSemVer("1.0.0-1"), MustParseSemVer("1.0.0-alpha")) < 0);
    assert(CompareSemVer(MustParseSemVer("1.0.0-alpha"), MustParseSemVer("1.0.0-alpha.1")) < 0);

    // Build metadata MUST be ignored for precedence.
    assert(CompareSemVer(MustParseSemVer("1.0.0+build.1"), MustParseSemVer("1.0.0+build.2")) == 0);
    assert(CompareSemVer(MustParseSemVer("1.0.0-beta.1+exp.sha"), MustParseSemVer("1.0.0-beta.1+other.99")) == 0);

    // Two-part toggle.
    SemVer v;
    assert(!ParseSemVer("1.2", v, false));
    assert(ParseSemVer("1.2", v, true) && v.major == 1 && v.minor == 2 && v.patch == 0);

    // Invalid SemVer 2.0.0 inputs.
    const std::vector<std::string> invalid{
        "", "1", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03",
        "1.2.3-", "1.2.3+", "1.2.3-01", "1.2.3-alpha..1",
        "1.2.3+build..1", "1.2.3-alpha$1", "v1.2.3", "1.2.3 "};
    for (const auto& bad : invalid) {
        assert(!ParseSemVer(bad, v, false));
    }
    std::puts("ok: SemVer 2.0.0 parsing and ordering");
}

void RangeChecks() {
    assert(MatchesRange("0.2.0-beta.2", ">0.2.0-beta.1"));
    assert(MatchesRange("0.2.0", ">0.2.0-beta.1"));
    assert(MatchesRange("0.2.1", ">0.2.0-beta.1"));
    assert(!MatchesRange("0.2.0-beta.1", ">0.2.0-beta.1"));
    assert(!MatchesRange("0.2.0-alpha.9", ">0.2.0-beta.1"));
    assert(!MatchesRange("0.1.9", ">0.2.0-beta.1"));

    assert(MatchesRange("0.2.0", ">=0.2.0 <1.0.0"));
    assert(MatchesRange("0.9.9", ">=0.2.0 <1.0.0"));
    assert(!MatchesRange("0.1.9", ">=0.2.0 <1.0.0"));
    assert(!MatchesRange("1.0.0", ">=0.2.0 <1.0.0"));

    // ^0.2.0 -> >=0.2.0, <0.3.0 (excludes 0.3.0-alpha.1)
    assert(MatchesRange("0.2.0", "^0.2.0"));
    assert(MatchesRange("0.2.5", "^0.2.0"));
    assert(!MatchesRange("0.1.9", "^0.2.0"));
    assert(!MatchesRange("0.3.0-alpha.1", "^0.2.0"));
    assert(!MatchesRange("0.3.0", "^0.2.0"));
    assert(!MatchesRange("1.0.0", "^0.2.0"));

    // ^0.0.3 -> >=0.0.3, <0.0.4 (excludes 0.0.4-alpha.1)
    assert(MatchesRange("0.0.3", "^0.0.3"));
    assert(MatchesRange("0.0.3+build.5", "^0.0.3"));
    assert(!MatchesRange("0.0.2", "^0.0.3"));
    assert(!MatchesRange("0.0.4-alpha.1", "^0.0.3"));
    assert(!MatchesRange("0.0.4", "^0.0.3"));

    // ^1.2.3 -> >=1.2.3, <2.0.0
    assert(MatchesRange("1.2.3", "^1.2.3"));
    assert(MatchesRange("1.9.9", "^1.2.3"));
    assert(!MatchesRange("1.2.2", "^1.2.3"));
    assert(!MatchesRange("2.0.0-alpha.1", "^1.2.3"));
    assert(!MatchesRange("2.0.0", "^1.2.3"));

    // ~0.2.1 -> >=0.2.1, <0.3.0
    assert(MatchesRange("0.2.1", "~0.2.1"));
    assert(MatchesRange("0.2.9", "~0.2.1"));
    assert(!MatchesRange("0.2.0", "~0.2.1"));
    assert(!MatchesRange("0.3.0-alpha.1", "~0.2.1"));
    assert(!MatchesRange("0.3.0", "~0.2.1"));

    // !=1.0.0
    assert(MatchesRange("0.9.9", "!=1.0.0"));
    assert(MatchesRange("1.0.1", "!=1.0.0"));
    assert(!MatchesRange("1.0.0", "!=1.0.0"));

    // || union
    assert(MatchesRange("1.2.0", ">=1.0.0 <1.5.0 || >=2.0.0"));
    assert(MatchesRange("2.0.0", ">=1.0.0 <1.5.0 || >=2.0.0"));
    assert(!MatchesRange("1.5.0", ">=1.0.0 <1.5.0 || >=2.0.0"));
    assert(!MatchesRange("0.9.0", ">=1.0.0 <1.5.0 || >=2.0.0"));

    // * and empty wildcard
    assert(MatchesRange("0.0.1", "*"));
    assert(MatchesRange("3.2.1", ""));

    // bareIsMinimum
    assert(MatchesRange("1.2.0", "1.2.0", false));
    assert(!MatchesRange("1.2.1", "1.2.0", false));
    assert(MatchesRange("1.2.0", "1.2.0", true));
    assert(MatchesRange("1.3.0", "1.2.0", true));
    assert(!MatchesRange("1.1.9", "1.2.0", true));

    // Invalid ranges
    SemVerRange bad;
    for (const char* expr : {"||", ">=1.0.0 ||", ">=", "^", "not-a-ver", ">= *"}) {
        assert(!ParseSemVerRange(expr, bad, false));
    }
    std::puts("ok: SemVer 2.0.0 range expressions");
}

void UrlAndSummaryChecks() {
    assert(SanitizeExternalUrl("https://github.com/org/repo") == "https://github.com/org/repo");
    assert(SanitizeExternalUrl("  http://example.com/path?x=1  ") == "http://example.com/path?x=1");
    assert(SanitizeExternalUrl("HTTPS://example.com/Path") == "HTTPS://example.com/Path");

    for (const char* unsafe : {
            static_cast<const char*>(nullptr), "", "   ",
            "javascript:alert(1)", "JAVASCRIPT:alert(1)",
            "file:///C:/Windows/win.ini", "data:text/html,hi", "ftp://example.com",
            "https://example.com/a b", "https://example.com/a\nb",
            "https://example.com/a\x01" "b", "https://example.com/a\x7f" "b"}) {
        assert(SanitizeExternalUrl(unsafe).empty());
    }

    const std::vector<std::pair<std::string, std::string>> localized{
        {"en", "English text"},
        {"zh-Hans", "Simplified Chinese"},
        {"ja", ""},
    };
    assert(ResolveLocalizedSummary("Fallback", localized, "zh-Hans") == "Simplified Chinese");
    assert(ResolveLocalizedSummary("Fallback", localized, "ZH-HANS") == "Simplified Chinese");
    assert(ResolveLocalizedSummary("Fallback", localized, "en-US") == "English text");
    assert(ResolveLocalizedSummary("Fallback", localized, "ja") == "Fallback");
    assert(ResolveLocalizedSummary("Fallback", localized, "ko") == "Fallback");
    assert(ResolveLocalizedSummary("Fallback", localized, "") == "Fallback");

    // ExtractMetadata validation and sanitization
    GakuLoader::LoadedPlugin out;
    std::vector<std::string> warnings;
    GakuPluginMetadataV1 badAbi{};
    badAbi.abiVersion = 99u;
    badAbi.structSize = sizeof(GakuPluginMetadataV1);
    assert(!ExtractMetadata(badAbi, out, warnings));

    GakuPluginMetadataV1 badSize{};
    badSize.abiVersion = GAKU_METADATA_ABI_VERSION;
    badSize.structSize = sizeof(GakuPluginMetadataV1) - 1u;
    assert(!ExtractMetadata(badSize, out, warnings));

    const GakuPluginDependencyV1 deps[] = {
        {"", "Empty", "*", GAKU_DEPENDENCY_REQUIRED, 0u},
        {"bad.kind", "BadKind", "*", 99u, 0u},
        {"ok.dep", "Ok Dep", "^1.0.0", GAKU_DEPENDENCY_REQUIRED, 0u},
    };
    GakuPluginMetadataV1 raw{};
    raw.abiVersion = GAKU_METADATA_ABI_VERSION;
    raw.structSize = sizeof(GakuPluginMetadataV1);
    raw.originalAuthor = "Author";
    raw.repositoryUrl = "https://example.com/repo";
    raw.releaseUrl = "javascript:alert(1)";
    raw.issuesUrl = "file:///etc/passwd";
    raw.dependencyCount = 3u;
    raw.dependencies = deps;
    warnings.clear();
    assert(ExtractMetadata(raw, out, warnings));
    assert(out.hasMetadata);
    assert(out.repositoryUrl == "https://example.com/repo");
    assert(out.releaseUrl.empty() && out.issuesUrl.empty());
    assert(out.dependencies.size() == 1 && out.dependencies[0].pluginId == "ok.dep");
    assert(warnings.size() >= 4);
    std::puts("ok: SanitizeExternalUrl, ResolveLocalizedSummary, ExtractMetadata");
}

PluginCandidate MakeCandidate(
    std::string id,
    std::string version,
    std::vector<GakuLoader::DesktopDependencyInfo> deps = {},
    std::string minLoader = {},
    bool enabled = true) {
    PluginCandidate cand;
    cand.enabled = enabled;
    cand.plugin.id = std::move(id);
    cand.plugin.name = cand.plugin.id;
    cand.plugin.version = std::move(version);
    cand.plugin.dependencies = std::move(deps);
    cand.plugin.minLoaderVersion = std::move(minLoader);
    return cand;
}

GakuLoader::DesktopDependencyInfo MakeDep(uint32_t kind, std::string id, std::string range = "*") {
    GakuLoader::DesktopDependencyInfo dep;
    dep.kind = kind;
    dep.pluginId = std::move(id);
    dep.versionRange = std::move(range);
    return dep;
}

const GakuLoader::LoadedPlugin& FindById(const std::vector<PluginCandidate>& list, const std::string& id) {
    const auto it = std::find_if(list.begin(), list.end(),
        [&](const PluginCandidate& c) { return c.plugin.id == id; });
    assert(it != list.end());
    return it->plugin;
}

size_t IndexOf(const std::vector<PluginCandidate>& list, const std::string& id) {
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].plugin.id == id) return i;
    }
    assert(false);
    return list.size();
}

void GraphChecks() {
    std::vector<std::string> diags;

    // 1. Required missing + mismatch + cascade
    {
        std::vector<PluginCandidate> list;
        list.push_back(MakeCandidate("base", "1.0.0"));
        list.push_back(MakeCandidate("needs.missing", "1.0.0", {MakeDep(GAKU_DEPENDENCY_REQUIRED, "ghost", "*")}));
        list.push_back(MakeCandidate("needs.newer", "1.0.0", {MakeDep(GAKU_DEPENDENCY_REQUIRED, "base", ">=2.0.0")}));
        list.push_back(MakeCandidate("cascade", "1.0.0", {MakeDep(GAKU_DEPENDENCY_REQUIRED, "needs.newer", "*")}));
        ResolveAndSortPlugins(list, "1.1.0", diags);

        assert(!FindById(list, "base").failed);
        assert(FindById(list, "needs.missing").failed &&
            FindById(list, "needs.missing").loadState == GAKU_PLUGIN_STATE_DEPENDENCY_ERROR);
        assert(FindById(list, "needs.missing").dependencies[0].targetState == GAKU_PLUGIN_STATE_NOT_FOUND);
        assert(FindById(list, "needs.newer").failed &&
            FindById(list, "needs.newer").loadState == GAKU_PLUGIN_STATE_DEPENDENCY_ERROR);
        assert(FindById(list, "cascade").failed &&
            FindById(list, "cascade").loadState == GAKU_PLUGIN_STATE_DEPENDENCY_ERROR);
    }

    // 2. Required cycle (2-node and self-cycle) rejected
    {
        std::vector<PluginCandidate> list;
        list.push_back(MakeCandidate("cycle.x", "1.0.0", {MakeDep(GAKU_DEPENDENCY_REQUIRED, "cycle.y")}));
        list.push_back(MakeCandidate("cycle.y", "1.0.0", {MakeDep(GAKU_DEPENDENCY_REQUIRED, "cycle.x")}));
        list.push_back(MakeCandidate("cycle.self", "1.0.0", {MakeDep(GAKU_DEPENDENCY_REQUIRED, "cycle.self")}));
        ResolveAndSortPlugins(list, "1.1.0", diags);

        for (const char* id : {"cycle.x", "cycle.y", "cycle.self"}) {
            const auto& p = FindById(list, id);
            assert(p.failed && p.loadState == GAKU_PLUGIN_STATE_DEPENDENCY_ERROR);
            assert(p.statusReason == "Circular required dependency");
        }
    }

    // 3. Optional cycle degraded (both survive)
    {
        std::vector<PluginCandidate> list;
        list.push_back(MakeCandidate("opt.a", "1.0.0", {MakeDep(GAKU_DEPENDENCY_OPTIONAL, "opt.b", ">=1.0.0")}));
        list.push_back(MakeCandidate("opt.b", "1.0.0", {MakeDep(GAKU_DEPENDENCY_OPTIONAL, "opt.a", ">=1.0.0")}));
        ResolveAndSortPlugins(list, "1.1.0", diags);

        const auto& a = FindById(list, "opt.a");
        const auto& b = FindById(list, "opt.b");
        assert(!a.failed && a.loadState == GAKU_PLUGIN_STATE_QUERIED);
        assert(!b.failed && b.loadState == GAKU_PLUGIN_STATE_QUERIED);
        assert(a.dependencies[0].cycleDegraded || b.dependencies[0].cycleDegraded);
    }

    // 4. Mixed Required + Optional cycle: Required edge wins ordering, Optional edge degrades
    {
        std::vector<PluginCandidate> list;
        list.push_back(MakeCandidate("aaa.consumer", "1.0.0", {MakeDep(GAKU_DEPENDENCY_REQUIRED, "zzz.base", ">=1.0.0")}));
        list.push_back(MakeCandidate("zzz.base", "1.0.0", {MakeDep(GAKU_DEPENDENCY_OPTIONAL, "aaa.consumer", ">=1.0.0")}));
        ResolveAndSortPlugins(list, "1.1.0", diags);

        assert(!FindById(list, "zzz.base").failed);
        assert(!FindById(list, "aaa.consumer").failed);
        assert(IndexOf(list, "zzz.base") < IndexOf(list, "aaa.consumer"));
        assert(FindById(list, "zzz.base").dependencies[0].cycleDegraded);
    }

    // 5. Conflict rejected (INCOMPATIBLE); disabled conflict target does not fail
    {
        std::vector<PluginCandidate> list;
        list.push_back(MakeCandidate("plugin.a", "1.0.0", {
            MakeDep(GAKU_DEPENDENCY_CONFLICT, "plugin.b", ">=1.0.0"),
            MakeDep(GAKU_DEPENDENCY_CONFLICT, "plugin.disabled", "*"),
        }));
        list.push_back(MakeCandidate("plugin.b", "1.2.0"));
        list.push_back(MakeCandidate("plugin.disabled", "1.0.0", {}, {}, false));
        ResolveAndSortPlugins(list, "1.1.0", diags);

        const auto& a = FindById(list, "plugin.a");
        assert(a.failed && a.loadState == GAKU_PLUGIN_STATE_INCOMPATIBLE);
        assert(!a.dependencies[0].satisfied);
        assert(a.dependencies[1].satisfied);
        assert(!FindById(list, "plugin.b").failed);
        assert(FindById(list, "plugin.disabled").loadState == GAKU_PLUGIN_STATE_DISABLED);
    }

    // 6. minLoaderVersion rejected (INCOMPATIBLE)
    {
        std::vector<PluginCandidate> list;
        list.push_back(MakeCandidate("too.new", "1.0.0", {}, "2.0.0"));
        list.push_back(MakeCandidate("bad.min", "1.0.0", {}, "not-semver"));
        list.push_back(MakeCandidate("ok.min", "1.0.0", {}, "1.1.0"));
        ResolveAndSortPlugins(list, "1.1.0", diags);

        assert(FindById(list, "too.new").failed &&
            FindById(list, "too.new").loadState == GAKU_PLUGIN_STATE_INCOMPATIBLE);
        assert(FindById(list, "bad.min").failed &&
            FindById(list, "bad.min").loadState == GAKU_PLUGIN_STATE_INCOMPATIBLE);
        assert(!FindById(list, "ok.min").failed &&
            FindById(list, "ok.min").loadState == GAKU_PLUGIN_STATE_QUERIED);
    }

    std::puts("ok: ResolveAndSortPlugins dependency graph and topological ordering");
}

const GakuLoader::LoadedPlugin& FindLoaded(const char* id) {
    const auto& plugins = GakuLoader::GetLoadedPlugins();
    const auto it = std::find_if(plugins.begin(), plugins.end(),
        [id](const GakuLoader::LoadedPlugin& p) { return p.id == id; });
    assert(it != plugins.end());
    return *it;
}

size_t LoadedIndex(const char* id) {
    const auto& plugins = GakuLoader::GetLoadedPlugins();
    for (size_t i = 0; i < plugins.size(); ++i) {
        if (plugins[i].id == id) return i;
    }
    assert(false);
    return plugins.size();
}

const GakuLoader::DesktopPluginPanel& FindSnapshotPlugin(
    const GakuLoader::DesktopHostSnapshot& snap, const char* id) {
    const auto it = std::find_if(snap.plugins.begin(), snap.plugins.end(),
        [id](const GakuLoader::DesktopPluginPanel& p) { return p.id == id; });
    assert(it != snap.plugins.end());
    return *it;
}

void HostIntegrationChecks(const std::filesystem::path& root) {
    const auto local = root / L"GakuLoader";
    std::filesystem::create_directories(local / L"Configs");
    const auto iniPath = local / L"Configs" / L"gakuloader.ini";
    assert(WritePrivateProfileStringW(L"auxiliary", L"aux-marker.dll", L"true", iniPath.c_str()));

    GakuLoader::DisableHotkeyPollerForTest();
    GakuLoader::Initialize(root, local);
    GakuLoader::ActivatePlugins();

    // aux-marker.dll must never be passed to LoadLibrary.
    assert(GetModuleHandleW(L"aux-marker.dll") == nullptr);
    assert(!std::filesystem::exists(local / L"plugins" / L"aux-marker.loaded"));

    // broken-dep.dll must be rejected and unloaded via FreeLibrary.
    assert(GetModuleHandleW(L"broken-dep.dll") == nullptr);

    const auto& base = FindLoaded("meta.base");
    const auto& consumer = FindLoaded("meta.consumer");
    const auto& broken = FindLoaded("meta.broken");
    const auto& optA = FindLoaded("meta.opt_cycle_a");
    const auto& optB = FindLoaded("meta.opt_cycle_b");

    // aaa-consumer.dll activated AFTER zzz-base.dll and bound "meta.base/v1" + queried gakuloader.plugins.
    assert(LoadedIndex("meta.base") < LoadedIndex("meta.consumer"));
    assert(base.activated && !base.failed && base.loadState == GAKU_PLUGIN_STATE_ACTIVATED);
    assert(base.hasMetadata);
    assert(base.originalAuthor == "Original Base Author");
    assert(base.maintainers.size() == 2 && base.maintainers[0] == "Maintainer One" && base.maintainers[1] == "Maintainer Two");
    assert(base.subVersion == "build.42");
    assert(base.repositoryUrl == "https://example.com/meta-base");
    assert(base.releaseUrl == "https://example.com/meta-base/releases");
    assert(base.issuesUrl == "https://example.com/meta-base/issues");
    assert(base.license == "MIT");
    assert(base.auxiliaryDlls.size() == 1 && base.auxiliaryDlls[0] == "ini-aux.dll");

    assert(consumer.activated && !consumer.failed && consumer.loadState == GAKU_PLUGIN_STATE_ACTIVATED);

    assert(!broken.activated && broken.failed && broken.handle == nullptr);
    assert(broken.loadState == GAKU_PLUGIN_STATE_DEPENDENCY_ERROR);

    assert(optA.activated && !optA.failed && optA.loadState == GAKU_PLUGIN_STATE_ACTIVATED);
    assert(optB.activated && !optB.failed && optB.loadState == GAKU_PLUGIN_STATE_ACTIVATED);

    GakuLoader::DesktopHostSnapshot snap;
    assert(GakuLoader::SnapshotDesktopHost(snap));
    assert(snap.activated && !snap.runtimeReady);
    assert(std::find(snap.auxiliaryFiles.begin(), snap.auxiliaryFiles.end(), L"aux-marker.dll") !=
        snap.auxiliaryFiles.end());

    const auto& snapBroken = FindSnapshotPlugin(snap, "meta.broken");
    assert(snapBroken.failed && snapBroken.loadState == GAKU_PLUGIN_STATE_DEPENDENCY_ERROR);

    const auto& snapOptA = FindSnapshotPlugin(snap, "meta.opt_cycle_a");
    const auto& snapOptB = FindSnapshotPlugin(snap, "meta.opt_cycle_b");
    const bool hasCycleDegraded =
        (!snapOptA.dependencies.empty() && snapOptA.dependencies[0].cycleDegraded) ||
        (!snapOptB.dependencies.empty() && snapOptB.dependencies[0].cycleDegraded);
    assert(hasCycleDegraded);

    const auto& snapConsumer = FindSnapshotPlugin(snap, "meta.consumer");
    assert(snapConsumer.dependencies.size() == 3);
    assert(snapConsumer.dependencies[0].pluginId == "meta.base" &&
        snapConsumer.dependencies[0].satisfied &&
        snapConsumer.dependencies[0].targetState == GAKU_PLUGIN_STATE_ACTIVATED);
    assert(snapConsumer.dependencies[1].pluginId == "meta.missing" &&
        snapConsumer.dependencies[1].satisfied &&
        snapConsumer.dependencies[1].targetState == GAKU_PLUGIN_STATE_NOT_FOUND);
    assert(snapConsumer.dependencies[2].pluginId == "meta.opt_cycle_a" &&
        snapConsumer.dependencies[2].satisfied &&
        snapConsumer.dependencies[2].targetState == GAKU_PLUGIN_STATE_ACTIVATED);

    GakuLoader::NotifyRuntimeReady();
    assert(GakuLoader::SnapshotDesktopHost(snap));
    assert(snap.runtimeReady);
    assert(FindSnapshotPlugin(snap, "meta.base").loadState == GAKU_PLUGIN_STATE_RUNTIME_READY);
    assert(FindSnapshotPlugin(snap, "meta.consumer").loadState == GAKU_PLUGIN_STATE_RUNTIME_READY);
    assert(FindSnapshotPlugin(snap, "meta.opt_cycle_a").loadState == GAKU_PLUGIN_STATE_RUNTIME_READY);
    assert(FindSnapshotPlugin(snap, "meta.opt_cycle_b").loadState == GAKU_PLUGIN_STATE_RUNTIME_READY);
    assert(FindSnapshotPlugin(snap, "meta.broken").loadState == GAKU_PLUGIN_STATE_DEPENDENCY_ERROR);

    assert(GakuLoader::BindMainThread(GetCurrentThreadId()));
    GakuLoader::Shutdown();

    const auto logPath = local / L"Logs" / L"gakuloader.log";
    std::ifstream logFile(logPath, std::ios::binary);
    assert(logFile.good());
    std::ostringstream buffer;
    buffer << logFile.rdbuf();
    const std::string logText = buffer.str();
    assert(logText.find("[Plugin:meta.base] base ready") != std::string::npos);
    assert(logText.find("[ActivateSummary]") != std::string::npos);
    assert(logText.find("auxiliary_skipped=1") != std::string::npos);

    std::puts("ok: real-DLL integration (topological activation, gakuloader.plugins, FreeLibrary unload, [auxiliary], ServiceLog tagging, ActivateSummary)");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::fputs("usage: plugin-metadata-tests <synthetic absolute root>\n", stderr);
        return 2;
    }
    const std::filesystem::path root(argv[1]);
    assert(root.is_absolute());

    SemVerChecks();
    RangeChecks();
    UrlAndSummaryChecks();
    GraphChecks();
    HostIntegrationChecks(root);

    std::puts("PASS: plugin metadata, SemVer 2.0.0, dependency scheduler, gakuloader.plugins, [auxiliary], and ServiceLog tagging");
    return 0;
}
