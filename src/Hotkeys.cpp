#include "Hotkeys.hpp"
#include "Host.hpp"
#include <windows.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cctype>

namespace GakuLoader::Hotkeys {
// Defined below the key tables; the conflict reporter needs it earlier.
std::string ComboName(uint32_t virtualKey, uint32_t modifiers);
namespace {
struct Binding {
    void* owner{nullptr};
    std::string pluginId, actionId, displayName;
    uint32_t virtualKey{0}, modifiers{0};
    GakuHotkeyFn callback{nullptr};
    void* userData{nullptr};
    bool down{false}, conflicted{false}, unbound{false};
};
// "pluginId.actionId" -> override. virtualKey 0 means explicitly unbound.
struct Override { uint32_t virtualKey{0}, modifiers{0}; };

std::mutex g_mutex;
std::vector<Binding> g_bindings;
std::map<std::string, Override> g_overrides;
bool g_managerDown{false};

// A collision is a configuration fact the user cannot see in-game: two owners
// claim one combo, so both bindings stop dispatching. Report every transition
// once, with the exact ini key to edit, instead of leaving a dead key behind.
std::string ConflictedKeyList(const std::vector<Binding>& bindings) {
    std::string result;
    for (const auto& binding : bindings) {
        if (!binding.conflicted) continue;
        if (!result.empty()) result += ", ";
        result += binding.pluginId + "." + binding.actionId;
        result += " (" + ComboName(binding.virtualKey, binding.modifiers) + ")";
    }
    return result;
}

bool EqualsIgnoreCase(const std::wstring& left, const wchar_t* right) {
    if (!right) return false;
    size_t index = 0;
    for (; index < left.size() && right[index]; ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) return false;
    }
    return index == left.size() && right[index] == L'\0';
}

uint32_t CurrentModifiers(const PollContext& context) {
    uint32_t modifiers = 0;
    if (context.isKeyDown(VK_CONTROL)) modifiers |= GAKU_HOTKEY_MOD_CTRL;
    if (context.isKeyDown(VK_SHIFT)) modifiers |= GAKU_HOTKEY_MOD_SHIFT;
    if (context.isKeyDown(VK_MENU)) modifiers |= GAKU_HOTKEY_MOD_ALT;
    if (context.isKeyDown(VK_LWIN) || context.isKeyDown(VK_RWIN)) modifiers |= GAKU_HOTKEY_MOD_WIN;
    return modifiers;
}

void RecomputeConflicts() {
    const std::string previous = ConflictedKeyList(g_bindings);
    // A combo claimed by more than one owner disables exactly those bindings.
    // Nothing is silently double-dispatched and no plugin is rejected.
    for (auto& binding : g_bindings) binding.conflicted = false;
    for (size_t left = 0; left < g_bindings.size(); ++left) {
        for (size_t right = left + 1; right < g_bindings.size(); ++right) {
            auto& a = g_bindings[left];
            auto& b = g_bindings[right];
            if (a.unbound || b.unbound || a.owner == b.owner) continue;
            if (a.virtualKey == b.virtualKey && a.modifiers == b.modifiers) {
                a.conflicted = b.conflicted = true;
            }
        }
    }
    const std::string current = ConflictedKeyList(g_bindings);
    if (current == previous) return;
    if (!current.empty()) {
        LogWarn("[Hotkeys] conflict: %s; conflicting bindings do not dispatch. "
            "Rebind one in gakuloader.ini [hotkeys] as pluginId.actionId=COMBO",
            current.c_str());
    } else {
        LogInfo("[Hotkeys] conflict cleared: %s dispatches again", previous.c_str());
    }
}

struct KeyName { const wchar_t* name; uint32_t virtualKey; };
const KeyName kNames[] = {
    {L"SPACE", VK_SPACE}, {L"TAB", VK_TAB}, {L"ENTER", VK_RETURN}, {L"RETURN", VK_RETURN},
    {L"ESC", VK_ESCAPE}, {L"ESCAPE", VK_ESCAPE}, {L"BACKSPACE", VK_BACK}, {L"INSERT", VK_INSERT},
    {L"DELETE", VK_DELETE}, {L"DEL", VK_DELETE}, {L"HOME", VK_HOME}, {L"END", VK_END},
    {L"PAGEUP", VK_PRIOR}, {L"PAGEDOWN", VK_NEXT}, {L"UP", VK_UP}, {L"DOWN", VK_DOWN},
    {L"LEFT", VK_LEFT}, {L"RIGHT", VK_RIGHT},
    {L"COMMA", VK_OEM_COMMA}, {L"PERIOD", VK_OEM_PERIOD}, {L"MINUS", VK_OEM_MINUS},
    {L"PLUS", VK_OEM_PLUS}, {L"EQUAL", VK_OEM_PLUS}, {L"SLASH", VK_OEM_2},
    {L"BACKSLASH", VK_OEM_5}, {L"SEMICOLON", VK_OEM_1}, {L"QUOTE", VK_OEM_7},
    {L"LEFTBRACKET", VK_OEM_4}, {L"RIGHTBRACKET", VK_OEM_6}, {L"GRAVE", VK_OEM_3},
    {L"TILDE", VK_OEM_3},
};

bool ParseKey(const std::wstring& token, uint32_t& virtualKey) {
    if (token.empty()) return false;
    if (token.size() == 1) {
        wchar_t character = static_cast<wchar_t>(std::towupper(token[0]));
        if ((character >= L'A' && character <= L'Z') || (character >= L'0' && character <= L'9')) {
            virtualKey = static_cast<uint32_t>(character);
            return true;
        }
    }
    if (token[0] == L'F' || token[0] == L'f') {
        int number = 0;
        bool digits = token.size() > 1;
        for (size_t index = 1; index < token.size() && digits; ++index) {
            if (token[index] < L'0' || token[index] > L'9') digits = false;
            else number = number * 10 + (token[index] - L'0');
        }
        if (digits && number >= 1 && number <= 24) {
            virtualKey = static_cast<uint32_t>(VK_F1 + number - 1);
            return true;
        }
    }
    for (const auto& entry : kNames) {
        if (EqualsIgnoreCase(token, entry.name)) { virtualKey = entry.virtualKey; return true; }
    }
    return false;
}
}

