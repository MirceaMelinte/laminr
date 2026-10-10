#pragma once
#include "upscalers/IFeature.h"

#include "SysUtils.h"
#include "OwnedMutex.h"

// Base D3D12 and Streamline headers the rest of the project reaches through Config.h
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <shaders/Shader_Dx12.h>

#include <set>
#include <map>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <shared_mutex>
#include <unordered_map>
#include <sl.h>
#include <sl1.h>
#include <sl_dlss_g.h>
#include <ankerl/unordered_dense.h>
#include <flag-set-cpp/flag_set.hpp>

enum class WorkingMode : uint32_t
{
    Dxgi,
    D3d12,
    Other,
};

enum class PostCode : uint32_t
{
    SlPluginsAlreadyInMemory,
    _
};

enum class GameEngineType : uint32_t
{
    Unity,
    Unreal,
    Katana,
    Other,
};

class State
{
  public:
    static State& Instance()
    {
        static State instance;
        return instance;
    }

    std::string gameExe;
    std::string gameName;
    std::string gameVersion;
    GameEngineType gameEngine = GameEngineType::Other;

    bool nvngxDx12Inited = false;

    bool isOptiPatcherSucceed = false;

    // Reseting on creation of new feature
    std::optional<bool> autoExposure;

    bool skipHeapCapture = false;

    // NVNGX init parameters
    uint64_t NVNGX_ApplicationId = 1337;
    std::wstring NVNGX_ApplicationDataPath;
    std::string NVNGX_ProjectId;
    NVSDK_NGX_Version NVNGX_Version {};
    const NVSDK_NGX_FeatureCommonInfo* NVNGX_FeatureInfo = nullptr;
    std::vector<std::wstring> NVNGX_FeatureInfo_Paths;
    NVSDK_NGX_LoggingInfo NVNGX_Logger { nullptr, NVSDK_NGX_LOGGING_LEVEL_OFF, false };
    NVSDK_NGX_EngineType NVNGX_Engine = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    std::string NVNGX_EngineVersion;
    std::optional<std::wstring> NVNGX_DLSS_Path;
    std::optional<std::wstring> NVNGX_DLSSD_Path;
    std::optional<std::wstring> NVNGX_DLSSG_Path;

    // NGX OTA
    std::string NGX_OTA_Dlss;
    std::string NGX_OTA_Dlssd;

    feature_version streamlineVersion = { 0, 0, 0 };

    // Has value when Opti was able to hook sl and the game set DLSSG options
    std::optional<int> dlssgMfgMax = std::nullopt;

    API api = API::NotSelected;
    API swapchainApi = API::NotSelected;

    // Framerate
    bool reflexLimitsFps = false;
    bool rtssReflexInjection = false;
    UINT64 reflexFrameId = 0;
    UINT64 frameCount = 0;

    // for realtime changes
    ankerl::unordered_dense::map<unsigned int, bool> changeBackend;
    Upscaler newBackend = Upscaler::Reset;

    float lastMipBias = 100.0f;
    float lastMipBiasMax = -100.0f;

    bool dlssgGameDMFGSupported = false;
    sl::DLSSGMode dlssgLastSetMode = sl::DLSSGMode::eOff;
    int dlssgDetectedInterpolationCount = 0;

    // DLSS
    bool dlssPresetsOverriddenExternally = false;
    bool dlssPresetsOverridenByOpti = false;
    uint32_t dlssRenderPresetExternal = 0;
    uint32_t dlssRenderPresetDLAA = 0;
    uint32_t dlssRenderPresetUltraQuality = 0;
    uint32_t dlssRenderPresetQuality = 0;
    uint32_t dlssRenderPresetBalanced = 0;
    uint32_t dlssRenderPresetPerformance = 0;
    uint32_t dlssRenderPresetUltraPerformance = 0;

    // DLSSD
    bool dlssdPresetsOverriddenExternally = false;
    bool dlssdPresetsOverridenByOpti = false;
    uint32_t dlssdRenderPresetExternal = 0;
    uint32_t dlssdRenderPresetDLAA = 0;
    uint32_t dlssdRenderPresetUltraQuality = 0;
    uint32_t dlssdRenderPresetQuality = 0;
    uint32_t dlssdRenderPresetBalanced = 0;
    uint32_t dlssdRenderPresetPerformance = 0;
    uint32_t dlssdRenderPresetUltraPerformance = 0;

    // Skips dxgi.dll load interception while we call DXGI ourselves
    bool skipDxgiLoadChecks = false;

    // Linux checks
    bool isRunningOnLinux = false;

    // Other checks
    WorkingMode workingMode = WorkingMode::Other;

