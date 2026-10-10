#pragma once

#include "SysUtils.h"
#include "Util.h"
#include "Config.h"
#include "Logger.h"

#include <proxies/Ntdll_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

#include "detours/detours.h"

#include <filesystem>

inline const char* project_id_override = "24480451-f00d-face-1304-0308dabad187";
constexpr unsigned long long app_id_override = 0x24480451;

typedef uint32_t (*PFN_NVSDK_NGX_GetSnippetVersion)(void);
static feature_version GetVersionUsingNGXSnippet(const std::vector<std::string>& dlls)
{
    uint32_t highestVersion = 0;
    for (const auto& dll : dlls)
    {
        PFN_NVSDK_NGX_GetSnippetVersion _GetSnippetVersion =
            (PFN_NVSDK_NGX_GetSnippetVersion) DetourFindFunction(dll.c_str(), "NVSDK_NGX_GetSnippetVersion");
        if (_GetSnippetVersion)
        {
            LOG_TRACE("_GetSnippetVersion ptr from {}: {:X}", dll, (ULONG64) _GetSnippetVersion);
            uint32_t version = _GetSnippetVersion();
            highestVersion = std::max(version, highestVersion);
        }
    }

    feature_version version;

    version.major = (highestVersion & 0xFFFF0000) / 0x00010000;
    version.minor = (highestVersion & 0x0000FF00) / 0x00000100;
    version.patch = (highestVersion & 0x000000FF) / 0x00000001;

    return version;
}

typedef NVSDK_NGX_Result (*PFN_CUDA_Init)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                          const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                          NVSDK_NGX_Version InSDKVersion);
typedef NVSDK_NGX_Result (*PFN_CUDA_Init_ProjectID)(const char* InProjectId, NVSDK_NGX_EngineType InEngineType,
                                                    const char* InEngineVersion, const wchar_t* InApplicationDataPath,
                                                    NVSDK_NGX_Version InSDKVersion,
                                                    const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_CUDA_Shutdown)(void);
typedef NVSDK_NGX_Result (*PFN_CUDA_GetParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_CUDA_AllocateParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_CUDA_GetCapabilityParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_CUDA_DestroyParameters)(NVSDK_NGX_Parameter* InParameters);
typedef NVSDK_NGX_Result (*PFN_CUDA_GetScratchBufferSize)(NVSDK_NGX_Feature InFeatureId,
                                                          const NVSDK_NGX_Parameter* InParameters,
                                                          size_t* OutSizeInBytes);
typedef NVSDK_NGX_Result (*PFN_CUDA_CreateFeature)(NVSDK_NGX_Feature InFeatureID,
                                                   const NVSDK_NGX_Parameter* InParameters,
                                                   NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_CUDA_EvaluateFeature)(const NVSDK_NGX_Handle* InFeatureHandle,
                                                     const NVSDK_NGX_Parameter* InParameters,
                                                     PFN_NVSDK_NGX_ProgressCallback InCallback);
typedef NVSDK_NGX_Result (*PFN_CUDA_ReleaseFeature)(NVSDK_NGX_Handle* InHandle);

typedef NVSDK_NGX_Result (*PFN_D3D12_Init)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                           ID3D12Device* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo,
                                           NVSDK_NGX_Version InSDKVersion);
typedef NVSDK_NGX_Result (*PFN_D3D12_Init_ProjectID)(const char* InProjectId, NVSDK_NGX_EngineType InEngineType,
                                                     const char* InEngineVersion, const wchar_t* InApplicationDataPath,
                                                     ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                                     const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_D3D12_Init_Ext)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath,
                                               ID3D12Device* InDevice, NVSDK_NGX_Version InSDKVersion,
                                               const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo);
typedef NVSDK_NGX_Result (*PFN_D3D12_Shutdown)(void);
typedef NVSDK_NGX_Result (*PFN_D3D12_Shutdown1)(ID3D12Device* InDevice);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_AllocateParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetCapabilityParameters)(NVSDK_NGX_Parameter** OutParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_DestroyParameters)(NVSDK_NGX_Parameter* InParameters);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetScratchBufferSize)(NVSDK_NGX_Feature InFeatureId,
                                                           const NVSDK_NGX_Parameter* InParameters,
                                                           size_t* OutSizeInBytes);
typedef NVSDK_NGX_Result (*PFN_D3D12_CreateFeature)(ID3D12GraphicsCommandList* InCmdList, NVSDK_NGX_Feature InFeatureID,
                                                    NVSDK_NGX_Parameter* InParameters, NVSDK_NGX_Handle** OutHandle);
