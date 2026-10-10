$ErrorActionPreference='Stop'
. "$PSScriptRoot/../installer/ownership.ps1"
. "$PSScriptRoot/../installer/library.ps1"
$root=Join-Path $env:TEMP ('laminr-state-'+[guid]::NewGuid().ToString('N'))
$game=Join-Path $root 'Xbox/Content'
$store=Join-Path $root 'Store'
New-Item -ItemType Directory -Path $game -Force|Out-Null
Set-Content (Join-Path $game 'FlightSimulator2024.exe') 'fixture'
Set-Content (Join-Path $game 'ReShade64.dll') 'fixture'
if((GetFusionInstallState $store (Split-Path $game)) -ne 'Graphics files detected - not tracked'){throw 'Parent folder did not detect untracked graphics files'}
$mp=ManifestPath $store $game
New-Item -ItemType Directory -Path (Split-Path $mp) -Force|Out-Null
WriteState @{Game=$game;Status='Installed';Files=@(@{Path='winmm.dll';SourcePath='winmm.dll'})} $mp
if((GetFusionInstallState $store $game) -notmatch 'incomplete'){throw 'Missing loader reported installed'}
[IO.File]::WriteAllBytes((Join-Path $game 'winmm.dll'),[byte[]]@())
if((GetFusionInstallState $store $game) -notmatch 'incomplete'){throw 'Empty loader reported installed'}
Set-Content (Join-Path $game 'winmm.dll') 'fixture'
if((GetFusionInstallState $store $game) -ne 'Installed'){throw 'Stored loader not detected'}
'PASS: parent-folder discovery and missing/empty loader states are accurate'
