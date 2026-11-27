// SPDX-License-Identifier: GPL-3.0-only
//
// GakuLoader is licensed under GNU GPL version 3.
// Additional permission for independent plugins is provided under
// the GakuLoader Plugin Exception.
//
// See LICENSE and LICENSES/GakuLoader-Plugin-Exception.txt.

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

namespace GakuLoader::DxgiAdapter {

struct AdapterInfo {
    bool valid = false;
    bool isSoftware = false;
    LUID luid{};
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    uint64_t dedicatedVideoMemoryBytes = 0;
    std::wstring description;
    std::string source;
};

inline bool SameLuid(const LUID& a, const LUID& b) {
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

inline bool IsSoftwareAdapterDesc(const DXGI_ADAPTER_DESC1& desc) {
    if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) return true;
    // Microsoft Basic Render Driver (WARP) vendor/device pair.
    return desc.VendorId == 0x1414u && desc.DeviceId == 0x008Cu;
}

inline AdapterInfo FromDesc1(const DXGI_ADAPTER_DESC1& desc, const char* source = "") {
    AdapterInfo info;
    info.valid = true;
    info.isSoftware = IsSoftwareAdapterDesc(desc);
    info.luid = desc.AdapterLuid;
    info.vendorId = desc.VendorId;
    info.deviceId = desc.DeviceId;
    info.dedicatedVideoMemoryBytes = static_cast<uint64_t>(desc.DedicatedVideoMemory);
    info.description = desc.Description;
    info.source = source ? source : "";
    return info;
}

inline AdapterInfo DescribeAdapter(IDXGIAdapter1* adapter, const char* source = "") {
    if (!adapter) return {};
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) return {};
    return FromDesc1(desc, source);
}

inline AdapterInfo DescribeDeviceAdapter(ID3D11Device* device, const char* source = "host-d3d11") {
    if (!device) return {};
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || !dxgiDevice) return {};
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(&adapter)) || !adapter) return {};
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
    if (FAILED(adapter.As(&adapter1)) || !adapter1) return {};
    return DescribeAdapter(adapter1.Get(), source);
}

inline std::wstring FormatAdapterLuidArg(const LUID& luid) {
    return std::to_wstring(luid.HighPart) + L"," + std::to_wstring(luid.LowPart);
}

// Selects the DXGI adapter that aligns with the game's rendering GPU:
// 1. On Windows 10/11 (IDXGIFactory6), DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
//    honors per-app Windows Graphics Settings overrides while defaulting to the
//    discrete GPU on hybrid-GPU laptops (matching Unity's GPU choice).
// 2. Without IDXGIFactory6, prefers a matching host D3D11 device LUID if given,
//    then the non-software adapter with the largest dedicated VRAM.
// 3. Falls back to the first software adapter only if no hardware adapter exists.
inline bool SelectGameAlignedAdapter(
    IDXGIFactory1* factory,
    const LUID* preferredDeviceLuid,
    AdapterInfo& outInfo,
    Microsoft::WRL::ComPtr<IDXGIAdapter1>* outAdapter = nullptr) {
    outInfo = {};
    if (outAdapter) outAdapter->Reset();
    if (!factory) return false;

    struct Candidate {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        AdapterInfo info;
    };

    std::vector<Candidate> hardware;
    Candidate firstSoftware;

    for (UINT i = 0;; ++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) continue;
        AdapterInfo info = DescribeAdapter(adapter.Get(), "dxgi-enum");
        if (!info.valid) continue;
        if (info.isSoftware) {
            if (!firstSoftware.info.valid) {
                info.source = "dxgi-software-only";
                firstSoftware = {adapter, std::move(info)};
            }
        } else {
            hardware.push_back({adapter, std::move(info)});
        }
    }

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory6))) && factory6) {
        for (UINT i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            const HRESULT hr = factory6->EnumAdapterByGpuPreference(
                i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
            if (hr == DXGI_ERROR_NOT_FOUND || FAILED(hr)) break;
            if (!adapter) continue;
            AdapterInfo info = DescribeAdapter(adapter.Get(), "dxgi-high-performance");
            if (info.valid && !info.isSoftware) {
                outInfo = std::move(info);
                if (outAdapter) *outAdapter = adapter;
                return true;
            }
        }
    }

    if (preferredDeviceLuid) {
        for (auto& cand : hardware) {
            if (SameLuid(cand.info.luid, *preferredDeviceLuid)) {
                outInfo = cand.info;
                outInfo.source = "host-d3d11";
                if (outAdapter) *outAdapter = cand.adapter;
                return true;
            }
        }
    }

    if (!hardware.empty()) {
        size_t bestIdx = 0;
        for (size_t i = 1; i < hardware.size(); ++i) {
            if (hardware[i].info.dedicatedVideoMemoryBytes > hardware[bestIdx].info.dedicatedVideoMemoryBytes) {
                bestIdx = i;
            }
        }
        outInfo = hardware[bestIdx].info;
        outInfo.source = hardware.size() > 1 ? "dxgi-max-vram" : "dxgi-enum";
        if (outAdapter) *outAdapter = hardware[bestIdx].adapter;
        return true;
    }

    if (firstSoftware.info.valid) {
        outInfo = std::move(firstSoftware.info);
        if (outAdapter) *outAdapter = firstSoftware.adapter;
        return true;
    }

    return false;
}

inline bool SelectSystemGameAlignedAdapter(
    const LUID* preferredDeviceLuid,
    AdapterInfo& outInfo,
    Microsoft::WRL::ComPtr<IDXGIAdapter1>* outAdapter = nullptr) {
    outInfo = {};
    if (outAdapter) outAdapter->Reset();
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) return false;
    return SelectGameAlignedAdapter(factory.Get(), preferredDeviceLuid, outInfo, outAdapter);
}

} // namespace GakuLoader::DxgiAdapter
