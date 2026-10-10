$ErrorActionPreference='Stop'
. "$PSScriptRoot/../installer/ownership.ps1"
. "$PSScriptRoot/../installer/recovery.ps1"
function AssertClosed($Game){}
function Check($ok,$message){if(-not $ok){throw $message};"PASS: $message"}
$fixture=Join-Path $env:TEMP ('laminr-own-ini-'+[guid]::NewGuid().ToString('N'))
$game=Join-Path $fixture 'Game';$payload=Join-Path $fixture 'Payload';$store=Join-Path $fixture 'Store'
New-Item -ItemType Directory -Path $game,"$payload/laminrData/Shaders","$payload/laminrData/Presets" -Force|Out-Null
Set-Content "$payload/winmm.dll" 'fixture loader'
Set-Content "$payload/laminr.ini" '[Menu]'
Set-Content "$payload/laminrData/Shaders/Unneeded.fx" 'unneeded'
Set-Content "$payload/laminrData/Presets/My look.ini" 'Techniques='
@(Get-ChildItem $payload -Recurse -File|ForEach-Object {@{Path=$_.FullName.Substring($payload.Length+1);Hash=(HashFile $_.FullName)}})|ConvertTo-Json|Set-Content "$payload/files.json"
$mp=InstallFusion $game $payload $store "$fixture/Manager.exe" -IncludeEffects $false
Check (-not(Test-Path "$game/laminrData/Shaders/Unneeded.fx")) 'INI-only core install excludes unrelated bundled shaders'
Check (Test-Path "$game/laminrData/Presets/My look.ini") 'Own-INI install includes the bundled default look'
Set-Content "$game/laminrData/Presets/My look.ini" 'user-edited look'
$ini=Join-Path $fixture 'User look.ini';$catalogue=Join-Path $fixture 'Packages.ini'
Set-Content $ini "Techniques=Colour@Needed.fx`n[Needed.fx]`nStrength=0.5"
Set-Content $catalogue "[test]`nEffectFiles=Needed.fx,Unneeded.fx`nDownloadUrl=https://github.com/example/test/archive/main.zip"
$selected=GetPresetPackages @(GetMissingPresetShaders $ini $game) $catalogue
Check ($selected.Count -eq 1 -and $selected['test'].Shaders.Count -eq 1 -and $selected['test'].Shaders[0] -eq 'Needed.fx') 'Dependency plan selects only the referenced FX'
New-Item -ItemType Directory -Path "$game/laminrData/Shaders" -Force|Out-Null
Set-Content "$game/laminrData/Shaders/Needed.fx" 'existing effect'
$relative=SaveOwnPreset $ini $game $catalogue
InstallPresetDependencies (OwnedPath $game $relative) $game $catalogue
Check ((HashFile $ini) -eq (HashFile (OwnedPath $game $relative))) 'Imported preset is preserved exactly'
Check (-not(Test-Path "$game/laminrData/Shaders/Unneeded.fx")) 'Existing dependency satisfies preset without full FX download'
Set-Content $ini 'Techniques=LegacyUnqualified'
$rejected=$false;try{TestOwnPreset $ini $game $catalogue}catch{$rejected=$true}
Check (-not $rejected) 'Legacy preset can be saved without guessing shader downloads'
Set-Content $ini "Techniques=Private@Unavailable.fx"
$rejected=$false;try{TestOwnPreset $ini $game $catalogue}catch{$rejected=$true}
Check (-not $rejected) 'Custom FX do not block saving the preset'
$customRelative=SaveOwnPreset $ini $game $catalogue
$warning=InstallPresetDependencies (OwnedPath $game $customRelative) $game $catalogue
Check ($warning -like '*preset has been installed*Unavailable.fx*author*') 'Unavailable FX produce an actionable warning while keeping the preset'
$mp=InstallFusion $game $payload $store "$fixture/Manager.exe" -ReplaceMods @(FindFusionConflicts $game) -ReplaceExisting $true -PreserveConfiguration $true
Check (-not(Test-Path "$game/laminrData/Shaders/Unneeded.fx") -and (Test-Path (OwnedPath $game $relative))) 'Repair retains preset-only choice and imported look'

Check ((Get-Content "$game/laminrData/Presets/My look.ini" -Raw).Trim() -eq 'user-edited look') 'Repair preserves edited bundled look'
$fullGame=Join-Path $fixture 'FullGame';New-Item -ItemType Directory -Path $fullGame|Out-Null
$fullManifest=InstallFusion $fullGame $payload $store "$fixture/Manager.exe" -IncludeEffects $true
Check (Test-Path "$fullGame/laminrData/Shaders/Unneeded.fx") 'Full FX install includes the bundled shaders'
Set-Content $ini "Techniques=Colour@Needed.fx`nTechniqueSorting=Colour@Needed.fx,Slow@SlowSharp.fx,MiAO@MiAO.fx`n[Unavailable.fx]`nValue=1"
Check (@(GetPresetShaderNames $ini).Count -eq 1 -and (GetPresetShaderNames $ini) -eq 'Needed.fx') 'Disabled ordering and unused parameter sections are not required dependencies'
TestOwnPreset $ini $game $catalogue
Set-Content $ini "Techniques=`nTechniqueSorting=Old@Unavailable.fx`n[Unavailable.fx]"
Check (@(GetPresetShaderNames $ini).Count -eq 0) 'Empty look does not require any disabled effects'

Add-Content $catalogue "`n[duplicate]`nEffectFiles=Unneeded.fx"
$known=GetPresetPackages @('Needed.fx','Unneeded.fx','Private.fx') $catalogue -AllowMissing
Check ($known.Count -eq 1 -and $known['test'].Shaders.Count -eq 1) 'Unknown and ambiguous FX do not block uniquely identified downloads'
