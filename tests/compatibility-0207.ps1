$ErrorActionPreference='Stop'
. "$PSScriptRoot/../installer/ownership.ps1"
. "$PSScriptRoot/../installer/library.ps1"
. "$PSScriptRoot/../installer/compatibility.ps1"
. "$PSScriptRoot/../installer/neural-download.ps1"
$root=Join-Path $env:TEMP ('laminr-0207-'+[guid]::NewGuid().ToString('N'))
$steam=Join-Path $root 'Steam';$xbox=Join-Path $root 'Xbox';$old=Join-Path $root '2020';$store=Join-Path $root 'Store'
New-Item -ItemType Directory -Path $steam,$xbox,$old -Force|Out-Null
$binary="$PSScriptRoot/../installer/FusionSetup.exe"
Copy-Item $binary "$steam/FlightSimulator2024.exe"
Copy-Item $binary "$xbox/FlightSimulator2024.exe"
Copy-Item $binary "$old/FlightSimulator.exe"
function Check($test,$label){if(-not $test){throw $label};"PASS: $label"}
$rtx50=[pscustomobject]@{Known=$true;Nvidia=$true;Names='NVIDIA GeForce RTX 5080'}
$rtx30=[pscustomobject]@{Known=$true;Nvidia=$true;Names='NVIDIA GeForce RTX 3080'}

# Only MSFS 2024 on an RTX 50-series card is a supported target.
AssertMsfsNvidiaTarget "$steam/FlightSimulator2024.exe" $rtx50
$rejected=$false;try{AssertMsfsNvidiaTarget "$steam/FlightSimulator2024.exe" $rtx30}catch{$rejected=$_.Exception.Message -like '*RTX 50-series*'}
Check $rejected 'Older RTX cards are refused'
$rejected=$false;try{AssertMsfsNvidiaTarget "$old/FlightSimulator.exe" $rtx50}catch{$rejected=$_.Exception.Message -like '*2024 only*'}
Check $rejected 'MSFS 2020 executable is refused'
$rejected=$false;try{FindFusionExecutable $old 'Steam'}catch{$rejected=$_.Exception.Message -like '*No supported simulator executable*'}
Check $rejected 'A 2020-only folder has no supported executable'
Check ((GetMsfsTitle $steam) -eq 'Microsoft Flight Simulator 2024') '2024 identity'
Check (-not (GetMsfsTitle $old)) '2020 folder is not a supported simulator'
Check ((ManifestPath $store $steam) -ne (ManifestPath $store $xbox)) 'Separate installation receipts'

# Steam loads through dxgi.dll; Xbox loads through winmm.dll beside gamelaunchhelper.exe.
$exe=@(FindFusionExecutable $steam 'Steam')[0].Path
Check ($exe -eq (Join-Path $steam 'FlightSimulator2024.exe')) 'Steam resolves the game EXE'
$plan=GetFusionCompatibility $steam $exe $rtx50 'Steam'
Check ($plan.PossibleInput -and $plan.DownloadNvidia -and $plan.Proxy -eq 'dxgi.dll') 'Steam loader and NVIDIA downloads'
Check (-not (GetFusionCompatibility $steam $exe $rtx30 'Steam').DownloadNvidia) 'No NVIDIA downloads for older RTX cards'
$missing=$false;try{FindFusionExecutable $xbox 'Xbox'}catch{$missing=$_.Exception.Message -like '*launcher is missing*'}
Check $missing 'Xbox without its launch helper asks for a game repair'
Copy-Item $binary "$xbox/gamelaunchhelper.exe"
$exe=@(FindFusionExecutable $xbox 'Xbox')[0].Path
Check ($exe -like '*gamelaunchhelper.exe') 'Xbox uses the launch helper'
Check ((GetFusionCompatibility $xbox $exe $rtx50 'Xbox').Proxy -eq 'winmm.dll') 'Xbox loader'

# The neural model needs exactly one RTX 50-series card.
Check ((GetNeuralDownload $rtx50).Family -eq 'RTX 50') 'RTX 50 selects the pinned model'
foreach($family in @('2060','3080','4070')){
 $rejected=$false;try{GetNeuralDownload ([pscustomobject]@{Names='NVIDIA GeForce RTX '+$family})}catch{$rejected=$_.Exception.Message -like '*exactly one*'}
 Check $rejected "RTX $family gets no neural model"
}
$rejected=$false;try{GetNeuralDownload ([pscustomobject]@{Names='NVIDIA GeForce RTX 5080, NVIDIA GeForce RTX 5090'})}catch{$rejected=$true}
Check $rejected 'Two RTX 50 cards are ambiguous'
