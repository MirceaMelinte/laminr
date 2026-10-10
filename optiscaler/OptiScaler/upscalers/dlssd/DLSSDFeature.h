#pragma once

#include "SysUtils.h"
#include <proxies/NVNGX_Proxy.h>
#include <upscalers/IFeature.h>

class DLSSDFeature : public virtual IFeature
{
  private:
    feature_version _version = { 0, 0, 0 };

  protected:
    NVSDK_NGX_Handle _dlssdHandle = {};
    NVSDK_NGX_Handle* _p_dlssdHandle = nullptr;
    // Set once NVNGXProxy::InitDx12 has run; only skips the deliberate delay between init and feature creation.
    inline static bool _dlssdInitedDx12 = false;

    void ProcessEvaluateParams(NVSDK_NGX_Parameter* InParameters);
    void ProcessInitParams(NVSDK_NGX_Parameter* InParameters);
    void ReadVersion();

    float GetSharpness(const NVSDK_NGX_Parameter* InParameters);

  public:
    feature_version Version() override { return feature_version { _version.major, _version.minor, _version.patch }; }
    Upscaler GetUpscalerType() const override { return Upscaler::DLSSD; }

    DLSSDFeature(unsigned int handleId, NVSDK_NGX_Parameter* InParameters);

    ~DLSSDFeature();
};
