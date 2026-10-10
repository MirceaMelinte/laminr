#include "pch.h"

#include "Config.h"

#include "Util.h"

#include <hooks/Streamline_Hooks.h>
#include <misc/IdentifyGpu.h>

#include <SimpleIni.h>
#include "../../shared/AtomicConfigWrite.h"
#include "../../shared/MenuGeometryIni.h"
#include <mutex>

static CSimpleIniA ini;
static std::recursive_mutex configIniMutex;

static bool SaveOwnedIni(CSimpleIniA& value, const std::filesystem::path& path)
{
    std::string text;
    return value.Save(text) >= 0 && laminr::AtomicConfigWrite(path, text);
}

static inline int64_t GetTicks()
{
    LARGE_INTEGER ticks;

    if (!QueryPerformanceCounter(&ticks))
        return 0;

    return ticks.QuadPart;
}

static inline bool isInteger(const std::string& str, int& value)
{
    std::istringstream iss(str);
    return (iss >> value) && iss.eof();
}

static inline bool isUInt(const std::string& str, uint32_t& value)
{
    std::istringstream iss(str);
    return (iss >> value) && iss.eof();
}

static inline bool isFloat(const std::string& str, float& value)
{
    std::istringstream iss(str);
    return (iss >> value) && iss.eof();
}

Config::Config()
{
    absoluteFileName = Util::DllPath().parent_path() / fileName;
    Reload(absoluteFileName);
}

