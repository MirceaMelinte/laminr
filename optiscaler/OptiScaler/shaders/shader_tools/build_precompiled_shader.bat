@echo off

if "%~1"=="" (
    echo Usage: %~nx0 ShaderName
    exit /b 1
)

set ShaderName=%1

echo Creating Dx12 CSO
"%~dp0dxc.exe" -T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect "%ShaderName%.hlsl" -Fo "%ShaderName%_Shader.cso"

echo Creating Dx12 Header
python "%~dp0create_header.py" "%ShaderName%_Shader.cso" "%ShaderName%_Shader.h" %ShaderName%_cso