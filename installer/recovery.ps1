. "$PSScriptRoot/import-effects.ps1"
function TestOwnPreset([string]$Preset,[string]$Game,[string]$Catalogue){
 if([IO.Path]::GetExtension($Preset) -ne '.ini' -or -not(Test-Path -LiteralPath $Preset -PathType Leaf)){throw 'Choose an INI preset first.'}
 $names=@(GetPresetShaderNames $Preset)
 $text=[IO.File]::ReadAllText($Preset)
 if($text -notmatch '(?im)^Techniques='){throw 'This INI is not a ReShade look preset (Techniques is missing).'}
 # Missing/custom shaders must not prevent saving a valid preset.
 # Dependency installation reports unavailable files after installing known FX.
}
function SaveOwnPreset([string]$Preset,[string]$Game,[string]$Catalogue){
 AssertClosed $Game;TestOwnPreset $Preset $Game $Catalogue
 $relative='laminrData/Presets/Imported-'+[guid]::NewGuid().ToString('N')+'/'+[IO.Path]::GetFileName($Preset)
 $dest=OwnedPath $Game $relative;New-Item -ItemType Directory -Path (Split-Path $dest) -Force|Out-Null
 [IO.File]::Copy($Preset,$dest,$false)
 return $relative
}
function ResetLaminrSettings([string]$Game,[string]$Payload,[string]$Store){
 AssertClosed $Game
 $mp=ManifestPath $Store $Game
 if(-not(Test-Path -LiteralPath $mp)){throw 'No recorded laminr installation for this folder.'}
 $m=ReadLaminrState $mp
 if($m.Status -ne 'Installed'){throw 'Install laminr first.'}
 $backup=Join-Path (Split-Path $mp) ('Settings-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $backup|Out-Null
 foreach($name in @('laminr.ini','ReShade.ini')){ $dest=OwnedPath $Game $name;if(Test-Path -LiteralPath $dest){Copy-Item -LiteralPath $dest -Destination (Join-Path $backup $name)} }
 try{
  foreach($name in @('laminr.ini','ReShade.ini')){Copy-Item -LiteralPath (Join-Path $Payload $name) -Destination (OwnedPath $Game $name) -Force}
  $look=OwnedPath $Game 'laminrData/Presets/laminr recovery.ini'
  if(Test-Path -LiteralPath $look){$look=OwnedPath $Game ('laminrData/Presets/laminr recovery-'+[guid]::NewGuid().ToString('N')+'.ini')}
  "Techniques=`r`nTechniqueSorting="|Set-Content -LiteralPath $look -Encoding UTF8
  $config=OwnedPath $Game 'ReShade.ini';$text=Get-Content -LiteralPath $config -Raw
  $text=$text -replace '(?m)^PresetPath=.*$',('PresetPath=.\'+$look.Substring((FullPath $Game).Length+1))
  [IO.File]::WriteAllText($config,$text,[Text.UTF8Encoding]::new($false))
 }catch{foreach($name in @('laminr.ini','ReShade.ini')){if(Test-Path -LiteralPath (Join-Path $backup $name)){Copy-Item -LiteralPath (Join-Path $backup $name) -Destination (OwnedPath $Game $name) -Force}};throw}
 return $backup
}
function ResetLaminrDownloadCache([string]$Store){
 # Park only disposable manager downloads. Original backups, game receipts,
 # installed files and the channel/manager state are outside this boundary.
 $base=[IO.Path]::GetFullPath($Store).TrimEnd('\','/')
 $source=Join-Path $base 'Downloads'
 if(-not(Test-Path -LiteralPath $source -PathType Container)){return 'No manager download cache to reset.'}
 [void](OwnedPath $base 'Downloads')
 $items=@(Get-ChildItem -LiteralPath $source -Recurse -Force -ErrorAction Stop)
 if($items.Count -gt 10000){throw 'Download cache contains too many entries for safe recovery. Nothing was moved.'}
 foreach($item in $items){
  if(-not $item.FullName.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase) -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Download cache contains a linked or unexpected entry. Nothing was moved.'}
 }
 $recovery=OwnedPath $base ('DownloadRecovery/'+[guid]::NewGuid().ToString('N'))
 [void][IO.Directory]::CreateDirectory((Split-Path $recovery -Parent))
 [IO.Directory]::Move($source,$recovery)
 return 'Download cache reset. Existing download data was preserved locally in '+$recovery+'. Retry the failed download. Installed files and original backups were kept.'
}
