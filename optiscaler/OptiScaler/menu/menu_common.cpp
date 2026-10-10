#include "pch.h"
#include "menu_common.h"
#include "Logger.h"
#include "../../../shared/SnapshotRequest.h"
#include "../../../shared/SnapshotStorage.h"
#include "../../../shared/RenderingCapability.h"
#include "../../../shared/D3D12Capabilities.h"
#include <imgui/ImGuiNotify.hpp>
#include <shellapi.h>
#include "laminr_effects_ui.inl"
#include "../../../shared/LaminrVersion.h"
#include <dlssnr/DlssNr_ExposureScan.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <dlssnr/DlssNr.h>

#include "input/input_system.h"

#include "font/Hack_Compressed.h"

#include <hooks/Reflex_Hooks.h>

#include <version_check.h>

#include <imgui/imgui_internal.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <mutex>
#include <cstdarg>

#include <array>
#include <chrono>
#include <memory>
#include <type_traits>
#include <misc/IdentifyGpu.h>

namespace
{
laminr::menu_geometry::State mainMenuGeometry;
std::string mainMenuGeometryContext;

void SaveMenuGeometryBoundary(bool boundary = false, bool editing = false)
{
    const auto now = GetTickCount64();
    if (mainMenuGeometryContext.empty() || !mainMenuGeometry.Due(now, editing, boundary)) return;
    const bool saved = Config::Instance()->SaveMenuGeometry(mainMenuGeometryContext, *mainMenuGeometry.Preferred());
    mainMenuGeometry.Saved(now, saved);
    if (!saved) LOG_WARN("Menu geometry could not be saved; existing configuration retained");
}

std::string MenuGeometryContext(HWND window, bool isUwp)
{
    // The existing menu has HWND-bound surfaces, not a reliable headset/eye ID.
    // Use only that established identity; never infer VR from render dimensions.
    // Separate overlay/direct modes and window classes; simultaneous same-class
    // secondary windows get separate bounded slots. Recreated HWNDs reuse a slot.
    static laminr::menu_geometry::Contexts contexts;
    wchar_t name[256] {};
    if (!window || !GetClassNameW(window, name, 256)) return {};
    uint64_t hash = 14695981039346656037ull;
    for (const auto* p = name; *p; ++p) { hash ^= static_cast<uint16_t>(*p); hash *= 1099511628211ull; }
    const std::string base = std::string(Config::Instance()->OverlayMenu.value_or_default() ? "overlay-" : "direct-") +
        (isUwp ? "uwp-" : "win-") + std::to_string(hash);
    // Excess unknown surfaces get defaults, never overwrite another view.
    return contexts.Select(base,reinterpret_cast<uintptr_t>(window),[](uintptr_t handle) {
        return IsWindow(reinterpret_cast<HWND>(handle)) != FALSE;
    });
}
}

#define MARK_ALL_BACKENDS_CHANGED()                                                                                    \
    for (auto& singleChangeBackend : State::Instance().changeBackend)                                                  \
        singleChangeBackend.second = true;

// Match the live rendering device, not the first/preferred GPU in the PC.
static GpuInformation RenderingGpu(const State& state) {
    LUID luid {};
    if (state.currentD3D12Device) luid = state.currentD3D12Device->GetAdapterLuid();
    if (!luid.HighPart && !luid.LowPart) return {};
    return IdentifyGpu::getGpuByLuid(luid); // Unknown never becomes the first adapter.
}

static float fontSize = 14.0f; // just changing this doesn't make other elements scale ideally
static ImVec2 overlaySize(0.0f, 0.0f);
static ImVec2 overlayPosition(-1000.0f, -1000.0f);
static bool _hdrTonemapApplied = false;
static ImVec4 SdrColors[ImGuiCol_COUNT];

static bool inputMenu = false;
static bool freshMenuPress=false;
static bool backupMenuArmed=false;
static bool inputFps = false;
static bool inputFpsCycle = false;
static uint64_t lastInputTick = 0;
constexpr uint64_t debounceThreshold = 1000;

static bool hasGamepad = false;
static std::string windowTitle;
static std::string selectedUpscalerName = "";
static Upscaler currentBackend = Upscaler::Reset;
static std::string currentBackendName = "";
static int refreshRate = 0;

static std::string updateNoticeTag;
static std::string updateNoticeUrl;
static float lastMenuScale = 0.0f;
static CustomOptional<uint32_t> comboPreset { 0 };
static int lastKey = 0;
static bool inputDlssNr = false;
static bool capturingKey = false;

template <typename T, size_t N> struct RingBuffer
{
    std::array<T, N> data {};
    size_t head { 0 };
    size_t count { N };
    double sum { 0.0 };

    RingBuffer() { data.fill(static_cast<T>(0)); }

    void Push(T v)
    {
        if (count == N)
        {
            sum -= data[head];
        }
        else
        {
            ++count;
        }
        data[head] = v;
        sum += v;
        head = (head + 1) % N;
    }

    size_t Size() const { return N; }

    T At(size_t i) const
    {
        size_t start = head;
        return data[(start + i) % N];
    }

    float Average() const { return static_cast<float>(sum / static_cast<double>(N)); }
};

const int plotWidth = 360;
static RingBuffer<float, plotWidth> gFrameTimes;
static RingBuffer<float, plotWidth> gUpscalerTimes;

inline std::string StrFmt(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(len, '\0');
    va_start(args, fmt);
    std::vsnprintf(out.data(), len + 1, fmt, args);
    va_end(args);
    return out;
}

void MenuCommon::UpdateManualInput(HWND targetHwnd)
{
    OptiInput::BeginFrame(targetHwnd);

    const auto config = Config::Instance();

    auto CheckShortcut = [&](int vk, bool& inputFlag, const char* logMessage)
    {
        if (inputFlag)
            return;

        if (vk <= 0 || vk >= 256)
            return;

        const bool isMenu=vk==config->ShortcutKey.value_or_default();
        if(isMenu&&OptiInput::IsMenuKeyPressed())freshMenuPress=true;
        if ((isMenu?OptiInput::IsMenuKeyReleased():OptiInput::IsKeyReleased(vk)) && (!isMenu||freshMenuPress))
        {
            if(isMenu)freshMenuPress=false;
            lastKey = vk;
            // receivingWmInputs = false;
            inputFlag = true;
            if(isMenu)LOG_INFO("{}",logMessage);else LOG_DEBUG("{}", logMessage);
        }
    };

    const auto currentTick = GetTickCount64();
    const bool canAcceptInputs = lastInputTick + debounceThreshold < currentTick;

    const int swapKey=config->PresetHotSwapKey.value_or_default();
    const bool swapAllowed=canAcceptInputs&&!capturingKey&&OptiInput::IsFocused()&&swapKey>0&&swapKey<256&&!LaminrUI::SwapKeyConflict(swapKey)&&(!ImGui::GetCurrentContext()||!ImGui::GetIO().WantTextInput)&&!OptiInput::IsKeyDown(VK_CONTROL)&&!OptiInput::IsKeyDown(VK_SHIFT)&&!OptiInput::IsKeyDown(VK_MENU);
    LaminrUI::PollHotSwap(swapAllowed&&swapKey!=config->SnapshotKey.value_or_default()&&OptiInput::IsKeyPressed(swapKey),swapAllowed&&OptiInput::IsKeyReleased(swapKey),swapAllowed&&swapKey!=config->SnapshotKey.value_or_default());
    const int snapshotKey=config->SnapshotKey.value_or_default();
    const bool snapshotAllowed=canAcceptInputs&&!capturingKey&&OptiInput::IsFocused()&&snapshotKey>0&&snapshotKey<256&&!LaminrUI::SnapshotConflict(snapshotKey)&&(!ImGui::GetCurrentContext()||!ImGui::GetIO().WantTextInput)&&!OptiInput::IsKeyDown(VK_CONTROL)&&!OptiInput::IsKeyDown(VK_SHIFT)&&!OptiInput::IsKeyDown(VK_MENU);
    LaminrUI::PollSnapshot(snapshotAllowed&&OptiInput::IsKeyPressed(snapshotKey),snapshotAllowed);
    if (!capturingKey && canAcceptInputs)
    {
        const int backup=config->BackupShortcutKey.value_or_default();
        if(!OptiInput::IsFocused())backupMenuArmed=false;
        const bool chord=OptiInput::IsFocused()&&(OptiInput::IsKeyDown(VK_CONTROL)||OptiInput::IsKeyDown(VK_LCONTROL)||OptiInput::IsKeyDown(VK_RCONTROL))&&(OptiInput::IsKeyDown(VK_SHIFT)||OptiInput::IsKeyDown(VK_LSHIFT)||OptiInput::IsKeyDown(VK_RSHIFT));
        if(backup>0&&backup<256&&OptiInput::IsKeyPressed(backup)&&chord)backupMenuArmed=true;
        if(backupMenuArmed&&OptiInput::IsKeyReleased(backup)){
            inputMenu=true;backupMenuArmed=false;freshMenuPress=false;
            LOG_INFO("laminr backup menu shortcut received (Ctrl+Shift+{})",backup);
        }
        CheckShortcut(config->ShortcutKey.value_or_default(), inputMenu, "Primary menu shortcut received");
        CheckShortcut(config->FpsShortcutKey.value_or_default(), inputFps, "Menu key pressed, will be switching FPS");
        CheckShortcut(config->FpsCycleShortcutKey.value_or_default(), inputFpsCycle,
                      "Menu key pressed, will be switching FPS mode");
        CheckShortcut(config->DlssNrToggleKey.value_or_default(), inputDlssNr,
                      "Neural Rendering key pressed, will be toggling the pass");
    }
    else if (capturingKey)
    {
        lastInputTick = currentTick;
    }

    lastKey = OptiInput::GetLastPressedKey();
}

void MenuCommon::ShowTooltip(const char* tip)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tip);
        ImGui::EndTooltip();
    }
}

void MenuCommon::ShowHelpMarker(const char* tip)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ShowTooltip(tip);
}

void MenuCommon::ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName)
{
    ImGui::SameLine();

    ImGui::BeginDisabled(!initFlag->has_value());

    if (ImGui::Button(buttonName.c_str()))
    {
        initFlag->reset();
        ReInitUpscaler();
    }

    ImGui::EndDisabled();
}

inline void MenuCommon::ReInitUpscaler()
{
    if (!State::Instance().currentFeature)
        return;

    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
        State::Instance().newBackend = Upscaler::DLSSD;
    else
        State::Instance().newBackend = currentBackend;

    MARK_ALL_BACKENDS_CHANGED();
}

void MenuCommon::SeparatorWithHelpMarker(const char* label, const char* tip)
{
    auto marker = "(?) ";
    ImGui::SeparatorTextEx(0, label, ImGui::FindRenderedTextEnd(label),
                           ImGui::CalcTextSize(marker, ImGui::FindRenderedTextEnd(marker)).x);
    ShowHelpMarker(tip);
}

class Keybind
{
    std::string name;
    int id;
    bool waitingForKey = false;

  public:
    Keybind(std::string name, int id) : name(name), id(id) {}

    static std::string KeyNameFromVirtualKeyCode(USHORT virtualKey)
    {
        if (virtualKey == 0 || virtualKey == (USHORT) UnboundKey)
            return "Unbound";
        if (virtualKey == VK_SNAPSHOT)
            return "Print Screen";
        if (virtualKey >= VK_NUMPAD0 && virtualKey <= VK_NUMPAD9)
            return "NumPad " + std::to_string(virtualKey - VK_NUMPAD0);

        UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);

        // Keys like Home would display as Num 0 without this fix
        switch (virtualKey)
        {
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_NUMLOCK:
        case VK_DIVIDE:
        case VK_RCONTROL:
        case VK_RMENU:
            scanCode |= 0xE000;
            break;
        }

        LONG lParam = (scanCode & 0xFF) << 16;
        if (scanCode & 0xE000)
            lParam |= 1 << 24;

        wchar_t buf[64] = {};
        if (GetKeyNameTextW(lParam, buf, static_cast<int>(std::size(buf))) != 0)
            return wstring_to_string(buf);

        return "Unknown";
    }

    void Render(CustomOptional<int>& configKey, bool showValue=true)
    {
        ImGui::PushID(id);
        if (ImGui::Button(name.c_str()))
        {
            waitingForKey = true;
            capturingKey = true;
            lastKey = 0;
        }
        ImGui::PopID();

        if (waitingForKey)
        {
            ImGui::SameLine();
            ImGui::Text("Press any key...");

            if (lastKey == 0 || lastKey == VK_LBUTTON || lastKey == VK_RBUTTON || lastKey == VK_MBUTTON)
                return;

            if (lastKey == VK_ESCAPE)
            {
                waitingForKey = false;
                capturingKey = false;
                return;
            }

            if (lastKey == VK_BACK)
                lastKey = UnboundKey;

            configKey = lastKey;
            waitingForKey = false;
            capturingKey = false;
            return;
        }

        if(showValue){ImGui::SameLine();ImGui::TextUnformatted(KeyNameFromVirtualKeyCode(configKey.value_or_default()).c_str());}

        ImGui::SameLine();
        ImGui::PushID(id);
        if (ImGui::Button("R"))
        {
            configKey.reset();
        }
        ImGui::PopID();
    }
};

Upscaler MenuCommon::GetBackendCode(const API api)
{
    if (auto feature = State::Instance().currentFeature)
        return feature->GetUpscalerType();

    return Config::Instance()->Dx12Upscaler.value_or_default();
}

void MenuCommon::GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name)
{
    upscaler = GetBackendCode(api);
    *name = UpscalerDisplayName(upscaler, api);
}

template <HasDefaultValue B> void MenuCommon::AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value)
{
    const char* states[] = { "AUTO",
                             "COMMON",
                             "VERTEX_AND_CONSTANT_BUFFER",
                             "INDEX_BUFFER",
                             "RENDER_TARGET",
                             "UNORDERED_ACCESS",
                             "DEPTH_WRITE",
                             "DEPTH_READ",
                             "NON_PIXEL_SHADER_RESOURCE",
                             "PIXEL_SHADER_RESOURCE",
                             "STREAM_OUT",
                             "INDIRECT_ARGUMENT",
                             "COPY_DEST",
                             "COPY_SOURCE",
                             "RESOLVE_DEST",
                             "RESOLVE_SOURCE",
                             "RAYTRACING_ACCELERATION_STRUCTURE",
                             "SHADING_RATE_SOURCE",
                             "GENERIC_READ",
                             "ALL_SHADER_RESOURCE",
                             "PRESENT",
                             "PREDICATION",
                             "VIDEO_DECODE_READ",
                             "VIDEO_DECODE_WRITE",
                             "VIDEO_PROCESS_READ",
                             "VIDEO_PROCESS_WRITE",
                             "VIDEO_ENCODE_READ",
                             "VIDEO_ENCODE_WRITE" };
    const int values[] = { -1,  0,   1,     2,      4,      8,      16,      32,       64,   128,
                           256, 512, 1024,  2048,   4096,   8192,   4194304, 16777216, 2755, 192,
                           0,   310, 65536, 131072, 262144, 524288, 2097152, 8388608 };

    int selected = value->value_or(-1);

    const char* selectedName = "";

    for (int n = 0; n < 28; n++)
    {
        if (values[n] == selected)
        {
            selectedName = states[n];
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), selectedName))
    {
        if (ImGui::Selectable(states[0], !value->has_value()))
            value->reset();

        for (int n = 1; n < 28; n++)
        {
            if (ImGui::Selectable(states[n], selected == values[n]))
                *value = values[n];
        }

        ImGui::EndCombo();
    }
}

static uint32_t GetPresetIndex(IFeature* feature, bool dlssd = false)
{
    auto ratio = (float) feature->TargetWidth() / (float) feature->RenderWidth();

    if (!dlssd)
    {
        if (State::Instance().dlssPresetsOverridenByOpti)
        {
            LOG_DEBUG("DLSS Presets overridden by Opti, using Opti preset indices with ratio: {}", ratio);

            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->RenderPresetForAll.value_or(
                    Config::Instance()->RenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssPresetsOverriddenExternally)
        {
            LOG_DEBUG("DLSS Presets overridden externally, using external preset index: {}",
                      State::Instance().dlssRenderPresetExternal);

            return State::Instance().dlssRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssRenderPresetDLAA;
            }
        }
    }
    else
    {
        if (State::Instance().dlssdPresetsOverridenByOpti)
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetPerformance.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetBalanced.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetQuality.value_or_default());
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetUltraQuality.value_or_default());
            }
            else
            {
                return Config::Instance()->DLSSDRenderPresetForAll.value_or(
                    Config::Instance()->DLSSDRenderPresetDLAA.value_or_default());
            }
        }
        else if (State::Instance().dlssdPresetsOverriddenExternally)
        {
            return State::Instance().dlssdRenderPresetExternal;
        }
        else
        {
            if (ratio <= (Config::Instance()->QualityRatio_UltraPerformance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Performance.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetPerformance;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Balanced.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetBalanced;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_Quality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetQuality;
            }
            else if (ratio <= (Config::Instance()->QualityRatio_UltraQuality.value_or_default() + 0.01f))
            {
                return State::Instance().dlssdRenderPresetUltraQuality;
            }
            else
            {
                return State::Instance().dlssdRenderPresetDLAA;
            }
        }
    }

    return 0;
}

