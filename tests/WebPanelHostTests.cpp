#include "WebView2Ui.hpp"
#include "DxgiAdapter.hpp"
#include <wrl/implements.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
int g_checks = 0;
void Check(bool condition, const char* message) {
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}

int main() {
    using namespace GakuLoader;
    WebView2Ui::TestHooks hooks;
    int envCalls = 0, initializeCalls = 0, visibleCalls = 0, boundsCalls = 0, dpiCalls = 0;
    int focusCalls = 0, postCalls = 0, destroyPanelCalls = 0, shutdownCalls = 0;
    int docScriptCalls = 0, execScriptCalls = 0;
    RECT lastBounds{};
    float lastDpi = 0.0f;
    std::string lastPanel, lastDocScript, lastExecScript;
    bool lastVisible = false;
    hooks.ensureEnvironment = [&](const WebView2Ui::EnvironmentConfig&) { ++envCalls; };
    hooks.initializePanel = [&](const std::string& panelId, const WebView2Ui::PanelConfig& config) {
        ++initializeCalls;
        lastPanel = panelId;
        Check(config.entryPath == L"index.html", "initialize entry path");
    };
    hooks.visible = [&](const std::string& panelId, bool visible) {
        ++visibleCalls;
        lastPanel = panelId;
        lastVisible = visible;
    };
    hooks.bounds = [&](const std::string& panelId, const RECT& bounds) {
        ++boundsCalls;
        lastPanel = panelId;
        lastBounds = bounds;
    };
    hooks.dpi = [&](float scale) { ++dpiCalls; lastDpi = scale; };
    hooks.focus = [&](const std::string& panelId) { ++focusCalls; lastPanel = panelId; };
    hooks.postMessage = [&](const std::string& panelId, const std::string& json) {
        ++postCalls;
        lastPanel = panelId;
        Check(json == "{}", "post message payload");
    };
    hooks.documentScript = [&](const std::string& panelId, const std::string& script) {
        ++docScriptCalls;
        lastPanel = panelId;
        lastDocScript = script;
    };
    hooks.executeScript = [&](const std::string& panelId, const std::string& script) {
        ++execScriptCalls;
        lastPanel = panelId;
        lastExecScript = script;
    };
    hooks.destroyPanel = [&](const std::string& panelId) { ++destroyPanelCalls; lastPanel = panelId; };
    hooks.shutdown = [&] { ++shutdownCalls; };
    WebView2Ui::ConfigureTest(hooks);

    bool readyA = false, documentA = false, focusedA = false, textInputA = false, tabLeftA = false;
    bool readyB = false, documentB = false;
    std::string failureA, failureB;
    WebView2Ui::Callbacks callbacksA;
    callbacksA.onReady = [&](bool ok, const std::string& diagnostic) { readyA = ok; if (!ok) failureA = diagnostic; };
    callbacksA.onDocumentReady = [&] { documentA = true; };
    callbacksA.onFocusChanged = [&](bool value) { focusedA = value; };
    callbacksA.onTextInputChanged = [&](bool value) { textInputA = value; };
    callbacksA.onTabLeave = [&](bool) { tabLeftA = true; };

    WebView2Ui::Callbacks callbacksB;
    callbacksB.onReady = [&](bool ok, const std::string& diagnostic) { readyB = ok; if (!ok) failureB = diagnostic; };
    callbacksB.onDocumentReady = [&] { documentB = true; };

    WebView2Ui::EnvironmentConfig envConfig;
    envConfig.userDataDir = L"synthetic-shared-user-data";

    WebView2Ui::PanelConfig configA;
    configA.assetsDir = L"C:\\synthetic-assets-a";
    configA.virtualHost = L"panel-a.invalid";
    configA.entryPath = L"index.html";
    configA.dpiScale = 1.0f;

    WebView2Ui::PanelConfig configB;
    configB.assetsDir = L"C:\\synthetic-assets-b";
    configB.virtualHost = L"panel-b.invalid";
    configB.entryPath = L"index.html";
    configB.dpiScale = 1.0f;

    const HWND parent = reinterpret_cast<HWND>(uintptr_t{1});
    Check(WebView2Ui::InitializePanel("plugin.a", parent, envConfig, configA, callbacksA), "synthetic initialize A");
    Check(WebView2Ui::InitializePanel("plugin.b", parent, envConfig, configB, callbacksB), "synthetic initialize B");
    Check(envCalls == 1, "shared environment created once for two panels");
    Check(initializeCalls == 2, "both panels initialized");
    Check(docScriptCalls == 2, "typography guidance injected on document created for both panels");
    Check(readyA && documentA && WebView2Ui::IsReady("plugin.a") && WebView2Ui::IsDocumentReady("plugin.a"), "panel A ready");
    Check(readyB && documentB && WebView2Ui::IsReady("plugin.b") && WebView2Ui::IsDocumentReady("plugin.b"), "panel B ready");

    WebView2Ui::SetBounds("plugin.a", RECT{10, 20, 300, 400});
    Check(boundsCalls == 1 && lastPanel == "plugin.a" && lastBounds.left == 10 && lastBounds.right == 300, "bounds hook");
    WebView2Ui::SetVisible("plugin.a", true);
    Check(visibleCalls == 1 && lastVisible && WebView2Ui::IsVisible("plugin.a") && !WebView2Ui::IsVisible("plugin.b"), "visible hook isolated");
    WebView2Ui::SetDpiScale(1.5f);
    Check(dpiCalls == 1 && lastDpi == 1.5f, "dpi hook");
    WebView2Ui::Focus("plugin.a");
    Check(focusCalls == 1 && lastPanel == "plugin.a", "focus hook");
    WebView2Ui::SimulateFocusForTest("plugin.a", true);
    Check(focusedA && textInputA, "focus callback");
    WebView2Ui::SimulateTabLeaveForTest("plugin.a", true);
    Check(tabLeftA && !focusedA && !textInputA, "tab leave callback");
    WebView2Ui::PostMessageJson("plugin.b", "{}");
    Check(postCalls == 1 && lastPanel == "plugin.b", "post message hook isolated to B");

    const std::string guidanceScript = WebView2Ui::BuildTypographyGuidanceScript({1, 1.25f, 1.5f, 3});
    for (const char* token : {
        "--gaku-font-sans", "Microsoft YaHei UI",
        "--gaku-font-mono", "Cascadia Mono",
        "--gaku-ui-scale", "'--gaku-ui-scale':'1.25'",
        "--gaku-text-micro", "11px",
        "--gaku-text-xs", "12px",
        "--gaku-text-sm", "13px",
        "--gaku-text-base", "14px",
        "--gaku-text-md", "15px",
        "--gaku-text-lg", "18px",
        "--gaku-text-title", "22px",
        "--gaku-leading-tight", "1.35",
        "--gaku-leading-base", "1.5",
        "--gaku-control-height", "34px",
        "--gaku-control-height-sm", "28px",
        "--gaku-row-height", "56px",
        "--gaku-radius-panel", "0px",
        "window.__GAKU_UI_GUIDANCE__", "version:1", "uiScale:1.25", "dpiScale:1.5", "theme:3", "apple-light",
        "typography:", "metrics:", "getGakuUiGuidance", "gaku:ui-guidance-changed"
    }) {
        Check(guidanceScript.find(token) != std::string::npos, token);
    }
    const int docBeforeGuidance = docScriptCalls;
    const int execBeforeGuidance = execScriptCalls;
    WebView2Ui::SetUiGuidance({1, 1.25f, 1.5f, 3});
    Check(docScriptCalls == docBeforeGuidance + 2, "SetUiGuidance updates document-created scripts for both panels");
    Check(execScriptCalls == execBeforeGuidance + 2, "SetUiGuidance executes live update on both ready panels");
    Check(lastExecScript.find("'--gaku-ui-scale':'1.25'") != std::string::npos, "live guidance carries uiScale");
    Check(WebView2Ui::GetUiGuidance().theme == 3 && WebView2Ui::GetUiGuidance().uiScale == 1.25f, "GetUiGuidance reflects latest guidance");

    // Single-panel renderer failure must not break sibling panels or the shared environment.
    WebView2Ui::SimulateProcessFailedForTest("plugin.a", "renderer gone", false);
    Check(!readyA && !WebView2Ui::IsReady("plugin.a") && !WebView2Ui::IsVisible("plugin.a"), "panel A process failure fallback");
    Check(failureA == "renderer gone", "panel A failure diagnostic");
    Check(readyB && WebView2Ui::IsReady("plugin.b") && WebView2Ui::IsEnvironmentReady(), "panel B and shared env unaffected");

    WebView2Ui::DestroyPanel("plugin.a");
    Check(destroyPanelCalls == 1 && !WebView2Ui::HasPanel("plugin.a") && WebView2Ui::HasPanel("plugin.b"), "destroy single panel keeps B");
    Check(WebView2Ui::IsEnvironmentReady(), "destroy single panel keeps shared environment");

    WebView2Ui::Shutdown();
    Check(shutdownCalls >= 1 && !WebView2Ui::HasPanel("plugin.b") && !WebView2Ui::IsEnvironmentReady(), "shutdown releases all");

    hooks.failDocument = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(WebView2Ui::InitializePanel("plugin.a", parent, envConfig, configA, callbacksA), "initialize with failed document");
    Check(WebView2Ui::IsReady("plugin.a") && !WebView2Ui::IsDocumentReady("plugin.a"), "document failure state");
    WebView2Ui::SetVisible("plugin.a", true);
    Check(!WebView2Ui::IsVisible("plugin.a"), "hidden until document ready");
    const int execBeforeUndoc = execScriptCalls;
    WebView2Ui::SetUiGuidance({1, 1.1f, 1.0f, 4});
    Check(execScriptCalls == execBeforeUndoc, "SetUiGuidance skips ExecuteScript before document ready");
    WebView2Ui::Shutdown();

    hooks.failDocument = false;
    WebView2Ui::ConfigureTest(hooks);

    WebView2Ui::PanelConfig badConfig = configA;
    badConfig.assetsDir.clear();
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir", parent, envConfig, badConfig, callbacksA),
        "reject empty assetsDir");
    Check(!WebView2Ui::LastDiagnostic("plugin.bad.dir").empty(), "empty assetsDir diagnostic");

    badConfig = configA;
    badConfig.virtualHost = L"bad/host";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.slash", parent, envConfig, badConfig, callbacksA),
        "reject virtualHost with slash");

    badConfig = configA;
    badConfig.virtualHost = L"bad:host";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.port", parent, envConfig, badConfig, callbacksA),
        "reject virtualHost with colon");

    badConfig = configA;
    badConfig.entryPath = L"C:\\index.html";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.abs", parent, envConfig, badConfig, callbacksA),
        "reject absolute entryPath");

    badConfig = configA;
    badConfig.entryPath = L"..\\escape.html";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.rel", parent, envConfig, badConfig, callbacksA),
        "reject parent traversal entryPath");

    badConfig = configA;
    badConfig.entryPath = L"C:index.html";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.drive", parent, envConfig, badConfig, callbacksA),
        "reject drive-relative entryPath");

    badConfig = configA;
    badConfig.entryPath = L"index.html?preview=1";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.query", parent, envConfig, badConfig, callbacksA),
        "reject query string in entryPath");

    badConfig = configA;
    badConfig.virtualHost = L".bad.invalid";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.dot", parent, envConfig, badConfig, callbacksA),
        "reject leading dot virtualHost");

    badConfig = configA;
    badConfig.virtualHost = L"bad.invalid.";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.trail", parent, envConfig, badConfig, callbacksA),
        "reject trailing dot virtualHost");

    badConfig = configA;
    badConfig.virtualHost = L"bad..host.invalid";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.dotdot", parent, envConfig, badConfig, callbacksA),
        "reject consecutive dots in virtualHost");

    badConfig = configA;
    badConfig.virtualHost = L"bad%host.invalid";
    Check(!WebView2Ui::InitializePanel("plugin.bad.host.percent", parent, envConfig, badConfig, callbacksA),
        "reject percent in virtualHost");

    badConfig = configA;
    badConfig.entryPath = L".";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.dot", parent, envConfig, badConfig, callbacksA),
        "reject current-dir entryPath");

    badConfig = configA;
    badConfig.entryPath = L"sub\\.";
    Check(!WebView2Ui::InitializePanel("plugin.bad.entry.subdot", parent, envConfig, badConfig, callbacksA),
        "reject trailing dot segment in entryPath");

    badConfig = configA;
    badConfig.assetsDir = L"relative-assets";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.rel", parent, envConfig, badConfig, callbacksA),
        "reject relative assetsDir");
    Check(!WebView2Ui::LastDiagnostic("plugin.bad.dir.rel").empty(), "relative assetsDir diagnostic");

    badConfig = configA;
    badConfig.assetsDir = L"C:drive-relative";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.drive", parent, envConfig, badConfig, callbacksA),
        "reject drive-relative assetsDir");

    badConfig = configA;
    badConfig.assetsDir = L"\\root-relative";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.root", parent, envConfig, badConfig, callbacksA),
        "reject root-relative assetsDir");

    badConfig = configA;
    badConfig.assetsDir = L"C:\\synthetic\\..\\escape";
    Check(!WebView2Ui::InitializePanel("plugin.bad.dir.dotdot", parent, envConfig, badConfig, callbacksA),
        "reject parent traversal segment in assetsDir");
    WebView2Ui::Shutdown();

    hooks.failInitialize = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(!WebView2Ui::InitializePanel("plugin.a", parent, envConfig, configA, callbacksA), "initialize failure");
    Check(!WebView2Ui::LastDiagnostic("plugin.a").empty(), "initialize failure diagnostic");
    WebView2Ui::Shutdown();

    hooks.failInitialize = false;
    hooks.failEnvironment = true;
    WebView2Ui::ConfigureTest(hooks);
    Check(!WebView2Ui::EnsureEnvironment(envConfig), "environment failure");
    Check(WebView2Ui::HasEnvironmentFailed() && !WebView2Ui::LastEnvironmentDiagnostic().empty(), "environment diagnostic");
    WebView2Ui::Shutdown();

    // ---- Pure-software simulated second DXGI adapter & WebView2 GPU alignment tests ----
    {
        using Microsoft::WRL::ComPtr;
        using Microsoft::WRL::Make;
        using Microsoft::WRL::RuntimeClass;
        using Microsoft::WRL::RuntimeClassFlags;
        using Microsoft::WRL::ClassicCom;
        using namespace GakuLoader::DxgiAdapter;

        class FakeDxgiAdapter1 final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDXGIAdapter1> {
            DXGI_ADAPTER_DESC1 desc_{};
        public:
            explicit FakeDxgiAdapter1(const DXGI_ADAPTER_DESC1& desc) : desc_(desc) {}
            HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) override { return E_NOINTERFACE; }
            HRESULT STDMETHODCALLTYPE EnumOutputs(UINT, IDXGIOutput**) override { return DXGI_ERROR_NOT_FOUND; }
            HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* out) override {
                if (!out) return E_INVALIDARG;
                std::memset(out, 0, sizeof(*out));
                std::wcsncpy(out->Description, desc_.Description, 128);
                out->VendorId = desc_.VendorId;
                out->DeviceId = desc_.DeviceId;
                out->SubSysId = desc_.SubSysId;
                out->Revision = desc_.Revision;
                out->DedicatedVideoMemory = desc_.DedicatedVideoMemory;
                out->DedicatedSystemMemory = desc_.DedicatedSystemMemory;
                out->SharedSystemMemory = desc_.SharedSystemMemory;
                out->AdapterLuid = desc_.AdapterLuid;
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID, LARGE_INTEGER*) override { return DXGI_ERROR_UNSUPPORTED; }
            HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* out) override {
                if (!out) return E_INVALIDARG;
                *out = desc_;
                return S_OK;
            }
        };

        class FakeDxgiFactory6 final : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IDXGIFactory6> {
            std::vector<ComPtr<IDXGIAdapter1>> adapters_;
            std::vector<size_t> highPerfOrder_;
            bool exposeFactory6_ = true;
        public:
            FakeDxgiFactory6(std::vector<ComPtr<IDXGIAdapter1>> adapters, std::vector<size_t> highPerfOrder, bool exposeFactory6)
                : adapters_(std::move(adapters)), highPerfOrder_(std::move(highPerfOrder)), exposeFactory6_(exposeFactory6) {}

            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
                if (!exposeFactory6_ && riid == __uuidof(IDXGIFactory6)) {
                    *ppv = nullptr;
                    return E_NOINTERFACE;
                }
                return RuntimeClass::QueryInterface(riid, ppv);
            }
            HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, UINT, const void*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID, const IUnknown*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, UINT*, void*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE GetParent(REFIID, void**) override { return E_NOINTERFACE; }
            HRESULT STDMETHODCALLTYPE EnumAdapters(UINT, IDXGIAdapter**) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND, UINT) override { return S_OK; }
            HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE, IDXGIAdapter**) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT index, IDXGIAdapter1** out) override {
                if (!out) return E_INVALIDARG;
                if (index >= adapters_.size()) { *out = nullptr; return DXGI_ERROR_NOT_FOUND; }
                *out = adapters_[index].Get();
                (*out)->AddRef();
                return S_OK;
            }
            BOOL STDMETHODCALLTYPE IsCurrent() override { return TRUE; }
            BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override { return FALSE; }
            HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE, LUID*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND, UINT, DWORD*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE, DWORD*) override { return E_NOTIMPL; }
            void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD) override {}
            HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND, UINT, DWORD*) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE, DWORD*) override { return E_NOTIMPL; }
            void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD) override {}
            HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**) override { return E_NOTIMPL; }
            UINT STDMETHODCALLTYPE GetCreationFlags() override { return 0; }
            HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID, REFIID, void**) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID, void**) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE, void*, UINT) override { return E_NOTIMPL; }
            HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT index, DXGI_GPU_PREFERENCE pref, REFIID riid, void** out) override {
                if (!out) return E_INVALIDARG;
                if (pref != DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE || index >= highPerfOrder_.size()) {
                    *out = nullptr;
                    return DXGI_ERROR_NOT_FOUND;
                }
                return adapters_[highPerfOrder_[index]].CopyTo(riid, out);
            }
        };

        auto makeDesc = [](const wchar_t* name, UINT vendor, UINT device, SIZE_T vramMb, DWORD lowLuid, UINT flags) {
            DXGI_ADAPTER_DESC1 d{};
            std::wcsncpy(d.Description, name, 127);
            d.VendorId = vendor;
            d.DeviceId = device;
            d.DedicatedVideoMemory = vramMb * 1024ull * 1024ull;
            d.AdapterLuid = LUID{lowLuid, 0};
            d.Flags = flags;
            return d;
        };

        ComPtr<IDXGIAdapter1> igpu = Make<FakeDxgiAdapter1>(makeDesc(L"AMD Radeon(TM) 610M (Simulated)", 0x1002, 0x164E, 486, 0x1111, 0));
        ComPtr<IDXGIAdapter1> dgpu = Make<FakeDxgiAdapter1>(makeDesc(L"NVIDIA GeForce RTX 4060 Laptop GPU (Simulated)", 0x10DE, 0x28E0, 8192, 0x2222, 0));
        ComPtr<IDXGIAdapter1> sw = Make<FakeDxgiAdapter1>(makeDesc(L"Microsoft Basic Render Driver (Simulated)", 0x1414, 0x008C, 0, 0x3333, DXGI_ADAPTER_FLAG_SOFTWARE));

        // Case 1: Dual-GPU laptop (iGPU at index 0, dGPU at index 1, WARP at index 2).
        // IDXGIFactory6 HIGH_PERFORMANCE returns dGPU first -> WebView2 aligns to 0,8738 (0x2222).
        ComPtr<FakeDxgiFactory6> dualFactory = Make<FakeDxgiFactory6>(
            std::vector<ComPtr<IDXGIAdapter1>>{igpu, dgpu, sw},
            std::vector<size_t>{1, 0, 2},
            true);
        AdapterInfo chosen;
        Check(SelectGameAlignedAdapter(dualFactory.Get(), nullptr, chosen), "dual-gpu selection succeeds");
        Check(chosen.valid && !chosen.isSoftware && chosen.vendorId == 0x10DE && chosen.luid.LowPart == 0x2222,
            "dual-gpu selects discrete RTX 4060 over iGPU index 0");
        Check(chosen.source == "dxgi-high-performance", "dual-gpu selection source is dxgi-high-performance");

        WebView2Ui::EnvironmentConfig gpuEnv = envConfig;
        gpuEnv.hasAdapterLuid = true;
        gpuEnv.adapterIsSoftware = chosen.isSoftware;
        gpuEnv.adapterLuid = chosen.luid;
        gpuEnv.devToolsEnabled = false;
        Check(WebView2Ui::BuildAdditionalBrowserArguments(gpuEnv) == L"--use-angle=d3d11 --use-adapter-luid=0,8738",
            "WebView2 args pin ANGLE D3D11 to discrete GPU LUID");

        gpuEnv.devToolsEnabled = true;
        Check(WebView2Ui::BuildAdditionalBrowserArguments(gpuEnv) ==
            L"--use-angle=d3d11 --use-adapter-luid=0,8738 --auto-open-devtools-for-tabs",
            "WebView2 args preserve devtools switch alongside GPU LUID");

        gpuEnv.alignGpuAdapter = false;
        Check(WebView2Ui::BuildAdditionalBrowserArguments(gpuEnv) == L"--auto-open-devtools-for-tabs",
            "alignGpuAdapter=false omits GPU pinning args");

        // Case 2: Windows Graphics Settings forced game to iGPU (HIGH_PERFORMANCE returns iGPU first).
        ComPtr<FakeDxgiFactory6> igpuOverrideFactory = Make<FakeDxgiFactory6>(
            std::vector<ComPtr<IDXGIAdapter1>>{igpu, dgpu, sw},
            std::vector<size_t>{0, 1, 2},
            true);
        Check(SelectGameAlignedAdapter(igpuOverrideFactory.Get(), nullptr, chosen), "igpu-override selection succeeds");
        Check(chosen.vendorId == 0x1002 && chosen.luid.LowPart == 0x1111,
            "honors Windows Graphics Settings override to iGPU");

        // Case 3: No IDXGIFactory6 available; host D3D11 device hint wins, else max VRAM hardware adapter.
        ComPtr<FakeDxgiFactory6> legacyFactory = Make<FakeDxgiFactory6>(
            std::vector<ComPtr<IDXGIAdapter1>>{igpu, dgpu, sw},
            std::vector<size_t>{},
            false);
        const LUID igpuLuid{0x1111, 0};
        Check(SelectGameAlignedAdapter(legacyFactory.Get(), &igpuLuid, chosen) &&
            chosen.luid.LowPart == 0x1111 && chosen.source == "host-d3d11",
            "legacy factory aligns to host D3D11 device LUID");
        Check(SelectGameAlignedAdapter(legacyFactory.Get(), nullptr, chosen) &&
            chosen.luid.LowPart == 0x2222 && chosen.source == "dxgi-max-vram",
            "legacy factory without hint picks largest VRAM hardware adapter");

        // Case 4: Software adapter at index 0 is skipped when a hardware second adapter exists.
        ComPtr<FakeDxgiFactory6> swFirstFactory = Make<FakeDxgiFactory6>(
            std::vector<ComPtr<IDXGIAdapter1>>{sw, igpu},
            std::vector<size_t>{0, 1},
            true);
        Check(SelectGameAlignedAdapter(swFirstFactory.Get(), nullptr, chosen) &&
            !chosen.isSoftware && chosen.luid.LowPart == 0x1111,
            "skips software adapter at index 0 in favor of hardware adapter");

        // Case 5: Software-only DXGI environment does NOT emit --use-adapter-luid.
        ComPtr<FakeDxgiFactory6> swOnlyFactory = Make<FakeDxgiFactory6>(
            std::vector<ComPtr<IDXGIAdapter1>>{sw},
            std::vector<size_t>{0},
            true);
        Check(SelectGameAlignedAdapter(swOnlyFactory.Get(), nullptr, chosen) && chosen.isSoftware,
            "software-only factory reports isSoftware=true");
        WebView2Ui::EnvironmentConfig swEnv = envConfig;
        swEnv.hasAdapterLuid = true;
        swEnv.adapterIsSoftware = chosen.isSoftware;
        swEnv.adapterLuid = chosen.luid;
        Check(WebView2Ui::BuildAdditionalBrowserArguments(swEnv).empty(),
            "software-only adapter does not emit hardware --use-adapter-luid");

        // Case 6: Real OS DXGI + real pure-software WARP second adapter + real D3D11 devices + WebView2Ui EnsureEnvironment.
        ComPtr<IDXGIFactory1> realFactory;
        Check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&realFactory))) && realFactory, "real CreateDXGIFactory1");
        ComPtr<IDXGIFactory4> realFactory4;
        Check(SUCCEEDED(realFactory.As(&realFactory4)) && realFactory4, "real IDXGIFactory4 for EnumWarpAdapter");
        ComPtr<IDXGIAdapter1> realWarp;
        Check(SUCCEEDED(realFactory4->EnumWarpAdapter(IID_PPV_ARGS(&realWarp))) && realWarp, "real EnumWarpAdapter");
        const AdapterInfo warpInfo = DescribeAdapter(realWarp.Get(), "real-warp");
        Check(warpInfo.valid && warpInfo.isSoftware && warpInfo.vendorId == 0x1414,
            "real WARP second adapter detected as software 0x1414");

        // Create a real D3D11 device directly on the pure-software WARP second DXGI adapter.
        ComPtr<ID3D11Device> warpDevice;
        const HRESULT warpHr = D3D11CreateDevice(
            realWarp.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &warpDevice, nullptr, nullptr);
        Check(SUCCEEDED(warpHr) && warpDevice, "D3D11CreateDevice succeeds on pure-software WARP second adapter");
        const AdapterInfo warpDeviceInfo = DescribeDeviceAdapter(warpDevice.Get(), "warp-d3d11");
        Check(warpDeviceInfo.valid && SameLuid(warpDeviceInfo.luid, warpInfo.luid),
            "DescribeDeviceAdapter reads back exact WARP second adapter LUID");

        AdapterInfo realAligned;
        Check(SelectGameAlignedAdapter(realFactory.Get(), nullptr, realAligned) && realAligned.valid,
            "SelectGameAlignedAdapter succeeds on live OS DXGI");
        Check(!realAligned.isSoftware ? !SameLuid(realAligned.luid, warpInfo.luid) : true,
            "live OS DXGI prefers hardware adapter over WARP second adapter");

        hooks.failEnvironment = false;
        hooks.ensureEnvironment = [&](const WebView2Ui::EnvironmentConfig& cfg) {
            Check(cfg.hasAdapterLuid && SameLuid(cfg.adapterLuid, realAligned.luid),
                "EnsureEnvironment received aligned DXGI LUID");
        };
        WebView2Ui::ConfigureTest(hooks);
        WebView2Ui::EnvironmentConfig liveEnv = envConfig;
        liveEnv.hasAdapterLuid = realAligned.valid;
        liveEnv.adapterIsSoftware = realAligned.isSoftware;
        liveEnv.adapterLuid = realAligned.luid;
        Check(WebView2Ui::EnsureEnvironment(liveEnv), "EnsureEnvironment with aligned DXGI adapter");
        if (!realAligned.isSoftware) {
            Check(WebView2Ui::LastBrowserArguments() ==
                (L"--use-angle=d3d11 --use-adapter-luid=" + FormatAdapterLuidArg(realAligned.luid)),
                "LastBrowserArguments records exact --use-angle=d3d11 --use-adapter-luid");
        }
        WebView2Ui::Shutdown();
    }

    std::printf("PASS: %d checks - shared WebView2 environment, lifecycle/isolation, and dual-DXGI GPU alignment seams\n", g_checks);
    return 0;
}
