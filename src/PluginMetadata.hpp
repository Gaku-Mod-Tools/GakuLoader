// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once
#include "Host.hpp"
#include "GakuPluginMetadata.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace GakuLoader::Metadata {

struct SemVer {
    uint32_t major{0};
    uint32_t minor{0};
    uint32_t patch{0};
    std::vector<std::string> prerelease;
    std::string build;
    bool valid{false};
};

namespace detail {

inline bool IsAsciiDigit(char c) {
    return c >= '0' && c <= '9';
}

inline bool IsAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline bool IsIdentChar(char c) {
    return IsAsciiDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}

inline char ToAsciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

inline std::string_view TrimAscii(std::string_view sv) {
    while (!sv.empty() && IsAsciiSpace(sv.front())) sv.remove_prefix(1);
    while (!sv.empty() && IsAsciiSpace(sv.back())) sv.remove_suffix(1);
    return sv;
}

inline bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (ToAsciiLower(a[i]) != ToAsciiLower(b[i])) return false;
    }
    return true;
}

inline bool IStartsWith(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) return false;
    return IEquals(text.substr(0, prefix.size()), prefix);
}

inline bool ParseNumericSegment(std::string_view s, uint32_t& out) {
    if (s.empty()) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    uint32_t value = 0;
    for (char c : s) {
        if (!IsAsciiDigit(c)) return false;
        const uint32_t digit = static_cast<uint32_t>(c - '0');
        if (value > (std::numeric_limits<uint32_t>::max)() / 10u) return false;
        value *= 10u;
        if (value > (std::numeric_limits<uint32_t>::max)() - digit) return false;
        value += digit;
    }
    out = value;
    return true;
}

inline bool IsAllDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!IsAsciiDigit(c)) return false;
    }
    return true;
}

inline bool SplitDotSegments(std::string_view s, std::vector<std::string_view>& out) {
    out.clear();
    if (s.empty()) return false;
    size_t start = 0;
    while (true) {
        const size_t dot = s.find('.', start);
        const std::string_view seg = (dot == std::string_view::npos)
            ? s.substr(start)
            : s.substr(start, dot - start);
        if (seg.empty()) return false;
        out.push_back(seg);
        if (dot == std::string_view::npos) break;
        start = dot + 1;
    }
    return true;
}

inline std::string SafeCopyCString(const char* ptr, size_t maxBytes = 4096) {
    if (!ptr) return {};
    const size_t len = strnlen(ptr, maxBytes);
    return std::string(ptr, len);
}

} // namespace detail

inline bool ParseSemVer(std::string_view input, SemVer& out, bool allowTwoPart = false) {
    out = SemVer{};
    if (input.empty()) return false;

    std::string_view coreAndPre = input;
    const size_t plusPos = input.find('+');
    if (plusPos != std::string_view::npos) {
        const std::string_view buildPart = input.substr(plusPos + 1);
        coreAndPre = input.substr(0, plusPos);
        std::vector<std::string_view> buildSegs;
        if (!detail::SplitDotSegments(buildPart, buildSegs)) return false;
        for (const auto seg : buildSegs) {
            for (char c : seg) {
                if (!detail::IsIdentChar(c)) return false;
            }
        }
        out.build = std::string(buildPart);
    }

    std::string_view corePart = coreAndPre;
    const size_t dashPos = coreAndPre.find('-');
    if (dashPos != std::string_view::npos) {
        const std::string_view prePart = coreAndPre.substr(dashPos + 1);
        corePart = coreAndPre.substr(0, dashPos);
        std::vector<std::string_view> preSegs;
        if (!detail::SplitDotSegments(prePart, preSegs)) return false;
        out.prerelease.reserve(preSegs.size());
        for (const auto seg : preSegs) {
            bool allDigits = true;
            for (char c : seg) {
                if (!detail::IsIdentChar(c)) return false;
                if (!detail::IsAsciiDigit(c)) allDigits = false;
            }
            if (allDigits && seg.size() > 1 && seg[0] == '0') return false;
            out.prerelease.emplace_back(seg);
        }
    }

    std::vector<std::string_view> coreSegs;
    if (!detail::SplitDotSegments(corePart, coreSegs)) return false;
    if (coreSegs.size() != 3 && !(allowTwoPart && coreSegs.size() == 2)) return false;

    if (!detail::ParseNumericSegment(coreSegs[0], out.major)) return false;
    if (!detail::ParseNumericSegment(coreSegs[1], out.minor)) return false;
    if (coreSegs.size() == 3) {
        if (!detail::ParseNumericSegment(coreSegs[2], out.patch)) return false;
    } else {
        out.patch = 0;
    }

    out.valid = true;
    return true;
}