bool ParseCombo(const std::wstring& text, uint32_t& virtualKey, uint32_t& modifiers) {
    virtualKey = 0; modifiers = 0;
    std::wstring token;
    bool haveKey = false;
    for (size_t index = 0; index <= text.size(); ++index) {
        const wchar_t character = index < text.size() ? text[index] : L'+';
        if (character == L'+') {
            const size_t first = token.find_first_not_of(L" \t");
            const size_t last = token.find_last_not_of(L" \t");
            if (first != std::wstring::npos) {
                token = token.substr(first, last - first + 1);
                if (EqualsIgnoreCase(token, L"CTRL") || EqualsIgnoreCase(token, L"CONTROL")) {
                    modifiers |= GAKU_HOTKEY_MOD_CTRL;
                } else if (EqualsIgnoreCase(token, L"SHIFT")) {
                    modifiers |= GAKU_HOTKEY_MOD_SHIFT;
                } else if (EqualsIgnoreCase(token, L"ALT")) {
                    modifiers |= GAKU_HOTKEY_MOD_ALT;
                } else if (EqualsIgnoreCase(token, L"WIN")) {
                    modifiers |= GAKU_HOTKEY_MOD_WIN;
                } else if (!haveKey && ParseKey(token, virtualKey)) {
                    haveKey = true;
                } else {
                    return false;
                }
            }
            token.clear();
        } else {
            token.push_back(character);
        }
    }
    return haveKey;
}

std::string ComboName(uint32_t virtualKey, uint32_t modifiers) {
    if (!virtualKey) return "<unbound>";
    std::string name;
    if (modifiers & GAKU_HOTKEY_MOD_CTRL) name += "Ctrl+";
    if (modifiers & GAKU_HOTKEY_MOD_SHIFT) name += "Shift+";
    if (modifiers & GAKU_HOTKEY_MOD_ALT) name += "Alt+";
    if (modifiers & GAKU_HOTKEY_MOD_WIN) name += "Win+";
    if ((virtualKey >= 'A' && virtualKey <= 'Z') || (virtualKey >= '0' && virtualKey <= '9')) {
        name.push_back(static_cast<char>(virtualKey));
    } else if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        name += "F" + std::to_string(virtualKey - VK_F1 + 1);
    } else {
        for (const auto& entry : kNames) {
            if (entry.virtualKey == virtualKey && std::wcslen(entry.name) > 2) {
                for (const wchar_t* character = entry.name; *character; ++character) {
                    name.push_back(static_cast<char>(*character));
                }
                return name;
            }
        }
        name += "VK" + std::to_string(virtualKey);
    }
    return name;
}

void LoadOverrides(const std::filesystem::path& config) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_overrides.clear();
    std::vector<wchar_t> buffer(8192, L'\0');
    const DWORD length = GetPrivateProfileSectionW(L"hotkeys", buffer.data(),
        static_cast<DWORD>(buffer.size()), config.c_str());
    if (!length) return;
    for (const wchar_t* line = buffer.data(); *line; line += std::wcslen(line) + 1) {
        const wchar_t* separator = std::wcschr(line, L'=');
        if (!separator) continue;
        std::wstring key(line, separator - line);
        const wchar_t* rawValue = separator + 1;
        while (*rawValue == L' ' || *rawValue == L'\t') ++rawValue;
        const size_t last = std::wcslen(rawValue);
        std::wstring value(rawValue, last);
        while (!value.empty() && (value.back() == L' ' || value.back() == L'\t' || value.back() == L'\r'))
            value.pop_back();
        // Plugin ids and action ids are ASCII; normalise to a lowercase narrow
        // key so the ini value matches regardless of case.
        std::string qualified;
        for (wchar_t character : key) {
            if (character > 0x7f) { qualified.clear(); break; }
            qualified.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
        if (qualified.empty()) continue;
        Override override{};
        if (!value.empty() && !ParseCombo(value, override.virtualKey, override.modifiers)) {
            // An unparsable override must not silently unbind a working hotkey.
            continue;
        }
        g_overrides[qualified] = override;
        LogInfo("[Hotkeys] override %s = %s", qualified.c_str(),
            ComboName(override.virtualKey, override.modifiers).c_str());
    }
}

