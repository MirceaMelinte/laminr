$ErrorActionPreference='Stop'
. "$PSScriptRoot/../installer/ownership.ps1"
function AssertClosed($Game){}
$fixture=Join-Path $env:TEMP ('laminr-receipt-restore-'+[guid]::NewGuid().ToString('N'))
$game=Join-Path $fixture 'Game';$payload=Join-Path $fixture 'Payload';$store=Join-Path $fixture 'Store'
New-Item -ItemType Directory -Path $game,"$payload/laminrData" -Force|Out-Null
Set-Content "$payload/winmm.dll" 'loader'
Set-Content "$payload/laminrData/Effects-install.json" '[]'
@(Get-ChildItem $payload -Recurse -File|ForEach-Object {@{Path=$_.FullName.Substring($payload.Length+1);Hash=(HashFile $_.FullName)}})|ConvertTo-Json|Set-Content "$payload/files.json"
$mp=InstallFusion $game $payload $store "$fixture/setup.exe"
$m=Get-Content $mp -Raw|ConvertFrom-Json
$receipt=@($m.Files|Where-Object Path -like '*Effects-install.json')[0]
if(-not $receipt.Mutable){throw 'New receipt must be mutable'}
'PASS: new installations mark the FX receipt mutable'
# Reproduce the user's old manifest, then the installer's legitimate receipt update.
$receipt.Mutable=$false;WriteState $m $mp
Set-Content "$game/laminrData/Effects-install.json" '[{"Status":"Installed"}]'
Set-Content "$game/winmm.dll" 'unexpected binary change'
try{RestoreFusion $mp;throw 'Unexpected restore'}catch{if($_.Exception.Message -notlike '*file changed after installation: winmm.dll*'){throw}}
'PASS: modified binaries still block Restore'
Copy-Item -LiteralPath "$payload/winmm.dll" -Destination "$game/winmm.dll" -Force
Copy-Item -LiteralPath "$payload/winmm.dll" -Destination "$game/dxgi.dll" -Force
Set-Content "$game/version.dll" 'unrelated loader'
RestoreFusion $mp
if((Get-Content $mp -Raw|ConvertFrom-Json).Status -ne 'Restored'){throw 'Legacy receipt blocked Restore'}
'PASS: legacy manifest with changed FX receipt restores successfully'
if(Test-Path "$game/dxgi.dll"){throw 'Duplicate laminr loader remained'}
if((Get-Content "$game/version.dll") -ne 'unrelated loader'){throw 'Unrelated loader changed'}
'PASS: duplicate known loader removed; unrelated proxy kept'
Copy-Item -LiteralPath "$payload/winmm.dll" -Destination "$game/dxgi.dll" -Force
RestoreFusion $mp
if(Test-Path "$game/dxgi.dll"){throw 'Restored status skipped leftover loader'}
'PASS: already-restored installations recheck remaining known loaders'

# A bundled shader may have been edited by the user after installation.
$game2=Join-Path $fixture 'ChangedShaderGame';$payload2=Join-Path $fixture 'ShaderPayload'
New-Item -ItemType Directory -Path $game2,"$payload2/laminrData/Shaders/Custom" -Force|Out-Null
Set-Content "$payload2/winmm.dll" 'loader'
$guide='laminrData/Shaders/Custom/Edited.fx'
Set-Content "$payload2/$guide" 'original shader'
@(Get-ChildItem $payload2 -Recurse -File|ForEach-Object {@{Path=$_.FullName.Substring($payload2.Length+1);Hash=(HashFile $_.FullName)}})|ConvertTo-Json|Set-Content "$payload2/files.json"
function GetShaderRecoveryRoot { Join-Path $fixture 'Recovery' }
$mp2=InstallFusion $game2 $payload2 $store "$fixture/setup.exe"
Set-Content "$game2/$guide" 'edited shader'
$editedHash=HashFile "$game2/$guide"
RestoreFusion $mp2
$restored=Get-Content $mp2 -Raw|ConvertFrom-Json
if($restored.Status -ne 'Restored' -or (Test-Path "$game2/winmm.dll")){throw 'Changed shader blocked restore'}
$copy=Join-Path $restored.RecoveredShaders $guide
if((HashFile $copy) -ne $editedHash){throw 'Edited shader was not preserved'}
RestoreFusion $mp2
if((HashFile $copy) -ne $editedHash){throw 'Repeated restore removed recovery copy'}
'PASS: edited shader preserved outside app store; restore completes and repeat restore keeps it'
