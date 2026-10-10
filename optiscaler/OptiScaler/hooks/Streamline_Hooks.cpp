#include <pch.h>

#include "Streamline_Hooks.h"
#include "../../../shared/LaminrStreamlinePolicy.h"

#include <Util.h>
#include <Config.h>

#include <proxies/KernelBase_Proxy.h>

#include <json.hpp>
#include <sl1_reflex.h>
#include <magic_enum.hpp>
#include "detours/detours.h"

char* StreamlineHooks::trimStreamlineLog(const char* msg)
{
    char* result = (char*) malloc(strlen(msg) + 1);
    if (!result)
        return nullptr;

    strcpy(result, msg);

    size_t length = strlen(result);
    if (length > 0 && result[length - 1] == '\n')
    {
        result[length - 1] = '\0';
    }

    return result;
}

void StreamlineHooks::streamlineLogCallback(sl::LogType type, const char* msg)
{
    if (msg == nullptr)
        return;

    char* trimmed_msg = trimStreamlineLog(msg);
    if (trimmed_msg != nullptr)
    {
        switch (type)
        {
        case sl::LogType::eWarn:
            LOG_WARN("{}", trimmed_msg);
            break;
        case sl::LogType::eInfo:
            LOG_INFO("{}", trimmed_msg);
            break;
        case sl::LogType::eError:
            LOG_ERROR("{}", trimmed_msg);
            break;
        case sl::LogType::eCount:
            LOG_ERROR("{}", trimmed_msg);
            break;
        }

        free(trimmed_msg);
    }

    if (o_logCallback != nullptr)
        o_logCallback(type, msg);
}

sl::Result StreamlineHooks::hkslInit(const sl::Preferences& pref, uint64_t sdkVersion)
{
    LOG_FUNC();

    sl::Preferences localPref = pref;

    const auto& nativeVersion = State::Instance().streamlineVersion;
    const bool gamePluginsOnly = LaminrStreamlinePolicy::ApplyNativePolicy(
        Util::ExePath().filename().wstring(), nativeVersion.major, nativeVersion.minor, localPref);
    if (gamePluginsOnly)
    {
        // Keep the game's interposer and plugins together. Old MSFS 2020
        // interposers do not implement all hooks advertised by newer OTA plugins.
        LOG_INFO("MSFS2020 native Streamline compatibility: game plugin paths preserved; OTA download/load disabled; sdk:{} flags:{}->{} paths:{} features:{}",
                 sdkVersion, static_cast<uint64_t>(pref.flags), static_cast<uint64_t>(localPref.flags),
                 pref.numPathsToPlugins, pref.numFeaturesToLoad);
    }

    if (localPref.logMessageCallback != &streamlineLogCallback)
        o_logCallback = localPref.logMessageCallback;
    localPref.logLevel = sl::LogLevel::eCount;
    localPref.logMessageCallback = &streamlineLogCallback;

    // Treat engine type set in Streamline as ground truth
    if (localPref.engine == sl::EngineType::eUnreal)
        State::Instance().gameEngine = GameEngineType::Unreal;

    return o_slInit(localPref, sdkVersion);
}

sl::Result StreamlineHooks::hkslEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame,
                                                const sl::BaseStructure** inputs, uint32_t numInputs,
                                                sl::CommandBuffer* cmdBuffer)
{
    LOG_DEBUG("frameIndex: {}", static_cast<uint32_t>(frame));

    auto result = o_slEvaluateFeature(feature, frame, inputs, numInputs, cmdBuffer);
    return result;
}

sl::Result StreamlineHooks::hkslAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature,
                                                  const sl::ViewportHandle& viewport)
{
    LOG_FUNC();
    auto result = o_slAllocateResources(cmdBuffer, feature, viewport);
    return result;
}

sl::Result StreamlineHooks::hkslGetNativeInterface(void* proxyInterface, void** baseInterface)
{
    LOG_FUNC();
    auto result = o_slGetNativeInterface(proxyInterface, baseInterface);
    return result;
}

sl::Result StreamlineHooks::hkslSetD3DDevice(void* d3dDevice)
{
    LOG_FUNC();
    auto result = o_slSetD3DDevice(d3dDevice);
    return result;
}