typedef NVSDK_NGX_Result (*PFN_D3D12_ReleaseFeature)(NVSDK_NGX_Handle* InHandle);
typedef NVSDK_NGX_Result (*PFN_D3D12_GetFeatureRequirements)(IDXGIAdapter* Adapter,
                                                             const NVSDK_NGX_FeatureDiscoveryInfo* FeatureDiscoveryInfo,
                                                             NVSDK_NGX_FeatureRequirement* OutSupported);
typedef NVSDK_NGX_Result (*PFN_D3D12_EvaluateFeature)(ID3D12GraphicsCommandList* InCmdList,
                                                      const NVSDK_NGX_Handle* InFeatureHandle,
                                                      const NVSDK_NGX_Parameter* InParameters,
                                                      PFN_NVSDK_NGX_ProgressCallback InCallback);
typedef NVSDK_NGX_Result (*PFN_D3D12_PopulateParameters_Impl)(NVSDK_NGX_Parameter* InParameters);

typedef NVSDK_NGX_Result (*PFN_UpdateFeature)(const NVSDK_NGX_Application_Identifier* ApplicationId,
                                              const NVSDK_NGX_Feature FeatureID);

struct NvngxModule
{
    HMODULE dll = nullptr;
    std::wstring filePath;

    PFN_CUDA_Init CUDA_Init = nullptr;
    PFN_CUDA_Init_ProjectID CUDA_Init_ProjectID = nullptr;
    PFN_CUDA_Shutdown CUDA_Shutdown = nullptr;
    PFN_CUDA_GetParameters CUDA_GetParameters = nullptr;
    PFN_CUDA_AllocateParameters CUDA_AllocateParameters = nullptr;
    PFN_CUDA_GetCapabilityParameters CUDA_GetCapabilityParameters = nullptr;
    PFN_CUDA_DestroyParameters CUDA_DestroyParameters = nullptr;
    PFN_CUDA_GetScratchBufferSize CUDA_GetScratchBufferSize = nullptr;
    PFN_CUDA_CreateFeature CUDA_CreateFeature = nullptr;
    PFN_CUDA_EvaluateFeature CUDA_EvaluateFeature = nullptr;
    PFN_CUDA_ReleaseFeature CUDA_ReleaseFeature = nullptr;

    PFN_D3D12_Init D3D12_Init = nullptr;
    PFN_D3D12_Init_ProjectID D3D12_Init_ProjectID = nullptr;
    PFN_D3D12_Init_Ext D3D12_Init_Ext = nullptr;
    PFN_D3D12_Shutdown D3D12_Shutdown = nullptr;
    PFN_D3D12_Shutdown1 D3D12_Shutdown1 = nullptr;
    PFN_D3D12_GetParameters D3D12_GetParameters = nullptr;
    PFN_D3D12_AllocateParameters D3D12_AllocateParameters = nullptr;
    PFN_D3D12_GetCapabilityParameters D3D12_GetCapabilityParameters = nullptr;
    PFN_D3D12_DestroyParameters D3D12_DestroyParameters = nullptr;
    PFN_D3D12_GetScratchBufferSize D3D12_GetScratchBufferSize = nullptr;
    PFN_D3D12_CreateFeature D3D12_CreateFeature = nullptr;
    PFN_D3D12_ReleaseFeature D3D12_ReleaseFeature = nullptr;
    PFN_D3D12_GetFeatureRequirements D3D12_GetFeatureRequirements = nullptr;
    PFN_D3D12_EvaluateFeature D3D12_EvaluateFeature = nullptr;

    PFN_UpdateFeature UpdateFeature = nullptr;
};

class NVNGXProxy
{
  private:
    inline static NvngxModule _module;

    inline static bool _cudaInited = false;
    inline static bool _dx12Inited = false;

    inline static void LogCallback(const char* message, NVSDK_NGX_Logging_Level loggingLevel,
                                   NVSDK_NGX_Feature sourceComponent)
    {
        std::string logMessage(message);
        LOG_DEBUG("NVSDK Feature {}: {}", (UINT) sourceComponent, logMessage);
    }

  public:
    static void InitNVNGX(HMODULE nvngxModule = nullptr)
    {
        // if dll already loaded
        if (_module.dll != nullptr)
            return;

        LOG_INFO("");

        ScopedSkipDxgiLoadChecks scopedSkipDxgiLoadChecks {};

        if (nvngxModule != nullptr && _module.dll == nullptr)
        {
            _module.dll = nvngxModule;
        }

        std::vector<std::wstring> dllNames = { L"_nvngx.dll", L"nvngx.dll" };

        auto optiPath = Config::Instance()->MainDllPath.value();
        auto overridePath = Config::Instance()->NvngxPath.value_or(L"");

        if (_module.dll == nullptr)
        {
            for (size_t i = 0; i < dllNames.size(); i++)
            {
                LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                HMODULE memModule = nullptr;
                Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &_module.dll);

                if (_module.dll != nullptr)
                {
                    break;
                }
            }
        }

