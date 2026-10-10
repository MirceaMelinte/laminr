@echo off
set "LAMINR_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%LAMINR_VSWHERE%" (
 echo Install Visual Studio 2022 Build Tools with Desktop development with C++.
 exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%LAMINR_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LAMINR_VS=%%i"
if not defined LAMINR_VS exit /b 1
call "%LAMINR_VS%\VC\Auxiliary\Build\vcvars64.bat"
