#include "pch.h"

#include <dlssnr/DlssNr.h>

#include "ResTrack_dx12.h"

#include <detours/detours.h>

#include <mutex>

#ifndef STDMETHODCALLTYPE
#include <Unknwn.h> // or <objbase.h> to get STDMETHODCALLTYPE
#endif

typedef void(STDMETHODCALLTYPE* PFN_ExecuteCommandLists)(ID3D12CommandQueue* This, UINT NumCommandLists,
                                                         ID3D12CommandList* const* ppCommandLists);

using PFN_LateReset = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
static PFN_LateReset o_LateReset = nullptr;
static HRESULT STDMETHODCALLTYPE hkLateReset(ID3D12GraphicsCommandList* cmd, ID3D12CommandAllocator* allocator,
                                             ID3D12PipelineState* pipeline)
{
    const auto result = o_LateReset(cmd, allocator, pipeline);
    if (SUCCEEDED(result)) DlssNr::FinishedPictureResetCommandList(cmd);
    return result;
}

static PFN_ExecuteCommandLists o_ExecuteCommandLists = nullptr;

inline static IID streamlineRiid {};
inline static std::once_flag streamlineRiidInitFlag;

bool ResTrack_Dx12::CheckForRealObject(const std::string functionName, IUnknown* pObject, IUnknown** ppRealObject)
{
    std::call_once(streamlineRiidInitFlag,
                   []() { IIDFromString(L"{ADEC44E2-61F0-45C3-AD9F-1B37379284FF}", &streamlineRiid); });

    auto qResult = pObject->QueryInterface(streamlineRiid, (void**) ppRealObject);

    if (qResult == S_OK && *ppRealObject != nullptr)
    {
        LOG_INFO("{} Streamline proxy found!", functionName);
        (*ppRealObject)->Release();
        return true;
    }

    return false;
}

void ResTrack_Dx12::hkExecuteCommandLists(ID3D12CommandQueue* This, UINT NumCommandLists,
                                          ID3D12CommandList* const* ppCommandLists)
{
    o_ExecuteCommandLists(This, NumCommandLists, ppCommandLists);
    DlssNr::FinishedPictureSubmitted(This, NumCommandLists, ppCommandLists);
}

void ResTrack_Dx12::HookLateNrQueue(ID3D12Device* device)
{
    static std::mutex hookMutex;
    std::lock_guard<std::mutex> lock(hookMutex);
    HookToQueue(device);
    if (o_LateReset) return;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* cmd = nullptr;
    if (SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    {
        if (SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&cmd))))
        {
            ID3D12GraphicsCommandList* real = nullptr;
            if (!CheckForRealObject(__FUNCTION__, cmd, (IUnknown**)&real)) real = cmd;
            o_LateReset = (PFN_LateReset)(*(void***)real)[10];
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&)o_LateReset, hkLateReset);
            if (DetourTransactionCommit() != NO_ERROR) o_LateReset = nullptr;
            cmd->Close();
            cmd->Release();
        }
        allocator->Release();
    }
}

void ResTrack_Dx12::HookToQueue(ID3D12Device* InDevice)
{
    if (o_ExecuteCommandLists != nullptr)
        return;

    ID3D12CommandQueue* queue = nullptr;
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;

    auto hr = InDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));

    if (hr == S_OK)
    {
        ID3D12CommandQueue* realQueue = nullptr;
        if (!CheckForRealObject(__FUNCTION__, queue, (IUnknown**) &realQueue))
            realQueue = queue;

        // Get the vtable pointer
        PVOID* pVTable = *(PVOID**) realQueue;

        o_ExecuteCommandLists = (PFN_ExecuteCommandLists) pVTable[10];

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_ExecuteCommandLists != nullptr)
            DetourAttach(&(PVOID&) o_ExecuteCommandLists, hkExecuteCommandLists);

        auto detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("Failed to hook CommandList methods: {:X}", detourResult);
            o_ExecuteCommandLists = nullptr;
        }

        queue->Release();
    }
}