        if (_module.dll == nullptr)
        {
            auto regNGXCorePath = Util::NvngxPath();
            if (regNGXCorePath.has_value())
            {
                overridePath = regNGXCorePath.value();

                for (size_t i = 0; i < dllNames.size(); i++)
                {
                    LOG_DEBUG("Trying to load {}", wstring_to_string(dllNames[i]));

                    HMODULE memModule = nullptr;
                    Util::LoadProxyLibrary(dllNames[i], optiPath, overridePath, &memModule, &_module.dll);

                    if (_module.dll != nullptr)
                    {
                        break;
                    }
                }
            }
        }

        if (_module.dll != nullptr)
        {
            wchar_t modulePath[MAX_PATH];
            DWORD len = GetModuleFileNameW(_module.dll, modulePath, MAX_PATH);
            _module.filePath = std::wstring(modulePath);

            LOG_INFO("Loaded from {}", wstring_to_string(_module.filePath));
        }

        if (_module.dll != nullptr && _module.D3D12_Init_ProjectID == nullptr)
        {
            LOG_INFO("Getting nvngx method addresses");

            _module.D3D12_Init =
                (PFN_D3D12_Init) KernelBaseProxy::GetProcAddress_()(_module.dll, "NVSDK_NGX_D3D12_Init");
            _module.D3D12_Init_ProjectID = (PFN_D3D12_Init_ProjectID) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_Init_ProjectID");
            _module.D3D12_Init_Ext =
                (PFN_D3D12_Init_Ext) KernelBaseProxy::GetProcAddress_()(_module.dll, "NVSDK_NGX_D3D12_Init_Ext");
            _module.D3D12_Shutdown =
                (PFN_D3D12_Shutdown) KernelBaseProxy::GetProcAddress_()(_module.dll, "NVSDK_NGX_D3D12_Shutdown");
            _module.D3D12_Shutdown1 =
                (PFN_D3D12_Shutdown1) KernelBaseProxy::GetProcAddress_()(_module.dll, "NVSDK_NGX_D3D12_Shutdown1");
            _module.D3D12_GetParameters = (PFN_D3D12_GetParameters) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_GetParameters");
            _module.D3D12_AllocateParameters = (PFN_D3D12_AllocateParameters) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_AllocateParameters");
            _module.D3D12_GetCapabilityParameters =
                (PFN_D3D12_GetCapabilityParameters) KernelBaseProxy::GetProcAddress_()(
                    _module.dll, "NVSDK_NGX_D3D12_GetCapabilityParameters");
            _module.D3D12_DestroyParameters = (PFN_D3D12_DestroyParameters) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_DestroyParameters");
            _module.D3D12_GetScratchBufferSize = (PFN_D3D12_GetScratchBufferSize) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_GetScratchBufferSize");
            _module.D3D12_CreateFeature = (PFN_D3D12_CreateFeature) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_CreateFeature");
            _module.D3D12_ReleaseFeature = (PFN_D3D12_ReleaseFeature) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_ReleaseFeature");
            _module.D3D12_GetFeatureRequirements =
                (PFN_D3D12_GetFeatureRequirements) KernelBaseProxy::GetProcAddress_()(
                    _module.dll, "NVSDK_NGX_D3D12_GetFeatureRequirements");
            _module.D3D12_EvaluateFeature = (PFN_D3D12_EvaluateFeature) KernelBaseProxy::GetProcAddress_()(
                _module.dll, "NVSDK_NGX_D3D12_EvaluateFeature");

            _module.UpdateFeature =
                (PFN_UpdateFeature) KernelBaseProxy::GetProcAddress_()(_module.dll, "NVSDK_NGX_UpdateFeature");
        }
    }

    static void GetFeatureCommonInfo(NVSDK_NGX_FeatureCommonInfo* fcInfo)
    {
        if (State::Instance().NVNGX_DLSS_Path.has_value())
        {
            std::filesystem::path dlssPath(State::Instance().NVNGX_DLSS_Path.value());
            State::Instance().NVNGX_FeatureInfo_Paths.push_back(dlssPath.remove_filename().wstring());
        }

        // Allocate memory for the array of const wchar_t*
        wchar_t const** paths = new const wchar_t*[State::Instance().NVNGX_FeatureInfo_Paths.size()];

        // Copy the strings from the vector to the array
        for (size_t i = 0; i < State::Instance().NVNGX_FeatureInfo_Paths.size(); ++i)
        {
            paths[i] = State::Instance().NVNGX_FeatureInfo_Paths[i].c_str();
            LOG_DEBUG("paths[{0}]: {1}", i, wstring_to_string(State::Instance().NVNGX_FeatureInfo_Paths[i]));
        }

        fcInfo->PathListInfo.Path = paths;
        fcInfo->PathListInfo.Length = static_cast<unsigned int>(State::Instance().NVNGX_FeatureInfo_Paths.size());

        // Config logging
        fcInfo->LoggingInfo.MinimumLoggingLevel =
            Config::Instance()->LogLevel < 2 ? NVSDK_NGX_LOGGING_LEVEL_VERBOSE : NVSDK_NGX_LOGGING_LEVEL_ON;
        fcInfo->LoggingInfo.LoggingCallback = LogCallback;
        fcInfo->LoggingInfo.DisableOtherLoggingSinks = true;
    }

    static HMODULE NVNGXModule() { return _module.dll; }
    static std::wstring NVNGXModule_Path() { return _module.filePath; }

    static bool IsNVNGXInited()
    {
        return _module.dll != nullptr && _dx12Inited && Config::Instance()->DLSSEnabled.value_or_default();
    }

    // DirectX12
    static bool InitDx12(ID3D12Device* InDevice)
    {
        if (_dx12Inited)
            return true;

        InitNVNGX();

        if (_module.dll == nullptr)
            return false;

        NVSDK_NGX_FeatureCommonInfo fcInfo {};
        GetFeatureCommonInfo(&fcInfo);
        NVSDK_NGX_Result nvResult = NVSDK_NGX_Result_Fail;

        if (State::Instance().NVNGX_ProjectId != "" && _module.D3D12_Init_ProjectID != nullptr)
        {
            LOG_INFO("_module.D3D12_Init_ProjectID!");

            nvResult = _module.D3D12_Init_ProjectID(
                State::Instance().NVNGX_ProjectId.c_str(), State::Instance().NVNGX_Engine,
                State::Instance().NVNGX_EngineVersion.c_str(), State::Instance().NVNGX_ApplicationDataPath.c_str(),
                InDevice, State::Instance().NVNGX_Version, &fcInfo);
        }
        else if (_module.D3D12_Init_Ext != nullptr)
        {
            LOG_INFO("_module.D3D12_Init_Ext!");
            nvResult = _module.D3D12_Init_Ext(State::Instance().NVNGX_ApplicationId,
                                              State::Instance().NVNGX_ApplicationDataPath.c_str(), InDevice,
                                              State::Instance().NVNGX_Version, &fcInfo);
        }

        LOG_INFO("result: {0:X}", (UINT) nvResult);

        _dx12Inited = (nvResult == NVSDK_NGX_Result_Success);
        return _dx12Inited;
    }

    static void SetDx12Inited(bool value) { _dx12Inited = value; }

    static bool IsDx12Inited() { return _dx12Inited; }

    static PFN_D3D12_Init_ProjectID D3D12_Init_ProjectID() { return _module.D3D12_Init_ProjectID; }

    static PFN_D3D12_Init D3D12_Init() { return _module.D3D12_Init; }

    static PFN_D3D12_Init_Ext D3D12_Init_Ext() { return _module.D3D12_Init_Ext; }

    static PFN_D3D12_GetFeatureRequirements D3D12_GetFeatureRequirements()
    {
        return _module.D3D12_GetFeatureRequirements;
    }

    static PFN_D3D12_GetCapabilityParameters D3D12_GetCapabilityParameters()
    {
        return _module.D3D12_GetCapabilityParameters;
    }

    static PFN_D3D12_AllocateParameters D3D12_AllocateParameters() { return _module.D3D12_AllocateParameters; }

    static PFN_D3D12_GetParameters D3D12_GetParameters() { return _module.D3D12_GetParameters; }

    static PFN_D3D12_DestroyParameters D3D12_DestroyParameters()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_DestroyParameters;
    }

    static PFN_D3D12_CreateFeature D3D12_CreateFeature()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_CreateFeature;
    }

    static PFN_D3D12_EvaluateFeature D3D12_EvaluateFeature()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_EvaluateFeature;
    }

    static PFN_D3D12_ReleaseFeature D3D12_ReleaseFeature()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_ReleaseFeature;
    }

    static PFN_D3D12_Shutdown D3D12_Shutdown()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_Shutdown;
    }

    static PFN_D3D12_Shutdown1 D3D12_Shutdown1()
    {
        if (!_dx12Inited)
            return nullptr;

        return _module.D3D12_Shutdown1;
    }

    static PFN_UpdateFeature UpdateFeature()
    {
        if (_module.dll == nullptr)
            InitNVNGX();

        return _module.UpdateFeature;
    }
};
