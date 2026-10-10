#pragma once

#include "SysUtils.h"

#include <d3d12.h>

#include <string>

// Queue and command list hooks that let Neural Rendering follow the finished picture to submission.
class ResTrack_Dx12
{
  private:
    static void hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                      ID3D12CommandList* const* ppCommandLists);

    static void HookToQueue(ID3D12Device* InDevice);

    static bool CheckForRealObject(const std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

  public:
    static void HookLateNrQueue(ID3D12Device* device);
};