// TODO: disable presets based on the detected DLSS version
template <HasDefaultValue B> void MenuCommon::AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // clang-format off
    static const std::vector<MenuOption<uint32_t>> presets = {
        { NVSDK_NGX_DLSS_Hint_Render_Preset_Default, "DEFAULT", 
            "Whatever the game uses" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_A, "PRESET A",
            "Intended for Performance/Balanced/Quality modes.\nAn older variant best suited to combat ghosting...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_B, "PRESET B",
            "Intended for Ultra Performance mode.\nSimilar to Preset A...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_C, "PRESET C",
            "Intended for Performance/Balanced/Quality modes.\nGenerally favors current frame information...\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_D, "PRESET D",
            "Default preset for Performance/Balanced/Quality modes;\ngenerally favors image stability.\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_E, "PRESET E",
            "DLSS 3.7+, a better D preset\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_F, "PRESET F",
            "Default preset for Ultra Performance and DLAA modes\nRemoved on recent versions!" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_G, "PRESET G",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_H_Reserved, "PRESET H",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_I_Reserved, "PRESET I",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_J, "PRESET J",
            "Similar to preset K. Preset J might exhibit slightly\nless ghosting...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_K, "PRESET K",
            "Default preset for DLAA/Balanced/Quality modes...\n1st Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_L, "PRESET L",
            "Default for Ultra Perf mode\n2nd Gen Transformers" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_M, "PRESET M",
            "Default for Perf mode\n2nd Gen Transformer" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_N, "PRESET N",
            "Unused" },
        { NVSDK_NGX_DLSS_Hint_Render_Preset_O, "PRESET O",
            "Unused" },
        { NV_PRESET_LATEST, "Latest",
            "Latest supported by the dll" }
    };
    // clang-format on

    PopulateCombo(name, *value, presets);
}

template <HasDefaultValue B> void MenuCommon::AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value)
{
    // We don't have DLSSD definitions so using raw values
    static const std::vector<MenuOption<uint32_t>> presets = {
        { 0, "DEFAULT", "Whatever the game uses" },
        { 1, "PRESET A", "Preset A\nRemoved on recent versions!" },
        { 2, "PRESET B", "Preset B\nRemoved on recent versions!" },
        { 3, "PRESET C", "Preset C\nRemoved on recent versions!" },
        { 4, "PRESET D", "Default model, Transformer" },
        { 5, "PRESET E", "Latest Transformer model\nMust use if DoF guide is needed" },
        { 6, "PRESET F", "Latest Transformer model\nMust use if DoF guide is needed" },
        { NV_PRESET_LATEST, "Latest", "Latest supported by the dll" }
    };

    PopulateCombo(name, *value, presets);
}

template <typename TStorage, typename T>
void MenuCommon::PopulateCombo(const std::string& name, TStorage& currentValue,
                               const std::vector<MenuOption<T>>& options)
{
    if (options.empty())
        return;

    // Assumes that different types mean that TStorage is std::optional
    T currentVal;
    if constexpr (std::is_same_v<TStorage, T>)
        currentVal = currentValue;
    else
        currentVal = currentValue.value_or(options[0].value);

    // Find the label for the currently selected item
    std::string preview = "Unknown";
    for (const auto& opt : options)
    {
        if (opt.value == currentVal)
        {
            preview = opt.label;
            break;
        }
    }

    if (ImGui::BeginCombo(name.c_str(), preview.c_str()))
    {
        for (const auto& opt : options)
        {
            if (opt.hidden)
                continue;

            if (opt.disabled)
                ImGui::BeginDisabled();

            bool isSelected = (currentVal == opt.value);
            if (ImGui::Selectable(opt.label.c_str(), isSelected))
                currentValue = opt.value;

            // Show tooltip for the individual item if it exists
            if (!opt.tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", opt.tooltip.c_str());

            if (opt.disabled)
                ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
}

static ImVec4 toneMapColor(const ImVec4& color)
{
    if (State::Instance().isHdrActive ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr()))
    {
        // Controls how strongly HDR/UI colors are pushed into the tone mapper before compression.
        // Higher values make colors brighter before mapping; lower values make the result dimmer.
        constexpr float exposure = 1.0f;

        // Blends between original color and fully tone-mapped color.
        // 0.0 = no tone mapping, 1.0 = full Reinhard compression.
        constexpr float strength = 1.0f;

        float peak = std::max(color.x, std::max(color.y, color.z));

        if (peak <= 0.0f)
            return color;

        float exposedPeak = peak * exposure;
        float mappedPeak = exposedPeak / (1.0f + exposedPeak);

        float reinhardScale = mappedPeak / peak;
        float scale = 1.0f + (reinhardScale - 1.0f) * strength;

        return ImVec4(color.x * scale, color.y * scale, color.z * scale, color.w);
    }

    return color;
}

static void MenuHdrCheck(ImGuiIO io)
{
    // If game is using HDR, apply tone mapping to the ImGui style
    if (State::Instance().isHdrActive ||
        (!Config::Instance()->OverlayMenu.value_or_default() && State::Instance().currentFeature != nullptr &&
         State::Instance().currentFeature->IsHdr()))
    {
        if (!_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();

            CopyMemory(SdrColors, style.Colors, sizeof(style.Colors));

            // Apply tone mapping to the ImGui style
            for (int i = 0; i < ImGuiCol_COUNT; ++i)
            {
                ImVec4 color = style.Colors[i];
                style.Colors[i] = toneMapColor(color);
            }

            _hdrTonemapApplied = true;
        }
    }
    else
    {
        if (_hdrTonemapApplied)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            CopyMemory(style.Colors, SdrColors, sizeof(style.Colors));
            _hdrTonemapApplied = false;
        }
    }
}

static float MenuResolutionScale(ImGuiIO io)
{
    if (Config::Instance()->MenuScale.has_value())
        return Config::Instance()->MenuScale.value();

    // Calculate menu scale according to display resolution
    float y = State::Instance().screenHeight;

    if (io.DisplaySize.y != 0)
        y = (float) io.DisplaySize.y;

    // 1000p is minimum for 1.0 menu ratio
    float result = (float) ((int) (y / 108.0f)) / 10.0f;

    result = std::round(result * 10.0f) / 10.0f;

    if (result < 0.5f)
        result = 0.5f;

    if (result > 2.0f)
        result = 2.0f;

    return result;
}

static void ApplyThemeStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    auto conf = Config::Instance();
    bool lightTheme = conf->LightTheme.value_or_default();

    style.WindowRounding = 2.0f;
    style.ChildRounding = 1.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.FrameBorderSize = lightTheme ? 1.0f : 0.0f;
    style.TabBorderSize = lightTheme ? 1.0f : 0.0f;

    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 10.0f;

    auto Clamp01 = [](float v) { return std::max(0.0f, std::min(v, 1.0f)); };

    auto Mix = [](const ImVec4& a, const ImVec4& b, float t, float alpha = 1.0f)
    { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, alpha); };

    auto Luminance = [](const ImVec4& c) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };

    auto Saturate = [&](const ImVec4& color, float amount)
    {
        float lum = Luminance(color);

        return ImVec4(Clamp01(lum + (color.x - lum) * amount), Clamp01(lum + (color.y - lum) * amount),
                      Clamp01(lum + (color.z - lum) * amount), color.w);
    };

    ImVec4 accent = ImVec4(conf->MenuAccentColorR.value_or_default(), conf->MenuAccentColorG.value_or_default(),
                           conf->MenuAccentColorB.value_or_default(), 1.0f);

    ImVec4 bgAccent = ImVec4(conf->MenuBGColorR.value_or_default(), conf->MenuBGColorG.value_or_default(),
                             conf->MenuBGColorB.value_or_default(), 1.0f);

    float luminance = Luminance(accent);

    const ImVec4 bgDark = lightTheme ? ImVec4(0.80f, 0.82f, 0.86f, 1.00f) : ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    const ImVec4 bgMid = lightTheme ? ImVec4(0.89f, 0.91f, 0.95f, 1.00f) : ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    const ImVec4 bgLight = lightTheme ? ImVec4(0.96f, 0.97f, 0.99f, 1.00f) : ImVec4(0.14f, 0.14f, 0.15f, 1.00f);

    const ImVec4 textPrimary = lightTheme ? ImVec4(0.05f, 0.06f, 0.08f, 1.00f) : ImVec4(0.90f, 0.93f, 0.95f, 1.00f);
    const ImVec4 textDim = lightTheme ? ImVec4(0.22f, 0.25f, 0.31f, 1.00f) : ImVec4(0.54f, 0.58f, 0.62f, 1.00f);

    const ImVec4 borderCol = lightTheme ? ImVec4(0.35f, 0.40f, 0.50f, 1.00f) : ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    const ImVec4 dimBg = lightTheme ? ImVec4(0.30f, 0.33f, 0.38f, 0.20f) : ImVec4(0.09f, 0.10f, 0.13f, 0.20f);
    const ImVec4 modalDimBg = lightTheme ? ImVec4(0.22f, 0.24f, 0.28f, 0.55f) : ImVec4(0.04f, 0.04f, 0.07f, 0.55f);

    // MenuBGColor: only background/surface tint.
    auto BgTint = [&](const ImVec4& base, float strength = 1.0f, float alpha = 1.0f)
    {
        float t = lightTheme ? (0.180f * strength) : (0.120f * strength);
        return Mix(base, bgAccent, t, alpha);
    };

    // MenuAccentColor: all visible interactive accent colors.
    auto AccentSoft = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.14f, alpha) : Mix(bgDark, accent, 0.32f, alpha); };

    auto AccentMed = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.42f, alpha) : Mix(bgDark, accent, 0.55f, alpha); };

    auto AccentStrong = [&](float alpha = 1.0f) { return ImVec4(accent.x, accent.y, accent.z, alpha); };

    const ImVec4 bgTitle = AccentSoft();

    auto SurfaceHover = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.12f, alpha) : Mix(bgLight, accent, 0.18f, alpha); };

    auto SurfaceActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgLight, accent, 0.20f, alpha) : Mix(bgLight, accent, 0.28f, alpha); };

    auto TitleActive = [&](float alpha = 1.0f)
    { return lightTheme ? Mix(bgTitle, accent, 0.18f, alpha) : Mix(bgTitle, accent, 0.16f, alpha); };

    auto PlotAccent = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            // Darken slightly for contrast on light bg — no channel floors
            return Mix(accent, ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.20f, alpha);
        }

        // Brighten slightly for visibility on dark bg — no channel floors
        return Mix(accent, ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.35f, alpha);
    };

    auto PlotAccentHovered = [&](float alpha = 1.0f)
    {
        if (lightTheme)
        {
            return Mix(PlotAccent(alpha), ImVec4(0.00f, 0.00f, 0.00f, 1.00f), 0.15f, alpha);
        }

        return Mix(PlotAccent(alpha), ImVec4(1.00f, 1.00f, 1.00f, 1.00f), 0.25f, alpha);
    };

    auto AccentReadable = [&](float alpha = 1.0f)
    {
        // Apply saturation boost and luminance correction only here,
        // so AccentStrong / AccentMed / AccentSoft stay true to the user's pick.
        ImVec4 a = Saturate(accent, lightTheme ? 1.35f : 1.25f);
        float lum = Luminance(a);

        if (lightTheme && lum > 0.72f)
            a = Mix(a, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.35f, 1.0f);

        if (!lightTheme && lum < 0.25f)
            a = Mix(a, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.30f, 1.0f);

        return ImVec4(a.x, a.y, a.z, alpha);
    };

    ImVec4* c = ImGui::GetStyle().Colors;

    float minAlpha = Config::Instance()->MenuBGColorA.value_or_default() >= 0.5f
                         ? Config::Instance()->MenuBGColorA.value_or_default()
                         : 0.5f;

    c[ImGuiCol_Text] = textPrimary;
    c[ImGuiCol_TextDisabled] = textDim;
    c[ImGuiCol_TextLink] = AccentReadable();

    // MenuBGColor only.
    c[ImGuiCol_WindowBg] = BgTint(bgDark, 1.00f, Config::Instance()->MenuBGColorA.value_or_default());
    c[ImGuiCol_ChildBg] = BgTint(bgMid, 1.10f, minAlpha + 0.1f);
    c[ImGuiCol_PopupBg] =
        lightTheme ? BgTint(bgLight, 0.90f) : BgTint(ImVec4(0.09f, 0.10f, 0.13f, 0.97f), 0.90f, 0.97f);
    c[ImGuiCol_MenuBarBg] = BgTint(bgDark, 0.85f);
    c[ImGuiCol_DockingEmptyBg] = BgTint(bgDark, 0.75f);

    c[ImGuiCol_Border] = borderCol;
    c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Neutral background, not MenuBGColor.
    c[ImGuiCol_FrameBg] = BgTint(bgLight, 0.50f, minAlpha + 0.15f);
    c[ImGuiCol_FrameBgHovered] = SurfaceHover();
    c[ImGuiCol_FrameBgActive] = SurfaceActive();

    c[ImGuiCol_TitleBg] = BgTint(bgTitle, 0.40f);
    c[ImGuiCol_TitleBgActive] = TitleActive();
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(bgTitle.x, bgTitle.y, bgTitle.z, 0.75f);

    c[ImGuiCol_ScrollbarBg] = BgTint(bgDark, 0.60f, minAlpha + 0.2f);
    c[ImGuiCol_ScrollbarGrab] = AccentSoft();
    c[ImGuiCol_ScrollbarGrabHovered] = AccentMed();
    c[ImGuiCol_ScrollbarGrabActive] = AccentStrong();

    c[ImGuiCol_CheckMark] = AccentReadable();
    c[ImGuiCol_SliderGrab] = AccentMed();
    c[ImGuiCol_SliderGrabActive] = AccentReadable();
    c[ImGuiCol_InputTextCursor] = AccentReadable();

    c[ImGuiCol_Button] = AccentSoft();
    c[ImGuiCol_ButtonHovered] = AccentMed();
    c[ImGuiCol_ButtonActive] = AccentStrong();

    c[ImGuiCol_Header] = AccentSoft(0.90f);
    c[ImGuiCol_HeaderHovered] = AccentMed(0.95f);
    c[ImGuiCol_HeaderActive] = AccentStrong();

    c[ImGuiCol_Separator] = borderCol;
    c[ImGuiCol_SeparatorHovered] = AccentMed(0.85f);
    c[ImGuiCol_SeparatorActive] = AccentStrong();

    c[ImGuiCol_ResizeGrip] = AccentSoft(0.30f);
    c[ImGuiCol_ResizeGripHovered] = AccentStrong(0.70f);
    c[ImGuiCol_ResizeGripActive] = AccentStrong(0.95f);

    c[ImGuiCol_Tab] = AccentSoft();
    c[ImGuiCol_TabHovered] = AccentMed();
    c[ImGuiCol_TabSelected] = AccentSoft();
    c[ImGuiCol_TabSelectedOverline] = AccentStrong();
    c[ImGuiCol_TabDimmed] = BgTint(bgDark, 0.60f);
    c[ImGuiCol_TabDimmedSelected] = AccentSoft(0.75f);
    c[ImGuiCol_TabDimmedSelectedOverline] = borderCol;

    c[ImGuiCol_DockingPreview] = AccentStrong(0.70f);

    c[ImGuiCol_PlotLines] = PlotAccent();
    c[ImGuiCol_PlotLinesHovered] = PlotAccentHovered();
    c[ImGuiCol_PlotHistogram] = PlotAccent(0.85f);
    c[ImGuiCol_PlotHistogramHovered] = PlotAccentHovered();

    c[ImGuiCol_TableHeaderBg] = BgTint(bgMid, 0.80f, minAlpha + 0.25f);
    c[ImGuiCol_TableBorderStrong] = borderCol;
    c[ImGuiCol_TableBorderLight] = lightTheme ? ImVec4(0.68f, 0.72f, 0.80f, 1.00f) : AccentSoft();
    c[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = lightTheme ? ImVec4(0.00f, 0.00f, 0.00f, 0.045f) : ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    c[ImGuiCol_TreeLines] = borderCol;
    c[ImGuiCol_TextSelectedBg] = AccentMed(0.38f);
    c[ImGuiCol_DragDropTarget] = AccentStrong(0.90f);
    c[ImGuiCol_NavCursor] = AccentReadable();
    c[ImGuiCol_NavWindowingHighlight] = AccentStrong(0.70f);
    c[ImGuiCol_NavWindowingDimBg] = dimBg;
    c[ImGuiCol_ModalWindowDimBg] = modalDimBg;
    style.WindowPadding=ImVec2(18,16);style.FramePadding=ImVec2(10,7);style.ItemSpacing=ImVec2(10,9);
    style.WindowRounding=14;style.ChildRounding=10;style.FrameRounding=7;style.GrabRounding=7;
    c[ImGuiCol_WindowBg]=ImVec4(.055f,.045f,.080f,.98f);
    c[ImGuiCol_Header]=ImVec4(.14f,.10f,.20f,1.f);
    c[ImGuiCol_FrameBg]=ImVec4(.12f,.09f,.17f,1.f);

    _hdrTonemapApplied = false;
    MenuHdrCheck(ImGui::GetIO());
}