inline int CompareSemVer(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;

    const bool aHasPre = !a.prerelease.empty();
    const bool bHasPre = !b.prerelease.empty();
    if (!aHasPre && !bHasPre) return 0;
    if (!aHasPre) return 1;
    if (!bHasPre) return -1;

    const size_t count = (std::min)(a.prerelease.size(), b.prerelease.size());
    for (size_t i = 0; i < count; ++i) {
        const std::string& sa = a.prerelease[i];
        const std::string& sb = b.prerelease[i];
        const bool aNum = detail::IsAllDigits(sa);
        const bool bNum = detail::IsAllDigits(sb);
        if (aNum && bNum) {
            if (sa.size() != sb.size()) return sa.size() < sb.size() ? -1 : 1;
            if (sa != sb) return sa < sb ? -1 : 1;
        } else if (aNum != bNum) {
            return aNum ? -1 : 1;
        } else {
            if (sa != sb) return sa < sb ? -1 : 1;
        }
    }

    if (a.prerelease.size() != b.prerelease.size()) {
        return a.prerelease.size() < b.prerelease.size() ? -1 : 1;
    }
    return 0;
}

struct SemVerComparator {
    enum class Op {
        Eq,
        Ne,
        Gt,
        Ge,
        Lt,
        Le,
        LtUpperNoPre // Used for ^ and ~ upper bounds: excludes prereleases of the upper tuple
    };
    Op op{Op::Eq};
    SemVer ver;
};

struct SemVerAndGroup {
    bool matchAny{false};
    std::vector<SemVerComparator> comparators;
};

struct SemVerRange {
    bool valid{false};
    bool matchAll{false};
    std::vector<SemVerAndGroup> groups;
};

namespace detail {

inline bool IsStandaloneOperator(std::string_view token) {
    return token == ">=" || token == "<=" || token == "!=" || token == "==" ||
           token == ">" || token == "<" || token == "=" || token == "^" || token == "~";
}

inline bool AppendConstraintToken(std::string_view opStr, std::string_view verStr,
    bool hasExplicitOp, bool bareIsMinimum, SemVerAndGroup& group) {
    if (verStr.empty()) return false;

    SemVer ver;
    if (!ParseSemVer(verStr, ver, /*allowTwoPart=*/true)) return false;

    if (!hasExplicitOp) {
        group.comparators.push_back({
            bareIsMinimum ? SemVerComparator::Op::Ge : SemVerComparator::Op::Eq,
            std::move(ver)});
        return true;
    }

    if (opStr == "^") {
        SemVer upper;
        upper.valid = true;
        if (ver.major > 0) {
            if (ver.major == (std::numeric_limits<uint32_t>::max)()) return false;
            upper.major = ver.major + 1;
        } else if (ver.minor > 0) {
            if (ver.minor == (std::numeric_limits<uint32_t>::max)()) return false;
            upper.minor = ver.minor + 1;
        } else {
            if (ver.patch == (std::numeric_limits<uint32_t>::max)()) return false;
            upper.patch = ver.patch + 1;
        }
        group.comparators.push_back({SemVerComparator::Op::Ge, std::move(ver)});
        group.comparators.push_back({SemVerComparator::Op::LtUpperNoPre, std::move(upper)});
        return true;
    }

    if (opStr == "~") {
        if (ver.minor == (std::numeric_limits<uint32_t>::max)()) return false;
        SemVer upper;
        upper.valid = true;
        upper.major = ver.major;
        upper.minor = ver.minor + 1;
        group.comparators.push_back({SemVerComparator::Op::Ge, std::move(ver)});
        group.comparators.push_back({SemVerComparator::Op::LtUpperNoPre, std::move(upper)});
        return true;
    }

    SemVerComparator::Op op = SemVerComparator::Op::Eq;
    if (opStr == ">=") op = SemVerComparator::Op::Ge;
    else if (opStr == "<=") op = SemVerComparator::Op::Le;
    else if (opStr == "!=") op = SemVerComparator::Op::Ne;
    else if (opStr == "==" || opStr == "=") op = SemVerComparator::Op::Eq;
    else if (opStr == ">") op = SemVerComparator::Op::Gt;
    else if (opStr == "<") op = SemVerComparator::Op::Lt;
    else return false;

    group.comparators.push_back({op, std::move(ver)});
    return true;
}

} // namespace detail

