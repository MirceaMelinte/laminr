#pragma once
#include "SysUtils.h"

#include <sl.h>
#include <sl1.h>
#include <sl_dlss_g.h>
#include <sl_dlss.h>
#include <sl_pcl.h>
#include <sl_reflex.h>

#include "include/sl.param/parameters.h"

#include "Hook_Utils.h"

enum class BufferType : uint64_t
{
    Depth = 0,
    MotionVectors = 1,
    HUDLessColor = 2,
    ScalingInputColor = 3,
    ScalingOutputColor = 4,
    Normals = 5,
    Roughness = 6,
    Albedo = 7,
    SpecularAlbedo = 8,
    IndirectAlbedo = 9,
    SpecularMotionVectors = 10,
    DisocclusionMask = 11,
    Emissive = 12,
    Exposure = 13,
    NormalRoughness = 14,
    DiffuseHitNoisy = 15,
    DiffuseHitDenoised = 16,
    SpecularHitNoisy = 17,
    SpecularHitDenoised = 18,
    ShadowNoisy = 19,
    ShadowDenoised = 20,
    AmbientOcclusionNoisy = 21,
    AmbientOcclusionDenoised = 22,
    UIColorAndAlpha = 23,
    ShadowHint = 24,
    ReflectionHint = 25,
    ParticleHint = 26,
    TransparencyHint = 27,
    AnimatedTextureHint = 28,
    BiasCurrentColorHint = 29,
    RaytracingDistance = 30,
    ReflectionMotionVectors = 31,
    Position = 32,
    InvalidDepthMotionHint = 33,
    Alpha = 34,
    OpaqueColor = 35,
    ReactiveMaskHint = 36,
    TransparencyAndCompositionMaskHint = 37,
    ReflectedAlbedo = 38,
    ColorBeforeParticles = 39,
    ColorBeforeTransparency = 40,
    ColorBeforeFog = 41,
    SpecularHitDistance = 42,
    SpecularRayDirectionHitDistance = 43,
    SpecularRayDirection = 44,
    DiffuseHitDistance = 45,
    DiffuseRayDirectionHitDistance = 46,
    DiffuseRayDirection = 47,
    HiResDepth = 48,
    LinearDepth = 49,
    BidirectionalDistortionField = 50,
    TransparencyLayer = 51,
    TransparencyLayerOpacity = 52,
    Backbuffer = 53,
    NoWarpMask = 54,
    ColorAfterParticles = 55,
    ColorAfterTransparency = 56,
    ColorAfterFog = 57,
    ScreenSpaceSubsurfaceScatteringGuide = 58,
    ColorBeforeScreenSpaceSubsurfaceScattering = 59,
    ColorAfterScreenSpaceSubsurfaceScattering = 60,
    ScreenSpaceRefractionGuide = 61,
    ColorBeforeScreenSpaceRefraction = 62,
    ColorAfterScreenSpaceRefraction = 63,
    DepthOfFieldGuide = 64,
    ColorBeforeDepthOfField = 65,
    ColorAfterDepthOfField = 66,
    ScalingOutputAlpha = 67
};

class StreamlineHooks
{
  public:
    typedef void* (*PFN_slGetPluginFunction)(const char* functionName);
    typedef bool (*PFN_slOnPluginLoad)(sl::param::IParameters* params, const char* loaderJSON, const char** pluginJSON);
    typedef sl::Result (*PFN_slSetData)(const sl::BaseStructure* inputs, sl::CommandBuffer* cmdBuffer);
    typedef bool (*PFN_slSetConstants_sl1)(const void* data, uint32_t frameIndex, uint32_t id);

    static void updateDlssgOptions();

    static void unhookInterposer();
    static void hookInterposer(HMODULE slInterposer);

    static void unhookDlssg();
    static void hookDlssg(HMODULE slDlssg);

    static void unhookReflex();
    static void hookReflex(HMODULE slReflex);

    static void unhookCommon();
    static void hookCommon(HMODULE slCommon);

    static bool isInterposerHooked();
    static bool isDlssgHooked();
    static bool isCommonHooked();
    static bool isReflexHooked();

  private:
    // Interposer
    inline static decltype(&slInit) o_slInit = nullptr;
    inline static decltype(&slEvaluateFeature) o_slEvaluateFeature = nullptr;
    inline static decltype(&slAllocateResources) o_slAllocateResources = nullptr;
    inline static decltype(&slGetNativeInterface) o_slGetNativeInterface = nullptr;
    inline static decltype(&slSetD3DDevice) o_slSetD3DDevice = nullptr;

