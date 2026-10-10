# File discovery is evidence of a possible input, never proof of rendered support.
function GetNeuralRuntimeStatus([string]$Game,$Gpu){
 $cards=@($Gpu.Names -split ',\s*'|Where-Object {$_ -match '(?i)NVIDIA.*\bRTX\s*50\d\d'})
 if(-not $cards.Count){
  return [pscustomobject]@{Family='Unsupported';Expected='Not applicable';Path='';Version='';SHA256='';State='Unsupported on this hardware';Message='Neural Rendering in this build needs an NVIDIA RTX 50-series GPU.';DriverAssessment='No NVIDIA runtime is installed on this hardware.'}
 }
 $family=if($cards.Count -ne 1){'Multiple RTX 50 GPUs'}else{'RTX 50'}
 $expected='Original verified RTX 50 model'
 $file=Join-Path $Game 'nvngx_dlssnr.dll';$state='Missing';$version='Unknown';$hash=''
 $message='Model missing beside the game EXE. Add nvngx_dlssnr.dll; nvngx.dll_dlssnr.dll is a different helper and does not replace it.'
 if(Test-Path -LiteralPath $file -PathType Leaf){
  try{
   $hash=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
   $version=[Diagnostics.FileVersionInfo]::GetVersionInfo($file).FileVersion
   if($hash -ne 'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'){$state='Unverified';$message='Model found, but its SHA256 is not the RTX 50 runtime verified by this installer. Version alone does not establish compatibility.'}
   elseif($family -ne 'RTX 50'){$state='GPU selection required';$message='Known model found, but the active RTX GPU cannot be determined.'}
   elseif((Get-AuthenticodeSignature -LiteralPath $file).Status -ne 'Valid'){$state='Signature failed';$message='Original model signature verification failed.'}
   else{$state='Verified file';$message='Known model found for this GPU family. Loading and rendering still require an in-game check.'}
  }catch{$state='Unreadable';$message='Model exists but could not be inspected. Check file access permissions.'}
 }else{
  foreach($relative in @('laminrData/Engine/nvngx_dlssnr.dll','laminrData/nvngx_dlssnr.dll','streamline/nvngx_dlssnr.dll')){
   $other=Join-Path $Game $relative
   if(Test-Path -LiteralPath $other -PathType Leaf){$state='Alternate location';$message='Model found at '+$other+'. Use Add NVIDIA runtime to validate and place it beside the selected game EXE. This does not confirm the game loads it.';break}
  }
 }
 [pscustomobject]@{Family=$family;Expected=$expected;Path=$file;Version=$version;SHA256=$hash;State=$state;Message=$message;DriverAssessment='Minimum driver requirement for this model is unverified; driver compatibility is not confirmed.'}
}
function GetFusionCompatibility([string]$Game,[string]$Exe,$Gpu,[string]$Launcher=''){
 $rtx=[bool]($Gpu.Known -and $Gpu.Names -match '(?i)NVIDIA[^,]*\bRTX\s*50\d\d')
 $hits=New-Object 'System.Collections.Generic.List[object]'
 $pending=New-Object 'System.Collections.Generic.Queue[object]'
 $complete=$true;$visited=0
 if(Test-Path -LiteralPath $Game -PathType Container){$pending.Enqueue(@([IO.Path]::GetFullPath($Game),0))}else{$complete=$false}
 while($pending.Count -and $visited -lt 400){
  $item=$pending.Dequeue();$visited++
  try{$entries=@(Get-ChildItem -LiteralPath $item[0] -ErrorAction Stop)}catch{$complete=$false;continue}
  foreach($entry in $entries){
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
   if($entry.PSIsContainer){
    if($entry.Name -match '^(laminrData|Redist|__Installer|Support|Extras|\.git)$'){continue}
    if($item[1] -lt 6){$pending.Enqueue(@($entry.FullName,($item[1]+1)))}else{$complete=$false}
   }elseif($entry.Name -match '^nvngx_dlss(d|g)?\.dll$'){
    $kind=if($entry.Name -eq 'nvngx_dlss.dll'){'DLSS'}else{'Companion only'}
    $version=[Diagnostics.FileVersionInfo]::GetVersionInfo($entry.FullName).FileVersion
    $hits.Add([pscustomobject]@{File=$entry.FullName;Kind=$kind;Version=if($version){$version}else{'Unknown'}})
   }
  }
 }
 if($pending.Count){$complete=$false}
 $inputs=@($hits|Where-Object Kind -ne 'Companion only')
 $msfs=([IO.Path]::GetFileName($Exe) -eq 'FlightSimulator2024.exe' -or ([IO.Path]::GetFileName($Exe) -eq 'gamelaunchhelper.exe' -and (Test-Path -LiteralPath (Join-Path (Split-Path $Exe) 'FlightSimulator2024.exe'))))
 $possible=$msfs -or $inputs.Count -gt 0
 $addons=@()
 if($Exe){$addons=@(Get-ChildItem -LiteralPath (Split-Path $Exe) -Filter '*.addon64' -ErrorAction SilentlyContinue|Where-Object Name -match '(?i)dlss5|renodx')}
 # The selected executable is authoritative; folder names must not override a Steam copy.
 $proxy=if([IO.Path]::GetFileName($Exe) -eq 'gamelaunchhelper.exe'){'winmm.dll'}else{'dxgi.dll'}
 $performance=if($possible){'Possible DLSS connection found. Select DLSS in the simulator; this check cannot confirm it is running.'}else{'No DLSS connection found. Image effects can be tried; upscaling is unconfirmed.'}
 $neural=if(-not $Gpu.Known){'DLSS neural rendering: graphics card unknown. NVIDIA downloads are skipped.'}elseif(-not $rtx){'DLSS neural rendering needs an NVIDIA RTX 50-series GPU.'}elseif(-not $possible){'DLSS neural rendering: no compatible game connection identified. NVIDIA downloads are skipped.'}else{'DLSS neural rendering needs the matching RTX 50 model and a supported rendering path. Files alone do not enable it; it stays off by default.'}
 if($Gpu.Names -match ',' -and $rtx){$neural+=' More than one GPU was detected; ensure the game actually uses the RTX card.'}
 [pscustomobject]@{Schema=1;Game=$Game;Executable=$Exe;Gpu=$Gpu.Names;Proxy=$proxy;InputEvidence=@($hits.ToArray());ExternalAddons=@($addons|ForEach-Object Name);ScanComplete=$complete;PossibleInput=[bool]$possible;DownloadNvidia=[bool]($rtx -and $possible);Performance=$performance;NeuralRendering=$neural;RuntimeState='Not tested in game';Checked=(Get-Date -Format o)}
}
function FormatFusionCompatibility($Plan){
 $lines=@(('GPU: '+$Plan.Gpu),('Performance: '+$Plan.Performance),$Plan.NeuralRendering)
 if($Plan.PSObject.Properties['DriverVersions']){foreach($driver in $Plan.DriverVersions){$lines+=('Driver: '+$driver.Name+' - '+$driver.Version+' (Windows: '+$driver.WindowsVersion+')')}}
 if($Plan.PSObject.Properties['NeuralRuntime']){$nr=$Plan.NeuralRuntime;$lines+=@(('Required model: '+$nr.Expected),('Model status: '+$nr.State+' - '+$nr.Message),('Checked location: '+$nr.Path),('Model file version: '+$nr.Version),$nr.DriverAssessment)}
 if($Plan.DownloadNvidia){$lines+='Automatic downloads: DLSS 310.9.1 / Streamline 2.14.1, plus the RTX 50 neural-rendering model, checked by verified hash.'}
 if(-not $Plan.ScanComplete){$lines+='The file check was limited or some folders could not be read.'}
 if($Plan.ExternalAddons.Count){$lines+=('Another DLSS/ReShade add-on setup was found: '+($Plan.ExternalAddons -join ', ')+'. Install will offer to back up and replace it; Restore puts it back.')}
 foreach($hit in $Plan.InputEvidence){$lines+=([IO.Path]::GetFileName($hit.File)+' - found version '+$hit.Version+' (not proof it is used)')}
 $lines -join "`n"
}
function GetFusionInstalledVersions([string]$Game){
 # Installed files are reported separately; they must not count as native game support.
 foreach($relative in @('nvngx_dlssnr.dll','laminrData/Engine/nvngx_dlss.dll','laminrData/Engine/nvngx_dlssd.dll','laminrData/Engine/nvngx_dlssg.dll','laminrData/Engine/streamline/sl.interposer.dll')){
  $file=Join-Path $Game $relative
  if(Test-Path -LiteralPath $file -PathType Leaf){
   $version=[Diagnostics.FileVersionInfo]::GetVersionInfo($file).FileVersion
   [pscustomobject]@{File=$relative;Version=if($version){$version}else{'Unknown'};State='Stored file; not proof it is active'}
  }
 }
}
function SaveFusionCompatibility([string]$Game,$Plan){
 $file=OwnedPath $Game 'laminrData/Compatibility.json'
 $Plan|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $file -Encoding UTF8
}
