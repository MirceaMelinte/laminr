// DLSS Neural Rendering calls, isolated in a module the snippet will accept as a caller.
//
// The snippet resolves the module owning its return address and requires that module's path to contain
// "nvngx.dll" (the driver core is _nvngx.dll), rejecting anything else with FAIL_PlatformError before it
// inspects a single argument. Neither a ReShade add-on nor OptiScaler is named anything like that, so the
// calls are made from here instead and reached through the exports below.
//
// The parameter block is the core's capability block rather than a fresh one: it carries the snippet and
// preset callbacks a feature expects at create time. The core exports no Set/Get helpers (they are
// static-library inlines), so it is driven through its vtable. NVSDK_NGX_Parameter declares eight Set
// overloads then eight Get overloads, in this order: ULL, float, double, uint, int, ID3D11Resource*,
// ID3D12Resource*, void*.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {

// Slot indices confirmed by round-tripping values through the live block: a setter at N is read back by
// the getter at N+8. The unsigned setter is slot 3 (a feature create driven through it succeeds) and the
// resource getter answers at slot 8, so resources are written through slot 0 -- the 64-bit setter, which
// is what a resource handle is. Writing them through the typed D3D12 setter left them unset.
constexpr int VT_SET_ULL = 0;
// Where the float setter actually lives. The public header declares it at slot 1, and this block --
// the driver's own, not the header's implementation -- does not keep a float there: every float written
// to slot 1 reads back as FAIL_UnsupportedParameter while every uint lands. The host discovers the real
// slot by round-tripping a value and sets it here before anything else is written.
int g_floatSlot = 1;
constexpr int VT_SET_UINT = 3;

using PFN_SetULL = void(__thiscall *)(void *, const char *, unsigned long long);
using PFN_SetFloat = void(__thiscall *)(void *, const char *, float);
using PFN_SetUInt = void(__thiscall *)(void *, const char *, unsigned int);

void setUInt(void *params, const char *name, unsigned int v) {
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetUInt>(vt[VT_SET_UINT])(params, name, v);
}

void setFloat(void *params, const char *name, float v) {
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetFloat>(vt[g_floatSlot])(params, name, v);
}

void setResourcePtr(void *params, const char *name, void *v) {
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetULL>(vt[VT_SET_ULL])(params, name, (unsigned long long) v);
}

void setResource(void *params, const char *name, ID3D12Resource *v) {
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetULL>(vt[VT_SET_ULL])(params, name, (unsigned long long) v);
}

using PFN_NrInitExt = int(__cdecl *)(unsigned long long, const wchar_t *, ID3D12Device *, int,
                                     const void *);
// An explicit ControlMask takes precedence over the runtime's automatic mask.
// We do not supply one; clear it on the reusable parameter block instead of
// allowing another feature's stale mask/resource to silently override this toggle.
void setAutomaticMask(void *params, int enabled) {
    setResourcePtr(params, "DLSSNR.ControlMask", nullptr);
    setUInt(params, "DLSSNR.UseAutoMask", enabled != 0 ? 1u : 0u);
}
using PFN_NrCreate = int(__cdecl *)(ID3D12GraphicsCommandList *, int, const void *, void **);
using PFN_NrEvaluate = int(__cdecl *)(ID3D12GraphicsCommandList *, const void *, const void *, void *);
using PFN_NrRelease = int(__cdecl *)(void *);

struct Snippet {
    HMODULE module = nullptr;
    PFN_NrInitExt init = nullptr;
    PFN_NrCreate create = nullptr;
    PFN_NrEvaluate evaluate = nullptr;
    PFN_NrRelease release = nullptr;
    std::unordered_set<ID3D12Device*> initialisedDevices;

};

std::recursive_mutex g_snippetMutex;
std::map<std::wstring, Snippet> g_snippets;
std::unordered_map<void*, Snippet*> g_featureOwners;
thread_local std::string g_modelError;

void recordModelError(Snippet* snippet, const char* fallback) {
    auto message = snippet && snippet->module
        ? (const char*(*)())GetProcAddress(snippet->module, "NVFP4_GetLastError") : nullptr;
    const char* detail = message ? message() : nullptr;
    g_modelError = detail && *detail ? detail : fallback;
}