    inline static decltype(&sl1::slInit) o_slInit_sl1 = nullptr;

    inline static sl::PFun_LogMessageCallback* o_logCallback = nullptr;
    inline static sl1::pfunLogMessageCallback* o_logCallback_sl1 = nullptr;

    static sl::Result hkslInit(const sl::Preferences& pref, uint64_t sdkVersion);
    static bool hkslInit_sl1(const sl1::Preferences& pref, int applicationId);

    static sl::Result hkslEvaluateFeature(sl::Feature feature, const sl::FrameToken& frame,
                                          const sl::BaseStructure** inputs, uint32_t numInputs,
                                          sl::CommandBuffer* cmdBuffer);

    static sl::Result hkslAllocateResources(sl::CommandBuffer* cmdBuffer, sl::Feature feature,
                                            const sl::ViewportHandle& viewport);

    static sl::Result hkslGetNativeInterface(void* proxyInterface, void** baseInterface);

    static sl::Result hkslSetD3DDevice(void* d3dDevice);

    // DLSSG
    inline static PFN_slGetPluginFunction o_dlssg_slGetPluginFunction = nullptr;
    inline static decltype(&slDLSSGSetOptions) o_slDLSSGSetOptions = nullptr;
    inline static decltype(&slDLSSGGetState) o_slDLSSGGetState = nullptr;
    static inline sl::ViewportHandle lastDlssgViewport {}; // For updating options when we change them
    static inline sl::DLSSGOptions lastDlssgOptions {};

    static sl::Result hkslDLSSGSetOptions(const sl::ViewportHandle& viewport, const sl::DLSSGOptions& options);
    static sl::Result hkslDLSSGGetState(const sl::ViewportHandle& viewport, sl::DLSSGState& state,
                                        const sl::DLSSGOptions* options);
    static void* hkdlssg_slGetPluginFunction(const char* functionName);

    // Reflex
    inline static PFN_slGetPluginFunction o_reflex_slGetPluginFunction = nullptr;
    inline static PFN_slSetConstants_sl1 o_reflex_slSetConstants_sl1 = nullptr;

    static bool hkreflex_slSetConstants_sl1(const void* data, uint32_t frameIndex, uint32_t id);
    static void* hkreflex_slGetPluginFunction(const char* functionName);

    // Common
    inline static PFN_slGetPluginFunction o_common_slGetPluginFunction = nullptr;
    inline static PFN_slOnPluginLoad o_common_slOnPluginLoad = nullptr;

    static bool hkcommon_slOnPluginLoad(sl::param::IParameters* params, const char* loaderJSON,
                                        const char** pluginJSON);
    static void* hkcommon_slGetPluginFunction(const char* functionName);

    // Logging
    static char* trimStreamlineLog(const char* msg);
    static void streamlineLogCallback(sl::LogType type, const char* msg);
    static void streamlineLogCallback_sl1(sl1::LogType type, const char* msg);

    // Function signature checking
    VALIDATE_MEMBER_HOOK(hkslInit, decltype(&slInit))
    VALIDATE_MEMBER_HOOK(hkslInit_sl1, decltype(&sl1::slInit))
    VALIDATE_MEMBER_HOOK(hkslEvaluateFeature, decltype(&slEvaluateFeature))
    VALIDATE_MEMBER_HOOK(hkslAllocateResources, decltype(&slAllocateResources))
    VALIDATE_MEMBER_HOOK(hkslGetNativeInterface, decltype(&slGetNativeInterface))
    VALIDATE_MEMBER_HOOK(hkslSetD3DDevice, decltype(&slSetD3DDevice))
    VALIDATE_MEMBER_HOOK(hkslDLSSGSetOptions, decltype(&slDLSSGSetOptions))
    VALIDATE_MEMBER_HOOK(hkslDLSSGGetState, decltype(&slDLSSGGetState))
    VALIDATE_MEMBER_HOOK(hkdlssg_slGetPluginFunction, PFN_slGetPluginFunction)
    VALIDATE_MEMBER_HOOK(hkreflex_slSetConstants_sl1, PFN_slSetConstants_sl1)
    VALIDATE_MEMBER_HOOK(hkreflex_slGetPluginFunction, PFN_slGetPluginFunction)
    VALIDATE_MEMBER_HOOK(hkcommon_slOnPluginLoad, PFN_slOnPluginLoad)
    VALIDATE_MEMBER_HOOK(hkcommon_slGetPluginFunction, PFN_slGetPluginFunction)
};
