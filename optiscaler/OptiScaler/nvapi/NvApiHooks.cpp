#include "pch.h"
#include "NvApiHooks.h"
#include <dlssnr/DlssNrNative.h>
#include <NvApiDriverSettings.h>

#include "State.h"
#include <Config.h>

#include <proxies/KernelBase_Proxy.h>

#include <detours/detours.h>
#include <misc/IdentifyGpu.h>

// #define LOG_ALL_DRS_GET_CALLS

#ifdef LOG_ALL_DRS_GET_CALLS
#include <magic_enum.hpp>
#endif

NvAPI_Status __stdcall NvApiHooks::hkNvAPI_DRS_GetSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                                          NvU32 settingId, NVDRS_SETTING* pSetting)
{
    if (!o_NvAPI_DRS_GetSetting)
        return NVAPI_ERROR;

    auto result = o_NvAPI_DRS_GetSetting(hSession, hProfile, settingId, pSetting);
    if (pSetting && result == NVAPI_OK)
    {
#ifdef LOG_ALL_DRS_GET_CALLS
        LOG_TRACE("settingId: {:X}, settingLocation: {}, isCurrentPredefined: {}", settingId,
                  magic_enum::enum_name(pSetting->settingLocation), pSetting->isCurrentPredefined,
                  pSetting->isPredefinedValid);

        switch (pSetting->settingType)
        {
        case NVDRS_DWORD_TYPE:
            LOG_TRACE("    u32CurrentValue: {}, u32PredefinedValue: {}", pSetting->u32CurrentValue,
                      pSetting->u32PredefinedValue);
            break;
        case NVDRS_BINARY_TYPE:
            LOG_TRACE("    binary data");
            break;
        case NVDRS_STRING_TYPE:
            LOG_TRACE("    NVDRS_STRING_TYPE");
            break;
        case NVDRS_WSTRING_TYPE:
        {
            std::wstring wstrCurrentValue(reinterpret_cast<const wchar_t*>(pSetting->wszCurrentValue));
            std::wstring wstrPredefinedValue(reinterpret_cast<const wchar_t*>(pSetting->wszPredefinedValue));

            LOG_TRACE(L"    wszCurrentValue: {}, wszPredefinedValue: {}", wstrCurrentValue, wstrPredefinedValue);
            break;
        }
        }
#endif

        // TODO: maybe check those values and inform if they are being overridden externally

        if (settingId == NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_ID)
        {
            State::Instance().dlssRenderPresetExternal = pSetting->u32CurrentValue;

            State::Instance().dlssPresetsOverriddenExternally =
                pSetting->u32CurrentValue != NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            // Report no override, we will handle presets from now on
            pSetting->u32CurrentValue = NGX_DLSS_SR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            LOG_DEBUG("DLSS External override: {}", State::Instance().dlssRenderPresetExternal);
        }

        if (settingId == NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_ID)
        {
            State::Instance().dlssdRenderPresetExternal = pSetting->u32CurrentValue;

            State::Instance().dlssdPresetsOverriddenExternally =
                pSetting->u32CurrentValue != NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            // Report no override, we will handle presets from now on
            pSetting->u32CurrentValue = NGX_DLSS_RR_OVERRIDE_RENDER_PRESET_SELECTION_OFF;

            LOG_DEBUG("DLSSD External override: {}", State::Instance().dlssdRenderPresetExternal);
        }

        if (settingId == NGX_DLSS_RR_OVERRIDE_SCALING_RATIO_ID || settingId == NGX_DLSS_SR_OVERRIDE_SCALING_RATIO_ID)
        {
            if (Config::Instance()->UpscaleRatioOverrideEnabled.value_or_default())
            {
                auto ratio = Config::Instance()->UpscaleRatioOverrideValue.value_or_default();
                auto ratioPercentage = (uint32_t) std::round(100.f / ratio);

                // Uses the clamp from SR for RR but it should be fine
                ratioPercentage = std::clamp(ratioPercentage, (uint32_t) NGX_DLSS_SR_OVERRIDE_SCALING_RATIO_MIN,
                                             (uint32_t) NGX_DLSS_SR_OVERRIDE_SCALING_RATIO_MAX);

                pSetting->u32CurrentValue = ratioPercentage;
            }
        }
    }

    return result;
}

void* __stdcall NvApiHooks::hkNvAPI_QueryInterface(unsigned int InterfaceId)
{
    if (!o_NvAPI_QueryInterface)
        return nullptr;

    auto primaryGpu = IdentifyGpu::getPrimaryGpu();

    // Disable flip metering
    if (InterfaceId == GET_ID(NvAPI_D3D12_SetFlipConfig) &&
        Config::Instance()->DisableFlipMetering.value_or(primaryGpu.vendorId != VendorId::Nvidia))
    {
        LOG_INFO("FlipMetering is disabled!");
        return nullptr;
    }

    if (InterfaceId == GET_ID(NvAPI_D3D_SetSleepMode) || InterfaceId == GET_ID(NvAPI_D3D_Sleep) ||
        InterfaceId == GET_ID(NvAPI_D3D_GetLatency) || InterfaceId == GET_ID(NvAPI_D3D_SetLatencyMarker) ||
        InterfaceId == GET_ID(NvAPI_D3D12_SetAsyncFrameMarker))
    {
        // LOG_DEBUG("counter: {}, hookReflex()", qiCounter);
        ReflexHooks::hookReflex(o_NvAPI_QueryInterface);
        return ReflexHooks::getHookedReflex(InterfaceId);
    }

    ReflexHooks::hookReflex(o_NvAPI_QueryInterface);

    const auto functionPointer = o_NvAPI_QueryInterface(InterfaceId);

    if (functionPointer)
    {
        if (InterfaceId == GET_ID(NvAPI_DRS_GetSetting))
        {
            o_NvAPI_DRS_GetSetting = reinterpret_cast<decltype(&NvAPI_DRS_GetSetting)>(functionPointer);
            return &hkNvAPI_DRS_GetSetting;
        }
    }

    // LOG_DEBUG("counter: {} functionPointer: {:X}", qiCounter, (size_t)functionPointer);

    return DlssNrNative::WrapNvapi(InterfaceId,functionPointer);
}

// Requires HMODULE to make sure nvapi is loaded before calling this function
void NvApiHooks::Hook(HMODULE nvapiModule)
{
    if (o_NvAPI_QueryInterface != nullptr)
        return;

    if (nvapiModule == nullptr)
    {
        LOG_ERROR("Hook called with a nullptr nvapi module");
        return;
    }

    LOG_DEBUG("Trying to hook NvApi");

    o_NvAPI_QueryInterface =
        (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(nvapiModule, "nvapi_QueryInterface");

    LOG_DEBUG("OriginalNvAPI_QueryInterface = {0:X}", (unsigned long long) o_NvAPI_QueryInterface);

    if (o_NvAPI_QueryInterface != nullptr)
    {
        LOG_INFO("NvAPI_QueryInterface found, hooking!");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&) o_NvAPI_QueryInterface, hkNvAPI_QueryInterface);
        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook NvAPI_QueryInterface: {:X}", detourResult);
            o_NvAPI_QueryInterface = nullptr;
        }
    }
}

void NvApiHooks::Unhook()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_NvAPI_QueryInterface != nullptr)
        DetourDetach(&(PVOID&) o_NvAPI_QueryInterface, hkNvAPI_QueryInterface);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook NvAPI_QueryInterface: {:X}", detourResult);
    }
    else
    {
        o_NvAPI_QueryInterface = nullptr;
    }
}