void StreamlineHooks::streamlineLogCallback_sl1(sl1::LogType type, const char* msg)
{
    if (msg == nullptr)
        return;

    char* trimmed_msg = trimStreamlineLog(msg);

    if (trimmed_msg != nullptr)
    {
        switch (type)
        {
        case sl1::LogType::eLogTypeWarn:
            LOG_WARN("{}", trimmed_msg);
            break;
        case sl1::LogType::eLogTypeInfo:
            LOG_INFO("{}", trimmed_msg);
            break;
        case sl1::LogType::eLogTypeError:
            LOG_ERROR("{}", trimmed_msg);
            break;
        case sl1::LogType::eLogTypeCount:
            LOG_ERROR("{}", trimmed_msg);
            break;
        }

        free(trimmed_msg);
    }

    if (o_logCallback_sl1)
        o_logCallback_sl1(type, msg);
}

bool StreamlineHooks::hkslInit_sl1(const sl1::Preferences& pref, int applicationId)
{
    LOG_FUNC();

    sl1::Preferences localPref = pref;

    if (localPref.logMessageCallback != &streamlineLogCallback_sl1)
        o_logCallback_sl1 = localPref.logMessageCallback;
    localPref.logLevel = sl1::LogLevel::eLogLevelCount;
    localPref.logMessageCallback = &streamlineLogCallback_sl1;
    return o_slInit_sl1(localPref, applicationId);
}

bool StreamlineHooks::hkcommon_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                              const char** pluginJSON)
{
    LOG_FUNC();

    // TODO: do it better than "static" and hoping for the best
    static std::string config;

    auto result = o_common_slOnPluginLoad(params, loaderJSON, pluginJSON);

    nlohmann::json configJson = nlohmann::json::parse(*pluginJSON);

    auto& slVersion = State::Instance().streamlineVersion;

    // Grab a version of the potentially updated sl.common
    // Opti assumes that all plugins will have this version
    configJson.at("version").at("major").get_to(slVersion.major);
    configJson.at("version").at("minor").get_to(slVersion.minor);
    configJson.at("version").at("build").get_to(slVersion.patch);

    // Completely disables Streamline hooks
    // if (true)
    //    configJson["hooks"].clear();
    //    configJson["exclusive_hooks"].clear();
    //}

    config = configJson.dump();

    *pluginJSON = config.c_str();

    return result;
}

sl::Result StreamlineHooks::hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options)
{
    lastDlssgViewport = viewport;
    lastDlssgOptions = options;

    // Avoid reading past the game's struct's size
    sl::DLSSGOptions newOptions {};
    auto newStructVer = newOptions.structVersion;

    if (options.structVersion == 1)
        memcpy(&newOptions, &options, 104);
    else if (options.structVersion == 2 || options.structVersion == 3)
        memcpy(&newOptions, &options, 112);
    else if (options.structVersion == 4 || options.structVersion == 5)
        memcpy(&newOptions, &options, 120);
    else
        newOptions = options;

    newOptions.structVersion = newStructVer;

    auto& state = State::Instance();

    // Make DLSSG auto always mean On
    if (newOptions.mode == sl::DLSSGMode::eAuto)
        newOptions.mode = sl::DLSSGMode::eOn;

    const auto dlssgPotentiallyActive = newOptions.mode == sl::DLSSGMode::eOn ||
                                        newOptions.mode == sl::DLSSGMode::eAuto ||
                                        newOptions.mode == sl::DLSSGMode::eDynamic;

    LOG_TRACE("DLSSG Modified Mode: {}", magic_enum::enum_name(newOptions.mode));

    // Populate dlssgMfgMax once
    if (dlssgPotentiallyActive && state.streamlineVersion >= feature_version { 2, 7, 1 } &&
        !state.dlssgMfgMax.has_value())
    {
        sl::DLSSGState localState {};
        sl::DLSSGOptions localOptions {};
        if (o_slDLSSGGetState(viewport, localState, &localOptions) == sl::Result::eOk &&
            localState.numFramesToGenerateMax > 0 && localState.numFramesToGenerateMax < 6)
        {
            state.dlssgMfgMax = localState.numFramesToGenerateMax;
            LOG_TRACE("Saving original numFramesToGenerateMax: {}", state.dlssgMfgMax.value());
        }
    }

    const auto result = o_slDLSSGSetOptions(viewport, newOptions);
    // A rejected request must not become the effective mode used by Reflex.
    if (result == sl::Result::eOk) state.dlssgLastSetMode = newOptions.mode;
    return result;
}

