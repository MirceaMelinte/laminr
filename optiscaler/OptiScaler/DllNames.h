#pragma once

#include "SysUtils.h"

#include <proxies/KernelBase_Proxy.h>

#include <cwctype> // for std::towlower

#define DEFINE_NAME_VECTORS(varName, ...)                                                                              \
    inline std::vector<std::string> varName##Names = []                                                                \
    {                                                                                                                  \
        std::vector<std::string> v;                                                                                    \
        const char* libs[] = { __VA_ARGS__ };                                                                          \
        for (auto lib : libs)                                                                                          \
        {                                                                                                              \
            v.emplace_back(std::string(lib) + ".dll");                                                                 \
            v.emplace_back(std::string(lib));                                                                          \
        }                                                                                                              \
        return v;                                                                                                      \
    }();                                                                                                               \
    inline std::vector<std::wstring> varName##NamesW = []                                                              \
    {                                                                                                                  \
        std::vector<std::wstring> v;                                                                                   \
        const char* libs[] = { __VA_ARGS__ };                                                                          \
        for (auto lib : libs)                                                                                          \
        {                                                                                                              \
            std::string narrow(lib);                                                                                   \
            std::wstring wide(narrow.begin(), narrow.end());                                                           \
            v.emplace_back(wide + L".dll");                                                                            \
            v.emplace_back(wide);                                                                                      \
        }                                                                                                              \
        return v;                                                                                                      \
    }();

inline std::vector<std::string> dllNames;
inline std::vector<std::wstring> dllNamesW;

//"rtsshooks64.dll", "rtsshooks64", "rtsshooks.dll", "rtsshooks",

// clang-format off
DEFINE_NAME_VECTORS(overlay, "eosovh-win32-shipping",
                             "eosovh-win64-shipping",   // Epic
                             "gameoverlayrenderer",
                             "gameoverlayrenderer64",     // Steam
                             "socialclubd3d12renderer", // Rockstar
                             "owutils",                 // Overwolf
                             "owclient",
                             "galaxy",
                             "galaxy64",                // GOG Galaxy
                             "discordhook",
                             "discordhook64",
                             "discordoverlay",
                             "discordoverlay64",        // Discord
                             "overlay",
                             "overlay64"                  // Ubisoft
);

DEFINE_NAME_VECTORS(blockOverlay, "eosovh-win32-shipping",
                                  "eosovh-win64-shipping",
                                  "gameoverlayrenderer",
                                  "gameoverlayrenderer64",
                                  "owclient",
                                  "galaxy",
                                  "galaxy64",
                                  "discordhook",
                                  "discordhook64",
                                  "discordoverlay",
                                  "discordoverlay64",
                                  "overlay",
                                  "overlay64"
);

inline std::vector<std::wstring> blockedDllNamesW = { L"windhawk.dll", L"mactype.dll", L"mactype64.dll" };

// clang-format on

DEFINE_NAME_VECTORS(dx12, "d3d12");
DEFINE_NAME_VECTORS(dx12agility, "d3d12core");
DEFINE_NAME_VECTORS(dxgi, "dxgi");

DEFINE_NAME_VECTORS(nvngx, "nvngx", "_nvngx");
DEFINE_NAME_VECTORS(nvngxDlss, "nvngx_dlss");
DEFINE_NAME_VECTORS(nvapi, "nvapi64");
DEFINE_NAME_VECTORS(slInterposer, "sl.interposer");
DEFINE_NAME_VECTORS(slDlssg, "sl.dlss_g");
DEFINE_NAME_VECTORS(slReflex, "sl.reflex");
DEFINE_NAME_VECTORS(slCommon, "sl.common");

inline static bool CompareFileName(std::string* first, std::string* second)
{
    if (first->size() < second->size())
        return false;

    auto start = first->size() - second->size();

    bool match = true;
    for (size_t j = 0; j < second->size(); ++j)
    {
        if (std::tolower(static_cast<unsigned char>((*first)[start + j])) !=
            std::tolower(static_cast<unsigned char>((*second)[j])))
        {
            return false;
        }
    }

    return true;
}

inline static bool CompareFileNameW(std::wstring* first, std::wstring* second)
{
    if (first->size() < second->size())
        return false;

    auto start = first->size() - second->size();

    bool match = true;
    for (size_t j = 0; j < second->size(); ++j)
    {
        if (std::towlower((*first)[start + j]) != std::towlower((*second)[j]))
        {
            return false;
        }
    }

    return true;
}

inline static bool CheckDllName(std::string* dllName, std::vector<std::string>* namesList)
{
    for (auto& name : *namesList)
    {
        if (CompareFileName(dllName, &name))
            return true;
    }

    return false;
}

inline static bool CheckDllNameW(std::wstring* dllName, std::vector<std::wstring>* namesList)
{
    for (auto& name : *namesList)
    {
        if (CompareFileNameW(dllName, &name))
            return true;
    }

    return false;
}

inline static HMODULE GetDllNameModule(std::vector<std::string>* namesList)
{
    for (size_t i = 0; i < namesList->size(); i++)
    {
        auto name = namesList->at(i);
        auto module = KernelBaseProxy::GetModuleHandleA_()(name.c_str());

        if (module != nullptr)
            return module;
    }

    return nullptr;
}

inline static HMODULE GetDllNameWModule(std::vector<std::wstring>* namesList)
{
    for (size_t i = 0; i < namesList->size(); i++)
    {
        auto name = namesList->at(i);
        auto module = KernelBaseProxy::GetModuleHandleW_()(name.c_str());

        if (module != nullptr)
            return module;
    }

    return nullptr;
}