static double lastTime = 0.0;
static double lastFrameTime = 0.0;
static UINT64 uwpTargetFrame = 0;

void MenuCommon::Present()
{
    _frameCount++;

    auto now = Util::MillisecondsNow();

    if (lastTime > 0.0)
        lastFrameTime = now - lastTime;

    lastTime = now;

    if (_handle != nullptr)
        UpdateManualInput(_handle);
}

struct VersionCheckStatus
{
    bool completed = false;
    bool updateAvailable = false;
    std::string latestTag;
    std::string latestUrl;
    std::string error;
};

struct MenuCommon::RenderMenuContext
{
    State& state;
    decltype(Config::Instance()) config;
    ImGuiIO& io;
    IFeature* currentFeature = nullptr;

    double now = 0.0;
    double frameTime = 0.0;
    double frameRate = 0.0;
    float menuResScale = 1.0f;
    float fpsScale = 1.0f;
    float averageFrameTime = 0.0f;
    float averageUpscalerFT = 0.0f;

    bool frameTimesCalculated = false;
    bool newFrame = false;

    VersionCheckStatus versionStatus;
    std::string currentVersionText;

    // Cached when the menu is visible and shared by RenderMainMenuWindow section helpers.
    std::unique_ptr<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>> primaryGpu;
};

void MenuCommon::UpdateRenderTiming(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    if (config->OverlayMenu.value_or_default())
    {
        _frameCount++;

        // FPS & frame time calculation
        if (lastTime > 0.0)
        {
            frameTime = now - lastTime;
            frameRate = 1000.0 / frameTime;
        }

        lastTime = now;

        if (_handle != nullptr)
            UpdateManualInput(_handle);
    }
    else
    {
        MenuCommon::Present();

        frameTime = lastFrameTime;
        frameRate = 1000.0 / frameTime;
    }

    state.frameTimes.pop_front();
    state.frameTimes.push_back(frameTime);
}

void MenuCommon::UpdateMenuInputMode(RenderMenuContext& ctx)
{
    auto& io = ctx.io;

    // Moved here to prevent gamepad key replay
    if (_isVisible)
    {
        if (hasGamepad)
            io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    }
    else
    {
        capturingKey = false;
        hasGamepad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;
    }
}

void MenuCommon::HandleMenuShortcuts(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;

    // Handle Inputs
    {
        if (inputFps)
        {
            inputFps = false;
            config->ShowFps = !config->ShowFps.value_or_default();
        }

        if (inputDlssNr)
        {
            inputDlssNr = false;
            DlssNr::SetEnabled(!config->DlssNrEnabled.value_or_default());
            LOG_DEBUG("Neural Rendering toggle key pressed, setting DlssNrEnabled to {}",
                      config->DlssNrEnabled.value_or_default());

            ImGuiToast toast { ImGuiToastType::Info, 2000 };
            toast.setTitle("DLSS Neural Rendering");
            toast.setContent(config->DlssNrEnabled.value_or_default() ? "On" : "Off");
            ImGui::InsertNotification(toast);
        }

        if (inputFpsCycle && config->ShowFps.value_or_default())
            config->FpsOverlayType = (FpsOverlay) ((config->FpsOverlayType.value_or_default() + 1) % FpsOverlay_COUNT);

        if (inputMenu)
        {
            inputMenu = false;
            _isVisible = !_isVisible;

            LOG_DEBUG("Menu key pressed, {0}", _isVisible ? "opening ImGui" : "closing ImGui");

            if (_isVisible)
            {
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();

                OptiInput::ResetMenuInputTransientState();

                ApplyThemeStyle();

                refreshRate = Util::GetActiveRefreshRate(_handle);

                if (State::Instance().currentFeature != nullptr)
                {
                    if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSSD)
                        comboPreset = config->DLSSDRenderPresetForAll.value_or_default();
                    else if (State::Instance().currentFeature->GetUpscalerType() == Upscaler::DLSS)
                        comboPreset = config->RenderPresetForAll.value_or_default();
                }
            }
            else
            {
                SaveMenuGeometryBoundary(true);
                ImGui::CloseCurrentPopup();
            }

            io.MouseDrawCursor = _isVisible;
            io.WantCaptureKeyboard = _isVisible;
            io.WantCaptureMouse = _isVisible;
        }

        inputFpsCycle = false;
    }
}

void MenuCommon::UpdateVersionAndStartupNotifications(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& versionStatus = ctx.versionStatus;

    constexpr int updateNoticeTime = 10000;

    // Version check state is copied while locked, then consumed by the UI render pass.
    {
        std::scoped_lock lock(state.versionCheckMutex);
        versionStatus.completed = state.versionCheckCompleted;
        versionStatus.updateAvailable = state.updateAvailable;
        versionStatus.latestTag = state.latestVersionTag;
        versionStatus.latestUrl = state.latestVersionUrl;
        versionStatus.error = state.versionCheckError;
    }

    ctx.currentVersionText = VersionCheck::CurrentVersionString();

    if (versionStatus.completed && versionStatus.updateAvailable && !versionStatus.latestTag.empty())
    {
        if (updateNoticeTag != versionStatus.latestTag)
        {
            updateNoticeTag = versionStatus.latestTag;
            updateNoticeUrl = versionStatus.latestUrl;
            const auto notice = [&]()
            {
                ImGuiToast updateNotification { ImGuiToastType::Error, updateNoticeTime };
                updateNotification.setTitle("OptiScaler Update available");
                updateNotification.setContent(
                    "Press %s for more info",
                    Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str());
                ImGui::InsertNotification(updateNotification);
                return true;
            };
            static auto res = notice();
        }
    }

    // One-shot startup warning notifications.
    if (!state.postDone)
    {
        if (state.postCodes & PostCode::SlPluginsAlreadyInMemory)
        {
            auto filename = Util::DllPath().filename().string();
            to_lower_in_place(filename);

            ImGuiToast notification { ImGuiToastType::Warning, 10000 };
            notification.setTitle("Late Streamline hook detected");
            notification.setContent(
                "Consider renaming OptiScaler from %s to other supported name.\nYou may experience issues otherwise.",
                filename.c_str());
            ImGui::InsertNotification(notification);
        }

        state.postDone = true;
    }
}

void MenuCommon::BeginMenuFrameIfNeeded(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& newFrame = ctx.newFrame;

    // New frame check
    // The lamp is drawn while the menu is closed, which is the whole point of it. Tied to its own
    // setting and nothing else: an overlay that appears because a scan is running, rather than
    // because someone asked for it, is an overlay nobody asked for.
    const bool scanIndicator = config->DlssNrScanMeter.value_or_default() &&
                               DlssNr::ExposureScan::Where() != DlssNr::ExposureScan::Verdict::Off;

    if (LaminrUI::NeedsStartupFrame() || config->ShowFps.value_or_default() || _isVisible || ImGui::notifications.size() > 0 || scanIndicator ||
        (config->DlssNrCompare.value_or_default() != 0 && config->DlssNrCompareTags.value_or_default()))
    {
        if (!_isUWP)
        {
            ImGui_ImplWin32_NewFrame();
        }
        else
        {
            ImVec2 displaySize { state.screenWidth, state.screenHeight };
            ImGui_ImplUwp_NewFrame(displaySize);
        }

        OptiInput::FeedImGui(_isVisible);

        MenuHdrCheck(io);
        ImGui::NewFrame();

        newFrame = true;
    }
}

void MenuCommon::RenderNotifications(RenderMenuContext& ctx)
{
    auto config = ctx.config;
    auto& io = ctx.io;

    // Notifications
    bool tonemapRequired = State::Instance().isHdrActive ||
                           (!Config::Instance()->OverlayMenu.value_or_default() &&
                            State::Instance().currentFeature != nullptr && State::Instance().currentFeature->IsHdr());

    float screenHeight = State::Instance().screenHeight;
    if (io.DisplaySize.y != 0)
        screenHeight = io.DisplaySize.y;

    // Map resolution height to scale, 0.5 for 480p, 2.0 for 1440p
    constexpr float slope = (2.0f - 0.5f) / (1440.f - 480.f);
    float notificationScale = 0.5f + slope * (screenHeight - 480.f);
    notificationScale = std::clamp(notificationScale, 0.5f, 2.0f);

    if (config->UseHQFont.value_or_default())
        ImGui::PushFontSize(std::round(notificationScale * fontSize));

    // No fallback font, SetWindowFontScale needs to be called after Begin()

    ImGui::RenderNotifications(ImGuiToastPos::TopCenter, notificationScale, tonemapRequired);

    if (config->UseHQFont.value_or_default())
        ImGui::PopFontSize();
}

void MenuCommon::UpdateFrameTimeAverages(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& frameTimesCalculated = ctx.frameTimesCalculated;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // FPS Overlay font
    fpsScale = config->FpsScale.value_or(menuResScale);

    // Update frame time & upscaler time averages
    averageFrameTime = 0.0f;
    averageUpscalerFT = 0.0f;

    if (config->ShowFps.value_or_default() || _isVisible)
    {
        float frameCnt = 0;
        frameTime = 0;
        for (size_t i = 299; i > 199; i--)
        {
            if (state.frameTimes[i] > 0.0)
            {
                frameTime += state.frameTimes[i];
                frameCnt++;
            }
        }

        frameTime /= frameCnt;
        frameRate = 1000.0 / frameTime;
        frameTimesCalculated = true;

        float lastFT = static_cast<float>(state.frameTimes.empty() ? 0.0f : state.frameTimes.back());
        float lastUT = static_cast<float>(state.upscaleTimes.empty() ? 0.0f : state.upscaleTimes.back());
        gFrameTimes.Push(lastFT);
        gUpscalerTimes.Push(lastUT);

        averageFrameTime = gFrameTimes.Average();
        averageUpscalerFT = gUpscalerTimes.Average();
    }
}

// Labels for the comparison views.
//
// Drawn straight onto the foreground draw list, not as ImGui windows -- the last attempt made them
// draggable windows and the clamping fought the split. Here each label is clipped to its own side of
// the comparison, so in the wipe the moving split reveals and hides it exactly as it does the
// pictures, and there is nothing to drag. Both wipe labels sit in the same top-left corner, each
// clipped to its side, so whichever picture currently owns that corner is the one whose label shows.
void MenuCommon::RenderNrCompareTags()
{
    auto* config = Config::Instance();

    const uint32_t mode = config->DlssNrCompare.value_or_default();

    if (mode == 0 || !config->DlssNrCompareTags.value_or_default())
        return;

    const ImVec2 screen = ImGui::GetIO().DisplaySize;

    if (screen.x < 1.0f || screen.y < 1.0f)
        return;

    const bool swap = config->DlssNrCompareSwap.value_or_default();
    const float split = mode == 1 ? 0.5f
                                  : std::clamp(config->DlssNrCompareSplit.value_or_default(), 0.0f, 1.0f);
    const float splitX = split * screen.x;

    const float scale = std::clamp(config->DlssNrTagScale.value_or_default(), 0.5f, 5.0f);

    // The left side is the untouched frame unless swapped -- matching the shader's
    // showOriginal = (uv.x < split) != swap.
    const char* leftText = swap ? "DLSS NR : ON" : "DLSS NR : OFF";
    const char* rightText = swap ? "DLSS NR : OFF" : "DLSS NR : ON";

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const float margin = 10.0f * scale;

    // Both labels flank the divider along the top: the left picture's label is right-aligned just
    // left of the split, the right picture's is left-aligned just right of it. Each is clipped to its
    // own side, so in the wipe the split reveals and hides them along with the images.
    auto drawTag = [&](const char* text, float x, ImVec2 clipMin, ImVec2 clipMax)
    {
        const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);

        // Never let a label run off the visible frame as it grows.
        x = std::min(std::max(x, 0.0f), screen.x - size.x);
        float y = std::min(margin, screen.y - size.y - margin);
        y = std::max(y, 0.0f);

        dl->PushClipRect(clipMin, clipMax, true);
        dl->AddText(font, fontSize, ImVec2(x + 2.0f, y + 2.0f), IM_COL32(0, 0, 0, 210), text);
        dl->AddText(font, fontSize, ImVec2(x, y), IM_COL32(255, 255, 255, 255), text);
        dl->PopClipRect();
    };

    const ImVec2 leftSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, leftText);

    // Left picture's label: right edge a margin in from the split. Right picture's: left edge a margin
    // out from the split.
    drawTag(leftText, splitX - margin - leftSize.x, ImVec2(0.0f, 0.0f), ImVec2(splitX, screen.y));
    drawTag(rightText, splitX + margin, ImVec2(splitX, 0.0f), ImVec2(screen.x, screen.y));
}

