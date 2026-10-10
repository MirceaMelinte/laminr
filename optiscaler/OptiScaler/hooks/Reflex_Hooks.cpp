#include "pch.h"
#include "Reflex_Hooks.h"
#include <Config.h>

#include <magic_enum.hpp>

#include <math.h>

// #define LOG_REFLEX_CALLS

std::optional<TimingEntry> ReflexHooks::timingData[TimingType::TimingTypeCOUNT] {};

NvAPI_Status ReflexHooks::hkNvAPI_D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif
    // Store for later so we can adjust the fps whenever we want
    memcpy(&_lastSleepParams, pSetSleepModeParams, sizeof(NV_SET_SLEEP_MODE_PARAMS));
    _lastSleepDev = pDev;

    if (_minimumIntervalUs != 0)
        pSetSleepModeParams->minimumIntervalUs = _minimumIntervalUs;

    return o_NvAPI_D3D_SetSleepMode(pDev, pSetSleepModeParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_Sleep(IUnknown* pDev)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    _lastSleepDev = pDev;
    return o_NvAPI_D3D_Sleep(pDev);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    return o_NvAPI_D3D_GetLatency(pDev, pGetLatencyParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D_SetLatencyMarker(IUnknown* pDev,
                                                       NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    _updatesWithoutMarker = 0;

    // LOG_DEBUG("frameID: {}, markerType: {}", pSetLatencyMarkerParams->frameID,
    //           magic_enum::enum_name(pSetLatencyMarkerParams->markerType));

    // Some games just stop sending any async markers when DLSSG is disabled, so a reset is needed
    if (_lastAsyncMarkerFrameId + 10 < pSetLatencyMarkerParams->frameID)
    {
        _FgNumFramesToGenerate = 0;
    }

    // RTSS reflex markers have a frameid marker in the high bits
    if (pSetLatencyMarkerParams->frameID >> 32)
        State::Instance().rtssReflexInjection = true;

    // TODO: reflexFrameId gets constantly changed, up and down depending on the marker
    State::Instance().reflexFrameId = pSetLatencyMarkerParams->frameID;

    return o_NvAPI_D3D_SetLatencyMarker(pDev, pSetLatencyMarkerParams);
}

NvAPI_Status ReflexHooks::hkNvAPI_D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                            NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams)
{
#ifdef LOG_REFLEX_CALLS
    LOG_FUNC();
#endif

    _lastAsyncMarkerFrameId = pSetAsyncFrameMarkerParams->frameID;

    // if (pSetAsyncFrameMarkerParams->markerType == OUT_OF_BAND_PRESENT_START)
    //{
    //     constexpr size_t history_size = 20;
    //     static size_t counter = 0;
    //     static NvU64 previous_frame_ids[history_size] = {};

    //    previous_frame_ids[counter % history_size] = pSetAsyncFrameMarkerParams->frameID;
    //    counter++;

    //    size_t valid_count = (counter < history_size) ? counter : history_size;

    //    size_t max_run_length = 1;
    //    size_t current_run = 1;

    //    for (size_t i = 1; i < valid_count; i++)
    //    {
    //        if (previous_frame_ids[i] == previous_frame_ids[i - 1])
    //        {
    //            current_run++;
    //        }
    //        else
    //        {
    //            max_run_length = std::max(max_run_length, current_run);
    //            current_run = 1;
    //        }
    //    }

    //    max_run_length = std::max(max_run_length, current_run);

    //    int detected_mode = 0;
    //    if (max_run_length >= 4)
    //        detected_mode = 3;
    //    else if (max_run_length == 3)
    //        detected_mode = 2;
    //    else if (max_run_length == 2)
    //        detected_mode = 1;

    //    // --- Stability filter ---
    //    static int candidate_mode = 0; // No FG at start
    //    static int candidate_count = 0;
    //    constexpr int stability_threshold = 4;

    //    if (detected_mode == candidate_mode)
    //    {
    //        candidate_count++;
    //    }
    //    else
    //    {
    //        candidate_mode = detected_mode;
    //        candidate_count = 1;
    //    }

    //    // Redundant
    //    // max_run_length = std::max(max_run_length, current_run);

    //    // Only commit after stable detection
    //    if (candidate_count >= stability_threshold)
    //    {
    //        if (candidate_mode == 0 && _FgNumFramesToGenerate > 0)
    //        {
    //            _FgNumFramesToGenerate = 0;
    //            LOG_DEBUG("DLSS FG no longer detected");
    //        }
    //        else if (_FgNumFramesToGenerate != candidate_mode)
    //        {
    //            _FgNumFramesToGenerate = candidate_mode;

    //            LOG_DEBUG("DLSS FG detected: {}x mode", candidate_mode + 1);
    //        }
    //    }
    //}

    return o_NvAPI_D3D12_SetAsyncFrameMarker(pCommandQueue, pSetAsyncFrameMarkerParams);
}

void ReflexHooks::hookReflex(PFN_NvApi_QueryInterface& queryInterface)
{
    if (!_inited)
    {
#ifdef _DEBUG
        LOG_FUNC();
#endif

        o_NvAPI_D3D_SetSleepMode = GET_INTERFACE(NvAPI_D3D_SetSleepMode, queryInterface);
        o_NvAPI_D3D_Sleep = GET_INTERFACE(NvAPI_D3D_Sleep, queryInterface);
        o_NvAPI_D3D_GetLatency = GET_INTERFACE(NvAPI_D3D_GetLatency, queryInterface);
        o_NvAPI_D3D_SetLatencyMarker = GET_INTERFACE(NvAPI_D3D_SetLatencyMarker, queryInterface);
        o_NvAPI_D3D12_SetAsyncFrameMarker = GET_INTERFACE(NvAPI_D3D12_SetAsyncFrameMarker, queryInterface);

        _inited = o_NvAPI_D3D_SetSleepMode && o_NvAPI_D3D_Sleep && o_NvAPI_D3D_GetLatency &&
                  o_NvAPI_D3D_SetLatencyMarker && o_NvAPI_D3D12_SetAsyncFrameMarker;

        if (_inited)
            LOG_DEBUG("Inited Reflex hooks");
    }
}

uint8_t ReflexHooks::dlssgFrameCountToGenerate() { return _FgNumFramesToGenerate; }

void ReflexHooks::setDlssgFrameCount(uint8_t count) { _FgNumFramesToGenerate = count; }

bool ReflexHooks::isReflexHooked() { return _inited; }

void* ReflexHooks::getHookedReflex(unsigned int InterfaceId)
{
    if (InterfaceId == GET_ID(NvAPI_D3D_SetSleepMode) && o_NvAPI_D3D_SetSleepMode)
    {
        return &hkNvAPI_D3D_SetSleepMode;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D_Sleep) && o_NvAPI_D3D_Sleep)
    {
        return &hkNvAPI_D3D_Sleep;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D_GetLatency) && o_NvAPI_D3D_GetLatency)
    {
        return &hkNvAPI_D3D_GetLatency;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D_SetLatencyMarker) && o_NvAPI_D3D_SetLatencyMarker)
    {
        return &hkNvAPI_D3D_SetLatencyMarker;
    }
    if (InterfaceId == GET_ID(NvAPI_D3D12_SetAsyncFrameMarker) && o_NvAPI_D3D12_SetAsyncFrameMarker)
    {
        return &hkNvAPI_D3D12_SetAsyncFrameMarker;
    }

    return nullptr;
}