inline bool ParseSemVerRange(std::string_view expr, SemVerRange& out, bool bareIsMinimum = false) {
    out = SemVerRange{};
    const std::string_view trimmed = detail::TrimAscii(expr);
    if (trimmed.empty() || trimmed == "*") {
        out.valid = true;
        out.matchAll = true;
        return true;
    }

    std::vector<std::string_view> orParts;
    size_t pos = 0;
    while (true) {
        const size_t sep = trimmed.find("||", pos);
        if (sep == std::string_view::npos) {
            orParts.push_back(trimmed.substr(pos));
            break;
        }
        orParts.push_back(trimmed.substr(pos, sep - pos));
        pos = sep + 2;
    }

    for (const auto part : orParts) {
        const std::string_view groupView = detail::TrimAscii(part);
        if (groupView.empty()) {
            out = SemVerRange{};
            return false;
        }

        std::vector<std::string_view> tokens;
        size_t idx = 0;
        while (idx < groupView.size()) {
            while (idx < groupView.size() && detail::IsAsciiSpace(groupView[idx])) ++idx;
            if (idx >= groupView.size()) break;
            const size_t start = idx;
            while (idx < groupView.size() && !detail::IsAsciiSpace(groupView[idx])) ++idx;
            tokens.push_back(groupView.substr(start, idx - start));
        }

        if (tokens.empty()) {
            out = SemVerRange{};
            return false;
        }

        SemVerAndGroup group;
        bool sawWildcard = false;
        for (size_t i = 0; i < tokens.size(); ++i) {
            const std::string_view tok = tokens[i];
            if (tok == "*") {
                sawWildcard = true;
                continue;
            }
            if (detail::IsStandaloneOperator(tok)) {
                if (i + 1 >= tokens.size() || tokens[i + 1] == "*") {
                    out = SemVerRange{};
                    return false;
                }
                if (!detail::AppendConstraintToken(tok, tokens[i + 1], true, bareIsMinimum, group)) {
                    out = SemVerRange{};
                    return false;
                }
                ++i;
                continue;
            }

            std::string_view opStr;
            std::string_view verStr = tok;
            bool hasOp = false;
            for (const std::string_view prefix : {">=", "<=", "!=", "=="}) {
                if (tok.size() >= prefix.size() && tok.substr(0, prefix.size()) == prefix) {
                    opStr = prefix;
                    verStr = tok.substr(prefix.size());
                    hasOp = true;
                    break;
                }
            }
            if (!hasOp) {
                const char c = tok.front();
                if (c == '>' || c == '<' || c == '=' || c == '^' || c == '~') {
                    opStr = tok.substr(0, 1);
                    verStr = tok.substr(1);
                    hasOp = true;
                }
            }

            if (!detail::AppendConstraintToken(opStr, verStr, hasOp, bareIsMinimum, group)) {
                out = SemVerRange{};
                return false;
            }
        }

        if (group.comparators.empty()) {
            if (!sawWildcard) {
                out = SemVerRange{};
                return false;
            }
            group.matchAny = true;
        }
        out.groups.push_back(std::move(group));
    }

    out.valid = !out.groups.empty();
    return out.valid;
}

inline bool SatisfiesSemVerRange(std::string_view versionStr, const SemVerRange& range, bool& versionValid) {
    SemVer ver;
    versionValid = ParseSemVer(versionStr, ver, /*allowTwoPart=*/true);
    if (!range.valid) return false;
    if (range.matchAll) return true;

    for (const auto& group : range.groups) {
        if (group.matchAny) return true;
        if (!versionValid) continue;

        bool allMatch = true;
        for (const auto& cmp : group.comparators) {
            bool ok = false;
            switch (cmp.op) {
                case SemVerComparator::Op::Eq: ok = CompareSemVer(ver, cmp.ver) == 0; break;
                case SemVerComparator::Op::Ne: ok = CompareSemVer(ver, cmp.ver) != 0; break;
                case SemVerComparator::Op::Gt: ok = CompareSemVer(ver, cmp.ver) > 0; break;
                case SemVerComparator::Op::Ge: ok = CompareSemVer(ver, cmp.ver) >= 0; break;
                case SemVerComparator::Op::Lt: ok = CompareSemVer(ver, cmp.ver) < 0; break;
                case SemVerComparator::Op::Le: ok = CompareSemVer(ver, cmp.ver) <= 0; break;
                case SemVerComparator::Op::LtUpperNoPre:
                    ok = std::tie(ver.major, ver.minor, ver.patch) <
                         std::tie(cmp.ver.major, cmp.ver.minor, cmp.ver.patch);
                    break;
            }
            if (!ok) {
                allMatch = false;
                break;
            }
        }
        if (allMatch) return true;
    }
    return false;
}

inline std::string SanitizeExternalUrl(const char* raw) {
    if (!raw) return {};
    const std::string_view trimmed = detail::TrimAscii(detail::SafeCopyCString(raw, 2048));
    if (trimmed.empty()) return {};

    for (char c : trimmed) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || uc == 0x7F || detail::IsAsciiSpace(c) || c == '\v' || c == '\f') {
            return {};
        }
    }

    if (!detail::IStartsWith(trimmed, "https://") && !detail::IStartsWith(trimmed, "http://")) {
        return {};
    }
    return std::string(trimmed);
}

