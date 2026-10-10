#include "pch.h"
#include "../../../shared/BackendSelection.h"
#include "FeatureProvider_Dx12.h"

#include "Util.h"
#include "Config.h"

#include "NVNGX_Parameter.h"

#include "upscalers/dlss/DLSSFeature_Dx12.h"
#include "upscalers/dlssd/DLSSDFeature_Dx12.h"
#include <misc/IdentifyGpu.h>
#include <imgui/ImGuiNotify.hpp>

bool FeatureProvider_Dx12::GetFeature(Upscaler upscaler, UINT handleId, NVSDK_NGX_Parameter* parameters,
                                      std::unique_ptr<IFeature_Dx12>* feature, ID3D12Device* device)
{
    State& state = State::Instance();
    Config& cfg = *Config::Instance();
    const auto renderingGpu = IdentifyGpu::getGpuForDevice(device);
    const auto requested = upscaler;
    const auto hardware = renderingGpu.vendorId == VendorId::Invalid ? laminr::Evidence::Unknown :
        renderingGpu.dlssCapable ? laminr::Evidence::Yes : laminr::Evidence::No;
    ScopedSkipHeapCapture skipHeapCapture {};

    // DLSS only: there is no other backend to fall back to. A failure goes back to the game.
    if (upscaler != Upscaler::DLSSD)
        upscaler = Upscaler::DLSS;

    const bool rayReconstruction = upscaler == Upscaler::DLSSD;
    const char* selectionReason = laminr::NvidiaUpscalerUnavailableReason(
        hardware, rayReconstruction ? state.NVNGX_DLSSD_Path.has_value() : state.NVNGX_DLSS_Path.has_value());

    if (selectionReason == nullptr)
    {
        if (rayReconstruction)
            *feature = std::make_unique<DLSSDFeatureDx12>(handleId, parameters);
        else
            *feature = std::make_unique<DLSSFeatureDx12>(handleId, parameters);
    }
    else
    {
        feature->reset();
    }

    const bool loaded = *feature != nullptr && (*feature)->ModuleLoaded();

    if (selectionReason == nullptr)
        selectionReason = loaded ? "Initialization still required" : "NVIDIA upscaler module failed to load";

    if (!loaded)
        ImGui::InsertNotification({ ImGuiToastType::Error, 10000, "DLSS is unavailable: %s", selectionReason });

    LOG_INFO("laminr DX12 backend decision: requested={}; selected={}; GPU={}; LUID={:08X}:{:08X}; moduleLoaded={}; initialized=not-yet; reason={}",
        UpscalerDisplayName(requested), UpscalerDisplayName(upscaler), renderingGpu.name.empty() ? "Unknown" : renderingGpu.name,
        (UINT)renderingGpu.luid.HighPart, renderingGpu.luid.LowPart, loaded, selectionReason);

    // DLSSD is stored in the config as DLSS
    if (upscaler == Upscaler::DLSSD)
        upscaler = Upscaler::DLSS;

    cfg.Dx12Upscaler = upscaler;

    return loaded;
}

bool FeatureProvider_Dx12::ChangeFeature(Upscaler upscaler, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                                         UINT handleId, NVSDK_NGX_Parameter* parameters,
                                         ContextData<IFeature_Dx12>* contextData)
{
    State& state = State::Instance();
    Config& cfg = *Config::Instance();

    if (!state.changeBackend[handleId])
        return false;

    const bool dlssOnNonCapable = !IdentifyGpu::getGpuForDevice(device).dlssCapable && state.newBackend == Upscaler::DLSS;
    if (state.newBackend == Upscaler::Reset || dlssOnNonCapable)
        state.newBackend = cfg.Dx12Upscaler.value_or_default();

    contextData->changeBackendCounter++;

    LOG_INFO("changeBackend is true, counter: {0}", contextData->changeBackendCounter);

    // first release everything
    if (contextData->changeBackendCounter == 1)
    {
        if (contextData->feature != nullptr)
        {
            LOG_INFO("changing backend to {}", UpscalerDisplayName(state.newBackend));

            auto* dc = contextData->feature.get();
            // Use given params if using DLSS passthrough
            const bool isPassthrough = state.newBackend == Upscaler::DLSSD || state.newBackend == Upscaler::DLSS;

            contextData->createParams = isPassthrough ? parameters : GetNGXParameters(API::DX12, false);
            contextData->createParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, dc->GetFeatureFlags());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Width, dc->RenderWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_Height, dc->RenderHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutWidth, dc->DisplayWidth());
            contextData->createParams->Set(NVSDK_NGX_Parameter_OutHeight, dc->DisplayHeight());
            contextData->createParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, dc->PerfQualityValue());

            dc = nullptr;

            State::Instance().currentFeature = nullptr;

            Util::DelayedDestroy(std::move(contextData->feature));

            // LOG_DEBUG("sleeping before reset of current feature for 1000ms");
            // std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            // contextData->feature.reset();
            // contextData->feature = nullptr;
        }
        else // Clean up state if no feature is set
        {
            LOG_ERROR("can't find handle {0} in Dx12Contexts!", handleId);

            state.newBackend = Upscaler::Reset;
            state.changeBackend[handleId] = false;

            if (contextData->createParams != nullptr)
            {
                TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::D3D12_DestroyParameters());
                contextData->createParams = nullptr;
            }

            contextData->changeBackendCounter = 0;

            return false;
        }

        return true;
    }

    // create new feature
    if (contextData->changeBackendCounter == 2)
    {
        LOG_INFO("Creating new {} upscaler", UpscalerDisplayName(state.newBackend));
        contextData->feature.reset();

        if (!GetFeature(state.newBackend, handleId, contextData->createParams, &contextData->feature, device))
        {
            LOG_ERROR("Upscaler can't created");

            // Without a feature the init step would dereference null; end the change here.
            state.newBackend = Upscaler::Reset;
            state.changeBackend[handleId] = false;
            contextData->changeBackendCounter = 0;
            return false;
        }

        return true;
    }

    // init feature
    if (contextData->changeBackendCounter == 3)
    {
        auto initResult = contextData->feature->Init(device, cmdList, contextData->createParams);

        contextData->changeBackendCounter = 0;

        if (!initResult)
        {
            LOG_ERROR("init failed with {0} feature", UpscalerDisplayName(state.newBackend));
            ImGui::InsertNotification({ ImGuiToastType::Error, 10000, "DLSS failed to start after a settings change" });

            // No other backend to fall back to; stop here rather than retrying every frame.
            state.newBackend = Upscaler::Reset;
            state.changeBackend[handleId] = false;

            return false;
        }
        else
        {
            LOG_INFO("init successful for {0}, upscaler changed", UpscalerDisplayName(state.newBackend));

            state.newBackend = Upscaler::Reset;
            state.changeBackend[handleId] = false;
        }

        // If this is an OptiScaler fake NVNGX param table, delete it
        int optiParam = 0;

        if (contextData->createParams->Get("OptiScaler", &optiParam) == NVSDK_NGX_Result_Success && optiParam == 1)
        {
            TryDestroyNGXParameters(contextData->createParams, NVNGXProxy::D3D12_DestroyParameters());
            contextData->createParams = nullptr;
        }
    }

    // if initial feature can't be inited
    state.currentFeature = contextData->feature.get();

    return true;
}
