#pragma once
#include "SysUtils.h"

#include <proxies/Kernel32_Proxy.h>
#include <proxies/KernelBase_Proxy.h>

// Enables hooking of GetModuleHandle
// which might create issues, not tested very well
// #define HOOK_GET_MODULE

// #define HOOK_OUTPUT_DEBUG

#ifdef HOOK_GET_MODULE
// Handle nvngx.dll calls on GetModule handle
// #define GET_MODULE_NVNGX

// Handle Opti dll calls on GetModule handle
#define GET_MODULE_DLL
#endif

class KernelHooks
{
  private:
    inline static Kernel32Proxy::PFN_FreeLibrary o_K32_FreeLibrary = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryA o_K32_LoadLibraryA = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryW o_K32_LoadLibraryW = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryExA o_K32_LoadLibraryExA = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryExW o_K32_LoadLibraryExW = nullptr;
    inline static Kernel32Proxy::PFN_GetProcAddress o_K32_GetProcAddress = nullptr;
    inline static Kernel32Proxy::PFN_GetModuleHandleExW o_K32_GetModuleHandleExW = nullptr;
    inline static Kernel32Proxy::PFN_OutputDebugStringW o_K32_OutputDebugStringW = nullptr;
    inline static Kernel32Proxy::PFN_OutputDebugStringA o_K32_OutputDebugStringA = nullptr;

    inline static KernelBaseProxy::PFN_FreeLibrary o_KB_FreeLibrary = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryA o_KB_LoadLibraryA = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryW o_KB_LoadLibraryW = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryExA o_KB_LoadLibraryExA = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryExW o_KB_LoadLibraryExW = nullptr;
    inline static KernelBaseProxy::PFN_GetProcAddress o_KB_GetProcAddress = nullptr;

    static FARPROC WINAPI hk_K32_GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
    static BOOL WINAPI hk_K32_GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule);
    static FARPROC WINAPI hk_KB_GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
    static VOID WINAPI hk_K32_OutputDebugStringW(LPCWSTR lpOutputString);
    static VOID WINAPI hk_K32_OutputDebugStringA(LPCSTR lpOutputString);

    static HMODULE hk_K32_LoadLibraryW(LPCWSTR lpLibFileName);
    static HMODULE hk_K32_LoadLibraryA(LPCSTR lpLibFileName);
    static HMODULE hk_K32_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
    static HMODULE hk_K32_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
    static HMODULE hk_KB_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
    static BOOL hk_K32_FreeLibrary(HMODULE lpLibrary);

    static inline std::mutex hookMutex32;
    static inline std::mutex hookMutexBase;

  public:
    static void Hook()
    {
        std::lock_guard<std::mutex> lock(hookMutex32);

        LOG_DEBUG("");

        if (o_K32_GetProcAddress == nullptr)
            o_K32_GetProcAddress = Kernel32Proxy::Hook_GetProcAddress(hk_K32_GetProcAddress);

        if (o_K32_GetModuleHandleExW == nullptr)
            o_K32_GetModuleHandleExW = Kernel32Proxy::Hook_GetModuleHandleExW(hk_K32_GetModuleHandleExW);

#ifdef HOOK_OUTPUT_DEBUG
        if (o_K32_OutputDebugStringW == nullptr)
            o_K32_OutputDebugStringW = Kernel32Proxy::Hook_OutputDebugStringW(hk_K32_OutputDebugStringW);

        if (o_K32_OutputDebugStringA == nullptr)
            o_K32_OutputDebugStringA = Kernel32Proxy::Hook_OutputDebugStringA(hk_K32_OutputDebugStringA);
#endif

        if (!Config::Instance()->UseNtdllHooks.value_or_default())
        {
            if (o_K32_FreeLibrary == nullptr)
                o_K32_FreeLibrary = Kernel32Proxy::Hook_FreeLibrary(hk_K32_FreeLibrary);

            if (o_K32_LoadLibraryA == nullptr)
                o_K32_LoadLibraryA = Kernel32Proxy::Hook_LoadLibraryA(hk_K32_LoadLibraryA);

            if (o_K32_LoadLibraryW == nullptr)
                o_K32_LoadLibraryW = Kernel32Proxy::Hook_LoadLibraryW(hk_K32_LoadLibraryW);

            if (o_K32_LoadLibraryExA == nullptr)
                o_K32_LoadLibraryExA = Kernel32Proxy::Hook_LoadLibraryExA(hk_K32_LoadLibraryExA);

            if (o_K32_LoadLibraryExW == nullptr)
                o_K32_LoadLibraryExW = Kernel32Proxy::Hook_LoadLibraryExW(hk_K32_LoadLibraryExW);
        }
    }

    static void HookBase()
    {
        std::lock_guard<std::mutex> lock(hookMutexBase);

        LOG_DEBUG("");

        if (o_KB_GetProcAddress == nullptr)
            o_KB_GetProcAddress = KernelBaseProxy::Hook_GetProcAddress(hk_KB_GetProcAddress);

        if (!Config::Instance()->UseNtdllHooks.value_or_default())
        {
            if (o_KB_LoadLibraryExW == nullptr)
                o_KB_LoadLibraryExW = KernelBaseProxy::Hook_LoadLibraryExW(hk_KB_LoadLibraryExW);
        }
    }
};