inline std::string ResolveLocalizedSummary(
    const std::string& fallback,
    const std::vector<std::pair<std::string, std::string>>& localized,
    std::string_view langTag) {
    if (!langTag.empty()) {
        for (const auto& [tag, text] : localized) {
            if (!text.empty() && detail::IEquals(tag, langTag)) {
                return text;
            }
        }
        for (const auto& [tag, text] : localized) {
            if (tag.empty() || text.empty()) continue;
            if (langTag.size() > tag.size() && detail::IStartsWith(langTag, tag) && langTag[tag.size()] == '-') {
                return text;
            }
            if (tag.size() > langTag.size() && detail::IStartsWith(tag, langTag) && tag[langTag.size()] == '-') {
                return text;
            }
        }
    }
    return fallback;
}

inline bool ExtractMetadata(
    const GakuPluginMetadataV1& raw,
    LoadedPlugin& out,
    std::vector<std::string>& warnings) {
    if (raw.abiVersion != GAKU_METADATA_ABI_VERSION || raw.structSize < sizeof(GakuPluginMetadataV1)) {
        return false;
    }

    constexpr uint32_t kMaxArrayItems = 64;
    out.hasMetadata = true;
    out.originalAuthor = detail::SafeCopyCString(raw.originalAuthor, 256);

    out.maintainers.clear();
    if (raw.maintainers && raw.maintainerCount > 0) {
        if (raw.maintainerCount > kMaxArrayItems) {
            warnings.push_back("maintainerCount exceeds limit; truncated");
        }
        const uint32_t count = (std::min)(raw.maintainerCount, kMaxArrayItems);
        out.maintainers.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            std::string entry = detail::SafeCopyCString(raw.maintainers[i], 256);
            if (!entry.empty()) out.maintainers.push_back(std::move(entry));
        }
    }

    out.subVersion = detail::SafeCopyCString(raw.subVersion, 64);
    out.releaseChannel = detail::SafeCopyCString(raw.releaseChannel, 64);
    out.summary = detail::SafeCopyCString(raw.summary, 1024);

    out.localizedSummaries.clear();
    if (raw.localizedSummaries && raw.localizedSummaryCount > 0) {
        if (raw.localizedSummaryCount > kMaxArrayItems) {
            warnings.push_back("localizedSummaryCount exceeds limit; truncated");
        }
        const uint32_t count = (std::min)(raw.localizedSummaryCount, kMaxArrayItems);
        out.localizedSummaries.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            std::string tag = detail::SafeCopyCString(raw.localizedSummaries[i].languageTag, 64);
            std::string text = detail::SafeCopyCString(raw.localizedSummaries[i].text, 1024);
            if (!tag.empty() && !text.empty()) {
                out.localizedSummaries.emplace_back(std::move(tag), std::move(text));
            }
        }
    }

    auto sanitizeAndWarn = [&](const char* label, const char* rawUrl) -> std::string {
        const std::string rawCopy = detail::SafeCopyCString(rawUrl, 2048);
        std::string sanitized = SanitizeExternalUrl(rawUrl);
        if (!detail::TrimAscii(rawCopy).empty() && sanitized.empty()) {
            warnings.push_back(std::string("Rejected unsafe or malformed ") + label);
        }
        return sanitized;
    };
    out.repositoryUrl = sanitizeAndWarn("repositoryUrl", raw.repositoryUrl);
    out.releaseUrl = sanitizeAndWarn("releaseUrl", raw.releaseUrl);
    out.issuesUrl = sanitizeAndWarn("issuesUrl", raw.issuesUrl);

    out.license = detail::SafeCopyCString(raw.license, 128);
    out.minLoaderVersion = std::string(detail::TrimAscii(detail::SafeCopyCString(raw.minLoaderVersion, 128)));

    out.dependencies.clear();
    if (raw.dependencies && raw.dependencyCount > 0) {
        if (raw.dependencyCount > kMaxArrayItems) {
            warnings.push_back("dependencyCount exceeds limit; truncated");
        }
        const uint32_t count = (std::min)(raw.dependencyCount, kMaxArrayItems);
        out.dependencies.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& dep = raw.dependencies[i];
            std::string depId = detail::SafeCopyCString(dep.pluginId, 128);
            if (depId.empty()) {
                warnings.push_back("Ignored dependency entry with empty pluginId");
                continue;
            }
            if (dep.kind != GAKU_DEPENDENCY_REQUIRED &&
                dep.kind != GAKU_DEPENDENCY_OPTIONAL &&
                dep.kind != GAKU_DEPENDENCY_CONFLICT) {
                warnings.push_back("Ignored dependency '" + depId + "' with unknown kind");
                continue;
            }
            DesktopDependencyInfo info;
            info.kind = dep.kind;
            info.pluginId = std::move(depId);
            info.displayName = detail::SafeCopyCString(dep.displayName, 256);
            info.versionRange = detail::SafeCopyCString(dep.versionRange, 256);
            info.targetState = GAKU_PLUGIN_STATE_NOT_FOUND;
            info.satisfied = (dep.kind == GAKU_DEPENDENCY_OPTIONAL);
            out.dependencies.push_back(std::move(info));
        }
    }

    out.auxiliaryDlls.clear();
    if (raw.auxiliaryDlls && raw.auxiliaryDllCount > 0) {
        if (raw.auxiliaryDllCount > kMaxArrayItems) {
            warnings.push_back("auxiliaryDllCount exceeds limit; truncated");
        }
        const uint32_t count = (std::min)(raw.auxiliaryDllCount, kMaxArrayItems);
        out.auxiliaryDlls.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            std::string dll = detail::SafeCopyCString(raw.auxiliaryDlls[i], 260);
            if (!dll.empty()) out.auxiliaryDlls.push_back(std::move(dll));
        }
    }

    return true;
}

