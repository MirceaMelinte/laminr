#pragma once

#include <hooks/Reflex_Hooks.h>

#include "NvApiTypes.h"

class NvApiHooks
{
  public:
    inline static PFN_NvApi_QueryInterface o_NvAPI_QueryInterface = nullptr;
    inline static decltype(&NvAPI_DRS_GetSetting) o_NvAPI_DRS_GetSetting = nullptr;

    // inline static UINT64 qiCounter = 0;

    static NvAPI_Status __stdcall hkNvAPI_DRS_GetSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                                         NvU32 settingId, NVDRS_SETTING* pSetting);
    static void* __stdcall hkNvAPI_QueryInterface(unsigned int InterfaceId);
    static void Hook(HMODULE nvapiModule);
    static void Unhook();
};
