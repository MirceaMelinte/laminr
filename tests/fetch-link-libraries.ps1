$ErrorActionPreference='Stop'
. "$PSScriptRoot/../fetch-link-libraries.ps1"
# A throwaway root with fake library bytes standing in for the upstream files.
$fixture=Join-Path $env:TEMP ('laminr-link-libraries-'+[guid]::NewGuid().ToString('N'))
$script:upstream=Join-Path $fixture 'upstream';$root=Join-Path $fixture 'root'
New-Item -ItemType Directory -Path $script:upstream,$root -Force|Out-Null
[IO.File]::WriteAllText((Join-Path $script:upstream 'a.lib'),'pinned library A')
[IO.File]::WriteAllText((Join-Path $script:upstream 'b.lib'),'pinned library B')
$hashA=(Get-FileHash -LiteralPath (Join-Path $script:upstream 'a.lib') -Algorithm SHA256).Hash
$hashB=(Get-FileHash -LiteralPath (Join-Path $script:upstream 'b.lib') -Algorithm SHA256).Hash
$revision='e237f895623742b761f9e5f00067cb3dc62619f4'
@{Version=1;Libraries=@(@{Path='lib/a/a.lib';Upstream='x/a.lib';SHA256=$hashA},@{Path='lib/b.lib';Upstream='y/b.lib';SHA256=$hashB})}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $root 'link-libraries.json') -Encoding ASCII
Set-Content -LiteralPath (Join-Path $root 'optiscaler-revision.txt') -Value $revision -Encoding ASCII
$a=Join-Path $root 'lib/a/a.lib';$b=Join-Path $root 'lib/b.lib'
function CountTempDownloads{@(Get-ChildItem -LiteralPath ([IO.Path]::GetTempPath()) -Filter 'laminr-link-*.lib' -File).Count}
$script:urls=@()
function SaveLaminrLinkLibrary([string]$Url,[string]$Path){
 $script:urls+=$Url
 Copy-Item -LiteralPath (Join-Path $script:upstream (Split-Path $Url -Leaf)) -Destination $Path
}
try{
 $tempBefore=CountTempDownloads
 SyncLaminrLinkLibraries $root|Out-Null
 if($script:urls.Count -ne 2 -or $script:urls[0] -ne "https://raw.githubusercontent.com/optiscaler/OptiScaler/$revision/x/a.lib"){throw 'Unexpected download URLs'}
 if((Get-FileHash -LiteralPath $a).Hash -ne $hashA -or (Get-FileHash -LiteralPath $b).Hash -ne $hashB){throw 'Wrong libraries placed'}
 if((CountTempDownloads) -ne $tempBefore){throw 'Temporary download left behind'}
 'PASS: missing libraries are downloaded from the pinned revision and placed'

 SyncLaminrLinkLibraries $root|Out-Null
 if($script:urls.Count -ne 2){throw 'Verified libraries were downloaded again'}
 'PASS: libraries already in place are only verified'

 Set-Content -LiteralPath $a 'tampered'
 try{SyncLaminrLinkLibraries $root;throw 'Tampered library was accepted'}catch{if($_.Exception.Message -notmatch 'does not match the pinned SHA-256 in link-libraries.json'){throw}}
 if((Get-Content -LiteralPath $a -Raw).Trim() -ne 'tampered'){throw 'Tampered library was overwritten'}
 'PASS: a library with the wrong hash stops the build and is left alone'

 Remove-Item -LiteralPath $a
 [IO.File]::WriteAllText((Join-Path $script:upstream 'a.lib'),'changed upstream')
 try{SyncLaminrLinkLibraries $root;throw 'Mismatched download was accepted'}catch{if($_.Exception.Message -notmatch 'does not match its pinned SHA-256. Nothing was placed'){throw}}
 if(@(Get-ChildItem -LiteralPath (Split-Path $a) -File).Count){throw 'Mismatched download or partial file placed next to the target'}
 if((CountTempDownloads) -ne $tempBefore){throw 'Temporary download left behind'}
 'PASS: a download with the wrong hash places nothing and leaves no partial file'

 function SaveLaminrLinkLibrary([string]$Url,[string]$Path){throw 'offline'}
 try{SyncLaminrLinkLibraries $root;throw 'Failed download was accepted'}catch{
  $message=$_.Exception.Message
  if(-not($message.Contains("https://raw.githubusercontent.com/optiscaler/OptiScaler/$revision/x/a.lib") -and $message.Contains([IO.Path]::GetFullPath($a)) -and $message.Contains($hashA))){throw}
 }
 'PASS: a failed download names the URL, the target path and the expected hash'
}finally{Remove-Item -LiteralPath $fixture -Recurse -Force}