Snippet* loadSnippet(const wchar_t *path) {
    std::lock_guard<std::recursive_mutex> lock(g_snippetMutex);
    if (!path || !*path) return nullptr;
    auto& snippet = g_snippets[path];
    if (snippet.module) {
        return snippet.create && snippet.evaluate && snippet.release ? &snippet : nullptr;
    }
    snippet.module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!snippet.module) {
        return nullptr;
    }
    snippet.init = (PFN_NrInitExt) GetProcAddress(snippet.module, "NVSDK_NGX_D3D12_Init_Ext");
    snippet.create = (PFN_NrCreate) GetProcAddress(snippet.module, "NVSDK_NGX_D3D12_CreateFeature");
    snippet.evaluate = (PFN_NrEvaluate) GetProcAddress(snippet.module, "NVSDK_NGX_D3D12_EvaluateFeature");
    snippet.release = (PFN_NrRelease) GetProcAddress(snippet.module, "NVSDK_NGX_D3D12_ReleaseFeature");


    return snippet.create && snippet.evaluate && snippet.release ? &snippet : nullptr;
}


} // namespace

extern "C" {

// Called once, after the host has worked out which slot this block keeps floats in.
__declspec(dllexport) void dlssnr_call_set_float_slot(int slot) {
    if (slot >= 0 && slot < 8) {
        g_floatSlot = slot;
    }
}

// Writes a float through an arbitrary slot, so the host can find the right one by testing.
__declspec(dllexport) void dlssnr_call_probe_float(void *params, const char *name, float value,
                                                   int slot) {
    if (!params || slot < 0 || slot >= 8) {
        return;
    }
    void **vt = *reinterpret_cast<void ***>(params);
    reinterpret_cast<PFN_SetFloat>(vt[slot])(params, name, value);
}

// Last init and create results, so the add-on can log why a feature never appeared.
__declspec(dllexport) int dlssnr_call_last_init = 0;
__declspec(dllexport) int dlssnr_call_last_create = 0;
__declspec(dllexport) const char* dlssnr_call_error() { return g_modelError.c_str(); }

// ---------------------------------------------------------------------------------------------
// The model's own scaling ratio, asked for the way NVIDIA asks for it.
//
// The snippet publishes callbacks into a parameter block through PopulateParameters_Impl -- the same
// mechanism DLSS uses for DLSSOptimalSettingsCallback. The strings in nvngx_dlssnr.dll spell the
// contract out in order: DLSSNRComputeScalingRatioCallback, ComputeScalingRatioCommon,
// PerfQualityValue, then the two failures "missing PerfQualityValue for DLSSNR scaling ratio
// computation" and "unsupported PerfQualityValue %u ...", then DLSSNR.ScalingRatio itself.
//
// So: populate a block, read the callback out of it, set PerfQualityValue, call it, read the ratio
// back. Read-only -- it creates no feature and changes no feature state, which is exactly why it is
// worth doing before anything is built on the answer.
//
// It lives here rather than in the host because PopulateParameters_Impl is the snippet's own export
// and the snippet resolves its caller's module path.
// ---------------------------------------------------------------------------------------------

using PFN_NrPopulate = int(__cdecl *)(void *);
using PFN_NrRatioCallback = int(__cdecl *)(void *);

// Getters mirror setters eight slots up -- the same rule that put the resource getter at 8 opposite
// the 64-bit setter at 0. A pointer published into the block is written as a 64-bit value, so it comes
// back through slot 8; a float comes back through the discovered float slot plus eight.
constexpr int VT_GET_ULL = VT_SET_ULL + 8;

using PFN_GetULL = int(__thiscall *)(void *, const char *, unsigned long long *);
using PFN_GetFloat = int(__thiscall *)(void *, const char *, float *);

__declspec(dllexport) int dlssnr_last_ratio_result = 0;
__declspec(dllexport) int dlssnr_last_ratio_stage = 0;

// Asks the model what resolution it wants for a given quality level.
//
// perfQuality is NVSDK_NGX_PerfQuality_Value: 0 MaxPerf, 1 Balanced, 2 MaxQuality, 3 UltraPerformance,
// 4 UltraQuality, 5 DLAA. Returns 1 and writes outRatio on success. 0 means the callback was never
// published, so the mechanism is not live in this snippet; -1 means it was published and refused, which
// for this callback means the quality value is not one it supports. dlssnr_last_ratio_stage says how
// far it got, so a zero can be told apart from a snippet that would not load at all.
__declspec(dllexport) int dlssnr_query_scaling_ratio(const wchar_t *snippetPath, void *capabilityParams,
                                                     unsigned int perfQuality, float *outRatio) {
    dlssnr_last_ratio_stage = 0;

    auto* snippet = loadSnippet(snippetPath);
    if (!snippet || !capabilityParams || !outRatio) {
        return 0;
    }

    dlssnr_last_ratio_stage = 1;

    // Publishing is what puts the callback in the block. Harmless if it has already happened.
    auto populate = (PFN_NrPopulate) GetProcAddress(snippet->module, "NVSDK_NGX_D3D12_PopulateParameters_Impl");

    if (populate != nullptr) {
        volatile int populated = populate(capabilityParams);
        (void) populated;
        dlssnr_last_ratio_stage = 2;
    }

    void **vt = *reinterpret_cast<void ***>(capabilityParams);
    unsigned long long raw = 0;

    if (reinterpret_cast<PFN_GetULL>(vt[VT_GET_ULL])(capabilityParams, "DLSSNRComputeScalingRatioCallback",
                                                     &raw) != 1 ||
        raw == 0) {
        return 0;
    }

    dlssnr_last_ratio_stage = 3;

    setUInt(capabilityParams, "PerfQualityValue", perfQuality);

    // Cleared first so a callback that writes nothing cannot be mistaken for one that wrote zero.
    setFloat(capabilityParams, "DLSSNR.ScalingRatio", -1.0f);

    // Assigned rather than returned, for the same reason every other call through this module is.
    volatile int result = reinterpret_cast<PFN_NrRatioCallback>((void *) raw)(capabilityParams);
    dlssnr_last_ratio_result = (int) result;

    if (result != 1) {
        return -1;
    }

    dlssnr_last_ratio_stage = 4;

    float ratio = -1.0f;

    if (reinterpret_cast<PFN_GetFloat>(vt[g_floatSlot + 8])(capabilityParams, "DLSSNR.ScalingRatio",
                                                            &ratio) != 1) {
        return -1;
    }

    dlssnr_last_ratio_stage = 5;
    *outRatio = ratio;
    return 1;
}

// Creates a persistent Neural Rendering feature. The handle records initialisation work into cmd, so it
// must outlive that command list's execution; releasing it early loses the device.
__declspec(dllexport) void *dlssnr_call_create(const wchar_t *snippetPath, const wchar_t *dataPath,
                                               ID3D12Device *device, ID3D12GraphicsCommandList *cmd,
                                               void *capabilityParams, unsigned int width,
                                               unsigned int height, int preset, float intensity,
                                               int style, float localStructure, float localTone,
                                               float skinStructure, int useAutoMask,
                                               int uiCorrection) {
    std::lock_guard<std::recursive_mutex> lock(g_snippetMutex);
    g_modelError.clear();
    auto* snippet = loadSnippet(snippetPath);
    if (!snippet || !capabilityParams) {
        recordModelError(snippet, "model DLL or capability parameters unavailable");
        return nullptr;
    }
    if (!snippet->initialisedDevices.count(device) && snippet->init) {
        // OptiScaler's own generic application id, the one it already hands DLSS when a game's id is
        // not wanted. What was here before was 0x4350324B -- "CP2K" -- so every game that ever loaded
        // this announced itself to the driver as Cyberpunk 2077.
        dlssnr_call_last_init = snippet->init(0x24480451ull, dataPath, device, 0x0000015, capabilityParams);
        if (dlssnr_call_last_init != 1) {
            recordModelError(snippet, "model initialization failed");
            return nullptr;
        }
        snippet->initialisedDevices.insert(device);
    }
    setUInt(capabilityParams, "DLSSNR.Enabled", 1);
    setUInt(capabilityParams, "DLSSNR.Width", width);
    setUInt(capabilityParams, "DLSSNR.Height", height);
    setUInt(capabilityParams, "CreationNodeMask", 1);
    setUInt(capabilityParams, "VisibilityNodeMask", 1);
    // Written unconditionally, including zero. This block belongs to the driver and outlives the
    // feature, so skipping the write for "default" left whichever preset was chosen last still sitting
    // in it -- and going back to default did nothing at all.
    setUInt(capabilityParams, "DLSSNR.Hint.Render.Preset", (unsigned int) preset);

    // The tuning has to be here rather than at evaluate. Everything this sets before create takes
    // effect; everything set only at evaluate is ignored, which is why none of these controls did
    // anything for a long time. The model reads them once, when it builds the feature.
    setFloat(capabilityParams, "DLSSNR.Intensity", intensity);
    setUInt(capabilityParams, "DLSSNR.Style", (unsigned int) style);
    setFloat(capabilityParams, "DLSSNR.LocalStructureStrength", localStructure);
    setFloat(capabilityParams, "DLSSNR.LocalToneStrength", localTone);
    setFloat(capabilityParams, "DLSSNR.SkinStructureStrength", skinStructure);
    setAutomaticMask(capabilityParams, useAutoMask);
    setUInt(capabilityParams, "DLSSNR.UICorrection", (unsigned int) uiCorrection);
    void *handle = nullptr;
    dlssnr_call_last_create = snippet->create(cmd, 18, capabilityParams, &handle);
    if (dlssnr_call_last_create == 1 && handle) g_featureOwners[handle] = snippet;
    else recordModelError(snippet, "model feature creation failed");
    // A partial handle on failure is not usable.
    return dlssnr_call_last_create == 1 ? handle : nullptr;
}

// Colour and output are display resolution; depth and motion come from the game's own DLSS evaluation and
// may be render resolution, so each resource carries its own subrect and motion scales by the ratio.
__declspec(dllexport) int dlssnr_call_evaluate_v2(ID3D12GraphicsCommandList *cmd, void *feature,
                                               void *capabilityParams, ID3D12Resource *color,
                                               ID3D12Resource *depth, ID3D12Resource *motion,
                                               ID3D12Resource *output, unsigned int width,
                                               unsigned int height, unsigned int guideWidth,
                                               unsigned int guideHeight, unsigned int motionWidth,
                                               unsigned int motionHeight, unsigned int depthBaseX,
                                               unsigned int depthBaseY, unsigned int motionBaseX,
                                               unsigned int motionBaseY, int depthInverted, int reset,
                                               float intensity, int style, float localStructure,
                                               float localTone, float skinStructure, int useAutoMask,
                                               float mvScaleX, float mvScaleY) {
    std::lock_guard<std::recursive_mutex> lock(g_snippetMutex);
    g_modelError.clear();
    const auto owner = g_featureOwners.find(feature);
    if (!feature || !capabilityParams || owner == g_featureOwners.end()) {
        g_modelError = "invalid model feature or capability parameters";
        return 0;
    }
    setResource(capabilityParams, "DLSSNR.Color", color);
    setResource(capabilityParams, "DLSSNR.Depth", depth);
    setResource(capabilityParams, "DLSSNR.MVec", motion);
    setResource(capabilityParams, "DLSSNR.Output", output);

    // The block is shared with the game's own DLSS, which overwrites these between frames, so every
    // value the feature reads is set again here rather than relying on what create left behind.
    setUInt(capabilityParams, "DLSSNR.Enabled", 1);
    setUInt(capabilityParams, "DLSSNR.Width", width);
    setUInt(capabilityParams, "DLSSNR.Height", height);
    setUInt(capabilityParams, "DLSSNR.DepthInverted", (unsigned int) depthInverted);
    setUInt(capabilityParams, "DLSSNR.Reset", (unsigned int) reset);

    setUInt(capabilityParams, "DLSSNR.ColorSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.ColorSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.ColorSubrectWidth", width);
    setUInt(capabilityParams, "DLSSNR.ColorSubrectHeight", height);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectWidth", width);
    setUInt(capabilityParams, "DLSSNR.OutputSubrectHeight", height);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectBaseX", depthBaseX);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectBaseY", depthBaseY);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectWidth", guideWidth);
    setUInt(capabilityParams, "DLSSNR.DepthSubrectHeight", guideHeight);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectBaseX", motionBaseX);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectBaseY", motionBaseY);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectWidth", motionWidth);
    setUInt(capabilityParams, "DLSSNR.MVecSubrectHeight", motionHeight);

    // The game's own encoding, passed through. Deriving this from the resolutions was a guess, and at
    // native resolution it came out as exactly 1.0 -- so a game using normalised vectors was telling
    // the model almost nothing had moved.
    setFloat(capabilityParams, "DLSSNR.MVecScaleX", mvScaleX);
    setFloat(capabilityParams, "DLSSNR.MVecScaleY", mvScaleY);

    setFloat(capabilityParams, "DLSSNR.Intensity", intensity);
    setUInt(capabilityParams, "DLSSNR.Style", (unsigned int) style);
    setFloat(capabilityParams, "DLSSNR.LocalStructureStrength", localStructure);
    setFloat(capabilityParams, "DLSSNR.LocalToneStrength", localTone);
    setFloat(capabilityParams, "DLSSNR.SkinStructureStrength", skinStructure);
    setAutomaticMask(capabilityParams, useAutoMask);

    // The result must not be returned directly. `return f(...)` is a tail call, and the compiler emits a
    // jmp rather than a call, which leaves this module's frame behind: the snippet then resolves its
    // caller to whoever called us and rejects it. Keeping the value in a volatile forces a real call and
    // a return through this module, which is the whole reason this file exists.
    volatile int result = owner->second->evaluate(cmd, feature, capabilityParams, nullptr);
    if (result != 1) recordModelError(owner->second, "model evaluation failed");
    return result;
}