bool Config::Reload(std::filesystem::path iniPath)
{
    const std::lock_guard lock(configIniMutex);
    auto pathWStr = iniPath.wstring();

    LOG_INFO("Trying to load ini from: {0}", wstring_to_string(pathWStr));
    if (ini.LoadFile(iniPath.c_str()) == SI_OK)
    {
        State::Instance().nvngxIniDetected = exists(iniPath.parent_path() / "nvngx.ini");
        _log.clear();

        // Upscalers
        {
            // transform converts only when optional has a value
            Dx12Upscaler.set_from_config(readString("Upscalers", "Dx12Upscaler", true).transform(CodeToUpscaler));
        }

        // Framerate
        {
            FramerateLimit.set_from_config(readFloat("Framerate", "FramerateLimit"));
        }

        // Camera fallbacks, kept in the [FSR] section for compatibility with existing configs
        {
            FsrVerticalFov.set_from_config(readFloat("FSR", "VerticalFov"));
            FsrCameraNear.set_from_config(readFloat("FSR", "CameraNear"));
            FsrCameraFar.set_from_config(readFloat("FSR", "CameraFar"));
        }

        // DLSS
        {
            // Don't enable again if set false because of no nvngx found
            DLSSEnabled.set_from_config(readBool("DLSS", "Enabled"));

            // --- DLSS 5 Neural Rendering (OptiScaler/dlssnr) ---
            DlssNrEnabled.set_from_config(false); // Explicit session opt-in, even for older INIs.
            DlssNrRunBeforeSr.set_from_config(readBool("DlssNr", "RunBeforeSR"));
            DlssNrFinishedPicture.set_from_config(readBool("DlssNr", "FinishedPicture"));
            DlssNrDeferredDlss.set_from_config(readBool("DlssNr", "DeferredDLSS"));
            DlssNrResidualAcrossRr.set_from_config(readBool("DlssNr", "ResidualAcrossRR"));
            DlssNrResidualAcrossRrBlend.set_from_config(readFloat("DlssNr", "ResidualAcrossRRBlend"));
            DlssNrResidualFg.set_from_config(readBool("DlssNr", "ResidualFG"));
            DlssNrPrecision.set_from_config(readUInt("DlssNr", "Precision"));
            if (DlssNrPrecision.value_or_default() != 4) DlssNrPrecision = 0u;
            DlssNrResidualFgApproxCamera.set_from_config(readBool("DlssNr", "ResidualFGApproxCamera"));
            DlssNrToggleKey.set_from_config(readInt("DlssNr", "ToggleKey"));
            DlssNrTransferStrength.set_from_config(readFloat("DlssNr", "TransferStrength"));
            DlssNrColourStrength.set_from_config(readFloat("DlssNr", "ColourStrength"));
            DlssNrMaxRatio.set_from_config(readFloat("DlssNr", "MaxRatio"));
            DlssNrTransfer.set_from_config(readUInt("DlssNr", "Transfer"));

            DlssNrWhitePointFromExposure.set_from_config(readBool("DlssNr", "WhitePointFromExposure"));
            DlssNrDebugView.set_from_config(readUInt("DlssNr", "DebugView"));
            DlssNrCompare.set_from_config(readUInt("DlssNr", "Compare"));
            DlssNrCompareSplit.set_from_config(readFloat("DlssNr", "CompareSplit"));
            DlssNrCompareZoom.set_from_config(readFloat("DlssNr", "CompareZoom"));
            DlssNrCompareSwap.set_from_config(readBool("DlssNr", "CompareSwap"));
            DlssNrCompareTags.set_from_config(readBool("DlssNr", "CompareTags"));
            DlssNrTagScale.set_from_config(readFloat("DlssNr", "TagScale"));
            DlssNrWorkingScale.set_from_config(readFloat("DlssNr", "WorkingScale"));

            if (auto v = readEnum<Scaler>("DlssNr", "ScalingDownscaler"))
                DlssNrScalingDownscaler.set_from_config(*v);
            else
                DlssNrScalingDownscaler.reset();
            DlssNrProxyProbe.set_from_config(readBool("DlssNr", "ProxyProbe"));
            DlssNrUseProxy.set_from_config(readBool("DlssNr", "UseProxy"));
            DlssNrScanExposure.set_from_config(readBool("DlssNr", "ScanExposure"));
            DlssNrWhitePointSource.set_from_config(readUInt("DlssNr", "WhitePointSource"));

            // Migrate the retired flag only when the new key was genuinely absent, which is why this
            // has to run AFTER the read above -- set_from_config assigns only into an empty optional,
            // so an ini that carries both keys keeps its explicit WhitePointSource.
            if (!DlssNrWhitePointSource.has_value() && DlssNrWhitePointFromExposure.has_value())
                DlssNrWhitePointSource = DlssNrWhitePointFromExposure.value() ? 1u : 0u;
            DlssNrScanMeter.set_from_config(readBool("DlssNr", "ScanMeter"));
            DlssNrScanTrim.set_from_config(readFloat("DlssNr", "ScanTrim"));
            DlssNrPasses.set_from_config(readUInt("DlssNr", "Passes"));
            DlssNrScanAnchorValue.set_from_config(readFloat("DlssNr", "ScanAnchorValue"));
            DlssNrScanAnchorWhitePoint.set_from_config(readFloat("DlssNr", "ScanAnchorWhitePoint"));
            DlssNrScanAnchors.set_from_config(readString("DlssNr", "ScanAnchors"));
            DlssNrScanInverted.set_from_config(readBool("DlssNr", "ScanInverted"));
            DlssNrWhitePointTrim.set_from_config(readFloat("DlssNr", "WhitePointTrim"));
            DlssNrAutoCapture.set_from_config(readBool("DlssNr", "AutoCapture"));
            DlssNrWhitePointScale.set_from_config(readFloat("DlssNr", "WhitePointScale"));
            DlssNrPreset.set_from_config(readUInt("DlssNr", "Preset"));
            DlssNrIntensity.set_from_config(readFloat("DlssNr", "Intensity"));
            DlssNrStyle.set_from_config(readUInt("DlssNr", "Style"));
            DlssNrPass2Preset.set_from_config(readUInt("DlssNr", "Pass2Preset"));
            DlssNrPass2Style.set_from_config(readUInt("DlssNr", "Pass2Style"));
            DlssNrPass3Preset.set_from_config(readUInt("DlssNr", "Pass3Preset"));
            DlssNrPass3Style.set_from_config(readUInt("DlssNr", "Pass3Style"));
            DlssNrLocalStructure.set_from_config(readFloat("DlssNr", "LocalStructure"));
            DlssNrLocalTone.set_from_config(readFloat("DlssNr", "LocalTone"));
            DlssNrSkinStructure.set_from_config(readFloat("DlssNr", "SkinStructure"));
            DlssNrAutoMask.set_from_config(readBool("DlssNr", "AutoMask"));
            DlssNrSkinProtection.set_from_config(readBool("DlssNr", "SkinProtection"));
            DlssNrSkinToneEnabled.set_from_config(readBool("DlssNr", "SkinToneEnabled"));
            DlssNrSkinDetail.set_from_config(readFloat("DlssNr", "SkinDetail"));
            DlssNrSkinColour.set_from_config(readFloat("DlssNr", "SkinColour"));
            DlssNrEnvironmentDetail.set_from_config(readFloat("DlssNr", "EnvironmentDetail"));
            DlssNrEnvironmentColour.set_from_config(readFloat("DlssNr", "EnvironmentColour"));
            DlssNrShowSkinMask.set_from_config(readBool("DlssNr", "ShowSkinMask"));
            DlssNrPass2Intensity.set_from_config(readFloat("DlssNr", "Pass2Intensity"));
            DlssNrPass2LocalStructure.set_from_config(readFloat("DlssNr", "Pass2LocalStructure"));
            DlssNrPass2LocalTone.set_from_config(readFloat("DlssNr", "Pass2LocalTone"));
            DlssNrPass2SkinStructure.set_from_config(readFloat("DlssNr", "Pass2SkinStructure"));
            DlssNrPass2AutoMask.set_from_config(readBool("DlssNr", "Pass2AutoMask"));
            DlssNrPass3Intensity.set_from_config(readFloat("DlssNr", "Pass3Intensity"));
            DlssNrPass3LocalStructure.set_from_config(readFloat("DlssNr", "Pass3LocalStructure"));
            DlssNrPass3LocalTone.set_from_config(readFloat("DlssNr", "Pass3LocalTone"));
            DlssNrPass3SkinStructure.set_from_config(readFloat("DlssNr", "Pass3SkinStructure"));
            DlssNrPass3AutoMask.set_from_config(readBool("DlssNr", "Pass3AutoMask"));
            DlssNrUnlockPasses.set_from_config(readBool("DlssNr", "UnlockPasses"));
            for (unsigned int i = 0; i < 27; ++i)
            {
                auto& pass = DlssNrExtraPasses[i];
                pass.style.set_from_config(readUInt("DlssNr", std::format("Pass{}Style", i + 4).c_str()));
                pass.intensity.set_from_config(readFloat("DlssNr", std::format("Pass{}Intensity", i + 4).c_str()));
                pass.structure.set_from_config(readFloat("DlssNr", std::format("Pass{}LocalStructure", i + 4).c_str()));
                pass.tone.set_from_config(readFloat("DlssNr", std::format("Pass{}LocalTone", i + 4).c_str()));
                pass.skin.set_from_config(readFloat("DlssNr", std::format("Pass{}SkinStructure", i + 4).c_str()));
                pass.autoMask.set_from_config(readBool("DlssNr", std::format("Pass{}AutoMask", i + 4).c_str()));
            }
            DlssNrReversibleMode.set_from_config(readUInt("DlssNr", "ReversibleMode"));
            DlssNrApplyModel.set_from_config(readBool("DlssNr", "ApplyModel"));
            DlssNrHoldFrame.set_from_config(readBool("DlssNr", "HoldFrame"));
            UseGenericAppIdWithDlss.set_from_config(readBool("DLSS", "UseGenericAppIdWithDlss"));

            RenderPresetOverride.set_from_config(readBool("DLSS", "RenderPresetOverride"));

            constexpr size_t presetCount = 17;

            if (auto setting = readInt("DLSS", "RenderPresetForAll");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                RenderPresetForAll.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetDLAA");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                RenderPresetDLAA.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetUltraQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                RenderPresetUltraQuality.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                RenderPresetQuality.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetBalanced");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                RenderPresetBalanced.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                RenderPresetPerformance.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetUltraPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                RenderPresetUltraPerformance.set_from_config(setting);
        }
        // DLSSD
        {
            // Don't enable again if set false because of no nvngx found
            DLSSDRenderPresetOverride.set_from_config(readBool("DLSSD", "RenderPresetOverride"));

            constexpr size_t presetCount = 7;

            if (auto setting = readInt("DLSSD", "RenderPresetForAll");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                DLSSDRenderPresetForAll.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetDLAA");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                DLSSDRenderPresetDLAA.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetUltraQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                DLSSDRenderPresetUltraQuality.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                DLSSDRenderPresetQuality.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetBalanced");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                DLSSDRenderPresetBalanced.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                DLSSDRenderPresetPerformance.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetUltraPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == NV_PRESET_LATEST))
                DLSSDRenderPresetUltraPerformance.set_from_config(setting);
        }

        // Logging
        {
            LogToFile.set_from_config(readBool("Log", "LogToFile"));
            LogLevel.set_from_config(readInt("Log", "LogLevel"));
            LogToConsole.set_from_config(readBool("Log", "LogToConsole"));
            LogToDebug.set_from_config(readBool("Log", "LogToDebug"));
            LogToNGX.set_from_config(readBool("Log", "LogToNGX"));
            OpenConsole.set_from_config(readBool("Log", "OpenConsole"));
            DebugWait.set_from_config(readBool("Log", "DebugWait"));
            LogSingleFile.set_from_config(readBool("Log", "SingleFile"));
            LogAsync.set_from_config(readBool("Log", "LogAsync"));
            LogAsyncThreads.set_from_config(readInt("Log", "LogAsyncThreads"));

            {
                auto setting = readString("Log", "LogFileName", false);

                if (setting.has_value() && setting.value().empty())
                    setting = std::nullopt;

                auto path = std::filesystem::path(setting.value_or(wstring_to_string(LogFileName.value_or_default())));
                auto filenameStem = path.stem();

                auto filename =
                    std::filesystem::path(LogSingleFile.value_or_default()
                                              ? filenameStem.wstring() + L".log"
                                              : filenameStem.wstring() + L"_" + std::to_wstring(GetTicks()) + L".log");

                if (setting.has_value())
                {
                    if (path.has_root_path())
                        LogFileName.set_from_config((path.parent_path() / filename).wstring());
                    else
                        LogFileName.set_from_config((Util::DllPath().parent_path() / path.parent_path() / filename).wstring());
                }
                else
                {
                    if (path.has_root_path())
                        LogFileName.set_volatile_value((path.parent_path() / filename).wstring());
                    else
                        LogFileName.set_volatile_value((Util::DllPath().parent_path() / path.parent_path() / filename).wstring());
                }
            }
        }

        // Sharpness
        {
            SharpnessShader.set_from_config(readString("Sharpness", "Shader", true).transform(CodeToSharpnessShader));
            OverrideSharpness.set_from_config(readBool("Sharpness", "OverrideSharpness"));

            if (auto setting = readFloat("Sharpness", "Sharpness"); setting.has_value())
                Sharpness.set_from_config(std::clamp(setting.value(), 0.0f, 1.3f));
        }

        // Menu
        {
            if (auto setting = readFloat("Menu", "Scale"); setting.has_value())
                MenuScale.set_from_config(std::clamp(setting.value(), 0.5f, 2.0f));

            // Don't enable again if set false because of Linux issue
            OverlayMenu.set_from_config(readBool("Menu", "OverlayMenu"));
            MenuPhysicalNavigationKey.set_from_config(readBool("Menu", "PhysicalNavigationKey"));
            ShortcutKey.set_from_config(readInt("Menu", "ShortcutKey"));
            BackupShortcutKey.set_from_config(readInt("Menu", "BackupShortcutKey"));
            PresetHotSwapKey.set_from_config(readInt("Menu", "PresetHotSwapKey"));
            SnapshotKey.set_from_config(readInt("Menu", "SnapshotKey"));

            ExtendedLimits.set_from_config(readBool("Menu", "ExtendedLimits"));
            ShowFps.set_from_config(readBool("Menu", "ShowFps"));
            UseHQFont.set_from_config(readBool("Menu", "UseHQFont"));

            if (auto setting = readUInt("Menu", "FpsOverlayPos"); setting.has_value())
            {
                FpsOverlayPosition.set_from_config(
                    (FpsOverlayPos) std::clamp(setting.value(), 0U, FpsOverlayPos_COUNT - 1));
            }

            if (auto setting = readUInt("Menu", "FpsOverlayType"); setting.has_value())
            {
                FpsOverlayType.set_from_config((FpsOverlay) std::clamp(setting.value(), 0U, FpsOverlay_COUNT - 1));
            }

            FpsShortcutKey.set_from_config(readInt("Menu", "FpsShortcutKey"));
            FpsCycleShortcutKey.set_from_config(readInt("Menu", "FpsCycleShortcutKey"));
            FpsOverlayHorizontal.set_from_config(readBool("Menu", "FpsOverlayHorizontal"));

            if (auto setting = readFloat("Menu", "FpsOverlayAlpha"); setting.has_value())
                FpsOverlayAlpha.set_from_config(std::clamp(setting.value(), 0.0f, 1.0f));

            if (auto setting = readFloat("Menu", "FpsScale"); setting.has_value())
                FpsScale.set_from_config(std::clamp(setting.value(), 0.5f, 2.0f));

            FontSize.set_from_config(readFloat("Menu", "FontSize"));
            TTFFontPath.set_from_config(readWString("Menu", "TTFFontPath"));

            LightTheme.set_from_config(readBool("Menu", "LightTheme"));
            OverlaysUseTheme.set_from_config(readBool("Menu", "OverlaysUseTheme"));
            MenuAccentColorR.set_from_config(readFloat("Menu", "AccentColorR"));
            MenuAccentColorG.set_from_config(readFloat("Menu", "AccentColorG"));
            MenuAccentColorB.set_from_config(readFloat("Menu", "AccentColorB"));
            MenuBGColorR.set_from_config(readFloat("Menu", "BGColorR"));
            MenuBGColorG.set_from_config(readFloat("Menu", "BGColorG"));
            MenuBGColorB.set_from_config(readFloat("Menu", "BGColorB"));
            MenuBGColorA.set_from_config(readFloat("Menu", "BGColorA"));
        }

        // Hooks
        {
            HookOriginalNvngxOnly.set_from_config(readBool("Hooks", "HookOriginalNvngxOnly"));
            EarlyHooking.set_from_config(readBool("Hooks", "EarlyHooking"));
            UseNtdllHooks.set_from_config(readBool("Hooks", "UseNtdllHooks"));
        }

        // RCAS
        {
            RcasEnabled.set_from_config(readBool("CAS", "Enabled"));

            MotionSharpnessEnabled.set_from_config(readBool("CAS", "MotionSharpnessEnabled"));

            if (auto setting = readFloat("CAS", "MotionSharpness"); setting.has_value())
                MotionSharpness.set_from_config(std::clamp(setting.value(), -1.3f, 1.3f));

            if (auto setting = readFloat("CAS", "MotionThreshold"); setting.has_value())
                MotionThreshold.set_from_config(std::clamp(setting.value(), 0.0f, 100.0f));

            if (auto setting = readFloat("CAS", "MotionScaleLimit"); setting.has_value())
                MotionScaleLimit.set_from_config(std::clamp(setting.value(), 0.01f, 100.0f));

            ContrastEnabled.set_from_config(readBool("CAS", "ContrastEnabled"));
            if (auto setting = readFloat("CAS", "Contrast"); setting.has_value())
                Contrast.set_from_config(std::clamp(setting.value(), -2.0f, 2.0f));

            DADepthScale.set_from_config(readFloat("CAS", "DADepthScale"));
            DADepthBias.set_from_config(readFloat("CAS", "DADepthBias"));
            DAClampOutput.set_from_config(readBool("CAS", "DAClampOutput"));

            MotionSharpnessDebug.set_from_config(readBool("CAS", "SharpenerDebug"));
        }

        // Magnifier
        {
            MagnifierEnabled.set_from_config(readBool("Magnifier", "Enabled"));

            if (auto setting = readFloat("Magnifier", "Size"); setting.has_value())
                MagnifierSize.set_from_config(std::clamp(setting.value(), 0.0f, 100.0f));

            if (auto setting = readInt("Magnifier", "ZoomFactor"); setting.has_value())
                MagnifierZoomFactor.set_from_config(std::clamp(setting.value(), 2, 20));

            if (auto setting = readFloat("Magnifier", "BorderSize"); setting.has_value())
                MagnifierBorderSize.set_from_config(std::clamp(setting.value(), 0.0f, 2.0f));

            if (auto setting = readFloat("Magnifier", "CursorOffsetX"); setting.has_value())
                MagnifierCursorOffsetX.set_from_config(std::clamp(setting.value(), -1000.f, 1000.f));
            if (auto setting = readFloat("Magnifier", "CursorOffsetY"); setting.has_value())
                MagnifierCursorOffsetY.set_from_config(std::clamp(setting.value(), -1000.f, 1000.f));

            if (auto setting = readFloat("Magnifier", "StaticPosX"); setting.has_value())
                MagnifierStaticPosX.set_from_config(std::clamp(setting.value(), 0.0f, 100.0f));
            if (auto setting = readFloat("Magnifier", "StaticPosY"); setting.has_value())
                MagnifierStaticPosY.set_from_config(std::clamp(setting.value(), 0.0f, 100.0f));
        }

        // Output Scaling
        {
            OutputScalingEnabled.set_from_config(readBool("OutputScaling", "Enabled"));

            if (auto v = readEnum<Scaler>("OutputScaling", "Downscaler"))
                OutputScalingDownscaler.set_from_config(*v);
            else
                OutputScalingDownscaler.reset();

            if (auto setting = readFloat("OutputScaling", "Multiplier"); setting.has_value())
                OutputScalingMultiplier.set_from_config(std::clamp(setting.value(), 0.5f, 3.0f));
        }

        // Init Flags
        {
            AutoExposure.set_from_config(readBool("InitFlags", "AutoExposure"));
            HDR.set_from_config(readBool("InitFlags", "HDR"));
            DepthInverted.set_from_config(readBool("InitFlags", "DepthInverted"));
            JitterCancellation.set_from_config(readBool("InitFlags", "JitterCancellation"));
            DisplayResolution.set_from_config(readBool("InitFlags", "DisplayResolution"));
        }

        // DRS
        {
            DrsMinOverrideEnabled.set_from_config(readBool("DRS", "DrsMinOverrideEnabled"));
            DrsMaxOverrideEnabled.set_from_config(readBool("DRS", "DrsMaxOverrideEnabled"));
        }

        // Upscale Ratio Override
        {
            UpscaleRatioOverrideEnabled.set_from_config(readBool("UpscaleRatio", "UpscaleRatioOverrideEnabled"));
            UpscaleRatioOverrideValue.set_from_config(readFloat("UpscaleRatio", "UpscaleRatioOverrideValue"));
        }

        // Quality Overrides
        {
            QualityRatioOverrideEnabled.set_from_config(readBool("QualityOverrides", "QualityRatioOverrideEnabled"));
            QualityRatio_DLAA.set_from_config(readFloat("QualityOverrides", "QualityRatioDLAA"));
            QualityRatio_UltraQuality.set_from_config(readFloat("QualityOverrides", "QualityRatioUltraQuality"));
            QualityRatio_Quality.set_from_config(readFloat("QualityOverrides", "QualityRatioQuality"));
            QualityRatio_Balanced.set_from_config(readFloat("QualityOverrides", "QualityRatioBalanced"));
            QualityRatio_Performance.set_from_config(readFloat("QualityOverrides", "QualityRatioPerformance"));
            QualityRatio_UltraPerformance.set_from_config(
                readFloat("QualityOverrides", "QualityRatioUltraPerformance"));
        }

        // Anisotropy
        {
            if (auto setting = readInt("Anisotropy", "AnisotropyOverride");
                setting.has_value() && setting.value() <= 16 && setting.value() >= 1)
                AnisotropyOverride.set_from_config(setting);

            if (AnisotropyOverride.has_value() && (AnisotropyOverride.value() > 16 || AnisotropyOverride.value() < 1))
                AnisotropyOverride.reset();

            AnisotropySkipPointFilter.set_from_config(readBool("Anisotropy", "SkipPointFilter"));
            AnisotropyModifyComp.set_from_config(readBool("Anisotropy", "AFModifyComparison"));
            AnisotropyModifyMinMax.set_from_config(readBool("Anisotropy", "AFModifyMinMax"));
        }

        // Mipmap
        {
            if (auto setting = readFloat("Mipmap", "MipmapBiasOverride");
                setting.has_value() && setting.value() <= 15.0 && setting.value() >= -15.0)
                MipmapBiasOverride.set_from_config(setting);

            // Unsure if that's needed but it resets invalid MipmapBiasOverride on config reload
            // Unexpected place for it but could be playing a role
            if (MipmapBiasOverride.has_value() &&
                (MipmapBiasOverride.value() > 15.0 || MipmapBiasOverride.value() < -15.0))
                MipmapBiasOverride.reset();

            MipmapBiasFixedOverride.set_from_config(readBool("Mipmap", "MipmapBiasFixedOverride"));
            MipmapBiasScaleOverride.set_from_config(readBool("Mipmap", "MipmapBiasScaleOverride"));
            MipmapBiasOverrideAll.set_from_config(readBool("Mipmap", "MipmapBiasOverrideAll"));
        }

        // Process Filter
        {
            ProcessExclusionList.set_from_config(readWString("ProcessFilter", "ProcessExclusionList", true));
            TargetProcess.set_from_config(readWString("ProcessFilter", "TargetProcessName", true));
        }

        // Hotfixes
        {
            CheckForUpdate.set_from_config(readBool("Hotfix", "CheckForUpdate"));
            DisableOverlays.set_from_config(readBool("Hotfix", "DisableOverlays"));

            RoundInternalResolution.set_from_config(readInt("Hotfix", "RoundInternalResolution"));

            RestoreComputeSignature.set_from_config(readBool("Hotfix", "RestoreComputeSignature"));
            RestoreGraphicSignature.set_from_config(readBool("Hotfix", "RestoreGraphicSignature"));
            ExtendedStateRestore.set_from_config(readBool("Hotfix", "ExtendedStateRestore"));
            PreferDedicatedGpu.set_from_config(readBool("Hotfix", "PreferDedicatedGpu"));
            PreferFirstDedicatedGpu.set_from_config(readBool("Hotfix", "PreferFirstDedicatedGpu"));
            SkipFirstFrames.set_from_config(readInt("Hotfix", "SkipFirstFrames"));
            UsePrecompiledShaders.set_from_config(readBool("Hotfix", "UsePrecompiledShaders"));
            ColorResourceBarrier.set_from_config(readInt("Hotfix", "ColorResourceBarrier"));
            MVResourceBarrier.set_from_config(readInt("Hotfix", "MotionVectorResourceBarrier"));
            DepthResourceBarrier.set_from_config(readInt("Hotfix", "DepthResourceBarrier"));
            MaskResourceBarrier.set_from_config(readInt("Hotfix", "ColorMaskResourceBarrier"));
            ExposureResourceBarrier.set_from_config(readInt("Hotfix", "ExposureResourceBarrier"));
            OutputResourceBarrier.set_from_config(readInt("Hotfix", "OutputResourceBarrier"));
        }

        // NvApi
        {
            DisableFlipMetering.set_from_config(readBool("NvApi", "DisableFlipMetering"));
        }

        // Inputs
        {
            EnableDlssInputs.set_from_config(readBool("Inputs", "EnableDlssInputs"));
        }

        // Plugins
        {
            PluginPath.set_from_config(readWString("Plugins", "Path"));
            LoadSpecialK.set_from_config(readBool("Plugins", "LoadSpecialK"));
            LoadReShade.set_from_config(readBool("Plugins", "LoadReShade"));
            LoadAsiPlugins.set_from_config(readBool("Plugins", "LoadAsiPlugins"));
            LateAsiPluginsDelay.set_from_config(readInt("Plugins", "LateAsiPluginsDelay"));
        }

        // HDR
        {
            ForceHDR.set_from_config(readBool("HDR", "ForceHDR"));
            UseHDR10.set_from_config(readBool("HDR", "UseHDR10"));
            SkipColorSpace.set_from_config(readBool("HDR", "SkipColorSpace"));
        }

        // V-Sync
        {
            OverrideVsync.set_from_config(readBool("V-Sync", "OverrideVsync"));
            ForceVsync.set_from_config(readBool("V-Sync", "ForceVsync"));
            VsyncInterval.set_from_config(readInt("V-Sync", "SyncInterval"));
        }

        // Libraries
        {
            MainDllPath.set_from_config(readWString("Libraries", "OptiDllPath"));

            NvngxPath.set_from_config(readWString("Libraries", "NvngxPath"));
            NVNGX_DLSS_Library.set_from_config(readWString("Libraries", "NvngxDlssPath"));
            DLSSFeaturePath.set_from_config(readWString("Libraries", "NvngxFeaturePath"));
        }

        return true;
    }

    return false;
}