GakuStatus Register(void* owner, const char* pluginId, const GakuHotkeyBinding* binding) {
    if (!owner || !binding || !pluginId || !pluginId[0]) return GAKU_E_INVALID_ARGUMENT;
    if (binding->abiVersion != GAKU_SERVICE_VERSION || binding->structSize != sizeof(*binding))
        return GAKU_E_VERSION;
    if (!binding->actionId || !binding->actionId[0] || std::strlen(binding->actionId) > 127)
        return GAKU_E_INVALID_ARGUMENT;
    uint32_t virtualKey = binding->virtualKey, modifiers = binding->modifiers;
    const std::string qualified = std::string(pluginId) + "." + binding->actionId;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto found = g_overrides.find(qualified);
        if (found != g_overrides.end()) { virtualKey = found->second.virtualKey; modifiers = found->second.modifiers; }
    }
    // F1 belongs to the manager. A plugin cannot take it, and the refusal is
    // scoped to this binding instead of the whole plugin. virtualKey 0 means an
    // explicit [hotkeys] unbind, which is accepted and never dispatches.
    if (virtualKey == kManagerKey) return GAKU_E_CONFLICT;
    if (!virtualKey && !binding->virtualKey) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& existing : g_bindings) {
        if (existing.owner != owner || existing.actionId != binding->actionId) continue;
        existing.displayName = binding->displayName ? binding->displayName : "";
        existing.virtualKey = virtualKey;
        existing.modifiers = modifiers;
        existing.callback = binding->callback;
        existing.userData = binding->userData;
        existing.unbound = virtualKey == 0;
        LogInfo("[Hotkeys] %s bound to %s", qualified.c_str(), ComboName(virtualKey, modifiers).c_str());
        RecomputeConflicts();
        return GAKU_OK;
    }
    Binding entry;
    entry.owner = owner;
    entry.pluginId = pluginId;
    entry.actionId = binding->actionId;
    entry.displayName = binding->displayName ? binding->displayName : binding->actionId;
    entry.virtualKey = virtualKey;
    entry.modifiers = modifiers;
    entry.callback = binding->callback;
    entry.userData = binding->userData;
    entry.unbound = virtualKey == 0;
    entry.down = entry.unbound ? false : ((GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0);
    g_bindings.push_back(std::move(entry));
    LogInfo("[Hotkeys] %s bound to %s", qualified.c_str(), ComboName(virtualKey, modifiers).c_str());
    RecomputeConflicts();
    return GAKU_OK;
}

GakuStatus Unregister(void* owner, const char* actionId) {
    if (!owner || !actionId) return GAKU_E_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto found = std::find_if(g_bindings.begin(), g_bindings.end(), [&](const Binding& binding) {
        return binding.owner == owner && binding.actionId == actionId;
    });
    if (found == g_bindings.end()) return GAKU_E_UNAVAILABLE;
    g_bindings.erase(found);
    RecomputeConflicts();
    return GAKU_OK;
}

void RetireOwner(void* owner) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_bindings.erase(std::remove_if(g_bindings.begin(), g_bindings.end(), [owner](const Binding& binding) {
        return binding.owner == owner;
    }), g_bindings.end());
    RecomputeConflicts();
}

void Poll(const PollContext& context, PollResult& output) {
    output.actions.clear();
    output.managerToggle = false;
    if (!context.isKeyDown) return;
    bool manager = false;
    std::vector<Action> pending;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // F1 is tracked even when another window is in front, so returning to
        // the game never replays a press that happened elsewhere.
        const bool managerDown = context.isKeyDown(kManagerKey);
        if (managerDown && !g_managerDown && context.foregroundOwnedByThisProcess) manager = true;
        g_managerDown = managerDown;
        const uint32_t modifiers = CurrentModifiers(context);
        const bool suppress = !context.foregroundOwnedByThisProcess || context.hostWindowWantsTextInput;
        for (auto& binding : g_bindings) {
            if (binding.unbound) { binding.down = false; continue; }
            const bool down = context.isKeyDown(binding.virtualKey);
            const bool edge = down && !binding.down;
            binding.down = down;
            if (!edge || suppress || binding.conflicted || !binding.callback) continue;
            if (modifiers != binding.modifiers) continue;
            pending.push_back({binding.owner, binding.callback, binding.userData});
        }
    }
    output.managerToggle = manager;
    output.actions = std::move(pending);
}

std::vector<Row> Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<Row> rows;
    rows.reserve(g_bindings.size());
    for (const auto& binding : g_bindings) {
        rows.push_back({binding.pluginId, binding.actionId, binding.displayName,
            ComboName(binding.virtualKey, binding.modifiers), binding.virtualKey, binding.modifiers,
            binding.conflicted, binding.unbound});
    }
    return rows;
}
}
