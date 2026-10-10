#pragma once
#include "RenderingCapability.h"
namespace laminr {
inline const char* NvidiaUpscalerUnavailableReason(Evidence hardware, bool runtimeLocated) {
    if (hardware == Evidence::No) return "DLSS capability is not available on the selected rendering adapter";
    if (hardware == Evidence::Unknown) return "Rendering adapter capability is unknown";
    if (!runtimeLocated) return "Requested NVIDIA upscaler runtime was not located";
    return nullptr;
}
// Shared by inventory matching and standalone regression tests. Never choose front().
template<class Range, class Luid>
inline const typename Range::value_type* FindRenderingAdapter(const Range& adapters, Luid luid) {
    for (const auto& adapter : adapters)
        if (adapter.luid.HighPart == luid.HighPart && adapter.luid.LowPart == luid.LowPart) return &adapter;
    return nullptr;
}
}