bool Config::LoadFromPath(const wchar_t* InPath)
{
    const std::lock_guard lock(configIniMutex);
    std::filesystem::path iniPath(InPath);
    auto newPath = iniPath / fileName;

    if (Reload(newPath))
    {
        absoluteFileName = newPath;
        return true;
    }

    return false;
}

std::string GetBoolValue(std::optional<bool> value)
{
    if (!value.has_value())
        return "auto";

    return value.value() ? "true" : "false";
}

template <typename T> std::string GetIntValue(std::optional<T> value, bool getHex = false)
{
    if (!value.has_value())
        return "auto";

    if constexpr (std::is_enum_v<T>)
    {
        using Underlying = std::underlying_type_t<T>;
        Underlying v = static_cast<Underlying>(value.value());

        if (getHex)
            return std::format("{:#x}", v);

        return std::to_string(v);
    }
    else
    {
        if (getHex)
            return std::format("{:#x}", value.value());

        return std::to_string(value.value());
    }
}

std::string GetFloatValue(std::optional<float> value)
{
    if (!value.has_value())
        return "auto";

    return std::to_string(value.value());
}

bool Config::SaveIni()
{
    const std::lock_guard lock(configIniMutex);
    // Upscalers
    {
        auto SaveUpscaler = [&](const char* key, auto& upscalerSetting)
        {
            std::string value = upscalerSetting.value_for_config()
                                    .transform(UpscalerToCode) // Turn enum into string
                                    .value_or("auto");

            ini.SetValue("Upscalers", key, value.c_str());
        };

        SaveUpscaler("Dx12Upscaler", Instance()->Dx12Upscaler);
    }

    // Framerate
    {
        ini.SetValue("Framerate", "FramerateLimit",
                     GetFloatValue(Instance()->FramerateLimit.value_for_config()).c_str());
    }

    // Output Scaling
    {
        ini.SetValue("OutputScaling", "Enabled",
                     GetBoolValue(Instance()->OutputScalingEnabled.value_for_config()).c_str());
        ini.SetValue("OutputScaling", "Multiplier",
                     GetFloatValue(Instance()->OutputScalingMultiplier.value_for_config()).c_str());
        ini.SetValue("OutputScaling", "Downscaler", GetIntValue(Instance()->OutputScalingDownscaler).c_str());
    }

    // Camera fallbacks
    {
        ini.SetValue("FSR", "VerticalFov", GetFloatValue(Instance()->FsrVerticalFov.value_for_config()).c_str());
        ini.SetValue("FSR", "CameraNear", GetFloatValue(Instance()->FsrCameraNear.value_for_config()).c_str());
        ini.SetValue("FSR", "CameraFar", GetFloatValue(Instance()->FsrCameraFar.value_for_config()).c_str());
    }

    // DLSS
    {
        ini.SetValue("DLSS", "Enabled", GetBoolValue(Instance()->DLSSEnabled.value_for_config()).c_str());

    // --- DLSS 5 Neural Rendering (OptiScaler/dlssnr) ---
    // Preserve tuning, but never arm neural rendering on the next launch.
    // Do not mutate the live option: saving must not interrupt the current session.
    ini.SetValue("DlssNr", "Enabled", "false");
    ini.SetValue("DlssNr", "FinishedPicture", GetBoolValue(Instance()->DlssNrFinishedPicture.value_for_config()).c_str());
    ini.SetValue("DlssNr", "RunBeforeSR",
                 GetBoolValue(Instance()->DlssNrRunBeforeSr.value_for_config()).c_str());
    ini.SetValue("DlssNr", "DeferredDLSS",
                 GetBoolValue(Instance()->DlssNrDeferredDlss.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ResidualAcrossRR",
                 GetBoolValue(Instance()->DlssNrResidualAcrossRr.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ResidualAcrossRRBlend",
                 GetFloatValue(Instance()->DlssNrResidualAcrossRrBlend.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ResidualFG", GetBoolValue(Instance()->DlssNrResidualFg.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Precision", GetIntValue(Instance()->DlssNrPrecision.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ResidualFGApproxCamera", GetBoolValue(Instance()->DlssNrResidualFgApproxCamera.value_for_config()).c_str());
    {
        auto toggle = Instance()->DlssNrToggleKey.value_for_config();
        ini.SetValue("DlssNr", "ToggleKey", GetIntValue(toggle, toggle > 0).c_str());
    }
    ini.SetValue("DlssNr", "TransferStrength",
                 GetFloatValue(Instance()->DlssNrTransferStrength.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ColourStrength",
                 GetFloatValue(Instance()->DlssNrColourStrength.value_for_config()).c_str());
    ini.SetValue("DlssNr", "MaxRatio", GetFloatValue(Instance()->DlssNrMaxRatio.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Transfer", GetIntValue(Instance()->DlssNrTransfer.value_for_config()).c_str());

    ini.SetValue("DlssNr", "WhitePointFromExposure",
                 GetBoolValue(Instance()->DlssNrWhitePointFromExposure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "DebugView", GetIntValue(Instance()->DlssNrDebugView.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Compare", GetIntValue(Instance()->DlssNrCompare.value_for_config()).c_str());
    ini.SetValue("DlssNr", "CompareSplit",
                 GetFloatValue(Instance()->DlssNrCompareSplit.value_for_config()).c_str());
    ini.SetValue("DlssNr", "CompareZoom",
                 GetFloatValue(Instance()->DlssNrCompareZoom.value_for_config()).c_str());
    ini.SetValue("DlssNr", "CompareSwap",
                 GetBoolValue(Instance()->DlssNrCompareSwap.value_for_config()).c_str());
    ini.SetValue("DlssNr", "CompareTags",
                 GetBoolValue(Instance()->DlssNrCompareTags.value_for_config()).c_str());
    ini.SetValue("DlssNr", "TagScale",
                 GetFloatValue(Instance()->DlssNrTagScale.value_for_config()).c_str());
    ini.SetValue("DlssNr", "WorkingScale", GetFloatValue(Instance()->DlssNrWorkingScale.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ScalingDownscaler", GetIntValue(Instance()->DlssNrScalingDownscaler).c_str());
    ini.SetValue("DlssNr", "AutoCapture", GetBoolValue(Instance()->DlssNrAutoCapture.value_for_config()).c_str());

    // These were read every launch but never written, so nothing set through the menu survived a
    // restart -- the white-point source, both trims, the anchor, the pass count and the rest all
    // reset to default on the next run.
    ini.SetValue("DlssNr", "WhitePointSource", GetIntValue(Instance()->DlssNrWhitePointSource.value_for_config()).c_str());
    ini.SetValue("DlssNr", "WhitePointTrim", GetFloatValue(Instance()->DlssNrWhitePointTrim.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ScanTrim", GetFloatValue(Instance()->DlssNrScanTrim.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ScanAnchorValue", GetFloatValue(Instance()->DlssNrScanAnchorValue.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ScanAnchorWhitePoint", GetFloatValue(Instance()->DlssNrScanAnchorWhitePoint.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ScanAnchors", Instance()->DlssNrScanAnchors.value_for_config_or("").c_str());
    ini.SetValue("DlssNr", "ScanInverted", GetBoolValue(Instance()->DlssNrScanInverted.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ScanMeter", GetBoolValue(Instance()->DlssNrScanMeter.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Passes", GetIntValue(Instance()->DlssNrPasses.value_for_config()).c_str());
    ini.SetValue("DlssNr", "UseProxy", GetBoolValue(Instance()->DlssNrUseProxy.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ProxyProbe", GetBoolValue(Instance()->DlssNrProxyProbe.value_for_config()).c_str());
    // ScanExposure is a developer override with no menu control; persist it so a set ini keeps it.
    ini.SetValue("DlssNr", "ScanExposure", GetBoolValue(Instance()->DlssNrScanExposure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "WhitePointScale",
                 GetFloatValue(Instance()->DlssNrWhitePointScale.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Preset", GetIntValue(Instance()->DlssNrPreset.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Intensity", GetFloatValue(Instance()->DlssNrIntensity.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Style", GetIntValue(Instance()->DlssNrStyle.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass2Preset",
                 GetIntValue(Instance()->DlssNrPass2Preset.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass2Style",
                 GetIntValue(Instance()->DlssNrPass2Style.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass3Preset",
                 GetIntValue(Instance()->DlssNrPass3Preset.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass3Style",
                 GetIntValue(Instance()->DlssNrPass3Style.value_for_config()).c_str());
    ini.SetValue("DlssNr", "LocalStructure",
                 GetFloatValue(Instance()->DlssNrLocalStructure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "LocalTone", GetFloatValue(Instance()->DlssNrLocalTone.value_for_config()).c_str());
    ini.SetValue("DlssNr", "SkinStructure",
                 GetFloatValue(Instance()->DlssNrSkinStructure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "AutoMask", GetBoolValue(Instance()->DlssNrAutoMask.value_for_config()).c_str());
    ini.SetValue("DlssNr", "SkinProtection", GetBoolValue(Instance()->DlssNrSkinProtection.value_for_config()).c_str());
    ini.SetValue("DlssNr", "SkinToneEnabled", GetBoolValue(Instance()->DlssNrSkinToneEnabled.value_for_config()).c_str());
    ini.SetValue("DlssNr", "SkinDetail", GetFloatValue(Instance()->DlssNrSkinDetail.value_for_config()).c_str());
    ini.SetValue("DlssNr", "SkinColour", GetFloatValue(Instance()->DlssNrSkinColour.value_for_config()).c_str());
    ini.SetValue("DlssNr", "EnvironmentDetail", GetFloatValue(Instance()->DlssNrEnvironmentDetail.value_for_config()).c_str());
    ini.SetValue("DlssNr", "EnvironmentColour", GetFloatValue(Instance()->DlssNrEnvironmentColour.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ShowSkinMask", GetBoolValue(Instance()->DlssNrShowSkinMask.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass2Intensity", GetFloatValue(Instance()->DlssNrPass2Intensity.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass2LocalStructure", GetFloatValue(Instance()->DlssNrPass2LocalStructure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass2LocalTone", GetFloatValue(Instance()->DlssNrPass2LocalTone.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass2SkinStructure", GetFloatValue(Instance()->DlssNrPass2SkinStructure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass2AutoMask", GetBoolValue(Instance()->DlssNrPass2AutoMask.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass3Intensity", GetFloatValue(Instance()->DlssNrPass3Intensity.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass3LocalStructure", GetFloatValue(Instance()->DlssNrPass3LocalStructure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass3LocalTone", GetFloatValue(Instance()->DlssNrPass3LocalTone.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass3SkinStructure", GetFloatValue(Instance()->DlssNrPass3SkinStructure.value_for_config()).c_str());
    ini.SetValue("DlssNr", "Pass3AutoMask", GetBoolValue(Instance()->DlssNrPass3AutoMask.value_for_config()).c_str());
    ini.SetValue("DlssNr", "UnlockPasses", GetBoolValue(Instance()->DlssNrUnlockPasses.value_for_config()).c_str());
    for (unsigned int i = 0; i < 27; ++i)
    {
        auto& pass = Instance()->DlssNrExtraPasses[i];
        ini.SetValue("DlssNr", std::format("Pass{}Style", i + 4).c_str(), GetIntValue(pass.style.value_for_config()).c_str());
        ini.SetValue("DlssNr", std::format("Pass{}Intensity", i + 4).c_str(), GetFloatValue(pass.intensity.value_for_config()).c_str());
        ini.SetValue("DlssNr", std::format("Pass{}LocalStructure", i + 4).c_str(), GetFloatValue(pass.structure.value_for_config()).c_str());
        ini.SetValue("DlssNr", std::format("Pass{}LocalTone", i + 4).c_str(), GetFloatValue(pass.tone.value_for_config()).c_str());
        ini.SetValue("DlssNr", std::format("Pass{}SkinStructure", i + 4).c_str(), GetFloatValue(pass.skin.value_for_config()).c_str());
        ini.SetValue("DlssNr", std::format("Pass{}AutoMask", i + 4).c_str(), GetBoolValue(pass.autoMask.value_for_config()).c_str());
    }
    ini.SetValue("DlssNr", "ReversibleMode", GetIntValue(Instance()->DlssNrReversibleMode.value_for_config()).c_str());
    ini.SetValue("DlssNr", "ApplyModel", GetBoolValue(Instance()->DlssNrApplyModel.value_for_config()).c_str());
    ini.SetValue("DlssNr", "HoldFrame", GetBoolValue(Instance()->DlssNrHoldFrame.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetOverride",
                     GetBoolValue(Instance()->RenderPresetOverride.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetForAll",
                     GetIntValue(Instance()->RenderPresetForAll.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetDLAA", GetIntValue(Instance()->RenderPresetDLAA.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetUltraQuality",
                     GetIntValue(Instance()->RenderPresetUltraQuality.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetQuality",
                     GetIntValue(Instance()->RenderPresetQuality.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetBalanced",
                     GetIntValue(Instance()->RenderPresetBalanced.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetPerformance",
                     GetIntValue(Instance()->RenderPresetPerformance.value_for_config()).c_str());
        ini.SetValue("DLSS", "RenderPresetUltraPerformance",
                     GetIntValue(Instance()->RenderPresetUltraPerformance.value_for_config()).c_str());
        ini.SetValue("DLSS", "UseGenericAppIdWithDlss",
                     GetBoolValue(Instance()->UseGenericAppIdWithDlss.value_for_config()).c_str());
    }

    // DLSSD
    {
        ini.SetValue("DLSSD", "RenderPresetOverride",
                     GetBoolValue(Instance()->DLSSDRenderPresetOverride.value_for_config()).c_str());
        ini.SetValue("DLSSD", "RenderPresetForAll",
                     GetIntValue(Instance()->DLSSDRenderPresetForAll.value_for_config()).c_str());
        ini.SetValue("DLSSD", "RenderPresetDLAA",
                     GetIntValue(Instance()->DLSSDRenderPresetDLAA.value_for_config()).c_str());
        ini.SetValue("DLSSD", "RenderPresetUltraQuality",
                     GetIntValue(Instance()->DLSSDRenderPresetUltraQuality.value_for_config()).c_str());
        ini.SetValue("DLSSD", "RenderPresetQuality",
                     GetIntValue(Instance()->DLSSDRenderPresetQuality.value_for_config()).c_str());
        ini.SetValue("DLSSD", "RenderPresetBalanced",
                     GetIntValue(Instance()->DLSSDRenderPresetBalanced.value_for_config()).c_str());
        ini.SetValue("DLSSD", "RenderPresetPerformance",
                     GetIntValue(Instance()->DLSSDRenderPresetPerformance.value_for_config()).c_str());
        ini.SetValue("DLSSD", "RenderPresetUltraPerformance",
                     GetIntValue(Instance()->DLSSDRenderPresetUltraPerformance.value_for_config()).c_str());
    }

    // Sharpness
    {
        std::string shader = SharpnessShader.value_for_config()
                                 .transform(SharpnessShaderToCode) // Turn enum into string
                                 .value_or("auto");

        ini.SetValue("Sharpness", "Shader", shader.c_str());

        ini.SetValue("Sharpness", "OverrideSharpness",
                     GetBoolValue(Instance()->OverrideSharpness.value_for_config()).c_str());
        ini.SetValue("Sharpness", "Sharpness", GetFloatValue(Instance()->Sharpness.value_for_config()).c_str());
    }

    // CAS
    {
        ini.SetValue("CAS", "Enabled", GetBoolValue(Instance()->RcasEnabled.value_for_config()).c_str());

        ini.SetValue("CAS", "MotionSharpnessEnabled",
                     GetBoolValue(Instance()->MotionSharpnessEnabled.value_for_config()).c_str());
        ini.SetValue("CAS", "MotionSharpness", GetFloatValue(Instance()->MotionSharpness.value_for_config()).c_str());
        ini.SetValue("CAS", "MotionThreshold", GetFloatValue(Instance()->MotionThreshold.value_for_config()).c_str());
        ini.SetValue("CAS", "MotionScaleLimit", GetFloatValue(Instance()->MotionScaleLimit.value_for_config()).c_str());

        ini.SetValue("CAS", "ContrastEnabled", GetBoolValue(Instance()->ContrastEnabled.value_for_config()).c_str());
        ini.SetValue("CAS", "Contrast", GetFloatValue(Instance()->Contrast.value_for_config()).c_str());

        ini.SetValue("CAS", "DADepthScale", GetFloatValue(Instance()->DADepthScale.value_for_config()).c_str());
        ini.SetValue("CAS", "DADepthBias", GetFloatValue(Instance()->DADepthBias.value_for_config()).c_str());
        ini.SetValue("CAS", "DAClampOutput", GetBoolValue(Instance()->DAClampOutput.value_for_config()).c_str());

        ini.SetValue("CAS", "SharpenerDebug",
                     GetBoolValue(Instance()->MotionSharpnessDebug.value_for_config()).c_str());
    }

    // Magnifier
    {
        ini.SetValue("Magnifier", "Enabled", GetBoolValue(Instance()->MagnifierEnabled.value_for_config()).c_str());

        ini.SetValue("Magnifier", "Size", GetFloatValue(Instance()->MagnifierSize.value_for_config()).c_str());
        ini.SetValue("Magnifier", "ZoomFactor",
                     GetIntValue(Instance()->MagnifierZoomFactor.value_for_config()).c_str());

        ini.SetValue("Magnifier", "BorderSize",
                     GetFloatValue(Instance()->MagnifierBorderSize.value_for_config()).c_str());
        ini.SetValue("Magnifier", "CursorOffsetX",
                     GetFloatValue(Instance()->MagnifierCursorOffsetX.value_for_config()).c_str());
        ini.SetValue("Magnifier", "CursorOffsetY",
                     GetFloatValue(Instance()->MagnifierCursorOffsetY.value_for_config()).c_str());

        ini.SetValue("Magnifier", "StaticPosX",
                     GetFloatValue(Instance()->MagnifierStaticPosX.value_for_config()).c_str());
        ini.SetValue("Magnifier", "StaticPosY",
                     GetFloatValue(Instance()->MagnifierStaticPosY.value_for_config()).c_str());
    }

    // Menu
    {
        ini.SetValue("Menu", "Scale", GetFloatValue(Instance()->MenuScale).c_str());
        ini.SetValue("Menu", "OverlayMenu", GetBoolValue(Instance()->OverlayMenu.value_for_config()).c_str());
        ini.SetValue("Menu", "PhysicalNavigationKey", GetBoolValue(Instance()->MenuPhysicalNavigationKey.value_for_config()).c_str());

        ini.SetLongValue("Menu", "BackupShortcutKey", Instance()->BackupShortcutKey.value_or_default());
        ini.SetLongValue("Menu", "PresetHotSwapKey", Instance()->PresetHotSwapKey.value_or_default());
        ini.SetLongValue("Menu", "SnapshotKey", Instance()->SnapshotKey.value_or_default());

        auto setting = Instance()->ShortcutKey.value_for_config();
        ini.SetValue("Menu", "ShortcutKey",
                     GetIntValue(Instance()->ShortcutKey.value_for_config(), setting > 0).c_str());

        ini.SetValue("Menu", "ExtendedLimits", GetBoolValue(Instance()->ExtendedLimits.value_for_config()).c_str());
        ini.SetValue("Menu", "ShowFps", GetBoolValue(Instance()->ShowFps.value_for_config()).c_str());
        ini.SetValue("Menu", "UseHQFont", GetBoolValue(Instance()->UseHQFont.value_for_config()).c_str());

        setting = Instance()->FpsShortcutKey.value_for_config();
        ini.SetValue("Menu", "FpsShortcutKey",
                     GetIntValue(Instance()->FpsShortcutKey.value_for_config(), setting > 0).c_str());

        setting = Instance()->FpsCycleShortcutKey.value_for_config();
        ini.SetValue("Menu", "FpsCycleShortcutKey",
                     GetIntValue(Instance()->FpsCycleShortcutKey.value_for_config(), setting > 0).c_str());

        ini.SetValue("Menu", "FpsOverlayPos", GetIntValue(Instance()->FpsOverlayPosition.value_for_config()).c_str());
        ini.SetValue("Menu", "FpsOverlayType", GetIntValue(Instance()->FpsOverlayType.value_for_config()).c_str());
        ini.SetValue("Menu", "FpsOverlayHorizontal",
                     GetBoolValue(Instance()->FpsOverlayHorizontal.value_for_config()).c_str());
        ini.SetValue("Menu", "FpsOverlayAlpha", GetFloatValue(Instance()->FpsOverlayAlpha.value_for_config()).c_str());
        ini.SetValue("Menu", "FpsScale", GetFloatValue(Instance()->FpsScale.value_for_config()).c_str());
        ini.SetValue("Menu", "FontSize", GetFloatValue(Instance()->FontSize.value_for_config()).c_str());
        ini.SetValue("Menu", "TTFFontPath",
                     wstring_to_string(Instance()->TTFFontPath.value_for_config_or(L"auto")).c_str());

        ini.SetValue("Menu", "LightTheme", GetBoolValue(Instance()->LightTheme.value_for_config()).c_str());
        ini.SetValue("Menu", "OverlaysUseTheme", GetBoolValue(Instance()->OverlaysUseTheme.value_for_config()).c_str());
        ini.SetValue("Menu", "AccentColorR", GetFloatValue(Instance()->MenuAccentColorR.value_for_config()).c_str());
        ini.SetValue("Menu", "AccentColorG", GetFloatValue(Instance()->MenuAccentColorG.value_for_config()).c_str());
        ini.SetValue("Menu", "AccentColorB", GetFloatValue(Instance()->MenuAccentColorB.value_for_config()).c_str());
        ini.SetValue("Menu", "BGColorR", GetFloatValue(Instance()->MenuBGColorR.value_for_config()).c_str());
        ini.SetValue("Menu", "BGColorG", GetFloatValue(Instance()->MenuBGColorG.value_for_config()).c_str());
        ini.SetValue("Menu", "BGColorB", GetFloatValue(Instance()->MenuBGColorB.value_for_config()).c_str());
        ini.SetValue("Menu", "BGColorA", GetFloatValue(Instance()->MenuBGColorA.value_for_config()).c_str());
    }

    // Hooks
    {
        ini.SetValue("Hooks", "HookOriginalNvngxOnly",
                     GetBoolValue(Instance()->HookOriginalNvngxOnly.value_for_config()).c_str());
        ini.SetValue("Hooks", "EarlyHooking", GetBoolValue(Instance()->EarlyHooking.value_for_config()).c_str());
        ini.SetValue("Hooks", "UseNtdllHooks", GetBoolValue(Instance()->UseNtdllHooks.value_for_config()).c_str());
    }

    // InitFlags
    {
        ini.SetValue("InitFlags", "AutoExposure", GetBoolValue(Instance()->AutoExposure.value_for_config()).c_str());
        ini.SetValue("InitFlags", "HDR", GetBoolValue(Instance()->HDR.value_for_config()).c_str());
        ini.SetValue("InitFlags", "DepthInverted", GetBoolValue(Instance()->DepthInverted.value_for_config()).c_str());
        ini.SetValue("InitFlags", "JitterCancellation",
                     GetBoolValue(Instance()->JitterCancellation.value_for_config()).c_str());
        ini.SetValue("InitFlags", "DisplayResolution",
                     GetBoolValue(Instance()->DisplayResolution.value_for_config()).c_str());
    }

    // Upscale Ratio Override
    {
        ini.SetValue("UpscaleRatio", "UpscaleRatioOverrideEnabled",
                     GetBoolValue(Instance()->UpscaleRatioOverrideEnabled.value_for_config()).c_str());
        ini.SetValue("UpscaleRatio", "UpscaleRatioOverrideValue",
                     GetFloatValue(Instance()->UpscaleRatioOverrideValue.value_for_config()).c_str());
    }

    // Quality Overrides
    {
        ini.SetValue("QualityOverrides", "QualityRatioOverrideEnabled",
                     GetBoolValue(Instance()->QualityRatioOverrideEnabled.value_for_config()).c_str());
        ini.SetValue("QualityOverrides", "QualityRatioDLAA",
                     GetFloatValue(Instance()->QualityRatio_DLAA.value_for_config()).c_str());
        ini.SetValue("QualityOverrides", "QualityRatioUltraQuality",
                     GetFloatValue(Instance()->QualityRatio_UltraQuality.value_for_config()).c_str());
        ini.SetValue("QualityOverrides", "QualityRatioQuality",
                     GetFloatValue(Instance()->QualityRatio_Quality.value_for_config()).c_str());
        ini.SetValue("QualityOverrides", "QualityRatioBalanced",
                     GetFloatValue(Instance()->QualityRatio_Balanced.value_for_config()).c_str());
        ini.SetValue("QualityOverrides", "QualityRatioPerformance",
                     GetFloatValue(Instance()->QualityRatio_Performance.value_for_config()).c_str());
        ini.SetValue("QualityOverrides", "QualityRatioUltraPerformance",
                     GetFloatValue(Instance()->QualityRatio_UltraPerformance.value_for_config()).c_str());
    }

    // Anisotropy
    {
        ini.SetValue("Anisotropy", "AnisotropyOverride",
                     GetIntValue(Instance()->AnisotropyOverride.value_for_config()).c_str());
        ini.SetValue("Anisotropy", "ModifyComparison",
                     GetBoolValue(Instance()->AnisotropyModifyComp.value_for_config()).c_str());
        ini.SetValue("Anisotropy", "ModifyMinMax",
                     GetBoolValue(Instance()->AnisotropyModifyMinMax.value_for_config()).c_str());
        ini.SetValue("Anisotropy", "SkipPointFilter",
                     GetBoolValue(Instance()->AnisotropySkipPointFilter.value_for_config()).c_str());
    }

    // Mipmap
    {
        ini.SetValue("Mipmap", "MipmapBiasOverride",
                     GetFloatValue(Instance()->MipmapBiasOverride.value_for_config()).c_str());
        ini.SetValue("Mipmap", "MipmapBiasOverrideAll",
                     GetBoolValue(Instance()->MipmapBiasOverrideAll.value_for_config()).c_str());
        ini.SetValue("Mipmap", "MipmapBiasFixedOverride",
                     GetBoolValue(Instance()->MipmapBiasFixedOverride.value_for_config()).c_str());
        ini.SetValue("Mipmap", "MipmapBiasScaleOverride",
                     GetBoolValue(Instance()->MipmapBiasScaleOverride.value_for_config()).c_str());
    }

    // Process Filter
    {
        ini.SetValue("ProcessFilter", "TargetProcessName",
                     wstring_to_string(Instance()->TargetProcess.value_for_config_or(L"auto")).c_str());
        ini.SetValue("ProcessFilter", "ProcessExclusionList",
                     wstring_to_string(Instance()->ProcessExclusionList.value_for_config_or(L"auto")).c_str());
    }

    // Hotfixes
    {
        ini.SetValue("Hotfix", "CheckForUpdate", GetBoolValue(Instance()->CheckForUpdate.value_for_config()).c_str());
        ini.SetValue("Hotfix", "DisableOverlays", GetBoolValue(Instance()->DisableOverlays.value_for_config()).c_str());

        ini.SetValue("Hotfix", "RoundInternalResolution",
                     GetIntValue(Instance()->RoundInternalResolution.value_for_config()).c_str());

        ini.SetValue("Hotfix", "RestoreComputeSignature",
                     GetBoolValue(Instance()->RestoreComputeSignature.value_for_config()).c_str());
        ini.SetValue("Hotfix", "RestoreGraphicSignature",
                     GetBoolValue(Instance()->RestoreGraphicSignature.value_for_config()).c_str());
        ini.SetValue("Hotfix", "ExtendedStateRestore",
                     GetBoolValue(Instance()->ExtendedStateRestore.value_for_config()).c_str());
        ini.SetValue("Hotfix", "SkipFirstFrames", GetIntValue(Instance()->SkipFirstFrames.value_for_config()).c_str());

        ini.SetValue("Hotfix", "UsePrecompiledShaders",
                     GetBoolValue(Instance()->UsePrecompiledShaders.value_for_config()).c_str());
        ini.SetValue("Hotfix", "PreferDedicatedGpu",
                     GetBoolValue(Instance()->PreferDedicatedGpu.value_for_config()).c_str());
        ini.SetValue("Hotfix", "PreferFirstDedicatedGpu",
                     GetBoolValue(Instance()->PreferFirstDedicatedGpu.value_for_config()).c_str());

        ini.SetValue("Hotfix", "ColorResourceBarrier",
                     GetIntValue(Instance()->ColorResourceBarrier.value_for_config()).c_str());
        ini.SetValue("Hotfix", "MotionVectorResourceBarrier",
                     GetIntValue(Instance()->MVResourceBarrier.value_for_config()).c_str());
        ini.SetValue("Hotfix", "DepthResourceBarrier",
                     GetIntValue(Instance()->DepthResourceBarrier.value_for_config()).c_str());
        ini.SetValue("Hotfix", "ColorMaskResourceBarrier",
                     GetIntValue(Instance()->MaskResourceBarrier.value_for_config()).c_str());
        ini.SetValue("Hotfix", "ExposureResourceBarrier",
                     GetIntValue(Instance()->ExposureResourceBarrier.value_for_config()).c_str());
        ini.SetValue("Hotfix", "OutputResourceBarrier",
                     GetIntValue(Instance()->OutputResourceBarrier.value_for_config()).c_str());
    }

    // Logging
    {
        ini.SetValue("Log", "LogToFile", GetBoolValue(Instance()->LogToFile.value_for_config()).c_str());
        ini.SetValue("Log", "LogLevel", GetIntValue(Instance()->LogLevel.value_for_config()).c_str());
        ini.SetValue("Log", "LogToConsole", GetBoolValue(Instance()->LogToConsole.value_for_config()).c_str());
        ini.SetValue("Log", "LogToDebug", GetBoolValue(Instance()->LogToDebug.value_for_config()).c_str());
        ini.SetValue("Log", "LogToNGX", GetBoolValue(Instance()->LogToNGX.value_for_config()).c_str());
        ini.SetValue("Log", "OpenConsole", GetBoolValue(Instance()->OpenConsole.value_for_config()).c_str());
        ini.SetValue("Log", "SingleFile", GetBoolValue(Instance()->LogSingleFile.value_for_config()).c_str());
        ini.SetValue("Log", "LogFileName",
                     wstring_to_string(Instance()->LogFileName.value_for_config_or(L"auto")).c_str());
        ini.SetValue("Log", "LogAsync", GetBoolValue(Instance()->LogAsync.value_for_config()).c_str());
        ini.SetValue("Log", "LogAsyncThreads", GetIntValue(Instance()->LogAsyncThreads.value_for_config()).c_str());
    }

    // NvApi
    {
        ini.SetValue("NvApi", "DisableFlipMetering",
                     GetBoolValue(Instance()->DisableFlipMetering.value_for_config()).c_str());
    }

    // DRS
    {
        ini.SetValue("DRS", "DrsMinOverrideEnabled",
                     GetBoolValue(Instance()->DrsMinOverrideEnabled.value_for_config()).c_str());
        ini.SetValue("DRS", "DrsMaxOverrideEnabled",
                     GetBoolValue(Instance()->DrsMaxOverrideEnabled.value_for_config()).c_str());
    }

    // Plugins
    {

        ini.SetValue("Plugins", "Path", wstring_to_string(Instance()->PluginPath.value_for_config_or(L"auto")).c_str());
        ini.SetValue("Plugins", "LoadSpecialK", GetBoolValue(Instance()->LoadSpecialK.value_for_config()).c_str());
        ini.SetValue("Plugins", "LoadReShade", GetBoolValue(Instance()->LoadReShade.value_for_config()).c_str());
        ini.SetValue("Plugins", "LoadAsiPlugins", GetBoolValue(Instance()->LoadAsiPlugins.value_for_config()).c_str());
        ini.SetValue("Plugins", "LateAsiPluginsDelay",
                     GetIntValue(Instance()->LateAsiPluginsDelay.value_for_config()).c_str());
    }

    // inputs
    {
        ini.SetValue("Inputs", "EnableDlssInputs",
                     GetBoolValue(Instance()->EnableDlssInputs.value_for_config()).c_str());
    }

    // V-Sync
    {
        ini.SetValue("V-Sync", "OverrideVsync", GetBoolValue(Instance()->OverrideVsync.value_for_config()).c_str());
        ini.SetValue("V-Sync", "ForceVsync", GetBoolValue(Instance()->ForceVsync.value_for_config()).c_str());
        ini.SetValue("V-Sync", "SyncInterval", GetIntValue(Instance()->VsyncInterval.value_for_config()).c_str());

        if (Instance()->VsyncInterval.has_value())
        {
            if (Instance()->VsyncInterval.value() < 0 || Instance()->VsyncInterval.value() > 3)
                Instance()->VsyncInterval.reset();
        }
    }

    // Libraries
    {
        ini.SetValue("Libraries", "OptiDllPath",
                     wstring_to_string(Instance()->MainDllPath.value_for_config_or(L"auto")).c_str());

        ini.SetValue("Libraries", "NvngxPath",
                     wstring_to_string(Instance()->NvngxPath.value_for_config_or(L"auto")).c_str());
        ini.SetValue("Libraries", "NvngxFeaturePath",
                     wstring_to_string(Instance()->DLSSFeaturePath.value_for_config_or(L"auto")).c_str());
        ini.SetValue("Libraries", "NvngxDlssPath",
                     wstring_to_string(Instance()->NVNGX_DLSS_Library.value_for_config_or(L"auto")).c_str());
    }

    // Sections and keys for removed features, just delete them
    {
        for (const char* section : { "FrameGen", "FSRFG", "FSRFGInputs", "OptiFG", "XeFG", "DLSSG", "NvngxFG",
                                     "Nukems", "fakenvapi", "XeSS", "Spoofing", "Dx11withDx12" })
            ini.Delete(section, nullptr);

        for (const char* key : { "HorizontalFov", "UseFsrInputValues", "VelocityFactor", "ReactiveScale",
                                 "ShadingScale", "AccAddPerFrame", "MinDisOccAcc", "DebugView", "UpscalerIndex", "FGIndex", "UseReactiveMaskForTransparency",
                                 "Fsr4ForceModel", "Fsr4Preset", "Fsr4EnableWatermark", "Fsr4DoNotLoadAmdxc64",
                                 "Fsr4ForceEnableInt8", "FsrNonLinearColorSpace", "FsrNonLinearPQ",
                                 "FsrNonLinearSRGB", "FsrAgilitySDKUpgrade" })
            ini.Delete("FSR", key);

        for (const char* key : { "EnableXeSSInputs", "UseFsr2Inputs", "UseFsr2Dx11Inputs", "UseFsr2VulkanInputs",
                                 "Fsr2Pattern", "UseFsr3Inputs", "Fsr3Pattern", "UseFfxInputs", "EnableHotSwapping",
                                 "EnableFsr2Inputs", "EnableFsr3Inputs", "EnableFfxInputs" })
            ini.Delete("Inputs", key);

        for (const char* key : { "FfxDx12Path", "FfxDx12SRPath", "FfxDx12FGPath", "FfxDx12RRPath", "FfxDx12RCPath",
                                 "FfxVkPath", "XeSSPath", "XeFGPath", "XeLLPath", "XeSSDx11Path" })
            ini.Delete("Libraries", key);

        ini.Delete("Upscalers", "Dx11Upscaler");
        ini.Delete("Upscalers", "VulkanUpscaler");
        ini.Delete("DlssNr", "ProbeD3D11");
        ini.Delete("Hotfix", "CreateD3D12DeviceForLuma");
        ini.Delete("InitFlags", "DisableReactiveMask");
        ini.Delete("FSR", "DlssReactiveMaskBias");
        ini.Delete("DlssNr", "TaaFallback");
        ini.Delete("Menu", "DisableSplash");
        ini.Delete("Hotfix", "SimulateWaitableObject");
        ini.Delete("Libraries", "NvapiPath");
        ini.Delete("Menu", "FGShortcutKey");
        ini.Delete("Plugins", "LoadCustomAmdxc64OnRdna2");
    }

    auto pathWStr = absoluteFileName.wstring();

    LOG_INFO("Trying to save ini to: {0}", wstring_to_string(pathWStr));

    return SaveOwnedIni(ini, absoluteFileName);
}

std::optional<laminr::menu_geometry::Geometry> Config::LoadMenuGeometry(const std::string& context)
{
    const std::lock_guard lock(configIniMutex);
    return laminr::menu_geometry::LoadIni(ini, context);
}

bool Config::SaveMenuGeometry(const std::string& context, const laminr::menu_geometry::Geometry& geometry)
{
    const std::lock_guard lock(configIniMutex);
    return laminr::menu_geometry::SaveIni(ini, absoluteFileName, context, geometry);
}

std::vector<std::string> Config::GetConfigLog() { return _log; }

std::optional<std::string> Config::readString(std::string section, std::string key, bool lowercase)
{
    std::string value = ini.GetValue(section.c_str(), key.c_str(), "auto");

    std::string lower = value;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "auto")
        return std::nullopt;

    _log.push_back(std::format("{}.{}: {}", section, key, value));

    return lowercase ? lower : value;
}

std::optional<std::wstring> Config::readWString(std::string section, std::string key, bool lowercase)
{
    std::string value = ini.GetValue(section.c_str(), key.c_str(), "auto");

    std::string lower = value;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "auto")
        return std::nullopt;

    _log.push_back(std::format("{}.{}: {}", section, key, value));

    return lowercase ? string_to_wstring(lower) : string_to_wstring(value);
}

std::optional<float> Config::readFloat(std::string section, std::string key)
{
    auto value = readString(section, key);

    try
    {
        float result;

        if (value.has_value() && isFloat(value.value(), result))
            return result;

        return std::nullopt;
    }
    catch (const std::bad_optional_access&) // missing or auto value
    {
        return std::nullopt;
    }
    catch (const std::invalid_argument&) // invalid float string for std::stof
    {
        return std::nullopt;
    }
    catch (const std::out_of_range&) // out of range for 32 bit float
    {
        return std::nullopt;
    }
}

std::optional<int> Config::readInt(std::string section, std::string key)
{
    auto value = readString(section, key);
    if (!value.has_value())
        return std::nullopt;

    const auto& s = *value;
    try
    {
        size_t idx = 0;
        int result;

        // detect hex prefix
        if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
        {
            result = std::stoi(s, &idx, 16);
        }
        else
        {
            result = std::stoi(s, &idx, 10);
        }

        // ensure we consumed the whole string
        if (idx == s.size())
            return result;
        else
            return std::nullopt;
    }
    catch (const std::bad_optional_access&) // missing or auto value
    {
        return std::nullopt;
    }
    catch (const std::invalid_argument&) // invalid float string for std::stof
    {
        return std::nullopt;
    }
    catch (const std::out_of_range&) // out// out of range for 32 bit float
    {
        return std::nullopt;
    }
}

std::optional<uint32_t> Config::readUInt(std::string section, std::string key)
{
    auto value = readString(section, key);
    if (!value.has_value())
        return std::nullopt;

    const auto& s = *value;
    try
    {
        size_t idx = 0;
        int result;

        // detect hex prefix
        if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
        {
            result = std::stoi(s, &idx, 16);
        }
        else
        {
            result = std::stoi(s, &idx, 10);
        }

        // ensure we consumed the whole string
        if (idx == s.size())
            return result;
        else
            return std::nullopt;
    }
    catch (const std::bad_optional_access&) // missing or auto value
    {
        return std::nullopt;
    }
    catch (const std::invalid_argument&) // invalid float string for std::stof
    {
        return std::nullopt;
    }
    catch (const std::out_of_range&) // out// out of range for 32 bit float
    {
        return std::nullopt;
    }
}

std::optional<bool> Config::readBool(std::string section, std::string key)
{
    auto value = readString(section, key, true);
    if (value == "true")
        return true;
    else if (value == "false")
        return false;

    return std::nullopt;
}

// Only use for unsigned enums that have Enum::Count as the last entry
template <typename Enum> std::optional<Enum> Config::readEnum(std::string section, std::string key)
{
    static_assert(std::is_enum_v<Enum>, "Enum type required");

    auto value = readUInt(section, key);
    if (!value.has_value())
        return std::nullopt;

    using Underlying = std::underlying_type_t<Enum>;

    if (*value < static_cast<Underlying>(Enum::Count))
        return static_cast<Enum>(*value);

    return std::nullopt;
}

Config* Config::Instance()
{
    if (!_config)
        _config = new Config();

    return _config;
}