void MenuCommon::RenderPerformanceOverlay(RenderMenuContext& ctx)
{
    RenderNrCompareTags();


    auto& state = ctx.state;
    auto config = ctx.config;
    auto& io = ctx.io;
    auto& currentFeature = ctx.currentFeature;
    auto& now = ctx.now;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;
    auto& menuResScale = ctx.menuResScale;
    auto& fpsScale = ctx.fpsScale;
    auto& averageFrameTime = ctx.averageFrameTime;
    auto& averageUpscalerFT = ctx.averageUpscalerFT;

    // If Fps overlay is visible
    if (config->ShowFps.value_or_default())
    {
        bool stylePushed = false;

        const static auto defaultStyle = ImGuiStyle();

        // Rescale the fps overlay every frame because it shares style with the main menu
        if (config->FpsScale.has_value() && config->FpsScale.value() != menuResScale)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, defaultStyle.WindowPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, defaultStyle.FramePadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, defaultStyle.CellPadding * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, defaultStyle.SeparatorTextPadding * fpsScale);

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, defaultStyle.ItemSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, defaultStyle.ItemInnerSpacing * fpsScale);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, defaultStyle.IndentSpacing * fpsScale);

            stylePushed = true;
        }

        // Set overlay position
        ImGui::SetNextWindowPos(overlayPosition, ImGuiCond_Always);

        // Set overlay window properties
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));  // Transparent border
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // Transparent frame background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, toneMapColor(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        }

        ImGui::SetNextWindowBgAlpha(config->FpsOverlayAlpha.value_or_default()); // Transparent background

        if (!config->OverlaysUseTheme.value_or_default())
        {
            ImVec4 green(0.0f, 1.0f, 0.0f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotLines, toneMapColor(green));
        }

        if (ImGui::Begin("Performance Overlay", nullptr,
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav))
        {
            std::string api = (state.swapchainApi == DX12 || state.api == DX12) ? "D3D12" : "???";

            if (config->UseHQFont.value_or_default())
                ImGui::PushFontSize(std::round(fpsScale * fontSize));
            else
                ImGui::SetWindowFontScale(fpsScale);

            std::string firstLine = "";
            std::string secondLine = "";
            std::string thirdLine = "";

            const auto overlayType = config->FpsOverlayType.value_or_default();
            const bool hasFeature = currentFeature && !currentFeature->IsFrozen();

            // Prepare Line 1
            std::string featurePart;
            std::string fpsPart;

            if (hasFeature)
            {
                featurePart = StrFmt(" | %s -> %s %u.%u.%u", ApiUpscalerInputName(state.currentInputApiName).c_str(),
                                     currentFeature->ShortName().c_str(), currentFeature->Version().major,
                                     currentFeature->Version().minor, currentFeature->Version().patch);
            }

            switch (overlayType)
            {
            case FpsOverlay_JustFPS:
                fpsPart = StrFmt("%6.1f ", frameRate);
                break;

            case FpsOverlay_Simple:
                fpsPart = StrFmt("FPS: %6.1f, %7.2f ms", frameRate, frameTime);
                break;

            default:
                fpsPart = StrFmt("FPS: %6.1f, Avg: %6.1f", frameRate, 1000.0f / averageFrameTime);
                break;
            }

            if (overlayType == FpsOverlay_JustFPS)
                firstLine = StrFmt("%s", fpsPart.c_str());
            else
                firstLine = StrFmt("%s | %s%s", api.c_str(), fpsPart.c_str(), featurePart.c_str());

            // Prepare Line 2
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                secondLine = StrFmt("Frame Time: %7.2f ms, Avg: %7.2f ms", state.frameTimes.back(), averageFrameTime);
            }

            // Prepare Line 3
            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                thirdLine =
                    StrFmt("Upscaler Time: %7.2f ms, Avg: %7.2f ms", state.upscaleTimes.back(), averageUpscalerFT);
            }

            ImVec2 plotSize;
            if (config->FpsOverlayHorizontal.value_or_default())
            {
                plotSize = { fpsScale * 150, fpsScale * 16 };
            }
            else
            {
                // Find the widest text width
                auto firstSize = ImGui::CalcTextSize(firstLine.c_str());
                auto secondSize = ImGui::CalcTextSize(secondLine.c_str());
                auto thirdSize = ImGui::CalcTextSize(thirdLine.c_str());
                auto textWidth = 0.0f;

                if (firstSize.x > secondSize.x)
                    textWidth = firstSize.x > thirdSize.x ? firstSize.x : thirdSize.x;
                else
                    textWidth = secondSize.x > thirdSize.x ? secondSize.x : thirdSize.x;

                auto minWidth = fpsScale * 300.0f;
                auto plotWidth = textWidth < minWidth ? minWidth : textWidth;

                plotSize = { plotWidth, fpsScale * 30 };
            }

            // Draw the overlay
            ImGui::Text(firstLine.c_str());

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Detailed)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(secondLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_DetailedGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of frame times
                ImGui::PlotLines(
                    "##FrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gFrameTimes, plotWidth, 0, nullptr, 0.0f, 66.6f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_Full)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text(" | ");
                    ImGui::SameLine(0.0f, 0.0f);
                }
                else
                {
                    ImGui::Spacing();
                }

                ImGui::Text(thirdLine.c_str());
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_FullGraph)
            {
                if (config->FpsOverlayHorizontal.value_or_default())
                    ImGui::SameLine(0.0f, 0.0f);

                // Graph of upscaler times
                ImGui::PlotLines(
                    "##UpscalerFrameTimeGraph",
                    [](void* rb, int idx) -> float { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); },
                    &gUpscalerTimes, plotWidth, 0, nullptr, 0.0f, 20.0f, plotSize);
            }

            if (config->FpsOverlayType.value_or_default() >= FpsOverlay_ReflexTimings)
            {
                constexpr auto delayBetweenPollsMs = 500;
                static auto previousPoll = 0.0;
                static bool gotData = false;

                if (previousPoll <= 0.001 || previousPoll + delayBetweenPollsMs < now)
                {
                    gotData = ReflexHooks::updateTimingData();
                    previousPoll = now;
                }

                auto& timingData = ReflexHooks::timingData;

                if (gotData && timingData[TimingType::TimeRange].has_value())
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    constexpr float offsetForText = 155;

                    const auto& rangeInNs = timingData[TimingType::TimeRange].value().length;

                    ImGui::Text("RfxId: %llu", state.reflexFrameId);
                    ImGui::Text("Reflex timings, whole frame: %.1fms", rangeInNs / 1000.0);

                    const auto maxWidth =
                        config->FpsOverlayHorizontal.value_or_default() ? ImGui::GetWindowWidth() : plotSize.x;

                    const auto drawTiming = [&](TimingType type, const char* desc, ImVec4 color)
                    {
                        if (!timingData[type].has_value())
                            return;

                        auto toneMappedColor = toneMapColor(color);

                        auto& timing = timingData[type].value();
                        float duration = static_cast<float>(timing.length * rangeInNs / 1000.0);
                        ImGui::TextColored(toneMappedColor, "%-12s %4.1fms", desc, duration);
                        auto leftLimit = ImGui::GetItemRectMin().x + offsetForText * fpsScale;
                        auto start = static_cast<float>(leftLimit + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                        timing.position);
                        auto end = static_cast<float>(start + (ImGui::GetItemRectMin().x + maxWidth - leftLimit) *
                                                                  timing.length);
                        auto pos = ImVec2(start, ImGui::GetItemRectMin().y);
                        auto size = ImVec2(end, ImGui::GetItemRectMax().y);
                        drawList->AddRectFilled(pos, size, ImGui::ColorConvertFloat4ToU32(toneMappedColor));
                    };

                    drawTiming(TimingType::Simulation, "Simulation", ImVec4(0.768f, 0.169f, 0.169f, 1.0f));
                    drawTiming(TimingType::RenderSubmit, "RenderSubmit", ImVec4(0.235f, 0.705f, 0.294f, 1.0f));
                    drawTiming(TimingType::Present, "Present", ImVec4(1.0f, 0.88f, 0.098f, 1.0f));
                    drawTiming(TimingType::Driver, "Driver", ImVec4(0.263f, 0.388f, 0.847f, 1.0f));
                    drawTiming(TimingType::OsRenderQueue, "RenderQueue", ImVec4(0.76f, 0.51f, 0.188f, 1.0f));
                    drawTiming(TimingType::GpuRender, "GpuRender", ImVec4(0.569f, 0.117f, 0.705f, 1.0f));
                }
            }
        }

        // Restore the style
        if (!config->OverlaysUseTheme.value_or_default())
            ImGui::PopStyleColor(5);
        else
            ImGui::PopStyleColor(2);

        // Get size for postioning
        overlaySize = ImGui::GetWindowSize();

        if (config->UseHQFont.value_or_default())
            ImGui::PopFontSize();

        ImGui::End();

        if (stylePushed)
            ImGui::PopStyleVar(7);

        // Left / Right
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_BottomLeft)
        {
            overlayPosition.x = 0;
        }
        else
        {
            overlayPosition.x = io.DisplaySize.x - overlaySize.x;
        }

        // Top / Bottom
        if (config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopLeft ||
            config->FpsOverlayPosition.value_or_default() == FpsOverlayPos_TopRight)
        {
            overlayPosition.y = 0;
        }
        else
        {
            overlayPosition.y = io.DisplaySize.y - overlaySize.y;
        }
    }
}

void MenuCommon::RenderActiveUpscalerSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;
    auto& primaryGpu = *ctx.primaryGpu;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // UPSCALER -----------------------------
        ImGui::SeparatorText("Upscaler");

        GetCurrentBackendInfo(state.api, currentBackend, &currentBackendName);

        ImGui::PushItemWidth(180.0f * menuResScale);

        ImGui::Text(primaryGpu.name.c_str());

        ImGui::Text("D3D12 | %s %d.%d.%d", currentFeature->ShortName().c_str(), currentFeature->Version().major,
                    currentFeature->Version().minor, currentFeature->Version().patch);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::Text("| Input: %s", ApiUpscalerInputName(state.currentInputApiName).c_str());

        ImGui::PopItemWidth();

        if (primaryGpu.dlssCapable && !state.NVNGX_DLSS_Path.has_value())
        {
            ImGui::Spacing();
            ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "nvngx_dlss.dll not found, DLSS disabled!");
        }
    }

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;

        // DLSS -----------------
        if ((config->DLSSEnabled.value_or_default() && currentBackend == Upscaler::DLSS &&
             currentFeature->Version().major > 2) ||
            usesDlssd)
        {

            if (usesDlssd)
                ImGui::SeparatorText("DLSSD Settings");
            else
                ImGui::SeparatorText("DLSS Settings");

            auto overridden =
                usesDlssd ? state.dlssdPresetsOverriddenExternally : state.dlssPresetsOverriddenExternally;

            if (overridden)
            {
                ImGui::TextColored(toneMapColor(ImVec4(1.f, 0.8f, 0.f, 1.f)), "Presets are overridden externally");
                ShowHelpMarker("This usually happens due to using tools\n"
                               "such as Nvidia App or Nvidia Inspector");
                // ImGui::Text("Selecting setting below will disable that external override\n"
                //             "but you need to Save Settings and restart the game");

                ImGui::Spacing();
            }

            if (usesDlssd)
            {
                if (bool pOverride = config->DLSSDRenderPresetOverride.value_or_default();
                    ImGui::Checkbox("Render Presets Override", &pOverride))
                    config->DLSSDRenderPresetOverride = pOverride;

                ShowHelpMarker("Each render preset has it strengths and weaknesses\n"
                               "Override to potentially improve image quality\n"
                               "Press apply after enable/disable");

                /*
                auto currentPresetIndex = GetPresetIndex(currentFeature, true);

                if (currentPresetIndex == 0)
                    ImGui::Text("Current Preset: Default");
                else
                    ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
                */

                ImGui::BeginDisabled(!config->DLSSDRenderPresetOverride.value_or_default() /*|| overridden*/);
                ImGui::PushItemWidth(135.0f * menuResScale);

                AddDLSSDRenderPreset("Override Preset", &comboPreset);

                ImGui::PopItemWidth();
                ImGui::EndDisabled();
            }
            else
            {
                if (bool pOverride = config->RenderPresetOverride.value_or_default();
                    ImGui::Checkbox("Render Presets Override", &pOverride))
                    config->RenderPresetOverride = pOverride;

                ShowHelpMarker("Each render preset has it strengths and weaknesses\n"
                               "Override to potentially improve image quality\n"
                               "Press Apply after enable/disable");

                /*
                auto currentPresetIndex = GetPresetIndex(currentFeature, false);

                if (currentPresetIndex == 0)
                    ImGui::Text("Current Preset: Default");
                else
                    ImGui::Text("Current Preset: %c", 64 + currentPresetIndex);
                */

                ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() /*|| overridden*/);

                ImGui::PushItemWidth(135.0f * menuResScale);

                AddDLSSRenderPreset("Override Preset", &comboPreset);

                ImGui::PopItemWidth();
                ImGui::EndDisabled();
            }

            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::Button("Apply Changes"))
            {
                LOG_DEBUG("Applying DLSS/DLSSD preset override changes, preset index: {}",
                          comboPreset.value_or_default());

                if (usesDlssd)
                {
                    config->DLSSDRenderPresetForAll = comboPreset.value_or_default();
                    state.newBackend = Upscaler::DLSSD;
                }
                else
                {
                    config->RenderPresetForAll = comboPreset.value_or_default();
                    state.newBackend = currentBackend;
                }

                MARK_ALL_BACKENDS_CHANGED();
            }

            ImGui::Spacing();

            if (auto ch = ScopedCollapsingHeader(usesDlssd ? "Advanced DLSSD Settings" : "Advanced DLSS Settings");
                ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                bool appIdOverride = config->UseGenericAppIdWithDlss.value_or_default();
                if (ImGui::Checkbox("Use Generic App Id with DLSS", &appIdOverride))
                    config->UseGenericAppIdWithDlss = appIdOverride;

                ShowHelpMarker("Use generic appid with NGX\n"
                               "Fixes OptiScaler preset override not working with certain games\n"
                               "Requires a game restart");

                ImGui::BeginDisabled(!config->RenderPresetOverride.value_or_default() || overridden);
                ImGui::Spacing();
                ImGui::PushItemWidth(135.0f * menuResScale);

                if (usesDlssd)
                {
                    AddDLSSDRenderPreset("DLAA Preset", &config->DLSSDRenderPresetDLAA);
                    AddDLSSDRenderPreset("UltraQ Preset", &config->DLSSDRenderPresetUltraQuality);
                    AddDLSSDRenderPreset("Quality Preset", &config->DLSSDRenderPresetQuality);
                    AddDLSSDRenderPreset("Balanced Preset", &config->DLSSDRenderPresetBalanced);
                    AddDLSSDRenderPreset("Perf Preset", &config->DLSSDRenderPresetPerformance);
                    AddDLSSDRenderPreset("UltraP Preset", &config->DLSSDRenderPresetUltraPerformance);
                }
                else
                {
                    AddDLSSRenderPreset("DLAA Preset", &config->RenderPresetDLAA);
                    AddDLSSRenderPreset("UltraQ Preset", &config->RenderPresetUltraQuality);
                    AddDLSSRenderPreset("Quality Preset", &config->RenderPresetQuality);
                    AddDLSSRenderPreset("Balanced Preset", &config->RenderPresetBalanced);
                    AddDLSSRenderPreset("Perf Preset", &config->RenderPresetPerformance);
                    AddDLSSRenderPreset("UltraP Preset", &config->RenderPresetUltraPerformance);
                }
                ImGui::PopItemWidth();
                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }
    }
}

void MenuCommon::RenderFramerateSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& menuResScale = ctx.menuResScale;

    // Framerate ---------------------
    if (state.reflexLimitsFps || config->OverlayMenu.value_or_default())
    {
        SeparatorWithHelpMarker("Framerate", "Uses Reflex when possible");

        std::string currentMethod = state.reflexLimitsFps ? "Reflex" : "Fallback";

        if (state.rtssReflexInjection)
            currentMethod.append(" (RTSS)");

        ImGui::Text("Current method: %s", currentMethod.c_str());

        // set initial value
        if (std::isinf(_limitFps))
            _limitFps = config->FramerateLimit.value_or_default();

        ImGui::SliderFloat("FPS Limit", &_limitFps, 0, 200, "%.0f");

        if (ImGui::Button("Apply Limit"))
        {
            config->FramerateLimit = _limitFps;
        }

        ImGui::SameLine(0.0f, 16.0f);

        if (ImGui::Button("Reset Limit"))
        {
            _limitFps = 0.0f;
            config->FramerateLimit = _limitFps;
        }

        ImGui::Spacing();
        if (auto ch = ScopedCollapsingHeader("VRR Frame Cap Calculator"); ch.IsHeaderOpen())
        {
            ScopedIndent indent {};
            ImGui::Spacing();

            ImGui::PushItemWidth(105.0f * menuResScale);
            ImGui::InputInt("Refresh Rate", &refreshRate, 1, 1, ImGuiInputTextFlags_None);
            ImGui::PopItemWidth();

            float refreshRateF = static_cast<float>(refreshRate);
            constexpr float margin = 0.3f; // in ms
            float frameCap = std::round(10000.f / (1000.f / refreshRateF + margin)) / 10.f;

            ImGui::Text("Calculated Cap: %.1f", frameCap);

            ImGui::SameLine(0.0f, 16.0f);

            if (ImGui::Button("Set as FPS Limit"))
            {
                _limitFps = frameCap;
                config->FramerateLimit = _limitFps;
            }
        }
    }
}

