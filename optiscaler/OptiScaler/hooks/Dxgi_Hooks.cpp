#include "pch.h"
#include "Dxgi_Hooks.h"

#include "DxgiFactory_Hooks.h"

#include <proxies/Dxgi_Proxy.h>

#include "Hook_Utils.h"

static DxgiProxy::PFN_CreateDxgiFactory o_CreateDXGIFactory = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory1 o_CreateDXGIFactory1 = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory2 o_CreateDXGIFactory2 = nullptr;

#pragma intrinsic(_ReturnAddress)

VALIDATE_HOOK(hkCreateDXGIFactory, DxgiProxy::PFN_CreateDxgiFactory)
inline static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    HRESULT result;
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    result = o_CreateDXGIFactory(riid, ppFactory);
#else
    result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif

    State::EnableChecks(owner);

    if (result != S_OK)
        return result;

    IDXGIFactory* real = nullptr;

    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    DxgiFactoryHooks::HookToFactory(*ppFactory);

    return result;
}

VALIDATE_HOOK(hkCreateDXGIFactory1, DxgiProxy::PFN_CreateDxgiFactory1)
inline static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    HRESULT result;
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    result = o_CreateDXGIFactory1(riid, ppFactory);
#else
    result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(owner);

    if (result != S_OK)
        return result;

    IDXGIFactory1* real = nullptr;

    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    DxgiFactoryHooks::HookToFactory(*ppFactory);

    return result;
}

VALIDATE_HOOK(hkCreateDXGIFactory2, DxgiProxy::PFN_CreateDxgiFactory2)
inline static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    HRESULT result;
    auto owner = State::GetOwner();
    State::DisableChecks(owner, "dxgi");
#ifndef DXGI_DEBUG_ENABLED
    result = o_CreateDXGIFactory2(Flags, riid, ppFactory);
#else
    result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(owner);

    if (result != S_OK)
        return result;

    IDXGIFactory2* real = nullptr;

    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    DxgiFactoryHooks::HookToFactory(*ppFactory);

    return result;
}

void DxgiHooks::Hook()
{
    std::lock_guard<std::mutex> lock(hookMutex);

    // Without the overlay menu we don't need DXGI hooks
    if (!Config::Instance()->OverlayMenu.value_or_default())
        return;

    if (DxgiProxy::Module() == nullptr)
        return;

    LOG_DEBUG("");

    if (o_CreateDXGIFactory == nullptr)
        o_CreateDXGIFactory = DxgiProxy::Hook_CreateDxgiFactory(hkCreateDXGIFactory);

    if (o_CreateDXGIFactory1 == nullptr)
        o_CreateDXGIFactory1 = DxgiProxy::Hook_CreateDxgiFactory1(hkCreateDXGIFactory1);

    if (o_CreateDXGIFactory2 == nullptr)
        o_CreateDXGIFactory2 = DxgiProxy::Hook_CreateDxgiFactory2(hkCreateDXGIFactory2);
}
