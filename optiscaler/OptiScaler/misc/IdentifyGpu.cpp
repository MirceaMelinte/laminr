#include "pch.h"
#include "../../../shared/BackendSelection.h"
#include "IdentifyGpu.h"

#include <magic_enum.hpp>
#include <wrl/client.h>

#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3d12_Proxy.h>
#include "nvapi/NvApiTypes.h"

using Microsoft::WRL::ComPtr;

// Prioritize Nvidia cards that can run DLSS and are connected to a display
// void sortGpus(std::vector<GpuInformation>& gpus)
//{
//    std::sort(gpus.begin(), gpus.end(),
//              [](const GpuInformation& a, const GpuInformation& b)
//              {
//                  auto isPreferredNvidia = [](const GpuInformation& gpu)
//                  {
//                      bool isNvidia = (gpu.vendorId == VendorId::Nvidia);
//                      return isNvidia && gpu.dlssCapable && !gpu.noDisplayConnected;
//                  };
//
//                  bool aIsPreferred = isPreferredNvidia(a);
//                  bool bIsPreferred = isPreferredNvidia(b);
//
//                  // If one is a preferred and the other isn't then the preferred one should be sorted first
//                  if (aIsPreferred != bIsPreferred)
//                  {
//                      return aIsPreferred;
//                  }
//
//                  if (a.softwareAdapter)
//                      return false;
//
//                  // Fallback on VRAM amount
//                  return a.dedicatedVramInBytes > b.dedicatedVramInBytes;
//              });
//}

std::vector<GpuInformation> IdentifyGpu::checkGpuInfo()
{
    auto localCachedInfo = std::vector<GpuInformation> {};

    DxgiProxy::Init();

    ComPtr<IDXGIFactory6> factory = nullptr;
    HRESULT result = S_FALSE;

    result = DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), (IDXGIFactory**) factory.GetAddressOf());

    if (result != S_OK || factory == nullptr)
    {
        // Will land here if getPrimaryGpu/getAllGpus are called from within DLL_PROCESS_ATTACH
        LOG_ERROR("Failed to create DXGI Factory, GPU info will be inaccurate!");
        return localCachedInfo;
    }

    UINT adapterIndex = 0;
    DXGI_ADAPTER_DESC1 adapterDesc {};
    ComPtr<IDXGIAdapter1> adapter;

    DXGI_GPU_PREFERENCE gpuPreference = DXGI_GPU_PREFERENCE_UNSPECIFIED;
    if (Config::Instance()->PreferDedicatedGpu.value_or_default())
        gpuPreference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;

    auto storePaths = Util::GetDriverStore();

    while (factory->EnumAdapterByGpuPreference(adapterIndex, gpuPreference, IID_PPV_ARGS(&adapter)) == S_OK)
    {
        if (adapter == nullptr)
        {
            adapterIndex++;
            continue;
        }

        result = adapter->GetDesc1(&adapterDesc);

        if (result == S_OK)
        {
            GpuInformation gpuInfo;
            gpuInfo.luid = adapterDesc.AdapterLuid;
            gpuInfo.vendorId = (VendorId::Value) adapterDesc.VendorId;
            gpuInfo.deviceId = adapterDesc.DeviceId;
            gpuInfo.subsystemId = adapterDesc.SubSysId;
            gpuInfo.revisionId = adapterDesc.Revision;
            gpuInfo.dedicatedVramInBytes = adapterDesc.DedicatedVideoMemory;

            std::wstring szName(adapterDesc.Description);
            gpuInfo.name = wstring_to_string(szName);

            if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                gpuInfo.softwareAdapter = true;

            Util::Luid luid(gpuInfo.luid.LowPart, gpuInfo.luid.HighPart);

            if (storePaths.contains(luid))
                gpuInfo.driverStore = storePaths[luid];

            localCachedInfo.push_back(std::move(gpuInfo));
        }
        else
        {
            LOG_DEBUG("Can't get description of adapter: {}", adapterIndex);
        }

        adapterIndex++;
    }

    // We might be getting the correct ordering by default.
    // Trying to sort by vendor might cause issues if someone
    // has some old Nvidia card in their system for example.
    // sortGpus(localCachedInfo);

    for (auto& gpuInfo : localCachedInfo)
    {
        if (gpuInfo.vendorId == VendorId::Nvidia)
        {
            queryNvapi(gpuInfo);
        }
    }

    return localCachedInfo;
}

