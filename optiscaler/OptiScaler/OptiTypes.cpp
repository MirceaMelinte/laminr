#include "pch.h"

#include "OptiTypes.h"
#include <misc/IdentifyGpu.h>
#include <unordered_map>

std::string ApiUpscalerInputName(ApiUpscalerInput upscaler)
{
    switch (upscaler)
    {
    case ApiUpscalerInput::DLSS_DX12:
        return "DLSS";
    default:
        return "????";
    }
}

std::string UpscalerDisplayName(Upscaler upscaler, API api)
{
    switch (upscaler)
    {
    case Upscaler::DLSS:
        return "DLSS";

    case Upscaler::DLSSD:
        return "DLSSD";
    }

    return "????";
}

std::string UpscalerShortName(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::DLSS:
        return "DLSS";

    case Upscaler::DLSSD:
        return "DLSSD";
    }

    return "????";
}

// Converts enum to the string codes for config
std::string UpscalerToCode(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::DLSS:
        return "dlss";
    case Upscaler::DLSSD:
        return "dlssd";
    default: // Upscaler::Reset and unknown
        return "";
    }
}

// Converts string codes into enum for config
// DLSS is the only backend: codes from older configs (xess, fsr*, ffx, dlss_12) select DLSS.
Upscaler CodeToUpscaler(const std::string& code)
{
    static const std::unordered_map<std::string, Upscaler> mapping = {
        { "dlss", Upscaler::DLSS },
        { "dlssd", Upscaler::DLSSD },
    };

    auto it = mapping.find(code);
    return (it != mapping.end()) ? it->second : Upscaler::DLSS;
}

// Converts enum to the string codes for config
std::string SharpnessShaderToCode(SharpenShader sharpenShader)
{
    switch (sharpenShader)
    {
    case SharpenShader::RCAS:
        return "rcas";
    case SharpenShader::DepthAware:
        return "da";
    case SharpenShader::LocalContrastDepthAware:
        return "lcda";
    default:
        return "";
    }
}

// Converts string codes into enum for config
SharpenShader CodeToSharpnessShader(const std::string& code)
{
    static const std::unordered_map<std::string, SharpenShader> mapping = {
        { "rcas", SharpenShader::RCAS },
        { "da", SharpenShader::DepthAware },
        { "lcda", SharpenShader::LocalContrastDepthAware },
    };

    auto it = mapping.find(code);
    return (it != mapping.end()) ? it->second : SharpenShader::RCAS;
}