void MenuCommon::RenderActiveImageSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    bool rcasEnabled = false;

    if (currentFeature != nullptr && !currentFeature->IsFrozen())
    {
        // SHARPNESS -----------------------------
        ImGui::SeparatorText("Sharpness");

        if (bool overrideSharpness = config->OverrideSharpness.value_or_default();
            ImGui::Checkbox("Override", &overrideSharpness))
        {
            config->OverrideSharpness = overrideSharpness;

            if (currentBackend == Upscaler::DLSS && currentFeature->Version().major < 3)
            {
                state.newBackend = currentBackend;
                MARK_ALL_BACKENDS_CHANGED();
            }
        }
        ShowHelpMarker("Ignores the value sent by the game\n"
                       "and uses the value set below");

        ImGui::SameLine(0.0f, 16.0f * menuResScale);

        float featuresCurrentSharpness = currentFeature->Sharpness();
        if (featuresCurrentSharpness > 0.0f)
            ImGui::TextDisabled("(Current sharpness: %.3f)", featuresCurrentSharpness);
        else
            ImGui::TextDisabled("(Current sharpness: disabled)");

        ImGui::BeginDisabled(!config->OverrideSharpness.value_or_default());

        float sharpness = config->Sharpness.value_or_default();

        if (ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, 1.0f))
            config->Sharpness = sharpness;

        ImGui::EndDisabled();

        // RCAS
        {
            // dlss version >= 2.5.1
            constexpr feature_version requiredDlssVersion = { 2, 5, 1 };
            rcasEnabled = currentBackend == Upscaler::DLSS && currentFeature->Version() >= requiredDlssVersion;

            ImGui::Spacing();
            ImGui::Spacing();

            if (bool rcas = config->RcasEnabled.value_or(rcasEnabled); ImGui::Checkbox("Enable RCAS/DA", &rcas))
                config->RcasEnabled = rcas;

            ShowHelpMarker("Enable OptiScaler's sharpening filter\n"
                           "By default uses a sharpening value provided by the game\n"
                           "Select 'Override' under 'Sharpness' and adjust the slider\n"
                           "to change it\n\n"
                           "Some upscalers have their own sharpness filter, so this\n"
                           "option is not always needed");

            ImGui::BeginDisabled(!config->RcasEnabled.value_or(rcasEnabled));

            auto sharpnessShader = (int32_t) Config::Instance()->SharpnessShader.value_or_default();

            if (ImGui::RadioButton("RCAS", &sharpnessShader, (int32_t) SharpenShader::RCAS))
            {
                Config::Instance()->SharpnessShader = SharpenShader::RCAS;
            }

            ShowHelpMarker("Use AMD's RCAS\n"
                           "Modified to add Contrast parameter\n"
                           "and MAS support");

            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::RadioButton("Depth Aware (RCAS)", &sharpnessShader, (int32_t) SharpenShader::DepthAware))
            {
                Config::Instance()->SharpnessShader = SharpenShader::DepthAware;
            }

            ShowHelpMarker("Use Depth Aware Sharpening (RCAS)\n"
                           "Smarter sharpening with less artifacts,\n"
                           "but also heavier\n\n"
                           "The farther away is the object, the more\n"
                           "sharpening is applied");

            ImGui::SameLine(0.0f, 6.0f);

            if (ImGui::RadioButton("Depth Aware (DAS)", &sharpnessShader,
                                   (int32_t) SharpenShader::LocalContrastDepthAware))
            {
                Config::Instance()->SharpnessShader = SharpenShader::LocalContrastDepthAware;
            }

            ShowHelpMarker("Use Depth Aware Sharpening (DAS)\n"
                           "Depth-aware directional adaptive luma sharpener\n"
                           "Smarter sharpening with less artifacts,\n"
                           "but also heavier\n\n"
                           "The farther away is the object, the more\n"
                           "sharpening is applied");

            ImGui::Spacing();

            if (bool overrideMotionSharpness = config->MotionSharpnessEnabled.value_or_default();
                ImGui::Checkbox("Enable Motion Adaptive Sharpness", &overrideMotionSharpness))
                config->MotionSharpnessEnabled = overrideMotionSharpness;
            ShowHelpMarker("Enables sharpness adjustments according to the motion");

            if (Config::Instance()->SharpnessShader.value_or_default() != SharpenShader::RCAS)
            {
                if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                    ImGui::Checkbox("DA + MAS Debug", &overrideMSDebug))
                    config->MotionSharpnessDebug = overrideMSDebug;

                ShowHelpMarker("Enable DA + MAS debug views\n"
                               "Blue tint for DA detected edges\n\n"
                               "More red areas will have more sharpness applied\n"
                               "Green areas will get reduced sharpness");

                if (auto ch = ScopedCollapsingHeader("Advanced DA Parameters"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (bool clamp = config->DAClampOutput.value_or(false); ImGui::Checkbox("Clamp Output", &clamp))
                    {
                        if (clamp)
                            config->DAClampOutput = true;
                        else
                            config->DAClampOutput.reset();
                    }

                    ShowHelpMarker("Clamps the final image to the [0, 1] range.\n\n"
                                   "Prevents overshoot artifacts such as bright halos or negative colors.\n"
                                   "Recommended for LDR pipelines; optional for HDR depending on tone-mapping.\n\n"
                                   "When not set OptiScaler controls it via upscalers HDR flag");

                    if (currentFeature->DepthLinear())
                    {
                        float depthBias = config->DADepthBias.value_or(0.0015f);
                        if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.005f, 0.03f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowHelpMarker("Ignores small depth differences before edge detection.\n\n"
                                       "Higher values reduce flickering and noise from minor depth changes, but may "
                                       "soften real geometry edges.\n"
                                       "Lower values preserve fine detail but can cause unstable or noisy edge "
                                       "detection.");

                        float depthScale = config->DADepthScale.value_or(250.0f);
                        if (ImGui::SliderFloat("Depth Scale", &depthScale, 100.0f, 600.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowHelpMarker("Controls how strongly sharpening is reduced across depth edges.\n\n"
                                       "Higher values more aggressively prevent sharpening across object boundaries "
                                       "(reduces halos).\n"
                                       "Lower values allow more sharpening to pass across edges (sharper but "
                                       "riskier).");
                    }
                    else
                    {
                        float depthBias = config->DADepthBias.value_or(0.001f);
                        if (ImGui::SliderFloat("Depth Bias", &depthBias, 0.0001f, 0.003f, "%.4f"))
                            config->DADepthBias = depthBias;

                        ShowHelpMarker("Ignores small depth differences before edge detection.\n\n"
                                       "Higher values reduce flickering and noise from minor depth changes, but may "
                                       "soften real geometry edges.\n"
                                       "Lower values preserve fine detail but can cause unstable or noisy edge "
                                       "detection.");

                        float depthScale = config->DADepthScale.value_or(35.0f);
                        if (ImGui::SliderFloat("Depth Scale", &depthScale, 25.0f, 400.0f, "%.1f"))
                            config->DADepthScale = depthScale;

                        ShowHelpMarker("Controls how strongly sharpening is reduced across depth edges.\n\n"
                                       "Higher values more aggressively prevent sharpening across object boundaries "
                                       "(reduces halos).\n"
                                       "Lower values allow more sharpening to pass across edges (sharper but "
                                       "riskier).");
                    }

                    if (ImGui::Button("Reset Depth Values"))
                    {
                        config->DADepthBias.reset();
                        config->DADepthScale.reset();
                    }
                }
            }
            else
            {
                if (bool contrastEnabled = config->ContrastEnabled.value_or_default();
                    ImGui::Checkbox("Contrast Enabled", &contrastEnabled))
                    config->ContrastEnabled = contrastEnabled;

                ShowHelpMarker("Controls sharpness at high contrast areas.");

                ImGui::BeginDisabled(!config->ContrastEnabled.value_or_default());

                float contrast = config->Contrast.value_or_default();
                if (ImGui::SliderFloat("Contrast", &contrast, -2.0f, 2.0f, "%.2f"))
                    config->Contrast = contrast;

                ShowHelpMarker("Positive values decrease sharpness at high contrast areas.\n"
                               "Negative values increase sharpness at high contrast areas.");

                ImGui::EndDisabled();
            }

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Motion Adaptive Sharpness##2"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                ImGui::BeginDisabled(!config->MotionSharpnessEnabled.value_or_default());

                if (Config::Instance()->SharpnessShader.value_or_default() == SharpenShader::RCAS)
                {
                    if (bool overrideMSDebug = config->MotionSharpnessDebug.value_or_default();
                        ImGui::Checkbox("MAS Debug", &overrideMSDebug))
                        config->MotionSharpnessDebug = overrideMSDebug;
                    ShowHelpMarker("Areas that are more red will have more sharpness applied\n"
                                   "Green areas will get reduced sharpness");
                }

                float motionSharpness = config->MotionSharpness.value_or_default();
                ImGui::SliderFloat("MotionSharpness", &motionSharpness, -1.0f, 1.0f, "%.3f");
                config->MotionSharpness = motionSharpness;

                ShowHelpMarker("Maximum amount of sharpness that motion can add or remove.\n\n"
                               "Negative values reduce sharpening in motion (recommended).\n"
                               "Positive values increase sharpening in motion.\n\n"
                               "The final adjustment scales with motion and is capped at this value.");

                float motionThreshod = config->MotionThreshold.value_or_default();
                ImGui::SliderFloat("MotionThreshod", &motionThreshod, 0.0f, 100.0f, "%.2f");
                config->MotionThreshold = motionThreshod;

                ShowHelpMarker("Minimum motion required before motion-based sharpening adjustment begins.\n\n"
                               "Higher values ignore small movements (more stable).\n"
                               "Lower values react to subtle motion (more sensitive).");

                float motionScale = config->MotionScaleLimit.value_or_default();
                ImGui::SliderFloat("MotionRange", &motionScale, 0.01f, 100.0f, "%.2f");
                config->MotionScaleLimit = motionScale;

                ShowHelpMarker("Defines the motion range over which the effect ramps from zero to full strength.\n\n"
                               "Values above the threshold are mapped into this range.\n"
                               "Larger values make the response smoother and more gradual.\n"
                               "Smaller values make the effect react more quickly and aggressively.");

                ImGui::EndDisabled();

                ImGui::Spacing();
                ImGui::Spacing();
            }

            ImGui::EndDisabled();
        }

        // UPSCALE RATIO OVERRIDE -----------------

        auto minSliderLimit = config->ExtendedLimits.value_or_default() ? 0.1f : 1.0f;
        auto maxSliderLimit = config->ExtendedLimits.value_or_default() ? 6.0f : 3.0f;

        ImGui::SeparatorText("Upscale Ratio Override");

        if (bool upOverride = config->UpscaleRatioOverrideEnabled.value_or_default();
            ImGui::Checkbox("Override all", &upOverride))
        {
            config->UpscaleRatioOverrideEnabled = upOverride;

            if (upOverride)
                config->QualityRatioOverrideEnabled = false;
        }
        ShowHelpMarker("Overrides every upscaler preset with the set value\n\n"
                       "1.5x on a 1080p screen means an internal res of 720p\n"
                       "1080 / 1.5 = 720");

        if (bool qOverride = config->QualityRatioOverrideEnabled.value_or_default();
            ImGui::Checkbox("Override per quality preset", &qOverride))
        {
            config->QualityRatioOverrideEnabled = qOverride;

            if (qOverride)
                config->UpscaleRatioOverrideEnabled = false;
        }

        ShowHelpMarker("Lets you override each preset's ratio individually\n"
                       "Note that not every game supports every quality preset\n\n"
                       "1.5x on a 1080p screen means internal resolution of 720p\n"
                       "1080 / 1.5 = 720");

        if (config->UpscaleRatioOverrideEnabled.value_or_default())
        {
            float urOverride = config->UpscaleRatioOverrideValue.value_or_default();
            ImGui::SliderFloat("All Ratios", &urOverride, minSliderLimit, maxSliderLimit, "%.3f");
            config->UpscaleRatioOverrideValue = urOverride;
        }

        if (config->QualityRatioOverrideEnabled.value_or_default())
        {
            float qDlaa = config->QualityRatio_DLAA.value_or_default();
            if (ImGui::SliderFloat("DLAA", &qDlaa, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_DLAA = qDlaa;

            float qUq = config->QualityRatio_UltraQuality.value_or_default();
            if (ImGui::SliderFloat("Ultra Quality", &qUq, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_UltraQuality = qUq;

            float qQ = config->QualityRatio_Quality.value_or_default();
            if (ImGui::SliderFloat("Quality", &qQ, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_Quality = qQ;

            float qB = config->QualityRatio_Balanced.value_or_default();
            if (ImGui::SliderFloat("Balanced", &qB, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_Balanced = qB;

            float qP = config->QualityRatio_Performance.value_or_default();
            if (ImGui::SliderFloat("Performance", &qP, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_Performance = qP;

            float qUp = config->QualityRatio_UltraPerformance.value_or_default();
            if (ImGui::SliderFloat("Ultra Performance", &qUp, minSliderLimit, maxSliderLimit, "%.3f"))
                config->QualityRatio_UltraPerformance = qUp;
        }

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            // OUTPUT SCALING -----------------------------
                {
                // if motion vectors are not display size
                ImGui::BeginDisabled(!currentFeature->LowResMV() &&
                                     currentFeature->RenderWidth() != currentFeature->DisplayWidth());

                ImGui::SeparatorText("Output Scaling");

                float defaultRatio = 1.5f;

                if (_ssRatio == 0.0f)
                {
                    _ssRatio = config->OutputScalingMultiplier.value_or(defaultRatio);
                    _ssEnabled = config->OutputScalingEnabled.value_or_default();
                    _ssDownsampler = config->OutputScalingDownscaler.value_or_default();
                }

                ImGui::BeginDisabled(currentBackend == Upscaler::DLSS &&
                                     currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                ImGui::Checkbox("Enable", &_ssEnabled);
                ImGui::EndDisabled();

                ShowHelpMarker("Upscales the image internally to a higher output resolution\n"
                               "then downscales it back to your display resolution\n\n"
                               "Values <1.0 make the upscaler cheaper\n"
                               "Values >1.0 make image sharper at the cost of performance\n\n"
                               "If greyed out, please check Git Wiki - Unreal Engine tweaks\n\n"
                               "Target res and total ratio at the bottom (max. total 3.0!)");

                ImGui::SameLine(0.0f, 6.0f);

                ImGui::BeginDisabled(!_ssEnabled);
                {
                    ImGui::PushItemWidth(95.0f * menuResScale);

                    // clang-format off
                    std::vector<MenuOption<Scaler>> ds_options = {
                        { Scaler::FSR1, "FSR1",
                            "Default option.\nGood enough image quality and very fast." },
                        { Scaler::Bicubic, "Bicubic",
                            "Fastest traditional option.\nProduces a very soft/blurry image, but might be okay for downscaling." },
                        { Scaler::CatmullRom, "Catmull-Rom",
                            "Designed primarily for downscaling.\nRetains good contrast with minimal artefacts, but softer than Lanczos." },
                        { Scaler::Lanczos2, "Lanczos2",
                            "Lighter and faster than Lanczos3.\nLess prone to ringing artefacts, but slightly blurrier." },
                        { Scaler::Lanczos3, "Lanczos3",
                            "Heavier version of Lanczos2.\nOffers the sharpest image, but is the most prone to ringing.\nConsidered the best along with Kaiser3." },
                        { Scaler::Kaiser2, "Kaiser2",
                            "Similar to Lanczos2.\nSmoother and less prone to artefacts than Lanczos, but slightly blurrier." },
                        { Scaler::Kaiser3, "Kaiser3",
                            "Similar to Lanczos3.\nFar less prone to artefacting than Lanczos3, but much heavier on the GPU.\nConsidered the best along with Lanczos3." },
                        { Scaler::Magic, "MAGIC",
                            "Specialised to prevent artifacts.\nEliminates harsh halos for a natural look, but can appear slightly soft." }
                    };
                    // clang-format on

                    const bool isUpsampleRatio = _ssRatio < 1.0f;
                    const std::string disabledReason = "Only FSR1 and Bicubic are supported when Ratio is below 1.0.";

                    for (auto& opt : ds_options)
                    {
                        if (isUpsampleRatio && opt.value > Scaler::Bicubic)
                            opt.set_disabled(true, opt.tooltip + "\n\n" + disabledReason);
                    }

                    if (isUpsampleRatio && _ssDownsampler > Scaler::Bicubic)
                        _ssDownsampler = Scaler::FSR1;

                    PopulateCombo("Downscaler", _ssDownsampler, ds_options);

                    ImGui::PopItemWidth();
                }
                ImGui::EndDisabled();

                bool applyEnabled = _ssEnabled != config->OutputScalingEnabled.value_or_default() ||
                                    _ssRatio != config->OutputScalingMultiplier.value_or(defaultRatio) ||
                                    _ssDownsampler != config->OutputScalingDownscaler.value_or_default();

                ImGui::BeginDisabled(!applyEnabled);
                if (ImGui::Button("Apply Change"))
                {
                    config->OutputScalingEnabled = _ssEnabled;
                    config->OutputScalingMultiplier = _ssRatio;

                    if (_ssRatio < 1.0f && _ssDownsampler > Scaler::Bicubic)
                        _ssDownsampler = Scaler::FSR1;

                    config->OutputScalingDownscaler = _ssDownsampler;

                    const bool usesDlssd = currentFeature->GetUpscalerType() == Upscaler::DLSSD;
                    if (usesDlssd)
                        state.newBackend = Upscaler::DLSSD;
                    else
                        state.newBackend = currentBackend;

                    MARK_ALL_BACKENDS_CHANGED();
                }
                ImGui::EndDisabled();

                ImGui::BeginDisabled(!_ssEnabled || currentFeature->RenderWidth() > currentFeature->DisplayWidth());
                ImGui::SliderFloat("Ratio", &_ssRatio, 0.5f, 3.0f, "%.2f");
                ImGui::EndDisabled();

                if (currentFeature != nullptr && !currentFeature->IsFrozen())
                {
                    ImGui::Text("Output Scaling is %s, Target Res: %dx%d (%.2f)\nJitter Count: %d",
                                config->OutputScalingEnabled.value_or_default() ? "ENABLED" : "DISABLED",
                                (uint32_t) (currentFeature->DisplayWidth() * _ssRatio),
                                (uint32_t) (currentFeature->DisplayHeight() * _ssRatio),
                                ((float) currentFeature->DisplayWidth() * _ssRatio) /
                                    (float) currentFeature->RenderWidth(),
                                currentFeature->JitterCount());
                }

                ImGui::EndDisabled();
            }
        }

        // INIT -----------------------------
        ImGui::SeparatorText("Init Flags");
        if (ImGui::BeginTable("init", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();

            if (bool autoExposure = currentFeature->AutoExposure(); ImGui::Checkbox("Auto Exposure", &autoExposure))
            {
                config->AutoExposure = autoExposure;
                ReInitUpscaler();
            }
            ShowResetButton(&config->AutoExposure, "R");
            ShowHelpMarker("Some Unreal Engine games need this\n\n"
                           "Try using if colours flickering or\n"
                           "objects have ghosting trails");

            ImGui::EndTable();

            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Advanced Init Flags"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                if (ImGui::BeginTable("init2", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableNextColumn();
                    if (bool depth = currentFeature->DepthInverted(); ImGui::Checkbox("Depth Inverted", &depth))
                    {
                        config->DepthInverted = depth;
                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->DepthInverted, "R##2");
                    ShowHelpMarker("You shouldn't need to change it");

                    ImGui::TableNextColumn();
                    if (bool hdr = currentFeature->IsHdr(); ImGui::Checkbox("HDR", &hdr))
                    {
                        config->HDR = hdr;
                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->HDR, "R##1");
                    ShowHelpMarker("Might help with purple hue in some games");

                    ImGui::TableNextColumn();
                    if (bool mv = !currentFeature->LowResMV(); ImGui::Checkbox("Display Res. MV", &mv))
                    {
                        config->DisplayResolution = mv;

                        // Disable output scaling when
                        // Display res MV is active
                        if (mv)
                        {
                            config->OutputScalingEnabled = false;
                            _ssEnabled = false;
                        }

                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->DisplayResolution, "R##4");
                    ShowHelpMarker("Mostly a fix for Unreal Engine games\n"
                                   "Top left part of the screen will be blurry");

                    ImGui::TableNextColumn();

                    if (bool jitter = currentFeature->JitteredMV(); ImGui::Checkbox("Jitter Cancellation", &jitter))
                    {
                        config->JitterCancellation = jitter;
                        ReInitUpscaler();
                    }
                    ShowResetButton(&config->JitterCancellation, "R##3");
                    ShowHelpMarker("Fix for games that send motion data with preapplied jitter");

                    ImGui::TableNextColumn();
                    ImGui::EndTable();
                }
            }
        }
    }
}

void MenuCommon::RenderAdvancedSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;

    // ADVANCED SETTINGS -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Advanced Settings"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            bool extendedLimits = config->ExtendedLimits.value_or_default();
            if (ImGui::Checkbox("Enable Extended Limits", &extendedLimits))
                config->ExtendedLimits = extendedLimits;

            ShowHelpMarker("Extended sliders limit for quality presets\n\n"
                           "Using this option changes resolution detection logic\n"
                           "and might cause issues and crashes!");
        }

        bool pcShaders = config->UsePrecompiledShaders.value_or_default();
        if (ImGui::Checkbox("Use Precompiled Shaders", &pcShaders))
        {
            config->UsePrecompiledShaders = pcShaders;
            state.newBackend = currentBackend;
            MARK_ALL_BACKENDS_CHANGED();
        }

        // DRS
        ImGui::SeparatorText("DRS (Dynamic Resolution Scaling)");
        if (ImGui::BeginTable("drs", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();
            if (bool drsMin = config->DrsMinOverrideEnabled.value_or_default();
                ImGui::Checkbox("Override Minimum", &drsMin))
                config->DrsMinOverrideEnabled = drsMin;
            ShowHelpMarker("Fix for games ignoring official DRS limits");

            ImGui::TableNextColumn();
            if (bool drsMax = config->DrsMaxOverrideEnabled.value_or_default();
                ImGui::Checkbox("Override Maximum", &drsMax))
                config->DrsMaxOverrideEnabled = drsMax;
            ShowHelpMarker("Fix for games ignoring official DRS limits");

            ImGui::EndTable();
        }

        // Non-DLSS hotfixes -----------------------------
        if (currentFeature != nullptr && !currentFeature->IsFrozen() && currentBackend != Upscaler::DLSS)
        {
            // BARRIERS -----------------------------
            ImGui::Spacing();
            if (auto ch = ScopedCollapsingHeader("Resource Barriers"); ch.IsHeaderOpen())
            {
                ScopedIndent indent {};
                ImGui::Spacing();

                AddResourceBarrier("Color", &config->ColorResourceBarrier);
                AddResourceBarrier("Depth", &config->DepthResourceBarrier);
                AddResourceBarrier("Motion", &config->MVResourceBarrier);
                AddResourceBarrier("Exposure", &config->ExposureResourceBarrier);
                AddResourceBarrier("Mask", &config->MaskResourceBarrier);
                AddResourceBarrier("Output", &config->OutputResourceBarrier);
            }

            // HOTFIXES -----------------------------
            if (state.api == DX12)
            {
                ImGui::Spacing();
                if (auto ch = ScopedCollapsingHeader("Root Signatures"); ch.IsHeaderOpen())
                {
                    ScopedIndent indent {};
                    ImGui::Spacing();

                    if (bool crs = config->RestoreComputeSignature.value_or_default();
                        ImGui::Checkbox("Restore Compute Root Signature", &crs))
                        config->RestoreComputeSignature = crs;

                    if (bool grs = config->RestoreGraphicSignature.value_or_default();
                        ImGui::Checkbox("Restore Graphic Root Signature", &grs))
                        config->RestoreGraphicSignature = grs;
                }
            }
        }
    }
}

void MenuCommon::RenderLoggingSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    // LOGGING -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Logging"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        if (config->LogToConsole.value_or_default() || config->LogToFile.value_or_default() ||
            config->LogToNGX.value_or_default())
            spdlog::default_logger()->set_level((spdlog::level::level_enum) config->LogLevel.value_or_default());
        else
            spdlog::default_logger()->set_level(spdlog::level::off);

        if (bool toFile = config->LogToFile.value_or_default(); ImGui::Checkbox("To File", &toFile))
        {
            config->LogToFile = toFile;
            PrepareLogger();
        }

        ImGui::SameLine(0.0f, 6.0f);
        if (bool toConsole = config->LogToConsole.value_or_default(); ImGui::Checkbox("To Console", &toConsole))
        {
            config->LogToConsole = toConsole;
            PrepareLogger();
        }

        const char* logLevels[] = { "Trace", "Debug", "Information", "Warning", "Error" };
        const char* selectedLevel = logLevels[config->LogLevel.value_or_default()];

        if (ImGui::BeginCombo("Log Level", selectedLevel))
        {
            for (int n = 0; n < 5; n++)
            {
                if (ImGui::Selectable(logLevels[n], (config->LogLevel.value_or_default() == n)))
                {
                    config->LogLevel = n;
                    spdlog::default_logger()->set_level(
                        (spdlog::level::level_enum) config->LogLevel.value_or_default());
                }
            }

            ImGui::EndCombo();
        }
    }
}

void MenuCommon::RenderApiAndTextureSettings(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto config = ctx.config;
    auto& currentFeature = ctx.currentFeature;
    auto& menuResScale = ctx.menuResScale;

    // V-SYNC -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("V-Sync Settings"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        auto forceVsyncOn = config->ForceVsync.has_value() && config->ForceVsync.value();
        auto forceVsyncOff = config->ForceVsync.has_value() && !config->ForceVsync.value();

        if (ImGui::Checkbox("V-Sync On", &forceVsyncOn))
        {
            if (forceVsyncOn)
            {
                config->ForceVsync = true;
            }
            else
            {
                config->ForceVsync.reset();
            }
        }
        ImGui::SameLine(0.0f, 16.0f);

        if (ImGui::Checkbox("V-Sync Off", &forceVsyncOff))
        {
            if (forceVsyncOff)
            {
                config->ForceVsync = false;
            }
            else
            {
                config->ForceVsync.reset();
            }
        }
        ImGui::SameLine(0.0f, 16.0f);

        ImGui::BeginDisabled(!forceVsyncOn);

        ImGui::PushItemWidth(50.0f * menuResScale);

        auto vsyncBuf = StrFmt("%d", config->VsyncInterval.value_or_default());
        if (ImGui::BeginCombo("Sync Int.", vsyncBuf.c_str()))
        {
            if (ImGui::Selectable("0", config->VsyncInterval.value_or_default() == 0))
            {
                config->VsyncInterval = 0;
            }

            if (ImGui::Selectable("1", config->VsyncInterval.value_or_default() == 1))
            {
                config->VsyncInterval = 1;
            }

            if (ImGui::Selectable("2", config->VsyncInterval.value_or_default() == 2))
            {
                config->VsyncInterval = 2;
            }

            if (ImGui::Selectable("3", config->VsyncInterval.value_or_default() == 3))
            {
                config->VsyncInterval = 3;
            }

            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ShowHelpMarker("Controls the DXGI Present sync interval, which determines how\n"
                       "the swap chain waits for vertical refresh.\n\n"
                       "0  = Present immediately, no VSync wait.\n"
                       "1  = Sync to every refresh, normal VSync.\n"
                       "2+ = Present every N refreshes, reducing effective frame rate.\n\n"
                       "Higher values can reduce tearing but may increase latency and cap FPS.\n"
                       "For most games, use 0 for lowest latency or 1 for normal VSync.");

        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 16.0f);

        if (ImGui::Button("Reset##10"))
        {
            config->ForceVsync.reset();
        }

        ShowHelpMarker("Force V-Sync On/Off & Sync Interval options");

    }

    // MIPMAP BIAS & Anisotropy -----------------------------
    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Mipmap Bias", (currentFeature == nullptr || currentFeature->IsFrozen())
                                                            ? ImGuiTreeNodeFlags_DefaultOpen
                                                            : 0);
        ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();
        if (config->MipmapBiasOverride.has_value() && _mipBias == 0.0f)
            _mipBias = config->MipmapBiasOverride.value();

        ImGui::SliderFloat("Mipmap Bias##2", &_mipBias, -15.0f, 15.0f, "%.6f");
        ShowHelpMarker("Can help with blurry textures in broken games\n"
                       "Negative values will make textures sharper\n"
                       "Positive values will make textures more blurry\n\n"
                       "Has a small performance impact");

        ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
        {
            ImGui::BeginDisabled(config->MipmapBiasScaleOverride.has_value() &&
                                 config->MipmapBiasScaleOverride.value());
            {
                bool mbFixed = config->MipmapBiasFixedOverride.value_or_default();
                if (ImGui::Checkbox("MB Fixed Override", &mbFixed))
                {
                    config->MipmapBiasScaleOverride.reset();
                    config->MipmapBiasFixedOverride = mbFixed;
                }

                ShowHelpMarker("Apply same override value to all textures");
            }
            ImGui::EndDisabled();

            ImGui::SameLine(0.0f, 6.0f);

            ImGui::BeginDisabled(config->MipmapBiasFixedOverride.has_value() &&
                                 config->MipmapBiasFixedOverride.value());
            {
                bool mbScale = config->MipmapBiasScaleOverride.value_or_default();
                if (ImGui::Checkbox("MB Scale Override", &mbScale))
                {
                    config->MipmapBiasFixedOverride.reset();
                    config->MipmapBiasScaleOverride = mbScale;
                }

                ShowHelpMarker("Apply override value as scale multiplier\n"
                               "When using scale mode, please use positive\n"
                               "override values to increase sharpness!");
            }
            ImGui::EndDisabled();

            bool mbAll = config->MipmapBiasOverrideAll.value_or_default();
            if (ImGui::Checkbox("MB Override All Textures", &mbAll))
                config->MipmapBiasOverrideAll = mbAll;

            ShowHelpMarker("Override all textures mipmap values\n"
                           "Normally OptiScaler only overrides\n"
                           "below zero mipmap values!");
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(config->MipmapBiasOverride.has_value() &&
                             config->MipmapBiasOverride.value() == _mipBias);
        {
            if (ImGui::Button("Set"))
            {
                config->MipmapBiasOverride = _mipBias;
                state.lastMipBias = 100.0f;
                state.lastMipBiasMax = -100.0f;
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 6.0f);

        ImGui::BeginDisabled(!config->MipmapBiasOverride.has_value());
        {
            if (ImGui::Button("Reset"))
            {
                config->MipmapBiasOverride.reset();
                _mipBias = 0.0f;
                state.lastMipBias = 100.0f;
                state.lastMipBiasMax = -100.0f;
            }
        }
        ImGui::EndDisabled();

        if (config->MipmapBiasOverride.has_value())
        {
            if (config->MipmapBiasFixedOverride.value_or_default())
            {
                ImGui::Text("Current : %.3f / %.3f, Target: %.3f", state.lastMipBias, state.lastMipBiasMax,
                            config->MipmapBiasOverride.value());
            }
            else if (config->MipmapBiasScaleOverride.value_or_default())
            {
                ImGui::Text("Current : %.3f / %.3f, Target: Base * %.3f", state.lastMipBias, state.lastMipBiasMax,
                            config->MipmapBiasOverride.value());
            }
            else
            {
                ImGui::Text("Current : %.3f / %.3f, Target: Base + %.3f", state.lastMipBias, state.lastMipBiasMax,
                            config->MipmapBiasOverride.value());
            }
        }
        else
        {
            ImGui::Text("Current : %.3f / %.3f", state.lastMipBias, state.lastMipBiasMax);
        }

        ImGui::Text("Will be applied after RESOLUTION/PRESET change !!!");
    }

    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader(
            "Anisotropic Filtering",
            (currentFeature == nullptr || currentFeature->IsFrozen()) ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();
        ImGui::PushItemWidth(65.0f * menuResScale);

        auto selectedAF =
            config->AnisotropyOverride.has_value() ? std::to_string(config->AnisotropyOverride.value()) : "Auto";
        if (ImGui::BeginCombo("Force Anisotropic Filtering", selectedAF.c_str()))
        {
            if (ImGui::Selectable("Auto", !config->AnisotropyOverride.has_value()))
                config->AnisotropyOverride.reset();

            if (ImGui::Selectable("1", config->AnisotropyOverride.value_or(0) == 1))
                config->AnisotropyOverride = 1;

            if (ImGui::Selectable("2", config->AnisotropyOverride.value_or(0) == 2))
                config->AnisotropyOverride = 2;

            if (ImGui::Selectable("4", config->AnisotropyOverride.value_or(0) == 4))
                config->AnisotropyOverride = 4;

            if (ImGui::Selectable("8", config->AnisotropyOverride.value_or(0) == 8))
                config->AnisotropyOverride = 8;

            if (ImGui::Selectable("16", config->AnisotropyOverride.value_or(0) == 16))
                config->AnisotropyOverride = 16;

            ImGui::EndCombo();
        }

        ImGui::PopItemWidth();

        bool afComp = config->AnisotropyModifyComp.value_or_default();
        if (ImGui::Checkbox("Modify Compare", &afComp))
            config->AnisotropyModifyComp = afComp;

        ShowHelpMarker("Update comparison filters");

        ImGui::SameLine(0.0f, 6.0f);

        bool afMinMax = config->AnisotropyModifyMinMax.value_or_default();
        if (ImGui::Checkbox("Modify Min/Max", &afMinMax))
            config->AnisotropyModifyMinMax = afMinMax;

        ShowHelpMarker("Update min/max filters");

        bool afSkipPoint = config->AnisotropySkipPointFilter.value_or_default();
        if (ImGui::Checkbox("Skip Point Filters", &afSkipPoint))
            config->AnisotropySkipPointFilter = afSkipPoint;

        ShowHelpMarker("Skip updating of point filters");

        ImGui::Text("Will might be applied after RESOLUTION/PRESET change !!!");
    }
}

void LaminrUI::DrawSnapshotKeybind(){
 auto c=Config::Instance();auto& value=c->SnapshotKey;
 static auto key=Keybind("SnapShot",16);key.Render(value);
 ImGui::PushTextWrapPos();
 if(SnapshotConflict(value.value_or_default()))ImGui::TextColored(ImVec4(1,.4f,.3f,1),"Choose a key that is not assigned to another laminr action.");
 ImGui::PopTextWrapPos();
 if(ImGui::Button("Take screenshot"))PollSnapshot(true,true);
 const auto game=Util::DllPath().parent_path();
 static char destination[4096]{};static bool initialized=false,jpeg=false;static int jpegQuality=90;static std::string preferenceStatus;
 if(!initialized){try{const auto saved=laminr::snapshot::settings(game);strncpy_s(destination,laminr::snapshot::utf8(saved.parent).c_str(),_TRUNCATE);jpeg=saved.jpeg;jpegQuality=saved.quality;}catch(const std::exception& e){preferenceStatus=e.what();}initialized=true;}
 ImGui::InputText("Snapshot parent folder",destination,sizeof(destination));
 int format=jpeg?1:0;if(ImGui::Combo("Image format",&format,"PNG (lossless)\0JPEG (smaller)\0"))jpeg=format==1;
 if(jpeg)ImGui::SliderInt("JPEG quality",&jpegQuality,1,100,"%d",ImGuiSliderFlags_AlwaysClamp);
 if(ImGui::Button("Save snapshot options")){try{laminr::snapshot::save_settings(game,{std::filesystem::u8path(destination),jpeg,jpegQuality});preferenceStatus="Snapshot options saved.";}catch(const std::exception& e){preferenceStatus=e.what();}}
 if(!preferenceStatus.empty())ImGui::TextWrapped("%s",preferenceStatus.c_str());
 std::filesystem::path folder;
 try{folder=laminr::snapshot::folder(laminr::snapshot::utf8(laminr::snapshot::settings(game).parent));}catch(...){folder=game/L"laminr Snapshots";}
 if(ImGui::Button("Browse screenshots")){
  std::error_code ec;std::filesystem::create_directories(folder,ec);
  if(ec||reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)SnapshotNotice(false,"Could not open the game's laminr Snapshots folder.");
 }
 ImGui::TextWrapped("Saved inside laminr Snapshots under your selected existing folder. HDR captures use PNG to preserve range. Save settings to keep your keybind.");
 ImGui::TextWrapped("Turn Num Lock on for number-pad digits. With Num Lock off they act as navigation keys and may conflict with another shortcut.");
}
void LaminrUI::DrawHotSwapKeybind(){auto& value=Config::Instance()->PresetHotSwapKey;static auto key=Keybind("Preset hotswap",15);key.Render(value);ImGui::TextWrapped("Save settings to keep this key. Escape cancels; Backspace clears it.");}

void MenuCommon::RenderKeybindSettings(RenderMenuContext& ctx)
{
    auto config = ctx.config;

    ImGui::Spacing();
    if (auto ch = ScopedCollapsingHeader("Keybinds"); ch.IsHeaderOpen())
    {
        ScopedIndent indent {};
        ImGui::Spacing();

        ImGui::Text("Primary keys are single keys. Backup: Ctrl+Shift+%s (BackupShortcutKey in laminr.ini).",
                    LaminrUI::MenuKeyLabel(Config::Instance()->BackupShortcutKey.value_or_default()).c_str());
        ImGui::Text("Escape to cancel, Backspace to unbind");
        ImGui::Spacing();

        static auto menu = Keybind("Menu", 10);
        static auto fpsOverlay = Keybind("FPS Overlay", 11);
        static auto fpsOverlayCycle = Keybind("FPS Overlay Cycle", 12);
        static auto dlssNrToggle = Keybind("Neural Rendering", 14);

        ImGui::Text("Backup menu: Ctrl+Shift+%s",Keybind::KeyNameFromVirtualKeyCode(config->BackupShortcutKey.value_or_default()).c_str());
        menu.Render(config->ShortcutKey);
        const int primaryMenuKey=config->ShortcutKey.value_or_default();
        const bool navigationKey=primaryMenuKey==VK_INSERT||primaryMenuKey==VK_DELETE||primaryMenuKey==VK_HOME||
            primaryMenuKey==VK_END||primaryMenuKey==VK_PRIOR||primaryMenuKey==VK_NEXT;
        ImGui::BeginDisabled(!navigationKey);
        bool physicalNavigation=config->MenuPhysicalNavigationKey.value_or_default();
        if(ImGui::Checkbox("Distinguish keypad navigation keys",&physicalNavigation))
            config->MenuPhysicalNavigationKey=physicalNavigation;
        ShowTooltip("Use the dedicated Insert/Home/End/Delete/Page Up/Page Down key. Legacy logical mode also accepts keypad navigation with Num Lock off. Physical mode needs WM/raw/DirectInput events; the backup menu shortcut remains available.");
        ImGui::EndDisabled();
        fpsOverlay.Render(config->FpsShortcutKey);
        fpsOverlayCycle.Render(config->FpsCycleShortcutKey);
        dlssNrToggle.Render(config->DlssNrToggleKey);
        LaminrUI::DrawHotSwapKeybind();
        ImGui::Separator(); LaminrUI::DrawSnapshotKeybind();
    }
}

void MenuCommon::RenderMainMenuGraphs(RenderMenuContext& ctx)
{
    auto& state = ctx.state;
    auto& currentFeature = ctx.currentFeature;
    auto& frameTime = ctx.frameTime;
    auto& frameRate = ctx.frameRate;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("plots", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextColumn();
        ImGui::Text("FrameTime");
        auto ft = StrFmt("%7.2f ms / %6.1f fps", frameTime, frameRate);
        ImGui::PlotLines(
            ft.c_str(), [](void* rb, int idx) -> float
            { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gFrameTimes, plotWidth);

        if (currentFeature != nullptr && !currentFeature->IsFrozen())
        {
            ImGui::TableNextColumn();
            ImGui::Text("Upscaler");

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !state.detailedGpuTimes.empty())
            {
                ImGui::BeginTooltip();

                ImGui::TextDisabled("Per shader breakdown:");
                if (ImGui::BeginTable("ShaderTimes", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    bool hasExtra = false;

                    for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
                    {
                        if (!includedInUpscalerTime)
                        {
                            hasExtra = true;
                            continue;
                        }

                        auto formattedTime = StrFmt("%7.2f ms", time);

                        ImGui::TableNextColumn();
                        ImGui::Text(name.c_str());

                        ImGui::TableNextColumn();
                        ImGui::Text(formattedTime.c_str());
                    }

                    std::optional<double> nrTime {};
                    nrTime = DlssNr::LastGpuTime();
                    if (hasExtra || nrTime.has_value())
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("Extra shaders:");
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("");
                        for (auto& [name, time, includedInUpscalerTime] : state.detailedGpuTimes)
                        {
                            if (includedInUpscalerTime)
                                continue;

                            auto formattedTime = StrFmt("%7.2f ms", time);

                            ImGui::TableNextColumn();
                            ImGui::Text(name.c_str());

                            ImGui::TableNextColumn();
                            ImGui::Text(formattedTime.c_str());
                        }

                        if (nrTime.has_value())
                        {
                            ImGui::TableNextColumn();
                            ImGui::Text("Neural Rendering (elapsed)");
                            ImGui::TableNextColumn();
                            ImGui::Text(StrFmt("%.2f ms", nrTime.value()).c_str());
                        }
                    }

                    ImGui::EndTable();
                }

                ImGui::EndTooltip();
            }

            auto ups = StrFmt("%7.2f ms", state.upscaleTimes.back());
            ImGui::PlotLines(
                ups.c_str(), [](void* rb, int idx) -> float
                { return static_cast<RingBuffer<float, plotWidth>*>(rb)->At(idx); }, &gUpscalerTimes, plotWidth);
        }

        ImGui::EndTable();
    }
}

void MenuCommon::RenderMainMenuWindow(RenderMenuContext& ctx)
{
    if(!ctx.newFrame)return;
    if(!LaminrUI::DrawStartup(_isVisible)){_isVisible=false;return;}
    if (!_isVisible) return;
    auto config=ctx.config;
    ctx.primaryGpu=std::make_unique<std::decay_t<decltype(IdentifyGpu::getPrimaryGpu())>>(RenderingGpu(ctx.state));
    const float uiScale=std::clamp(ctx.menuResScale,0.5f,2.0f);
    // Scope metrics and font size to this menu; overlays retain their own scale.
    const auto baseStyle=ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,baseStyle.WindowPadding*uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,baseStyle.FramePadding*uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,baseStyle.ItemSpacing*uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,baseStyle.ItemInnerSpacing*uiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize,baseStyle.ScrollbarSize*uiScale);
    ImGui::PushFontSize(std::round(fontSize*uiScale));
    const laminr::menu_geometry::View view {ctx.io.DisplaySize.x, ctx.io.DisplaySize.y};
    const auto area = laminr::menu_geometry::Available(view);
    const ImVec2 available(area.width, area.height);
    laminr::menu_geometry::Rect placement;
    if(mainMenuGeometry.Prepare(view,uiScale,placement)){
        ImGui::SetNextWindowSize(ImVec2(placement.width,placement.height),ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(placement.x,placement.y),ImGuiCond_Always);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(680.f*uiScale,available.x),std::min(420.f*uiScale,available.y)),ImVec2(std::min(1600.f*uiScale,available.x),available.y));
    bool visible=_isVisible;static bool saved=false;
    if(ImGui::Begin("laminr",&visible,ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)){
        ImGui::SetWindowFontScale(1.75f);ImGui::TextUnformatted("laminr  " LAMINR_VERSION_DISPLAY);ImGui::SetWindowFontScale(1.f);
        ImGui::SameLine(ImGui::GetWindowWidth()-100*uiScale);if(ImGui::SmallButton("Close"))visible=false;
        ImGui::Spacing();

        ImGui::Separator();
        static int page=0;const char* pages[]={"Performance","Neural rendering","Image effects","Settings & status"};
        for(int i=0;i<4;i++){if(i)ImGui::SameLine();ImGui::PushStyleColor(ImGuiCol_Button,page==i?ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive):ImVec4(.12f,.12f,.18f,1.f));if(ImGui::Button(pages[i],ImVec2((ImGui::GetWindowWidth()-70.f*uiScale)/4.f,40*uiScale)))page=i;ImGui::PopStyleColor();}
        ImGui::Separator();
        ImGui::BeginChild("laminr content",ImVec2(0,-56*uiScale));
        if(page==0){
            ImGui::TextWrapped("Get a sharper picture, a smoother frame rate, or a balance of both. Some options depend on the game.");
            ImGui::Spacing();ImGui::SeparatorText("Upscaling");
            laminr::BackendEvidence backendEvidence;
            backendEvidence.gameInput = ctx.currentFeature ? laminr::Evidence::Yes : laminr::Evidence::Unknown;
            if (ctx.currentFeature) {
                backendEvidence.initialized = ctx.currentFeature->IsInited() ? laminr::Evidence::Yes : laminr::Evidence::No;
                backendEvidence.active = ctx.currentFeature->IsInited() && !ctx.currentFeature->IsFrozen() ? laminr::Evidence::Yes : laminr::Evidence::No;
            }
            const char* backendStatus = laminr::BackendStageLabel(laminr::ResolveBackendStage(backendEvidence));
            static std::string lastBackendStatus;
            if(lastBackendStatus != backendStatus){
                LOG_INFO("Performance page backend: {}; API: {}", backendStatus, (int)ctx.state.api);
                lastBackendStatus = backendStatus;
            }
            if(ctx.currentFeature && ctx.currentFeature->IsInited() && !ctx.currentFeature->IsFrozen()){
                ImGui::Text("Connected: %s",ctx.currentFeature->ShortName().c_str());
                ImGui::TextWrapped("Choose the picture quality in your game's graphics settings. Higher quality keeps more detail; performance modes give the GPU less work.");
                if(ImGui::CollapsingHeader("Fine-tune DLSS"))RenderActiveUpscalerSettings(ctx);
            }else{
                ImGui::TextWrapped("Upscaling controls are waiting for DLSS. In MSFS graphics settings, select DLSS Super Resolution, apply the change, then enter a flight. TAA does not connect the DLSS upscaling controls.");
                ImGui::TextWrapped("If DLSS is already selected while flying, this may be a connection or initialization problem. Check laminrData/Performance.log in the game folder. Image effects work separately.");
            }
            ImGui::TextDisabled("Backend state: %s",backendStatus);
            const auto renderingGpu = RenderingGpu(ctx.state);
            ImGui::TextWrapped("Rendering GPU: %s", renderingGpu.name.empty() ? "Not identified from the active device" : renderingGpu.name.c_str());
            static std::string lastRenderingGpu;
            if (!renderingGpu.name.empty() && renderingGpu.name != lastRenderingGpu) {
                LOG_INFO("Rendering adapter: {}; LUID={:08X}:{:08X}; vendor={:04X}; device={:04X}",
                    renderingGpu.name, (UINT)renderingGpu.luid.HighPart, renderingGpu.luid.LowPart,
                    (UINT)renderingGpu.vendorId, renderingGpu.deviceId);
                lastRenderingGpu = renderingGpu.name;
            }

            ImGui::TextWrapped("An installed NVIDIA DLL is not proof that DLSS or Neural Rendering is active. Enter a flight with DLSS enabled. If controls remain unavailable, check laminrData/Performance.log in the game folder.");
            if (ImGui::CollapsingHeader("Rendering device capabilities")) {
                if (ctx.state.currentD3D12Device) {
                    const auto caps = laminr::QueryD3D12Capabilities(ctx.state.currentD3D12Device);
                    ImGui::Text("D3D12 native 16-bit shader operations: %s", laminr::EvidenceLabel(caps.native16));
                    ImGui::Text("D3D12 wave operations: %s", laminr::EvidenceLabel(caps.waves));
                    const char* blocked = laminr::NvidiaNeuralPreflight(caps);
                    ImGui::TextWrapped("NVIDIA neural backend: %s", blocked ? blocked : "Eligible for runtime initialization; not proof of model support");
                } else ImGui::TextWrapped("D3D12 capability evidence is unavailable for the current graphics API.");
            }
            ImGui::Spacing();ImGui::SeparatorText("Frame rate");
            ImGui::TextWrapped("A frame-rate limit can help keep motion steady and reduce GPU load.");
            if(ImGui::CollapsingHeader("Set a frame-rate limit"))RenderFramerateSettings(ctx);
            if(ImGui::CollapsingHeader("Sharpness and picture size"))RenderActiveImageSettings(ctx);
        }else if(page==1){
            const auto nrGpu = RenderingGpu(ctx.state);
            const bool nrUnsupportedGpu = nrGpu.vendorId == VendorId::AMD || nrGpu.vendorId == VendorId::Intel;
            if(nrUnsupportedGpu)ImGui::TextWrapped("Unavailable on this graphics card: this build's neural-rendering model requires an NVIDIA RTX 50-series GPU.");
            ImGui::TextWrapped("DLSS neural rendering changes the image using NVIDIA's model. Start with one pass; extra passes cost GPU time.");
            ImGui::Text("Model status: %s",DlssNr::IsRunning()?"Running":"Not running");
            if(auto reason=DlssNr::FailureReason();reason&&reason[0])ImGui::TextWrapped("%s",reason);
            if(DlssNr::MemoryPressureStopped() && ImGui::Button("Retry NR after memory pressure"))DlssNr::RetryAfterFailure();
            ImGui::BeginDisabled(nrUnsupportedGpu);
            bool enabled=config->DlssNrEnabled.value_or_default();
            if(ImGui::Checkbox("Use neural rendering",&enabled)) {
                config->DlssNrEnabled=enabled;
                LOG_INFO("Neural Rendering session request: {}; rendering GPU: {}",enabled,nrGpu.name);
            }

            ImGui::TextWrapped("Adds detail and changes lighting with NVIDIA's model. It can change faces and scenery, and may lower your frame rate.");
            bool applyModel=config->DlssNrApplyModel.value_or_default();
            if(ImGui::Checkbox("Apply the model",&applyModel))config->DlssNrApplyModel=applyModel;
            ImGui::SameLine();
            bool memoryOverride=DlssNr::MemoryPressureOverride();
            if(ImGui::Checkbox("Override memory pressure (this session)",&memoryOverride))DlssNr::SetMemoryPressureOverride(memoryOverride);
            ImGui::TextDisabled("May cause stutters or crashes if VRAM is low. Resets when the game closes.");
            const int passLimit=config->DlssNrUnlockPasses.value_or_default()?5:3;int passes=(int)std::clamp(config->DlssNrPasses.value_or_default(),1u,(uint32_t)passLimit);if(ImGui::SliderInt("Passes",&passes,1,passLimit))config->DlssNrPasses=(uint32_t)passes;
            ImGui::TextDisabled("Start with one. Extra passes can add detail, but take more time.");
            static float strength=1.f;if(!ImGui::IsAnyItemActive())strength=config->DlssNrIntensity.value_or_default();
            ImGui::SliderFloat("Strength",&strength,0.f,2.f,"%.2f");if(ImGui::IsItemDeactivatedAfterEdit())config->DlssNrIntensity=strength;
            int style=(int)config->DlssNrStyle.value_or_default();const char* styles[]={"Standard","Natural","Cinematic"};if(ImGui::Combo("Look",&style,styles,3))config->DlssNrStyle=(uint32_t)style;
            bool early=config->DlssNrRunBeforeSr.value_or_default();if(ImGui::Checkbox("Apply before upscaling",&early))config->DlssNrRunBeforeSr=early;
            ImGui::TextWrapped("Working on the smaller image can be faster. Applying it afterward may keep more fine detail.");
            DlssNr::RenderMenu(config,ctx.menuResScale);
            ImGui::EndDisabled();
        }else if(page==2){LaminrUI::DrawEffects();}
        else{
            ImGui::SeparatorText("UI size");
            const char* sizeNames[]={"Auto (match resolution)","100%","125%","150%","175%","200%"};
            const float sizeValues[]={0.f,1.f,1.25f,1.5f,1.75f,2.f};
            char sizeLabel[40]{};
            if(config->MenuScale.has_value())sprintf_s(sizeLabel,"%.0f%%",uiScale*100);
            else sprintf_s(sizeLabel,"Auto (%.0f%%)",uiScale*100);
            ImGui::SetNextItemWidth(240*uiScale);
            if(ImGui::BeginCombo("Menu size",sizeLabel)){
                for(int i=0;i<6;i++)if(ImGui::Selectable(sizeNames[i],i==0?!config->MenuScale.has_value():(config->MenuScale.has_value()&&std::abs(config->MenuScale.value()-sizeValues[i])<.01f))){
                    if(i==0)config->MenuScale.reset();else config->MenuScale=sizeValues[i];saved=false;
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("Enlarges menu text and controls. Auto uses 200%% at native 4K. Save settings to keep this choice for this game.");
            ImGui::SeparatorText("FPS overlay");
            bool showFps=config->ShowFps.value_or_default();
            if(LaminrUI::EffectSwitch("Show FPS",showFps)){config->ShowFps=!showFps;saved=false;}
            ImGui::SameLine();ImGui::TextUnformatted("Show FPS overlay");
            bool horizontal=config->FpsOverlayHorizontal.value_or_default();
            if(LaminrUI::EffectSwitch("Horizontal FPS",horizontal)){config->FpsOverlayHorizontal=!horizontal;saved=false;}
            ImGui::SameLine();ImGui::TextUnformatted("Horizontal layout");
            const char* positions[]={"Top left","Top right","Bottom left","Bottom right"};
            int position=std::clamp((int)config->FpsOverlayPosition.value_or_default(),0,3);
            ImGui::SetNextItemWidth(240*uiScale);
            if(ImGui::Combo("Position",&position,positions,4)){config->FpsOverlayPosition=(FpsOverlayPos)position;saved=false;}
            const char* details[]={"FPS only","Simple","Detailed","Detailed with graph","Full","Full with graph","Reflex timings"};
            int detail=std::clamp((int)config->FpsOverlayType.value_or_default(),0,6);
            ImGui::SetNextItemWidth(240*uiScale);
            if(ImGui::Combo("Detail level",&detail,details,7)){config->FpsOverlayType=(FpsOverlay)detail;saved=false;}
            char fpsSizeLabel[40]{};
            if(config->FpsScale.has_value())sprintf_s(fpsSizeLabel,"%.0f%%",config->FpsScale.value()*100);else strcpy_s(fpsSizeLabel,"Match menu size");
            ImGui::SetNextItemWidth(240*uiScale);
            if(ImGui::BeginCombo("FPS text size",fpsSizeLabel)){
                for(int i=0;i<6;i++)if(ImGui::Selectable(i==0?"Match menu size":sizeNames[i],i==0?!config->FpsScale.has_value():(config->FpsScale.has_value()&&std::abs(config->FpsScale.value()-sizeValues[i])<.01f))){
                    if(i==0)config->FpsScale.reset();else config->FpsScale=sizeValues[i];saved=false;
                }
                ImGui::EndCombo();
            }
            float opacity=config->FpsOverlayAlpha.value_or_default()*100;
            ImGui::SetNextItemWidth(240*uiScale);
            if(ImGui::SliderFloat("Background opacity",&opacity,0,100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)){config->FpsOverlayAlpha=opacity/100;saved=false;}
            bool themed=config->OverlaysUseTheme.value_or_default();
            if(LaminrUI::EffectSwitch("FPS theme",themed)){config->OverlaysUseTheme=!themed;saved=false;}
            ImGui::SameLine();ImGui::TextUnformatted("Use laminr colours");
            if(ImGui::Button("Reset FPS overlay")){
                config->ShowFps.reset();config->FpsOverlayPosition.reset();config->FpsOverlayType.reset();config->FpsScale.reset();config->FpsOverlayAlpha.reset();config->FpsOverlayHorizontal.reset();config->OverlaysUseTheme.reset();saved=false;
            }
            ImGui::TextWrapped("These controls change the overlay only. Save settings keeps your choices for this game.");
            ImGui::SeparatorText("Overlay colour");
            ImGui::TextWrapped("Choose an accent for your in-game menu. Purple is the default. This does not change your game's picture or your FX preset.");
            const char* paletteNames[]={"Purple","Blue","Teal","Green","Amber","Rose"};
            const ImVec4 palette[]={{.62f,.32f,1.f,1.f},{.28f,.56f,1.f,1.f},{.1f,.74f,.72f,1.f},{.3f,.76f,.4f,1.f},{1.f,.64f,.22f,1.f},{.94f,.36f,.62f,1.f}};
            auto setAccent=[&](const ImVec4& c){saved=false;config->MenuAccentColorR=c.x;config->MenuAccentColorG=c.y;config->MenuAccentColorB=c.z;ApplyThemeStyle();};
            for(int i=0;i<6;i++){
                if(i)ImGui::SameLine();
                ImGui::PushID(i);if(ImGui::ColorButton(paletteNames[i],palette[i],ImGuiColorEditFlags_NoTooltip,ImVec2(34*uiScale,28*uiScale)))setAccent(palette[i]);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",paletteNames[i]);ImGui::PopID();
            }
            float accent[]={config->MenuAccentColorR.value_or_default(),config->MenuAccentColorG.value_or_default(),config->MenuAccentColorB.value_or_default()};
            ImGui::SetNextItemWidth(280);
            if(ImGui::ColorEdit3("Custom colour",accent,ImGuiColorEditFlags_NoInputs))setAccent(ImVec4(accent[0],accent[1],accent[2],1.f));
            if(ImGui::Button("Reset to purple"))setAccent(palette[0]);
            ImGui::TextDisabled("Preview changes instantly. Use Save settings below to keep your choice for this game.");
            ImGui::Spacing();ImGui::Separator();
            const auto inputStatus=OptiInput::GetDebugState();
            ImGui::Text("Overlay initialized | Input: %s | Focus: %s",inputStatus.Initialized?"initialized":"not initialized",inputStatus.Focused?"game":"other window");
            ImGui::Text("Graphics API: %d | Upscaler: %s",(int)ctx.state.swapchainApi,ctx.currentFeature?ctx.currentFeature->ShortName().c_str():"not connected");
            ImGui::TextWrapped("For flicker or trails, compare one change at a time: image effects, then Neural Rendering, then the game's frame generation. Keep the setting that avoids the artifact. No universal artifact fix has been confirmed.");
            RenderMainMenuGraphs(ctx);RenderKeybindSettings(ctx);RenderLoggingSettings(ctx);
            if(ImGui::CollapsingHeader("Advanced compatibility")){RenderAdvancedSettings(ctx);RenderApiAndTextureSettings(ctx);}
        }
        ImGui::EndChild();ImGui::Separator();
        if(ImGui::Button("Save settings",ImVec2(160*uiScale,34*uiScale)))saved=config->SaveIni();
        ImGui::SameLine();if(saved)ImGui::TextDisabled("Settings saved. Neural rendering will start off.");else ImGui::TextDisabled("Menu: %s / Ctrl+Shift+%s",Keybind::KeyNameFromVirtualKeyCode(config->ShortcutKey.value_or_default()).c_str(),Keybind::KeyNameFromVirtualKeyCode(config->BackupShortcutKey.value_or_default()).c_str());
    }
    const auto position=ImGui::GetWindowPos();const auto size=ImGui::GetWindowSize();
    const auto* window=ImGui::GetCurrentWindow();const auto& imgui=*ImGui::GetCurrentContext();
    const bool geometryEditing=imgui.MovingWindow==window||imgui.ActiveIdWindow==window||imgui.NavWindowingTarget==window;
    mainMenuGeometry.Observe({position.x,position.y,size.x,size.y},geometryEditing,GetTickCount64());
    SaveMenuGeometryBoundary(false,geometryEditing);
    ImGui::End();ImGui::PopFontSize();ImGui::PopStyleVar(5);if(!visible)HideMenu();
}
void KeyUp(UINT vKey)
{
    if(vKey == Config::Instance()->ShortcutKey.value_or_default() && freshMenuPress){inputMenu=true;freshMenuPress=false;}
    inputFps = vKey == Config::Instance()->FpsShortcutKey.value_or_default();
    inputFpsCycle = vKey == Config::Instance()->FpsCycleShortcutKey.value_or_default();
}

