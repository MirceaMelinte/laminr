#include "pch.h"
#include "LibraryLoad_Hooks.h"

#include <Config.h>
#include <DllNames.h>

#include <proxies/Ntdll_Proxy.h>
#include <proxies/Kernel32_Proxy.h>
#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>

#include <proxies/NVNGX_Proxy.h>

#include <hooks/Dxgi_Hooks.h>
#include <hooks/D3D12_Hooks.h>
#include <hooks/Streamline_Hooks.h>

#include <misc/IdentifyGpu.h>
#include <nvapi/NvApiHooks.h>

// #define LOG_LIB_OPERATIONS

HMODULE LibraryLoadHooks::LoadLibraryCheckA(std::string libName, LPCSTR lpLibFullPath)
{
    auto fullPath = std::string(lpLibFullPath);

    return LoadLibraryCheckW(string_to_wstring(libName), string_to_wstring(fullPath).c_str());
}

HMODULE LibraryLoadHooks::LoadLibraryCheckW(std::wstring libName, LPCWSTR lpLibFullPath)
{
    auto libNameA = wstring_to_string(libName);

#ifdef LOG_LIB_OPERATIONS
    LOG_TRACE("{}", libNameA);
#endif

    // C:\\Path\\like\\this.dll
    auto path = std::filesystem::path(libName).lexically_normal();
    auto normalizedPath = path.wstring();
    to_lower_in_place(normalizedPath);

    std::filesystem::path localSlPath(Config::Instance()->MainDllPath.value());
    localSlPath = localSlPath / L"streamline"; // Hardcoded streamline folder
    auto normalizedLocalSlPath = localSlPath.lexically_normal();

    const bool pathInsideLocalSlPath = Util::IsSubpath(path, normalizedLocalSlPath);

    // exe path
    auto exePath = Util::ExePath().parent_path().wstring();

    for (size_t i = 0; i < exePath.size(); i++)
        exePath[i] = std::tolower(exePath[i]);

    auto pos = libName.rfind(exePath);

    if (Config::Instance()->EnableDlssInputs.value_or_default() && CheckDllNameW(&libName, &nvngxNamesW) &&
        (!Config::Instance()->HookOriginalNvngxOnly.value_or_default() || pos == std::string::npos))
    {
        LOG_INFO("nvngx call: {0}, returning this dll!", libNameA);

        // if (!dontCount)
        // loadCount++;

        return dllModule;
    }

    if ((State::Instance().workingMode != WorkingMode::Dxgi || !State::Instance().skipDxgiLoadChecks) &&
        CheckDllNameW(&libName, &dllNamesW))
    {
        if (!State::Instance().ServeOriginal())
        {
            LOG_INFO("{} call, returning this dll!", libNameA);
            return dllModule;
        }
        else
        {
            LOG_INFO("{} call, ServeOriginal active returning original dll!", libNameA);
            return originalModule;
        }
    }

    // nvngx_dlss
    if (Config::Instance()->DLSSEnabled.value_or_default() && Config::Instance()->NVNGX_DLSS_Library.has_value() &&
        CheckDllNameW(&libName, &nvngxDlssNamesW))
    {
        auto nvngxDlss = LoadNvngxDlss(libName);

        if (nvngxDlss != nullptr)
            return nvngxDlss;
        else
            LOG_ERROR("Trying to load dll: {}", libNameA);
    }

    // NGX OTA
    // Try to catch something like this:
    // c:\programdata/nvidia/ngx/models//dlss/versions/20316673/files/160_e658700.bin
    if (libName.ends_with(L".bin"))
    {
        auto loadedBin = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (loadedBin && normalizedPath.contains(L"\\versions\\"))
        {
            if (normalizedPath.contains(L"\\dlss\\"))
            {
                State::Instance().NGX_OTA_Dlss = wstring_to_string(lpLibFullPath);
            }

            if (normalizedPath.contains(L"\\dlssd\\"))
            {
                State::Instance().NGX_OTA_Dlssd = wstring_to_string(lpLibFullPath);
            }
        }
        return loadedBin;
    }

    // NvApi64.dll
    if (CheckDllNameW(&libName, &nvapiNamesW))
    {
        LOG_INFO("{} call!", libNameA);

        return LibraryLoadHooks::LoadNvApi();
    }

    // Streamline loaded from Opti's local streamline folder is left alone
    const bool shouldHookSl = !pathInsideLocalSlPath;

    // sl.interposer.dll
    if (CheckDllNameW(&libName, &slInterposerNamesW) && shouldHookSl)
    {
        auto streamlineModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (streamlineModule != nullptr)
        {
            StreamlineHooks::hookInterposer(streamlineModule);
            slInterposerModule = streamlineModule;
        }
        else
        {
            LOG_ERROR("Trying to load dll: {}", libNameA);
        }

        return streamlineModule;
    }

    // sl.dlss_g.dll
    if ((CheckDllNameW(&libName, &slDlssgNamesW) ||
         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_dlss_g_"))))
    {
        auto dlssgModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (dlssgModule != nullptr)
        {
            StreamlineHooks::hookDlssg(dlssgModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.dlss_g: {}", libNameA);
        }

        return dlssgModule;
    }

    // sl.reflex.dll
    if (shouldHookSl && (CheckDllNameW(&libName, &slReflexNamesW) ||
                         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_reflex_"))))
    {
        auto reflexModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (reflexModule != nullptr)
        {
            StreamlineHooks::hookReflex(reflexModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.reflex: {}", libNameA);
        }

        return reflexModule;
    }

    // sl.common.dll
    if (shouldHookSl && (CheckDllNameW(&libName, &slCommonNamesW) ||
                         (normalizedPath.contains(L"\\versions\\") && normalizedPath.contains(L"\\sl_common_"))))
    {
        auto commonModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

        if (commonModule != nullptr)
        {
            StreamlineHooks::hookCommon(commonModule);
        }
        else
        {
            LOG_ERROR("Trying to load dll as sl.common: {}", libNameA);
        }

        return commonModule;
    }

    if (CheckDllNameW(&libName, &blockedDllNamesW))
    {
        LOG_DEBUG("Blocking dll: {}", wstring_to_string(libName));
        return (HMODULE) 1337;
    }
    else if (Config::Instance()->DisableOverlays.value_or_default() && CheckDllNameW(&libName, &blockOverlayNamesW))
    {
        LOG_DEBUG("Blocking overlay dll: {}", wstring_to_string(libName));
        return (HMODULE) 1337;
    }
    else if (CheckDllNameW(&libName, &overlayNamesW))
    {
        // If we hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow here
        // Order of CreateSwapChain calls become
        // Game -> Overlay -> Opti
        // and Overlays really does not like Opti's wrapped swapchain
        // If we skip hooking here first Steam hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow
        // Then hopefully Opti hook and call order become
        // Game -> Opti -> Overlay
        // And Opti menu works with Overlay without issues

        std::filesystem::path path(libName);
        auto dllName = path.filename().string();

        thread_local bool insideThisCode = false;
        if (insideThisCode)
            return nullptr;

        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        LOG_DEBUG("Overlay dll: {}, module: {:X}", wstring_to_string(libName), (size_t) module);

        if (module != nullptr)
        {
            if (DxgiProxy::Module() != nullptr)
            {
                LOG_INFO("Calling CreateDxgiFactory methods for overlay!");
                IDXGIFactory* factory = nullptr;
                IDXGIFactory1* factory1 = nullptr;
                IDXGIFactory2* factory2 = nullptr;

                auto ocResult = DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory);
                LOG_DEBUG("CreateDxgiFactory result: {:X}", ocResult);

                if (ocResult == S_OK && factory != nullptr)
                {
                    insideThisCode = true;
                    LOG_DEBUG("CreateDxgiFactory ok");
                    factory->Release();
                }

                ocResult = DxgiProxy::CreateDxgiFactory1_()(__uuidof(factory1), &factory1);
                LOG_DEBUG("CreateDxgiFactory1 result: {:X}", ocResult);

                if (ocResult == S_OK && factory1 != nullptr)
                {
                    LOG_DEBUG("CreateDxgiFactory1 ok");
                    factory1->Release();
                }

                ocResult = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory2), &factory2);
                LOG_DEBUG("CreateDxgiFactory2 result: {:X}", ocResult);

                if (ocResult == S_OK && factory2 != nullptr)
                {
                    LOG_DEBUG("CreateDxgiFactory2 ok");
                    factory2->Release();
                }
            }

            return module;
        }
    }

    // Hooks
    if (CheckDllNameW(&libName, &dx12NamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
        {
            D3d12Proxy::Init(module);
            D3D12Hooks::Hook();
        }

        return module;
    }

    if (CheckDllNameW(&libName, &dx12agilityNamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, 0);

        if (module != nullptr)
            D3D12Hooks::HookAgility(module);

        return module;
    }

    if (!State::Instance().skipDxgiLoadChecks && CheckDllNameW(&libName, &dxgiNamesW))
    {
        auto module = NtdllProxy::LoadLibraryExW_Ldr(libName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        if (module != nullptr)
        {
            DxgiProxy::Init(module);
            DxgiHooks::Hook();
        }
    }

    CheckModulesInMemory();

    return nullptr;
}

std::optional<NTSTATUS> LibraryLoadHooks::FreeLibrary(PVOID lpLibrary)
{
    std::optional<NTSTATUS> result;

    if (lpLibrary == dllModule)
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for OptiScaler, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == DxgiProxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for DXGI, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == D3d12Proxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for D3D12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == Kernel32Proxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for Kernel32, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == KernelBaseProxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for KernelBase, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    else if (lpLibrary == NtdllProxy::Module())
    {
#ifdef LOG_LIB_OPERATIONS
        LOG_WARN("Call for ntdll, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
#endif
        State::Instance().modulesToFree.insert(lpLibrary);
        result = TRUE;
    }
    return result;
}

HMODULE LibraryLoadHooks::LoadNvApi()
{
    LOG_FUNC();

    // Try to load nvapi only from system32, like the original call would
    HMODULE nvapi = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

    if (nvapi != nullptr)
        NvApiHooks::Hook(nvapi);

    return nvapi;
}

HMODULE LibraryLoadHooks::LoadNvngxDlss(std::wstring originalPath)
{
    HMODULE nvngxDlss = nullptr;

    if (Config::Instance()->NVNGX_DLSS_Library.has_value())
    {
        nvngxDlss = NtdllProxy::LoadLibraryExW_Ldr(Config::Instance()->NVNGX_DLSS_Library.value().c_str(), NULL, 0);

        if (nvngxDlss != nullptr)
        {
            LOG_INFO("nvngx_dlss.dll loaded from {0}",
                     wstring_to_string(Config::Instance()->NVNGX_DLSS_Library.value()));
            return nvngxDlss;
        }
        else
        {
            LOG_WARN("nvngx_dlss.dll can't found at {0}",
                     wstring_to_string(Config::Instance()->NVNGX_DLSS_Library.value()));
        }
    }

    if (nvngxDlss == nullptr)
    {
        nvngxDlss = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

        if (nvngxDlss != nullptr)
        {
            LOG_INFO("nvngx_dlss.dll loaded from {0}", wstring_to_string(originalPath));
            return nvngxDlss;
        }
    }

    return nullptr;
}

void LibraryLoadHooks::CheckModulesInMemory()
{
    if (!StreamlineHooks::isInterposerHooked())
    {
        // hook streamline right away if it's already loaded
        HMODULE slModule = nullptr;
        slModule = GetDllNameWModule(&slInterposerNamesW);
        if (slModule != nullptr)
        {
            LOG_DEBUG("sl.interposer.dll already in memory");
            StreamlineHooks::hookInterposer(slModule);
            slInterposerModule = slModule;
        }
    }

    const auto isLocalStreamlineModule = [](HMODULE module) -> bool
    {
        if (module == nullptr)
            return false;

        char modulePath[MAX_PATH] = {};
        if (GetModuleFileNameA(module, modulePath, sizeof(modulePath)) == 0)
            return false;

        const auto path = std::filesystem::path(modulePath).lexically_normal();
        const std::filesystem::path localSlPath =
            std::filesystem::path(Config::Instance()->MainDllPath.value()) / L"streamline";
        return Util::IsSubpath(path, localSlPath.lexically_normal());
    };

    // DLSS-G
    if (!StreamlineHooks::isDlssgHooked())
    {
        HMODULE slDlssg = GetDllNameWModule(&slDlssgNamesW);

        if (slDlssg != nullptr && !isLocalStreamlineModule(slDlssg))
        {
            LOG_DEBUG("sl.dlss_g.dll already in memory");
            StreamlineHooks::hookDlssg(slDlssg);
        }
    }

    // Reflex
    if (!StreamlineHooks::isReflexHooked())
    {
        HMODULE slReflex = GetDllNameWModule(&slReflexNamesW);

        if (slReflex != nullptr)
        {
            if (isLocalStreamlineModule(slReflex))
                LOG_DEBUG("local sl.reflex.dll already in memory");
            else
                LOG_DEBUG("sl.reflex.dll already in memory");

            StreamlineHooks::hookReflex(slReflex);
        }
    }

    if (!StreamlineHooks::isCommonHooked())
    {
        HMODULE slCommon = nullptr;
        slCommon = GetDllNameWModule(&slCommonNamesW);
        if (slCommon != nullptr)
        {
            LOG_DEBUG("sl.common.dll already in memory");
            StreamlineHooks::hookCommon(slCommon);
        }
    }

}

bool LibraryLoadHooks::EndsWithInsensitive(std::wstring_view text, std::wstring_view suffix)
{
    if (suffix.size() > text.size())
        return false;

    if (suffix.empty())
        return true;

    const wchar_t* tail = text.data() + (text.size() - suffix.size());
    const int res =
        CompareStringOrdinal(tail, static_cast<int>(suffix.size()), suffix.data(), static_cast<int>(suffix.size()),
                             TRUE); // case-insensitive
    return res == CSTR_EQUAL;
}

bool LibraryLoadHooks::EndsWithInsensitive(const UNICODE_STRING& text, std::wstring_view suffix)
{
    return EndsWithInsensitive(std::wstring_view { text.Buffer, static_cast<size_t>(text.Length) / sizeof(wchar_t) },
                               suffix);
}

bool LibraryLoadHooks::StartsWithInsensitive(std::wstring_view str, std::wstring_view prefix)
{
    if (str.size() < prefix.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::towlower(str[i]) != std::towlower(prefix[i]))
            return false;
    }

    return true;
}

bool LibraryLoadHooks::IsApiSetName(const std::wstring_view& n)
{
    return StartsWithInsensitive(n, L"api-ms-win-") || StartsWithInsensitive(n, L"ext-ms-") ||
           StartsWithInsensitive(n, L"api-ms-onecore-");
}