sl::Result StreamlineHooks::hkslDLSSGGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                              const sl::DLSSGOptions* options)
{
    sl::Result result {};

    const auto originalStructVersion = state.structVersion;
    if (originalStructVersion < 4)
    {
        sl::DLSSGState newState {};

        // We might be feeding a newer struct to an older SL but that seems to work just fine for this Get function
        result = o_slDLSSGGetState(viewport, dynamic_cast<sl::DLSSGState&>(newState), options);
        if (result != sl::Result::eOk)
            return result;

        // Copy back data to game's struct
        memcpy(&state, &newState, 56); // struct ver 1 size
        state.structVersion = originalStructVersion;

        if (originalStructVersion >= 2)
        {
            state.numFramesToGenerateMax = newState.numFramesToGenerateMax;
            state.bReserved4 = newState.bReserved4;
            state.bIsVsyncSupportAvailable = newState.bIsVsyncSupportAvailable;
        }

        if (originalStructVersion >= 3)
        {
            state.inputsProcessingCompletionFence = newState.inputsProcessingCompletionFence;
            state.lastPresentInputsProcessingCompletionFenceValue =
                newState.lastPresentInputsProcessingCompletionFenceValue;
        }

        State::Instance().dlssgGameDMFGSupported = newState.bIsDynamicMFGSupported == sl::eTrue;
    }
    else
    {
        result = o_slDLSSGGetState(viewport, state, options);
        if (result != sl::Result::eOk)
            return result;
        State::Instance().dlssgGameDMFGSupported = state.bIsDynamicMFGSupported == sl::eTrue;
    }

    auto& optiState = State::Instance();

    if (optiState.streamlineVersion >= feature_version { 2, 7, 1 } && !optiState.dlssgMfgMax.has_value())
    {
        sl::DLSSGState localState {};
        sl::DLSSGOptions localOptions {};
        if (o_slDLSSGGetState(viewport, localState, &localOptions) == sl::Result::eOk &&
            localState.numFramesToGenerateMax > 0 && localState.numFramesToGenerateMax < 6)
        {
            optiState.dlssgMfgMax = localState.numFramesToGenerateMax;
            LOG_TRACE("Saving original numFramesToGenerateMax: {}", optiState.dlssgMfgMax.value());
        }
    }

    return result;
}

void* StreamlineHooks::hkdlssg_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slDLSSGSetOptions") == 0)
    {
        o_slDLSSGSetOptions = (decltype(&slDLSSGSetOptions)) o_dlssg_slGetPluginFunction(functionName);

        // Give steam overlay the original as it seems to be hooking it
        auto steamOverlay = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
        if (steamOverlay != nullptr)
        {
            if (HMODULE callerModule = Util::GetCallerModule(_ReturnAddress()); callerModule == steamOverlay)
                return o_slDLSSGSetOptions;
        }

        return &hkslDLSSGSetOptions;
    }

    if (strcmp(functionName, "slDLSSGGetState") == 0)
    {
        o_slDLSSGGetState = (decltype(&slDLSSGGetState)) o_dlssg_slGetPluginFunction(functionName);

        // Give steam overlay the original as it seems to be hooking it
        auto steamOverlay = KernelBaseProxy::GetModuleHandleA_()("gameoverlayrenderer64.dll");
        if (steamOverlay != nullptr)
        {
            if (HMODULE callerModule = Util::GetCallerModule(_ReturnAddress()); callerModule == steamOverlay)
                return o_slDLSSGGetState;
        }

        return &hkslDLSSGGetState;
    }

    // Ensure that we have those DLSSG calls
    if (!o_slDLSSGSetOptions)
        o_slDLSSGSetOptions = (decltype(&slDLSSGSetOptions)) o_dlssg_slGetPluginFunction("slDLSSGSetOptions");

    if (!o_slDLSSGGetState)
        o_slDLSSGGetState = (decltype(&slDLSSGGetState)) o_dlssg_slGetPluginFunction("slDLSSGGetState");

    return o_dlssg_slGetPluginFunction(functionName);
}

bool StreamlineHooks::hkreflex_slSetConstants_sl1(const void* data, uint32_t frameIndex, uint32_t id)
{
    // Streamline v1's version of slReflexSetOptions + slPCLSetMarker
    static sl1::ReflexConstants constants {};
    constants = *(const sl1::ReflexConstants*) data;

    LOG_DEBUG("mode: {}, frameIndex: {}, id: {}", (uint32_t) constants.mode, frameIndex, id);

    return o_reflex_slSetConstants_sl1(&constants, frameIndex, id);
}