    // Framegraph
    std::deque<double> upscaleTimes;
    std::deque<double> frameTimes;
    std::vector<DetailedGpuTime> detailedGpuTimes;
    double presentFrameTime = 0.0;
    std::mutex frameTimeMutex;

    // Opti checking if everything is setup correctly on game launch
    // Takes effect up to the first time Opti can show anything on the screen
    // Beyond that use notifications
    bool postDone = false;
    flag_set<PostCode> postCodes;

    // Version check
    std::mutex versionCheckMutex;
    bool versionCheckInProgress = false;
    bool versionCheckCompleted = false;
    bool updateAvailable = false;
    std::string latestVersionTag;
    std::string latestVersionUrl;
    std::string versionCheckError;

    // Swapchain info
    float screenWidth = 800.0;
    float screenHeight = 450.0;
    bool realExclusiveFullscreen = false;
    bool SCExclusiveFullscreen = false;
    bool SCAllowTearing = false;
    UINT SCLastFlags = 0;

    // HDR
    std::vector<IUnknown*> scBuffers;
    bool isHdrActive = false;

    ApiUpscalerInput currentInputApiName;

    bool isShuttingDown = false;
    std::set<PVOID> modulesToFree;

    // menu warnings
    bool nvngxIniDetected = false;

    IFeature* currentFeature = nullptr;
    IDXGISwapChain* currentSwapchain = nullptr;
    IDXGISwapChain* currentWrappedSwapchain = nullptr;
    IDXGISwapChain* currentRealSwapchain = nullptr;
    ID3D12Device* currentD3D12Device = nullptr;
    ID3D12CommandQueue* currentCommandQueue = nullptr;
    DXGI_SWAP_CHAIN_DESC currentSwapchainDesc {};

    std::vector<ID3D12Device*> d3d12Devices;

    static UINT GetOwner()
    {
        _lastOwner++;
        return _lastOwner;
    }

    // Moved checks here to prevent circular includes
    /// <summary>
    /// Enables skipping of LoadLibrary checks
    /// </summary>
    /// <param name="dllName">Lower case dll name without `.dll` at the end. Leave blank for skipping all dll's</param>
    static void DisableChecks(UINT owner, std::string dllName = "")
    {
        // if (_skipOwner == 0 || _skipOwner < owner)
        //{
        _skipOwner = owner;
        _skipChecks = true;
        _skipDllName[_skipOwner] = dllName;
        //}
    };

    static void EnableChecks(UINT owner)
    {
        _skipDllName.erase(_skipOwner);

        // if (_skipOwner == owner)
        //{
        if (_skipDllName.size() > 0)
        {
            // loop in reverse to get the last added owner
            _skipOwner = _skipDllName.rbegin()->first;
        }
        else
        {
            _skipOwner = 0;
        }

        _skipChecks = (_skipOwner != 0);
        //}
    };

    static void DisableServeOriginal(UINT owner)
    {
        if (_serveOwner == 0 || _serveOwner == owner)
        {
            _serveOriginal = false;
            _skipOwner = 0;
        }
    };

    static void EnableServeOriginal(UINT owner)
    {
        if (_serveOwner == 0 || _serveOwner == owner)
        {
            _serveOriginal = true;
            _skipOwner = owner;
        }
    };

    static bool SkipDllChecks() { return _skipChecks; }
    static std::string SkipDllName()
    {
        return _skipOwner == 0 ? "" : (_skipDllName.contains(_skipOwner) ? _skipDllName[_skipOwner] : "");
    }
    static bool ServeOriginal() { return _serveOriginal; }

  private:
    inline static bool _skipChecks = false;
    inline static std::map<UINT, std::string> _skipDllName;
    inline static UINT _skipOwner = 0;
    inline static UINT _lastOwner = 0;

    inline static bool _serveOriginal = false;
    inline static UINT _serveOwner = 0;

    State() = default;
};

class ScopedSkipDxgiLoadChecks
{
  private:
    bool previousState;

  public:
    ScopedSkipDxgiLoadChecks()
    {
        previousState = State::Instance().skipDxgiLoadChecks;
        State::Instance().skipDxgiLoadChecks = true;
    }

    ~ScopedSkipDxgiLoadChecks() { State::Instance().skipDxgiLoadChecks = previousState; }
};

class ScopedSkipHeapCapture
{
  private:
    bool previousState;

  public:
    ScopedSkipHeapCapture()
    {
        previousState = State::Instance().skipHeapCapture;
        State::Instance().skipHeapCapture = true;
    }

    ~ScopedSkipHeapCapture() { State::Instance().skipHeapCapture = previousState; }
};
