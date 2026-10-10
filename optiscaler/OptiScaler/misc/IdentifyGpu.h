#pragma once
#include <nvapi.h>
#include <proxies/D3D12_Proxy.h>

struct GpuInformation
{
    LUID luid {}; // Unique id to be able to reference the exact GPU
    std::string name {};
    VendorId::Value vendorId = VendorId::Invalid;
    uint32_t deviceId = 0x0;
    uint32_t subsystemId = 0x0;
    uint32_t revisionId = 0x0;
    size_t dedicatedVramInBytes = 0;
    bool softwareAdapter = false;
    std::filesystem::path driverStore {};

    // Nvidia
    bool dlssCapable = false;
    NV_GPU_ARCH_INFO nvidiaArchInfo {};
    bool noDisplayConnected = false;
};

inline constexpr bool IsEqualLUID(LUID luid1, LUID luid2)
{
    return luid1.HighPart == luid2.HighPart && luid1.LowPart == luid2.LowPart;
}

class IdentifyGpu
{
    inline static bool hasD3d12Capabilities = false;
    inline static std::mutex mutex {};
    inline static std::vector<GpuInformation> cache {};

    static std::vector<GpuInformation> checkGpuInfo();
    static void queryNvapi(GpuInformation& gpuInfo);

  public:
    // Sorted by priority, the first one should be treated as the primary one
    static std::vector<GpuInformation> getAllGpus();
    static GpuInformation getPrimaryGpu();
    static GpuInformation getGpuForDevice(ID3D12Device* device);
    static GpuInformation getGpuByLuid(LUID luid);
    static void updateD3d12Capabilities();
};