void* StreamlineHooks::hkreflex_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slSetConstants") == 0 && State::Instance().streamlineVersion.major == 1)
    {
        o_reflex_slSetConstants_sl1 = (PFN_slSetConstants_sl1) o_reflex_slGetPluginFunction(functionName);
        return &hkreflex_slSetConstants_sl1;
    }

    return o_reflex_slGetPluginFunction(functionName);
}

void* StreamlineHooks::hkcommon_slGetPluginFunction(const char* functionName)
{
    // LOG_DEBUG("{}", functionName);

    if (strcmp(functionName, "slOnPluginLoad") == 0)
    {
        o_common_slOnPluginLoad = (PFN_slOnPluginLoad) o_common_slGetPluginFunction(functionName);
        return &hkcommon_slOnPluginLoad;
    }

    return o_common_slGetPluginFunction(functionName);
}

void StreamlineHooks::updateDlssgOptions()
{
    if (o_slDLSSGSetOptions)
    {
        LOG_FUNC();
        hkslDLSSGSetOptions(lastDlssgViewport, lastDlssgOptions);
    }
}

// SL INTERPOSER

void StreamlineHooks::unhookInterposer()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_slEvaluateFeature)
        DetourDetach(&(PVOID&) o_slEvaluateFeature, hkslEvaluateFeature);

    if (o_slInit)
        DetourDetach(&(PVOID&) o_slInit, hkslInit);

    if (o_slInit_sl1)
        DetourDetach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

    // if (o_logCallback)
    //     DetourDetach(&(PVOID&) o_logCallback, streamlineLogCallback);
    // else if (o_logCallback_sl1)
    //     DetourDetach(&(PVOID&) o_logCallback_sl1, streamlineLogCallback);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("DetourTransactionCommit error: {:X}", detourResult);
    }
    else
    {
        o_slInit = nullptr;
        o_slInit_sl1 = nullptr;
        o_slEvaluateFeature = nullptr;
        o_logCallback = nullptr;
        o_logCallback_sl1 = nullptr;
    }
}

// Call it just after sl.interposer's load or if sl.interposer is already loaded
void StreamlineHooks::hookInterposer(HMODULE slInterposer)
{
    LOG_FUNC();

    if (!slInterposer)
    {
        LOG_WARN("Streamline module in NULL");
        return;
    }

    // Interposer needs this or it might end in an infinite loop calling itself
    static HMODULE last_slInterposer = nullptr;

    if (last_slInterposer == slInterposer)
        return;

    last_slInterposer = slInterposer;

    // Looks like when reading DLL version load methods are called
    // To prevent loops disabling checks for sl.interposer.dll
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "sl.interposer");

    if (o_slInit || o_slInit_sl1)
        unhookInterposer();

    {
        char dllPath[MAX_PATH];
        GetModuleFileNameA(slInterposer, dllPath, MAX_PATH);

        LOG_TRACE("slInterposer path: {}", dllPath);

        version_t sl_version;
        Util::GetFileVersion(string_to_wstring(dllPath), &sl_version);

        State::Instance().streamlineVersion.major = sl_version.major;
        State::Instance().streamlineVersion.minor = sl_version.minor;
        State::Instance().streamlineVersion.patch = sl_version.patch;

        LOG_INFO("Streamline version: {}.{}.{}", sl_version.major, sl_version.minor, sl_version.patch);

        if (sl_version.major >= 2)
        {
            o_slInit = reinterpret_cast<decltype(&slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));
            o_slEvaluateFeature = reinterpret_cast<decltype(&slEvaluateFeature)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slEvaluateFeature"));
            o_slAllocateResources = reinterpret_cast<decltype(&slAllocateResources)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slAllocateResources"));
            o_slGetNativeInterface = reinterpret_cast<decltype(&slGetNativeInterface)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slGetNativeInterface"));
            o_slSetD3DDevice = reinterpret_cast<decltype(&slSetD3DDevice)>(
                KernelBaseProxy::GetProcAddress_()(slInterposer, "slSetD3DDevice"));

            if (o_slInit != nullptr)
            {
                LOG_TRACE("Hooking v2");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                DetourAttach(&(PVOID&) o_slInit, hkslInit);

                if (o_slEvaluateFeature != nullptr)
                    DetourAttach(&(PVOID&) o_slEvaluateFeature, hkslEvaluateFeature);

                // if (o_slAllocateResources != nullptr)
                //     DetourAttach(&(PVOID&) o_slAllocateResources, hkslAllocateResources);

                // if (o_slGetNativeInterface != nullptr)
                //     DetourAttach(&(PVOID&) o_slGetNativeInterface, hkslGetNativeInterface);

                // if (o_slSetD3DDevice != nullptr)
                //     DetourAttach(&(PVOID&) o_slSetD3DDevice, hkslSetD3DDevice);

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("Failed to hook sl.interposer v2: {:X}", detourResult);
                    o_slInit = nullptr;
                    o_slEvaluateFeature = nullptr;
                    o_slAllocateResources = nullptr;
                    o_slGetNativeInterface = nullptr;
                    o_slSetD3DDevice = nullptr;
                }
            }
        }
        else if (sl_version.major == 1)
        {
            o_slInit_sl1 =
                reinterpret_cast<decltype(&sl1::slInit)>(KernelBaseProxy::GetProcAddress_()(slInterposer, "slInit"));

            LOG_INFO("SL1 exports - slInit: {}", o_slInit_sl1 != nullptr);

            if (o_slInit_sl1)
            {
                LOG_TRACE("Hooking v1");
                DetourTransactionBegin();
                DetourUpdateThread(GetCurrentThread());

                DetourAttach(&(PVOID&) o_slInit_sl1, hkslInit_sl1);

                auto detourResult = DetourTransactionCommit();
                if (detourResult != NO_ERROR)
                {
                    LOG_ERROR("Failed to hook sl.interposer v1: {:X}", detourResult);
                    o_slInit_sl1 = nullptr;
                }
            }
        }
    }

    State::EnableChecks(owner);
}

