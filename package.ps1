$ErrorActionPreference='Stop'
$root=$PSScriptRoot;$payload=Join-Path $root 'installer/PayloadFusion'
. "$root/build-identity.ps1"
. "$root/package-staging.ps1"
$native=AssertLaminrCandidateBuild $root
$payload=NewLaminrPackageStaging $root
$buildVersion='0.0.1';$buildChannel='stable'
Copy-Item -LiteralPath "$root/optiscaler/x64/Release/OptiScaler.dll" -Destination "$payload/winmm.dll" -Force
Copy-Item -LiteralPath "$root/reshade/bin/x64/Release/ReShade64.dll" -Destination "$payload/ReShade64.dll" -Force
CopyLaminrPackageInputs $root Effects "$payload/laminrData"
New-Item -ItemType Directory -Path $payload,(Join-Path $payload 'laminrData/Presets'),(Join-Path $payload 'laminrData/Shaders'),(Join-Path $payload 'laminrData/Textures'),(Join-Path $payload 'laminrData/Cache'),(Join-Path $payload 'laminrData/Licenses'),(Join-Path $payload 'laminrData/Engine') -Force|Out-Null
Copy-Item "$root/optiscaler/x64/Release/OptiScaler.dll" "$payload/winmm.dll" -Force
Copy-Item "$root/reshade/bin/x64/Release/ReShade64.dll" $payload -Force
Copy-Item "$root/optiscaler/x64/Release/a/nvngx.dll_dlssnr.dll" $payload -Force
Copy-Item "$root/optiscaler/LICENSE" "$payload/laminrData/Licenses/OptiScaler-GPL3.txt" -Force
foreach($notice in @('LICENSE','NOTICE.md')){Copy-Item (Join-Path $root $notice) (Join-Path "$payload/laminrData/Licenses" $notice) -Force}
New-Item -ItemType Directory -Path "$payload/laminrData/Licenses/LICENSES" -Force|Out-Null
foreach($notice in @('GPL-3.0.txt','ReShade-BSD-3-Clause.txt')){Copy-Item (Join-Path "$root/LICENSES" $notice) (Join-Path "$payload/laminrData/Licenses/LICENSES" $notice) -Force}
Copy-Item -LiteralPath "$root/LICENSES/ThirdParty" -Destination "$payload/laminrData/Licenses/LICENSES/ThirdParty" -Recurse -Force
Copy-Item "$root/reshade/LICENSE.md" "$payload/laminrData/Licenses/ReShade-BSD3.txt" -Force
CopyLaminrPackageInputs $root Notices "$payload/laminrData/Licenses"
@'
[Libraries]
OptiDllPath=laminrData/Engine
[Upscalers]
Dx12Upscaler=dlss
[Plugins]
LoadReShade=true
[Menu]
OverlayMenu=true
ShowFps=false
[Hotfix]
CheckForUpdate=false
[Log]
LogToFile=true
LogFileName=laminrData/Performance.log
LogLevel=2
LogAsync=true
[DlssNr]
Enabled=false
Passes=1
RunBeforeSR=true
'@ | Set-Content "$payload/laminr.ini" -Encoding ASCII
@'
[GENERAL]
EffectSearchPaths=.\laminrData\Shaders\**
TextureSearchPaths=.\laminrData\Textures\**
PresetPath=.\laminrData\Presets\My look.ini
IntermediateCachePath=.\laminrData\Cache
ScreenshotPath=.\laminr Snapshots
PerformanceMode=0
SkipLoadingDisabledEffects=1
[OVERLAY]
ShowSplash=0
TutorialProgress=4
[SCREENSHOT]
SavePath=.\laminr Snapshots
SaveBeforeShot=0
SaveOverlayShot=0
KeyScreenshot=0,0,0,0
'@ | Set-Content "$payload/ReShade.ini" -Encoding ASCII
"Techniques=`r`nTechniqueSorting=" | Set-Content "$payload/laminrData/Presets/My look.ini" -Encoding ASCII
foreach($dir in @('Shaders','Textures','Cache')){'laminr managed folder'|Set-Content "$payload/laminrData/$dir/.keep"}
New-Item -ItemType Directory -Path "$payload/laminrData/Tools" -Force|Out-Null
Copy-Item "$root/installer/store-paths.ps1" "$payload/laminrData/Tools/store-paths.ps1" -Force
Copy-Item "$root/installer/import-effects.ps1" "$payload/laminrData/Tools/import-effects.ps1" -Force
Copy-Item "$root/installer/EffectPackages.ini" "$payload/laminrData/Tools/EffectPackages.ini" -Force
New-Item -ItemType Directory -Path "$payload/laminrData/Tools/StandardHeaders" -Force|Out-Null
foreach($name in @('ReShade.fxh','ReShadeUI.fxh')){Copy-Item -LiteralPath "$root/installer/DefaultEffects/Shaders/Packages/00/$name" -Destination "$payload/laminrData/Tools/StandardHeaders" -Force}
# Fail closed on local evidence accidentally left in the embedded payload.
$private=@(Get-ChildItem -LiteralPath $payload -File -Recurse|Where-Object {$_.Name -match '(?i)(diagnostics-|Import-result-|\.dmp$|\.log$|Codex_|Licensing-review-|Ownership-licensing-audit|Release-.*draft|test-results|test-notes)'})
if($private.Count){throw ('Private/debug files found in package staging: '+($private.Name -join ', '))}
@{Version=$buildVersion;Channel=$buildChannel;SourceHead=$native.SourceHead;SourceSHA256=$native.SourceSHA256;BuiltUtc=$native.BuiltUtc;NativeBinaries=$native.Binaries}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$payload/laminrData/BuildIdentity.json" -Encoding UTF8
$files=@(Get-ChildItem $payload -File -Recurse|Where-Object {$_.FullName -ne (Join-Path $payload 'files.json')}|ForEach-Object {[pscustomobject]@{Path=$_.FullName.Substring($payload.Length+1);Hash=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
$files|ConvertTo-Json|Set-Content "$payload/files.json" -Encoding UTF8
$preview=Join-Path $root 'dist'
New-Item -ItemType Directory -Path $preview -Force|Out-Null
Push-Location "$root/installer"
try{
 & go test -count=1 -v .
 if($LASTEXITCODE){throw 'Embedded payload verification failed. Installer was not built.'}
 & go build -trimpath -ldflags '-H=windowsgui -s -w' -o "$preview/laminr_Version_0.0.1.exe" .
 if($LASTEXITCODE){throw 'Installer build failed.'}
}finally{Pop-Location}
TestLaminrFinalPackage "$preview/laminr_Version_0.0.1.exe" $root $buildChannel $buildVersion
Write-Output "Built: $preview"