struct PluginCandidate {
    LoadedPlugin plugin;
    bool enabled{true};
};

namespace detail {

inline bool CanReach(
    size_t start,
    size_t target,
    const std::vector<std::vector<size_t>>& adj) {
    if (start == target) return true;
    std::vector<bool> visited(adj.size(), false);
    std::vector<size_t> stack{start};
    visited[start] = true;
    while (!stack.empty()) {
        const size_t u = stack.back();
        stack.pop_back();
        for (const size_t v : adj[u]) {
            if (v == target) return true;
            if (!visited[v]) {
                visited[v] = true;
                stack.push_back(v);
            }
        }
    }
    return false;
}

} // namespace detail

inline void ResolveAndSortPlugins(
    std::vector<PluginCandidate>& candidates,
    std::string_view loaderVersion,
    std::vector<std::string>& diagnostics) {
    diagnostics.clear();
    const size_t n = candidates.size();
    std::unordered_map<std::string, size_t> idToIndex;
    idToIndex.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        auto& cand = candidates[i];
        if (!cand.enabled) {
            cand.plugin.loadState = GAKU_PLUGIN_STATE_DISABLED;
            cand.plugin.activated = false;
            cand.plugin.failed = false;
        } else {
            cand.plugin.loadState = GAKU_PLUGIN_STATE_QUERIED;
            cand.plugin.activated = false;
            cand.plugin.failed = false;
            cand.plugin.statusReason.clear();
        }
        if (!cand.plugin.id.empty() && !idToIndex.count(cand.plugin.id)) {
            idToIndex.emplace(cand.plugin.id, i);
        }
    }

    // Step A: minLoaderVersion compatibility check
    for (size_t i = 0; i < n; ++i) {
        auto& cand = candidates[i];
        if (!cand.enabled || cand.plugin.minLoaderVersion.empty()) continue;

        SemVerRange range;
        if (!ParseSemVerRange(cand.plugin.minLoaderVersion, range, /*bareIsMinimum=*/true)) {
            cand.plugin.loadState = GAKU_PLUGIN_STATE_INCOMPATIBLE;
            cand.plugin.failed = true;
            cand.plugin.statusReason = "Invalid minLoaderVersion: " + cand.plugin.minLoaderVersion;
            diagnostics.push_back(cand.plugin.id + ": " + cand.plugin.statusReason);
            continue;
        }

        bool loaderValid = false;
        if (!SatisfiesSemVerRange(loaderVersion, range, loaderValid) || !loaderValid) {
            cand.plugin.loadState = GAKU_PLUGIN_STATE_INCOMPATIBLE;
            cand.plugin.failed = true;
            cand.plugin.statusReason = "Requires GakuLoader " + cand.plugin.minLoaderVersion +
                " (current " + std::string(loaderVersion) + ")";
            diagnostics.push_back(cand.plugin.id + ": " + cand.plugin.statusReason);
        }
    }

    // Step B: Conflict checks
    for (size_t i = 0; i < n; ++i) {
        auto& cand = candidates[i];
        if (!cand.enabled) continue;

        for (auto& dep : cand.plugin.dependencies) {
            if (dep.kind != GAKU_DEPENDENCY_CONFLICT) continue;
            const auto found = idToIndex.find(dep.pluginId);
            if (found == idToIndex.end()) {
                dep.targetState = GAKU_PLUGIN_STATE_NOT_FOUND;
                dep.actualVersion.clear();
                dep.satisfied = true;
                continue;
            }
            const auto& target = candidates[found->second];
            dep.targetState = target.plugin.loadState;
            dep.actualVersion = target.plugin.version;

            if (!target.enabled || target.plugin.failed) {
                dep.satisfied = true;
                continue;
            }

            SemVerRange range;
            if (!ParseSemVerRange(dep.versionRange, range, /*bareIsMinimum=*/false)) {
                dep.satisfied = false;
                dep.diagnostic = "Invalid conflict version range '" + dep.versionRange + "' for " + dep.pluginId;
                if (!cand.plugin.failed) {
                    cand.plugin.loadState = GAKU_PLUGIN_STATE_INCOMPATIBLE;
                    cand.plugin.failed = true;
                    cand.plugin.statusReason = dep.diagnostic;
                }
                diagnostics.push_back(cand.plugin.id + ": " + dep.diagnostic);
                continue;
            }

            bool verValid = false;
            if (SatisfiesSemVerRange(target.plugin.version, range, verValid)) {
                dep.satisfied = false;
                dep.diagnostic = "Conflicts with " + target.plugin.id +
                    (target.plugin.version.empty() ? std::string{} : " (" + target.plugin.version + ")");
                if (!cand.plugin.failed) {
                    cand.plugin.loadState = GAKU_PLUGIN_STATE_INCOMPATIBLE;
                    cand.plugin.failed = true;
                    cand.plugin.statusReason = dep.diagnostic;
                }
                diagnostics.push_back(cand.plugin.id + ": " + dep.diagnostic);
            } else {
                dep.satisfied = true;
            }
        }
    }

    // Step C: Iterative fixed-point validation of Required dependencies + Required-only cycle detection
    for (;;) {
        bool changed = false;

        for (size_t i = 0; i < n; ++i) {
            auto& cand = candidates[i];
            if (!cand.enabled || cand.plugin.failed) continue;

            std::string firstError;
            for (auto& dep : cand.plugin.dependencies) {
                if (dep.kind != GAKU_DEPENDENCY_REQUIRED) continue;
                const auto found = idToIndex.find(dep.pluginId);
                if (found == idToIndex.end()) {
                    dep.targetState = GAKU_PLUGIN_STATE_NOT_FOUND;
                    dep.actualVersion.clear();
                    dep.satisfied = false;
                    dep.diagnostic = "Missing required dependency: " + dep.pluginId;
                } else {
                    const auto& target = candidates[found->second];
                    dep.targetState = target.plugin.loadState;
                    dep.actualVersion = target.plugin.version;

                    if (!target.enabled) {
                        dep.satisfied = false;
                        dep.diagnostic = "Required dependency is disabled: " + dep.pluginId;
                    } else if (target.plugin.failed) {
                        dep.satisfied = false;
                        dep.diagnostic = "Required dependency failed: " + dep.pluginId;
                    } else {
                        SemVerRange range;
                        if (!ParseSemVerRange(dep.versionRange, range, /*bareIsMinimum=*/false)) {
                            dep.satisfied = false;
                            dep.diagnostic = "Invalid version range '" + dep.versionRange + "' for " + dep.pluginId;
                        } else {
                            bool verValid = false;
                            if (!SatisfiesSemVerRange(target.plugin.version, range, verValid)) {
                                dep.satisfied = false;
                                if (!verValid) {
                                    dep.diagnostic = "Required dependency " + dep.pluginId +
                                        " has invalid version '" + target.plugin.version + "'";
                                } else {
                                    dep.diagnostic = "Requires " + dep.pluginId + " " + dep.versionRange +
                                        " (found " + target.plugin.version + ")";
                                }
                            } else {
                                dep.satisfied = true;
                                dep.diagnostic.clear();
                            }
                        }
                    }
                }

                if (!dep.satisfied && firstError.empty()) {
                    firstError = dep.diagnostic;
                }
            }

            if (!firstError.empty()) {
                cand.plugin.loadState = GAKU_PLUGIN_STATE_DEPENDENCY_ERROR;
                cand.plugin.failed = true;
                cand.plugin.statusReason = firstError;
                diagnostics.push_back(cand.plugin.id + ": " + firstError);
                changed = true;
            }
        }

        // Detect cycles formed solely by Required edges (dep -> dependent) via Tarjan's SCC.
        std::vector<std::vector<size_t>> reqAdj(n);
        std::vector<bool> hasSelfReq(n, false);
        for (size_t i = 0; i < n; ++i) {
            if (!candidates[i].enabled || candidates[i].plugin.failed) continue;
            for (const auto& dep : candidates[i].plugin.dependencies) {
                if (dep.kind != GAKU_DEPENDENCY_REQUIRED) continue;
                const auto found = idToIndex.find(dep.pluginId);
                if (found == idToIndex.end()) continue;
                const size_t depIdx = found->second;
                if (!candidates[depIdx].enabled || candidates[depIdx].plugin.failed) continue;
                if (depIdx == i) {
                    hasSelfReq[i] = true;
                } else {
                    reqAdj[depIdx].push_back(i);
                }
            }
        }

        std::vector<int> index(n, -1);
        std::vector<int> lowlink(n, -1);
        std::vector<bool> onStack(n, false);
        std::vector<size_t> stack;
        std::vector<bool> inReqCycle(n, false);
        int currentIndex = 0;

        auto strongConnect = [&](auto& self, size_t v) -> void {
            index[v] = currentIndex;
            lowlink[v] = currentIndex;
            ++currentIndex;
            stack.push_back(v);
            onStack[v] = true;

            for (const size_t w : reqAdj[v]) {
                if (index[w] == -1) {
                    self(self, w);
                    lowlink[v] = (std::min)(lowlink[v], lowlink[w]);
                } else if (onStack[w]) {
                    lowlink[v] = (std::min)(lowlink[v], index[w]);
                }
            }

            if (lowlink[v] == index[v]) {
                std::vector<size_t> scc;
                for (;;) {
                    const size_t w = stack.back();
                    stack.pop_back();
                    onStack[w] = false;
                    scc.push_back(w);
                    if (w == v) break;
                }
                if (scc.size() > 1 || (scc.size() == 1 && hasSelfReq[scc[0]])) {
                    for (const size_t member : scc) inReqCycle[member] = true;
                }
            }
        };

        for (size_t i = 0; i < n; ++i) {
            if (candidates[i].enabled && !candidates[i].plugin.failed && index[i] == -1) {
                strongConnect(strongConnect, i);
            }
        }

        for (size_t i = 0; i < n; ++i) {
            if (!inReqCycle[i]) continue;
            auto& cand = candidates[i];
            cand.plugin.loadState = GAKU_PLUGIN_STATE_DEPENDENCY_ERROR;
            cand.plugin.failed = true;
            cand.plugin.statusReason = "Circular required dependency";
            for (auto& dep : cand.plugin.dependencies) {
                if (dep.kind != GAKU_DEPENDENCY_REQUIRED) continue;
                const auto found = idToIndex.find(dep.pluginId);
                if (found != idToIndex.end() && inReqCycle[found->second]) {
                    dep.satisfied = false;
                    dep.targetState = GAKU_PLUGIN_STATE_DEPENDENCY_ERROR;
                    dep.diagnostic = "Circular required dependency";
                }
            }
            diagnostics.push_back(cand.plugin.id + ": Circular required dependency");
            changed = true;
        }

        if (!changed) break;
    }

    // Step D: Validate Optional dependencies
    for (size_t i = 0; i < n; ++i) {
        auto& cand = candidates[i];
        if (!cand.enabled) continue;
        for (auto& dep : cand.plugin.dependencies) {
            if (dep.kind != GAKU_DEPENDENCY_OPTIONAL) continue;
            const auto found = idToIndex.find(dep.pluginId);
            if (found == idToIndex.end()) {
                dep.targetState = GAKU_PLUGIN_STATE_NOT_FOUND;
                dep.actualVersion.clear();
                dep.satisfied = true;
                continue;
            }
            const auto& target = candidates[found->second];
            dep.targetState = target.plugin.loadState;
            dep.actualVersion = target.plugin.version;
            if (!target.enabled || target.plugin.failed) {
                dep.satisfied = true;
                continue;
            }

            SemVerRange range;
            if (!ParseSemVerRange(dep.versionRange, range, /*bareIsMinimum=*/false)) {
                dep.satisfied = false;
                dep.diagnostic = "Invalid optional version range '" + dep.versionRange + "' for " + dep.pluginId;
                diagnostics.push_back(cand.plugin.id + ": " + dep.diagnostic);
                continue;
            }

            bool verValid = false;
            if (!SatisfiesSemVerRange(target.plugin.version, range, verValid)) {
                dep.satisfied = false;
                if (!verValid) {
                    dep.diagnostic = "Optional dependency " + dep.pluginId +
                        " has invalid version '" + target.plugin.version + "'";
                } else {
                    dep.diagnostic = "Optional dependency " + dep.pluginId + " expects " +
                        dep.versionRange + " (found " + target.plugin.version + ")";
                }
                diagnostics.push_back(cand.plugin.id + ": " + dep.diagnostic);
            } else {
                dep.satisfied = true;
                dep.diagnostic.clear();
            }
        }
    }

    // Refresh targetState on all dependency entries for surviving vs failed targets
    for (size_t i = 0; i < n; ++i) {
        for (auto& dep : candidates[i].plugin.dependencies) {
            const auto found = idToIndex.find(dep.pluginId);
            if (found != idToIndex.end()) {
                dep.targetState = candidates[found->second].plugin.loadState;
            }
        }
    }

    // Step E: Topological sort on surviving valid plugins (Kahn's with alphabetical tie-breaking)
    struct TopoEdge {
        size_t from{0};
        size_t to{0};
        size_t depIdx{0};
        bool isOptional{false};
        bool active{true};
    };

    std::vector<size_t> survivors;
    std::vector<bool> isSurvivor(n, false);
    for (size_t i = 0; i < n; ++i) {
        if (candidates[i].enabled && !candidates[i].plugin.failed) {
            isSurvivor[i] = true;
            survivors.push_back(i);
        }
    }

    std::vector<TopoEdge> edges;
    std::vector<size_t> inDegree(n, 0);

    for (const size_t v : survivors) {
        auto& deps = candidates[v].plugin.dependencies;
        // Track Required edges first so duplicate Optional edges on the same pair are ignored
        std::vector<bool> hasRequiredFrom(n, false);
        for (size_t d = 0; d < deps.size(); ++d) {
            if (deps[d].kind != GAKU_DEPENDENCY_REQUIRED) continue;
            const auto found = idToIndex.find(deps[d].pluginId);
            if (found == idToIndex.end()) continue;
            const size_t u = found->second;
            if (!isSurvivor[u] || u == v || hasRequiredFrom[u]) continue;
            hasRequiredFrom[u] = true;
            edges.push_back({u, v, d, false, true});
            ++inDegree[v];
        }

        std::vector<bool> hasOptionalFrom(n, false);
        for (size_t d = 0; d < deps.size(); ++d) {
            if (deps[d].kind != GAKU_DEPENDENCY_OPTIONAL || !deps[d].satisfied) continue;
            const auto found = idToIndex.find(deps[d].pluginId);
            if (found == idToIndex.end()) continue;
            const size_t u = found->second;
            if (!isSurvivor[u]) continue;
            if (u == v) {
                deps[d].cycleDegraded = true;
                deps[d].diagnostic = "Optional self-dependency cycle degraded";
                continue;
            }
            if (hasRequiredFrom[u] || hasOptionalFrom[u]) continue;
            hasOptionalFrom[u] = true;
            edges.push_back({u, v, d, true, true});
            ++inDegree[v];
        }
    }

    std::vector<bool> popped(n, false);
    std::vector<size_t> ordered;
    ordered.reserve(survivors.size());

    while (ordered.size() < survivors.size()) {
        size_t nextNode = n;
        for (const size_t v : survivors) {
            if (popped[v] || inDegree[v] != 0) continue;
            if (nextNode == n || candidates[v].plugin.id < candidates[nextNode].plugin.id) {
                nextNode = v;
            }
        }

        if (nextNode != n) {
            popped[nextNode] = true;
            ordered.push_back(nextNode);
            for (const auto& e : edges) {
                if (e.active && e.from == nextNode && !popped[e.to] && inDegree[e.to] > 0) {
                    --inDegree[e.to];
                }
            }
            continue;
        }

        // Cycle involving at least one Optional edge: build active subgraph of unpopped nodes
        std::vector<std::vector<size_t>> activeAdj(n);
        for (const auto& e : edges) {
            if (e.active && !popped[e.from] && !popped[e.to]) {
                activeAdj[e.from].push_back(e.to);
            }
        }

        size_t dropIdx = edges.size();
        for (size_t eIdx = 0; eIdx < edges.size(); ++eIdx) {
            const auto& e = edges[eIdx];
            if (!e.active || !e.isOptional || popped[e.from] || popped[e.to]) continue;
            if (!detail::CanReach(e.to, e.from, activeAdj)) continue;
            if (dropIdx == edges.size() ||
                candidates[e.to].plugin.id > candidates[edges[dropIdx].to].plugin.id ||
                (candidates[e.to].plugin.id == candidates[edges[dropIdx].to].plugin.id &&
                 candidates[e.from].plugin.id > candidates[edges[dropIdx].from].plugin.id)) {
                dropIdx = eIdx;
            }
        }

        // Defensive fallback if somehow no cycle-closed optional edge was picked
        if (dropIdx == edges.size()) {
            for (size_t eIdx = 0; eIdx < edges.size(); ++eIdx) {
                const auto& e = edges[eIdx];
                if (!e.active || !e.isOptional || popped[e.from] || popped[e.to]) continue;
                if (dropIdx == edges.size() ||
                    candidates[e.to].plugin.id > candidates[edges[dropIdx].to].plugin.id) {
                    dropIdx = eIdx;
                }
            }
        }

        if (dropIdx == edges.size()) {
            // Unreachable under acyclic Required subgraph, but ensure loop termination
            for (const size_t v : survivors) {
                if (!popped[v]) {
                    popped[v] = true;
                    ordered.push_back(v);
                }
            }
            break;
        }

        auto& dropped = edges[dropIdx];
        dropped.active = false;
        if (inDegree[dropped.to] > 0) --inDegree[dropped.to];
        auto& depInfo = candidates[dropped.to].plugin.dependencies[dropped.depIdx];
        depInfo.cycleDegraded = true;
        if (!depInfo.diagnostic.empty()) depInfo.diagnostic += "; ";
        depInfo.diagnostic += "Optional ordering degraded due to dependency cycle";
        diagnostics.push_back(candidates[dropped.to].plugin.id + ": optional edge from " +
            candidates[dropped.from].plugin.id + " degraded due to cycle");
    }

    // Reorder candidates: surviving plugins in topological order first, then failed/disabled
    std::vector<PluginCandidate> reordered;
    reordered.reserve(n);
    std::vector<bool> placed(n, false);
    for (const size_t idx : ordered) {
        reordered.push_back(std::move(candidates[idx]));
        placed[idx] = true;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!placed[i]) {
            reordered.push_back(std::move(candidates[i]));
        }
    }
    candidates = std::move(reordered);
}

} // namespace GakuLoader::Metadata