// Inputs NVIDIA's own Streamline plugin sets that the positional exports predate: the model's global
// tone strength (read at create), and the interface as the game draws it -- its layer, its alpha, and
// the composited back buffer -- which is what the model's UI correction was designed around. Called
// before create and before every evaluate; absent resources are written as null, because the block
// outlives everything and a stale pointer is a freed resource.
__declspec(dllexport) void dlssnr_call_set_extras(void *capabilityParams, float globalTone,
                                                  ID3D12Resource *ui, ID3D12Resource *uiAlpha,
                                                  ID3D12Resource *backbuffer, unsigned int uiWidth,
                                                  unsigned int uiHeight, unsigned int bbWidth,
                                                  unsigned int bbHeight) {
    if (!capabilityParams) {
        return;
    }
    // DLSSNR.GlobalToneStrength is deliberately not written. The string does not appear anywhere in
    // nvngx_dlssnr.dll -- the model's own vocabulary is 61 DLSSNR.* names and this is not one of them,
    // so every write went into a map nothing reads. It exists in Streamline's sl.dlss_nr.dll, which is
    // presumably where it was copied from. Left as a parameter here so the signature does not churn.
    (void) globalTone;

    setResource(capabilityParams, "DLSSNR.UI", ui);
    setResource(capabilityParams, "DLSSNR.UIAlpha", uiAlpha);
    setResource(capabilityParams, "DLSSNR.Backbuffer", backbuffer);
    setUInt(capabilityParams, "DLSSNR.UISubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.UISubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.UISubrectWidth", uiWidth);
    setUInt(capabilityParams, "DLSSNR.UISubrectHeight", uiHeight);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectWidth", uiWidth);
    setUInt(capabilityParams, "DLSSNR.UIAlphaSubrectHeight", uiHeight);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectBaseX", 0);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectBaseY", 0);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectWidth", bbWidth);
    setUInt(capabilityParams, "DLSSNR.BackbufferSubrectHeight", bbHeight);
}

__declspec(dllexport) void dlssnr_call_release(void *feature) {
    std::lock_guard<std::recursive_mutex> lock(g_snippetMutex);
    const auto owner = g_featureOwners.find(feature);
    if (feature && owner != g_featureOwners.end()) {
        volatile int result = owner->second->release(feature); // retain this module's caller frame
        (void) result;
        g_featureOwners.erase(owner);
    }
}

} // extern "C"
