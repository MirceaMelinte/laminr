#include "pch.h"
#include "Kernel_Hooks.h"

#include "Streamline_Hooks.h"
#include "LibraryLoad_Hooks.h"

#include <Util.h>
#include <State.h>
#include <Config.h>

#include <cwctype>

#include "Hook_Utils.h"

#pragma intrinsic(_ReturnAddress)

static inline HMODULE CheckLoad(const std::wstring& name)
{
    do
    {
        if (State::Instance().isShuttingDown || LibraryLoadHooks::IsApiSetName(name))
            break;

        if (State::SkipDllChecks())
        {
            const std::wstring skip = string_to_wstring(State::SkipDllName());

            if (skip.empty() || LibraryLoadHooks::EndsWithInsensitive(name, std::wstring_view(skip)) ||
                LibraryLoadHooks::EndsWithInsensitive(name, std::wstring(skip + L".dll")))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(name.data()));
                break;
            }
        }

        auto moduleHandle = LibraryLoadHooks::LoadLibraryCheckW(name.data(), name.data());

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1337)
            break;

        if (moduleHandle != nullptr)
        {
            LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
            return moduleHandle;
        }
    } while (false);

    return nullptr;
}

VALIDATE_HOOK(hk_K32_GetProcAddress, Kernel32Proxy::PFN_GetProcAddress)
FARPROC WINAPI KernelHooks::hk_K32_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    if ((size_t) lpProcName < 0x000000000000F000 && hModule == dllModule)
        LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

    return o_K32_GetProcAddress(hModule, lpProcName);
}

VALIDATE_HOOK(hk_K32_GetModuleHandleExW, Kernel32Proxy::PFN_GetModuleHandleExW)
BOOL WINAPI KernelHooks::hk_K32_GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule)
{
    if (lpModuleName && dwFlags == GET_MODULE_HANDLE_EX_FLAG_PIN && lstrcmpW(L"nvapi64.dll", lpModuleName) == 0 &&
        phModule)
    {
        LOG_TRACE("Suspected SpecialK call for nvapi64");
        *phModule = LibraryLoadHooks::LoadNvApi();
        return true;
    }

    return o_K32_GetModuleHandleExW(dwFlags, lpModuleName, phModule);
}

VALIDATE_HOOK(hk_KB_GetProcAddress, KernelBaseProxy::PFN_GetProcAddress)
FARPROC WINAPI KernelHooks::hk_KB_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
    if ((size_t) lpProcName < 0x000000000000F000)
    {
        if (hModule == dllModule)
            LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

        return o_KB_GetProcAddress(hModule, lpProcName);
    }

    // if (hModule == dllModule && lpProcName != nullptr)
    //{
    //     LOG_TRACE("Trying to get process address of {}, caller: {}", lpProcName,
    //               Util::WhoIsTheCaller(_ReturnAddress()));
    // }

    return o_KB_GetProcAddress(hModule, lpProcName);
}

VALIDATE_HOOK(hk_K32_OutputDebugStringW, Kernel32Proxy::PFN_OutputDebugStringW)
VOID WINAPI KernelHooks::hk_K32_OutputDebugStringW(LPCWSTR lpOutputString)
{
    o_K32_OutputDebugStringW(lpOutputString);

    std::wstring result(lpOutputString);

    while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r'))
    {
        result.pop_back();
    }

    LOG_TRACE(L"{}", result);
}

VALIDATE_HOOK(hk_K32_OutputDebugStringA, Kernel32Proxy::PFN_OutputDebugStringA)
VOID WINAPI KernelHooks::hk_K32_OutputDebugStringA(LPCSTR lpOutputString)
{
    o_K32_OutputDebugStringA(lpOutputString);

    std::string result(lpOutputString);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
    {
        result.pop_back();
    }

    LOG_TRACE("{}", result);
}

// Load Library checks

VALIDATE_HOOK(hk_K32_LoadLibraryW, Kernel32Proxy::PFN_LoadLibraryW)
HMODULE KernelHooks::hk_K32_LoadLibraryW(LPCWSTR lpLibFileName)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring name(lpLibFileName);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryW(lpLibFileName);
}

VALIDATE_HOOK(hk_K32_LoadLibraryA, Kernel32Proxy::PFN_LoadLibraryA)
HMODULE KernelHooks::hk_K32_LoadLibraryA(LPCSTR lpLibFileName)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::string nameA(lpLibFileName);
    std::wstring name = string_to_wstring(nameA);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", nameA.data(), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryA(lpLibFileName);
}

VALIDATE_HOOK(hk_K32_LoadLibraryExW, Kernel32Proxy::PFN_LoadLibraryExW)
HMODULE KernelHooks::hk_K32_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring name(lpLibFileName);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

VALIDATE_HOOK(hk_K32_LoadLibraryExA, Kernel32Proxy::PFN_LoadLibraryExA)
HMODULE KernelHooks::hk_K32_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::string nameA(lpLibFileName);
    std::wstring name = string_to_wstring(nameA);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", nameA.data(), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_K32_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
}

VALIDATE_HOOK(hk_KB_LoadLibraryExW, KernelBaseProxy::PFN_LoadLibraryExW)
HMODULE KernelHooks::hk_KB_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    if (lpLibFileName == nullptr)
        return NULL;

    std::wstring name(lpLibFileName);

#ifdef _DEBUG
    // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

    auto result = CheckLoad(name);

    if (result != nullptr)
        return result;

    return o_KB_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

VALIDATE_HOOK(hk_K32_FreeLibrary, Kernel32Proxy::PFN_FreeLibrary)
BOOL KernelHooks::hk_K32_FreeLibrary(HMODULE lpLibrary)
{
    if (lpLibrary == nullptr)
        return STATUS_INVALID_PARAMETER;

#ifdef _DEBUG
    // LOG_TRACE("{:X}", (size_t) lpLibrary);
#endif

    if (!State::Instance().isShuttingDown)
    {
        auto result = LibraryLoadHooks::FreeLibrary(lpLibrary);

        if (result.has_value())
            return result.value() == TRUE;
    }

    return o_K32_FreeLibrary(lpLibrary);
}
