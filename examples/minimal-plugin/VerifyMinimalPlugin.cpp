// Offline consumer of the real sample DLL; never starts or injects into a game.
#include "GakuPlugin.h"
#include <windows.h>
#include <cstring>
#include <iostream>
#include <string>

namespace {
std::string messages;
void Write(void*, GakuLogLevel, const char* message) {
    messages += message;
    messages += '\n';
    std::cout << message << '\n';
}
GakuStatus Query(void*, const char* name, uint32_t version, uint32_t size, void* output) {
    if (!name || !output) return GAKU_E_INVALID_ARGUMENT;
    if (std::strcmp(name, GAKU_SERVICE_LOG)) return GAKU_E_UNAVAILABLE;
    if (version != GAKU_SERVICE_VERSION) return GAKU_E_VERSION;
    if (size < sizeof(GakuLogService)) return GAKU_E_SIZE;
    const GakuLogService service{GAKU_SERVICE_VERSION, sizeof(GakuLogService), nullptr, Write};
    std::memcpy(output, &service, sizeof(service));
    return GAKU_OK;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "usage: verify-minimal-plugin <absolute DLL path>\n"; return 2; }
    const auto module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { std::cerr << "DLL load failed: " << GetLastError() << '\n'; return 3; }
    using QueryFn = GakuStatus (*)(uint32_t, uint32_t, GakuPluginDescriptor*);
    const auto query = reinterpret_cast<QueryFn>(GetProcAddress(module, "GakuPlugin_Query"));
    if (!query) { std::cerr << "GakuPlugin_Query missing\n"; return 4; }
    GakuPluginDescriptor sentinel{};
    std::memset(&sentinel, 0x5a, sizeof(sentinel));
    auto descriptor = sentinel;
    if (query(GAKU_ABI_VERSION + 1, sizeof(descriptor), &descriptor) != GAKU_E_VERSION ||
        std::memcmp(&descriptor, &sentinel, sizeof(descriptor))) return 5;
    if (query(GAKU_ABI_VERSION, sizeof(descriptor) - 1, &descriptor) != GAKU_E_SIZE ||
        std::memcmp(&descriptor, &sentinel, sizeof(descriptor))) return 6;
    if (query(GAKU_ABI_VERSION, sizeof(descriptor), &descriptor) != GAKU_OK ||
        descriptor.abiVersion != GAKU_ABI_VERSION || descriptor.structSize != sizeof(descriptor) ||
        !descriptor.id || std::strcmp(descriptor.id, "example.minimal") || !descriptor.activate ||
        !descriptor.onRuntimeReady || !descriptor.onTick || !descriptor.shutdown) return 7;
    GakuHostApi host{GAKU_ABI_VERSION, sizeof(GakuHostApi), nullptr, Query, nullptr};
    void* state = nullptr;
    if (descriptor.activate(&host, &state) != GAKU_OK) return 8;
    descriptor.onRuntimeReady(state);
    descriptor.onTick(state, 1.0f / 60.0f);
    descriptor.shutdown(state);
    if (messages != "example.minimal activated\nexample.minimal runtime ready\nexample.minimal shutdown\n") return 9;
    // Mirror the host lifetime contract: keep the module resident until process exit.
    std::cout << "PASS: ABI rejection preserves output; activation, ready, tick, shutdown completed\n";
    return 0;
}