#define UPDATE_TIMING_ENTRY(name, type)                                                                                \
    if (frameReport.name##EndTime >= frameReport.name##StartTime)                                                      \
    {                                                                                                                  \
        double name##Pos = (double) (frameReport.name##StartTime - start) / rangeNs;                                   \
        double name##Length = (double) (frameReport.name##EndTime - frameReport.name##StartTime) / rangeNs;            \
        timingData[TimingType::type] = TimingEntry { .position = name##Pos, .length = name##Length };                  \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
        timingData[TimingType::type].reset();                                                                          \
    }

bool ReflexHooks::updateTimingData()
{
    if (!_inited)
        return false;

    auto processFrameReport = [&](const auto& frameReport) -> bool
    {
        uint64_t start = UINT64_MAX;
        uint64_t end = 0;

        // Please don't look, just thought it would be least work
        auto pTimes = (const uint64_t*) &frameReport.simStartTime;
        for (auto i = 0; i < 11; i++)
        {
            auto& time = pTimes[i];
            if (time == 0)
                continue;

            if (time < start)
                start = time;

            if (time > end)
                end = time;
        }

        if (end < start)
            return false;

        double rangeNs = static_cast<double>(end - start);

        timingData[TimingType::TimeRange] = TimingEntry { .position = 0, .length = rangeNs };
        UPDATE_TIMING_ENTRY(sim, Simulation)
        UPDATE_TIMING_ENTRY(renderSubmit, RenderSubmit)
        UPDATE_TIMING_ENTRY(present, Present)
        UPDATE_TIMING_ENTRY(driver, Driver)
        UPDATE_TIMING_ENTRY(osRenderQueue, OsRenderQueue)
        UPDATE_TIMING_ENTRY(gpuRender, GpuRender)

        if (frameReport.frameID != 0)
            State::Instance().reflexFrameId = frameReport.frameID;

        return true;
    };

    if (_lastSleepDev && o_NvAPI_D3D_GetLatency)
    {
        // Not calling free on this but it's static so hopefully fine
        static NV_LATENCY_RESULT_PARAMS* results = new NV_LATENCY_RESULT_PARAMS();
        results->version = NV_LATENCY_RESULT_PARAMS_VER;

        if (auto result = hkNvAPI_D3D_GetLatency(_lastSleepDev, results); result != NVAPI_OK)
        {
            LOG_WARN("NvAPI_D3D_GetLatency failed: {}", magic_enum::enum_name(result));
            return false;
        }

        // 64th element has the latest data
        return processFrameReport(results->frameReport[63]);
    }

    return false;
}

// For updating information about Reflex hooks
void ReflexHooks::update()
{
    // We can still use just the markers to limit the fps with Reflex disabled
    // But need to fallback in case a game stops sending them for some reason
    _updatesWithoutMarker++;

    if (_updatesWithoutMarker > 20 || !_inited)
    {
        State::Instance().reflexLimitsFps = false;
        return;
    }

    State::Instance().reflexLimitsFps = _lastSleepDev != nullptr;

    static float lastFps = 0;
    static bool lastReflexLimitsFps = State::Instance().reflexLimitsFps;

    // Reset required when toggling Reflex
    if (State::Instance().reflexLimitsFps != lastReflexLimitsFps)
    {
        lastReflexLimitsFps = State::Instance().reflexLimitsFps;
        lastFps = 0;
        setFPSLimit(0);
    }

    if (!State::Instance().reflexLimitsFps)
        return;

    float currentFps = Config::Instance()->FramerateLimit.value_or_default();
    static uint8_t lastFgNumFramesToGenerate = 0;

    if (lastFgNumFramesToGenerate != _FgNumFramesToGenerate)
    {
        lastFgNumFramesToGenerate = _FgNumFramesToGenerate;
        setFPSLimit(currentFps);

        if (_FgNumFramesToGenerate == 0)
            LOG_DEBUG("DLSS FG no longer detected");
        else
            LOG_DEBUG("DLSS FG detected, mode: {}x", _FgNumFramesToGenerate + 1);
    }

    if (currentFps != lastFps)
    {
        setFPSLimit(currentFps);
        lastFps = currentFps;
    }
}

// 0 - disables the fps cap
void ReflexHooks::setFPSLimit(float fps)
{
    LOG_INFO("Set FPS Limit to: {}", fps);
    if (fps == 0.0)
        _minimumIntervalUs = 0;
    else
        _minimumIntervalUs = static_cast<uint32_t>(std::round(1'000'000 / fps));

    if (_lastSleepDev != nullptr)
    {
        NV_SET_SLEEP_MODE_PARAMS temp {};
        memcpy(&temp, &_lastSleepParams, sizeof(NV_SET_SLEEP_MODE_PARAMS));
        temp.minimumIntervalUs = _minimumIntervalUs;
        o_NvAPI_D3D_SetSleepMode(_lastSleepDev, &temp);
    }
}