// The lamp, and only the lamp.
//
// Red for dark, green for full light, with its reading beside it. No status sentence: the whole
// point of a light meter is that it is read at a glance while playing, and a paragraph in the corner
// of somebody's game is not that. Everything wordy lives in the menu, which is where someone has
// already decided to stop and read.
//
// Drawn only when its own setting is on. An overlay that appears because a scan happens to be
// running is an overlay nobody asked for.
void RenderExposureScanIndicator(float alpha)
{
    using DlssNr::ExposureScan::Verdict;

    if (!Config::Instance()->DlssNrScanMeter.value_or_default())
        return;

    if (DlssNr::ExposureScan::Where() == Verdict::Off)
        return;

    int which = 0;
    float low = 0.0f, high = 0.0f;
    const float now = DlssNr::ExposureScan::BestValue(&which, &low, &high);

    // Nothing found yet, or no range to place it in: a dim lamp, which says "watching, no reading"
    // without saying it in words.
    const bool reading = now > 0.0f && high > low;

    float lit = 0.0f;

    if (reading)
    {
        // An exposure falls as the scene brightens, so the value reads backwards unless the buffer
        // holds the reciprocal -- the same question the anchor asks, answered from the same setting,
        // because a lamp contradicting the picture would be worse than no lamp.
        lit = (high - now) / (high - low);

        if (Config::Instance()->DlssNrScanInverted.value_or_default())
            lit = 1.0f - lit;

        lit = lit < 0.0f ? 0.0f : (lit > 1.0f ? 1.0f : lit);
    }

    // Red to amber to green. A straight red-to-green fade passes through a muddy brown at the
    // midpoint, and the midpoint is where most of a session is spent.
    const ImVec4 dark(0.90f, 0.22f, 0.20f, 1.0f);
    const ImVec4 mid(0.95f, 0.75f, 0.20f, 1.0f);
    const ImVec4 bright(0.35f, 0.88f, 0.38f, 1.0f);
    const ImVec4 idle(0.45f, 0.45f, 0.45f, 1.0f);

    ImVec4 lamp = idle;

    if (reading)
    {
        const float t = lit < 0.5f ? lit * 2.0f : (lit - 0.5f) * 2.0f;
        const ImVec4& a = lit < 0.5f ? dark : mid;
        const ImVec4& b = lit < 0.5f ? mid : bright;
        lamp = ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 12.0f, vp->WorkPos.y + 12.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(alpha);

    if (ImGui::Begin("DlssNrExposureScan", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove))
    {
        const float r = ImGui::GetFontSize() * 0.38f;
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 centre(at.x + r, at.y + ImGui::GetTextLineHeight() * 0.5f);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddCircleFilled(centre, r, ImGui::GetColorU32(lamp), 20);
        draw->AddCircle(centre, r, ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.6f)), 20, 1.5f);

        ImGui::Dummy(ImVec2(r * 2.0f + 6.0f, ImGui::GetTextLineHeight()));
        ImGui::SameLine();

        if (reading)
            ImGui::TextColored(lamp, "%3.0f%%  %.5f", lit * 100.0f, now);
        else
            ImGui::TextColored(idle, "--");
    }

    ImGui::End();
}