// SL DLSSG

void StreamlineHooks::unhookDlssg()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_dlssg_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook DLSSG: {:X}", detourResult);
        o_dlssg_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookDlssg(HMODULE slDlssg)
{
    LOG_FUNC();

    if (!slDlssg)
    {
        LOG_WARN("Dlssg module in NULL");
        return;
    }

    if (o_dlssg_slGetPluginFunction)
        unhookDlssg();

    o_dlssg_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slDlssg, "slGetPluginFunction"));

    if (o_dlssg_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.dlssg");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_dlssg_slGetPluginFunction, hkdlssg_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook DLSSG: {:X}", detourResult);
            o_dlssg_slGetPluginFunction = nullptr;
        }
    }
}

// SL REFLEX

void StreamlineHooks::unhookReflex()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_reflex_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_reflex_slGetPluginFunction, hkreflex_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook Reflex: {:X}", detourResult);
    }
    else
    {
        o_reflex_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookReflex(HMODULE slReflex)
{
    LOG_FUNC();

    if (!slReflex)
    {
        LOG_WARN("Reflex module in NULL");
        return;
    }

    if (o_reflex_slGetPluginFunction)
        unhookReflex();

    o_reflex_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slReflex, "slGetPluginFunction"));

    if (o_reflex_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.reflex");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_reflex_slGetPluginFunction, hkreflex_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook Reflex: {:X}", detourResult);
            o_reflex_slGetPluginFunction = nullptr;
        }
    }
}

// SL COMMON

void StreamlineHooks::unhookCommon()
{
    LOG_FUNC();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_common_slGetPluginFunction)
        DetourDetach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook Common: {:X}", detourResult);
    }
    else
    {
        o_common_slGetPluginFunction = nullptr;
    }
}

void StreamlineHooks::hookCommon(HMODULE slCommon)
{
    LOG_FUNC();

    if (!slCommon)
    {
        LOG_WARN("Common module in NULL");
        return;
    }

    if (o_common_slGetPluginFunction)
        unhookCommon();

    o_common_slGetPluginFunction =
        reinterpret_cast<PFN_slGetPluginFunction>(KernelBaseProxy::GetProcAddress_()(slCommon, "slGetPluginFunction"));

    if (o_common_slGetPluginFunction != nullptr)
    {
        LOG_TRACE("Hooking slGetPluginFunction in sl.common");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_common_slGetPluginFunction, hkcommon_slGetPluginFunction);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook Common: {:X}", detourResult);
            o_common_slGetPluginFunction = nullptr;
        }
    }
}

bool StreamlineHooks::isInterposerHooked() { return o_slInit != nullptr || o_slInit_sl1 != nullptr; }

bool StreamlineHooks::isDlssgHooked() { return o_dlssg_slGetPluginFunction != nullptr; }

bool StreamlineHooks::isCommonHooked() { return o_common_slGetPluginFunction != nullptr; }

bool StreamlineHooks::isReflexHooked() { return o_reflex_slGetPluginFunction != nullptr; }
