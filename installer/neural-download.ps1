function GetNeuralDownload($Gpu){
 $cards=@($Gpu.Names -split ',\s*'|Where-Object {$_ -match '(?i)NVIDIA.*\bRTX\s*50\d\d'})
 if($cards.Count -ne 1){throw 'The neural model needs exactly one NVIDIA RTX 50-series GPU. No model was changed.'}
 return [pscustomobject]@{Family='RTX 50';Url='https://github.com/RankFTW/rhi-repo/releases/download/dlssnr-310.8.0/nvngx_dlssnr_310.8.0.zip';ArchiveHash='388C0A7912E15EC911B9C9E11A692142B11FE387DDF2B637D8C358138FFFB3AC';ModelHash='E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E';Entry='nvngx_dlssnr.dll';Signed=$true}
}
function EnsureNeuralRuntime([string]$ManifestPath,$Gpu,[scriptblock]$Progress={param($text)}){
 $manifest=ReadLaminrState $ManifestPath
 AssertClosed $manifest.Game
 $selection=GetNeuralDownload $Gpu
 $target=OwnedPath $manifest.Game 'nvngx_dlssnr.dll'
 if((HashFile $target) -eq $selection.ModelHash){if($selection.Signed){AssertNvidiaFile $target};&$Progress 'Correct neural model already installed; download skipped.';return}
 $downloadStore=if($env:LAMINR_STORE){$env:LAMINR_STORE}else{Join-Path $env:LOCALAPPDATA 'laminr'}
 $temp=Join-Path $downloadStore ('Downloads/NR-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory -Path $temp -Force|Out-Null
 $archivePath=Join-Path $temp 'model.zip';$modelPath=Join-Path $temp 'nvngx_dlssnr.dll'
 try{
  &$Progress ('Downloading verified '+$selection.Family+' model from a community GitHub release...')
  GetVerifiedDownload $selection.Url $archivePath $selection.ArchiveHash
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zip=[IO.Compression.ZipFile]::OpenRead($archivePath)
  try{
   $entry=$zip.GetEntry($selection.Entry)
   if(-not $entry -or $entry.Length -gt 256MB){throw 'Expected neural model is missing or has an invalid size.'}
   # Extract only the pinned model; never install any bundled third-party loaders.
   [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$modelPath,$false)
  }finally{$zip.Dispose()}
  if((HashFile $modelPath) -ne $selection.ModelHash){throw 'Neural model hash verification failed. Existing model was left unchanged.'}
  if($selection.Signed){AssertNvidiaFile $modelPath}
  ImportNrRuntime $ManifestPath $modelPath
  if((HashFile $target) -ne $selection.ModelHash){throw 'Installed neural model verification failed.'}
  &$Progress 'GPU-matched model installed and verified. Rendering remains off until enabled in game.'
 }finally{
  foreach($file in @($archivePath,$modelPath)){if(Test-Path -LiteralPath $file){Remove-Item -LiteralPath $file -Force}}
  if(-not(Get-ChildItem -LiteralPath $temp -Force)){Remove-Item -LiteralPath $temp}
 }
}