bool MenuCommon::RenderMenu()
{
    if (!_isInited)
        return false;

    RenderMenuContext ctx { State::Instance(), Config::Instance(), ImGui::GetIO() };
    ctx.now = Util::MillisecondsNow();
    ctx.currentFeature = ctx.state.currentFeature;

    // 1) Collect timing and input state before any ImGui drawing.
    UpdateRenderTiming(ctx);
    UpdateMenuInputMode(ctx);
    HandleMenuShortcuts(ctx);

    // 2) Prepare one-shot notifications and start a new ImGui frame only when needed.
    UpdateVersionAndStartupNotifications(ctx);
    BeginMenuFrameIfNeeded(ctx);
    OptiInput::EndFrame(_isVisible);

    // 3) Draw lightweight overlay windows first, preserving the original order.
    ctx.menuResScale = MenuResolutionScale(ctx.io);
    // laminr starts with no splash or open menu.
    RenderNotifications(ctx);
    UpdateFrameTimeAverages(ctx);
    RenderPerformanceOverlay(ctx);
    RenderExposureScanIndicator(ctx.config->FpsOverlayAlpha.value_or_default());

    // 4) Draw the full settings menu last so popups and child windows keep their existing behavior.
    RenderMainMenuWindow(ctx);

    if (ctx.newFrame)
        ImGui::EndFrame();

    return ctx.newFrame;
}