void IdentifyGpu::queryNvapi(GpuInformation& gpuInfo)
{
    // Prevent recursion: NvAPI_Initialize (dxvk-nvapi) internally creates a DXGI factory
    // and enumerates adapters, which re-enters our hooked EnumAdapters/EnumAdapters1 and
    // calls getAllGpus() again while is_fetching is already set (returns empty list).
    // Skip the DXGI load checks so nvapi sees the real adapters, allowing init to succeed.
    ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

    auto nvapiModule = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

    // No nvapi, should not be nvidia, possibly external spoofing
    if (!nvapiModule)
        return;

    auto o_NvAPI_QueryInterface =
        (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(nvapiModule, "nvapi_QueryInterface");

    if (!o_NvAPI_QueryInterface)
    {
        NtdllProxy::FreeLibrary_Ldr(nvapiModule);
        return;
    }

    // A fake nvapi64.dll in system32 answers this id, assume it's not nvidia if found
    if (o_NvAPI_QueryInterface(0x21382138))
    {
        NtdllProxy::FreeLibrary_Ldr(nvapiModule);
        return;
    }

    auto* init = GET_INTERFACE(NvAPI_Initialize, o_NvAPI_QueryInterface);
    if (!init || init() != NVAPI_OK)
    {
        LOG_ERROR("Failed to init NvApi");
        NtdllProxy::FreeLibrary_Ldr(nvapiModule);
        return;
    }

    // Handle we want to grab
    NvPhysicalGpuHandle hPhysicalGpu {};

    // Grab logical GPUs to extract coresponding LUID
    auto* getLogicalGPUs = GET_INTERFACE(NvAPI_SYS_GetLogicalGPUs, o_NvAPI_QueryInterface);
    NV_LOGICAL_GPUS logicalGpus {};
    logicalGpus.version = NV_LOGICAL_GPUS_VER;
    if (getLogicalGPUs)
    {
        if (auto result = getLogicalGPUs(&logicalGpus); result != NVAPI_OK)
            LOG_ERROR("NvAPI_SYS_GetLogicalGPUs failed: {}", magic_enum::enum_name(result));
    }

    auto* getLogicalGpuInfo = GET_INTERFACE(NvAPI_GPU_GetLogicalGpuInfo, o_NvAPI_QueryInterface);

    if (getLogicalGpuInfo)
    {
        for (uint32_t i = 0; i < logicalGpus.gpuHandleCount; i++)
        {
            LUID luid;
            NV_LOGICAL_GPU_DATA logicalGpuData {};
            logicalGpuData.pOSAdapterId = &luid;
            logicalGpuData.version = NV_LOGICAL_GPU_DATA_VER;
            auto logicalGpu = logicalGpus.gpuHandleData[i].hLogicalGpu;

            if (auto result = getLogicalGpuInfo(logicalGpu, &logicalGpuData); result != NVAPI_OK)
                LOG_ERROR("NvAPI_GPU_GetLogicalGpuInfo failed: {}", magic_enum::enum_name(result));

            // We are looking at the correct GPU for this gpuInfo.luid
            if (IsEqualLUID(luid, gpuInfo.luid) && logicalGpuData.physicalGpuCount > 0)
            {
                if (logicalGpuData.physicalGpuCount > 1)
                    LOG_WARN("A logical GPU has more than a single physical GPU, we are only checking one");

                hPhysicalGpu = logicalGpuData.physicalGpuHandles[0];
            }
        }
    }

    auto* getArchInfo = GET_INTERFACE(NvAPI_GPU_GetArchInfo, o_NvAPI_QueryInterface);
    gpuInfo.nvidiaArchInfo.version = NV_GPU_ARCH_INFO_VER;
    if (getArchInfo && hPhysicalGpu && getArchInfo(hPhysicalGpu, &gpuInfo.nvidiaArchInfo) != NVAPI_OK)
        LOG_ERROR("Couldn't get GPU Architecture");

    auto* getConnectedDisplayIds = GET_INTERFACE(NvAPI_GPU_GetConnectedDisplayIds, o_NvAPI_QueryInterface);
    NvU32 displayCount = 0;
    if (getConnectedDisplayIds && hPhysicalGpu &&
        getConnectedDisplayIds(hPhysicalGpu, nullptr, &displayCount, 0) == NVAPI_OK && displayCount == 0)
    {
        gpuInfo.noDisplayConnected = true;
    }

    auto* unload = GET_INTERFACE(NvAPI_Unload, o_NvAPI_QueryInterface);
    if (!unload || unload() != NVAPI_OK)
        LOG_ERROR("Failed to unload NvApi");

    NtdllProxy::FreeLibrary_Ldr(nvapiModule);

    // Turing and newer
    if (Config::Instance()->DLSSEnabled.value_or_default())
        gpuInfo.dlssCapable = gpuInfo.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_TU100;
}

std::vector<GpuInformation> IdentifyGpu::getAllGpus()
{
    thread_local bool is_fetching = false;
    if (is_fetching)
        return {};

    std::scoped_lock lock(mutex);

    if (!cache.empty() && cache.front().deviceId != VendorId::Invalid)
    {
        return cache;
    }

    is_fetching = true;
    cache = checkGpuInfo();
    is_fetching = false;

    return cache;
}

GpuInformation IdentifyGpu::getGpuByLuid(LUID luid)
{
    const auto all = getAllGpus();
    const auto* match = laminr::FindRenderingAdapter(all, luid);
    return match ? *match : GpuInformation {};
}

GpuInformation IdentifyGpu::getGpuForDevice(ID3D12Device* device)
{
    return device ? getGpuByLuid(device->GetAdapterLuid()) : GpuInformation {};
}

GpuInformation IdentifyGpu::getPrimaryGpu()
{
    auto allGpus = getAllGpus();
    return !allGpus.empty() ? allGpus.front() : GpuInformation {};
}

void IdentifyGpu::updateD3d12Capabilities()
{
    if (hasD3d12Capabilities)
        return;

    // Making sure the cache is filled
    getAllGpus();

    auto d3d12Module = KernelBaseProxy::GetModuleHandleW_()(L"d3d12.dll");
    if (!d3d12Module)
        return;

    D3d12Proxy::Init(d3d12Module);

    {
        std::scoped_lock lock(mutex);
        if (hasD3d12Capabilities)
            return;
        hasD3d12Capabilities = true;
    }

    auto detectedGpus = IdentifyGpu::getAllGpus();
    std::string gpus = "Detected GPUs:\n";

    std::string indent(22, ' ');

    for (auto& gpu : detectedGpus)
    {
        gpus += std::format("{}{}\n", indent, gpu.name);
        gpus += std::format("{}    vendorId: {:X}, deviceId: {:X}, VRAM: {}MB\n", indent, (uint32_t) gpu.vendorId,
                            gpu.deviceId, gpu.dedicatedVramInBytes / (1024 * 1024));
        gpus += std::format("{}    Upscaler support - dlss: {}\n", indent, gpu.dlssCapable);
    }

    spdlog::info(gpus);
}