void MenuCommon::Init(HWND InHwnd, bool isUWP)
{
    SaveMenuGeometryBoundary(true);
    // Reset shutdown flag in case of re-init
    State::Instance().isShuttingDown = false;

    HWND oldHandle = nullptr;

    if (_handle != nullptr)
    {
        oldHandle = _handle;
        LOG_DEBUG("Old Handle: {:X}, ImGui Handle: {:X}", (size_t) oldHandle,
                  (size_t) ImGui::GetMainViewport()->PlatformHandleRaw);
    }

    _handle = InHwnd;
    _isVisible = false;inputMenu=false;freshMenuPress=false;backupMenuArmed=false;
    _isUWP = isUWP;

    LOG_DEBUG("Handle: {0:X}", (size_t) _handle);

    // In case d3d12 wasn't yet used up to this point, try to update GPU info late here
    IdentifyGpu::updateD3d12Capabilities();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    mainMenuGeometryContext=MenuGeometryContext(InHwnd,isUWP);
    mainMenuGeometry.Load(Config::Instance()->LoadMenuGeometry(mainMenuGeometryContext));
    ImGui::StyleColorsDark();

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    hasGamepad = (io.BackendFlags | ImGuiBackendFlags_HasGamepad) > 0;
    io.BackendFlags &= 30;
    io.ConfigFlags = ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoKeyboard;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
    io.WantSetMousePos = _isVisible;

    io.IniFilename = io.LogFilename = nullptr;

    bool initResult = false;

    if (io.BackendPlatformUserData == nullptr)
    {
        if (!isUWP)
        {
            initResult = ImGui_ImplWin32_Init(InHwnd);
            LOG_DEBUG("ImGui_ImplWin32_Init result: {0}", initResult);
        }
        else
        {
            initResult = ImGui_ImplUwp_Init(InHwnd);
            ImGui_BindUwpKeyUp(KeyUp);
            LOG_DEBUG("ImGui_ImplUwp_Init result: {0}", initResult);
        }
    }

    if (io.Fonts->Fonts.empty() && Config::Instance()->UseHQFont.value_or_default())
    {
        ImFontAtlas* atlas = io.Fonts;
        atlas->Clear();

        // This automatically becomes the next default font
        ImFontConfig fontConfig;

        if (Config::Instance()->FontSize.has_value())
            fontSize = Config::Instance()->FontSize.value();

        if (Config::Instance()->TTFFontPath.has_value())
        {
            io.FontDefault =
                atlas->AddFontFromFileTTF(wstring_to_string(Config::Instance()->TTFFontPath.value()).c_str(), fontSize,
                                          &fontConfig, io.Fonts->GetGlyphRangesDefault());
        }
        else
        {
            wchar_t windowsFolder[MAX_PATH]{};GetWindowsDirectoryW(windowsFolder,MAX_PATH);
            auto fontPath=std::filesystem::path(windowsFolder)/L"Fonts"/L"segoeui.ttf";
            fontSize=18.f;
            std::error_code ec;
            if(std::filesystem::is_regular_file(fontPath,ec))io.FontDefault=atlas->AddFontFromFileTTF(wstring_to_string(fontPath.wstring()).c_str(),fontSize,&fontConfig);
            if(!io.FontDefault)io.FontDefault=atlas->AddFontFromMemoryCompressedBase85TTF(hack_compressed_compressed_data_base85,fontSize,&fontConfig);
        }
    }

    if (!Config::Instance()->OverlayMenu.value_or_default())
    {
        _hdrTonemapApplied = false;
    }

    DWORD hwndPid = 0;
    DWORD hwndTid = GetWindowThreadProcessId(_handle, &hwndPid);

    LOG_DEBUG("HWND: {:X}, IsWindow: {}, HWND PID: {}, Current PID: {}, HWND TID: {}, Current TID: {}",
              (ULONG64) _handle, IsWindow(_handle), hwndPid, GetCurrentProcessId(), hwndTid, GetCurrentThreadId());

    OptiInput::Initialize(_handle, isUWP);

    ApplyThemeStyle();
    auto& style=ImGui::GetStyle();style.WindowPadding=ImVec2(18,16);style.FramePadding=ImVec2(10,7);style.ItemSpacing=ImVec2(10,9);style.WindowRounding=10;style.FrameRounding=5;style.GrabRounding=5;
    style.Colors[ImGuiCol_WindowBg]=ImVec4(.055f,.055f,.085f,.98f);
    _isInited = true;
    LOG_INFO("laminr overlay initialized on window {:X}; menu key {}, backup Ctrl+Shift+{}",(size_t)_handle,Config::Instance()->ShortcutKey.value_or_default(),Config::Instance()->BackupShortcutKey.value_or_default());
}

void MenuCommon::Shutdown()
{
    if (!MenuCommon::_isInited)
        return;
    SaveMenuGeometryBoundary(true);
    mainMenuGeometry.Load({});
    mainMenuGeometryContext.clear();

    // if (_oWndProc != nullptr)
    //{
    //     auto handle = (HWND) ImGui::GetMainViewport()->PlatformHandleRaw;
    //     SetLastError(0);
    //     auto restoreResult = SetWindowLongPtr(handle, GWLP_WNDPROC, (LONG_PTR) _oWndProc);
    //     auto error = GetLastError();

    //    if (restoreResult == 0 && error != 0)
    //    {
    //        LOG_ERROR("Failed to restore old WndProc. Error: {:X}", error);
    //    }

    //    _oWndProc = nullptr;
    //}

    if (!_isUWP)
        ImGui_ImplWin32_Shutdown();
    else
        ImGui_ImplUwp_Shutdown();

    ImGui::DestroyContext();

    _handle = nullptr;
    _isInited = false;
    _isVisible = false;
}

void MenuCommon::HideMenu()
{
    if (!_isVisible)
        return;
    SaveMenuGeometryBoundary(true);

    _isVisible = false;

    ImGuiIO& io = ImGui::GetIO();
    (void) io;

    io.MouseDrawCursor = _isVisible;
    io.WantCaptureKeyboard = _isVisible;
    io.WantCaptureMouse = _isVisible;
}
