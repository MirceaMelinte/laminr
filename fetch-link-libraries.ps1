# Fetch the prebuilt link libraries that upstream OptiScaler commits and this repository
# does not. Each file comes from the upstream commit in optiscaler-revision.txt and must
# match its SHA-256 in link-libraries.json. A file already in place is only verified.
$ErrorActionPreference='Stop'
function GetLaminrLinkLibraries([string]$Root){
 $definition=Get-Content -LiteralPath (Join-Path $Root 'link-libraries.json') -Raw|ConvertFrom-Json
 if($definition.Version -ne 1){throw 'Unsupported link library manifest'}
 return @($definition.Libraries)
}
function SaveLaminrLinkLibrary([string]$Url,[string]$Path){
 [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
 $ProgressPreference='SilentlyContinue'
 Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $Path -TimeoutSec 120
}
function SyncLaminrLinkLibraries([string]$Root){
 $revision=(Get-Content -LiteralPath (Join-Path $Root 'optiscaler-revision.txt') -Raw).Trim()
 if($revision -notmatch '^[0-9a-f]{40}$'){throw 'optiscaler-revision.txt must hold one full commit hash'}
 foreach($library in (GetLaminrLinkLibraries $Root)){
  $target=[IO.Path]::GetFullPath((Join-Path $Root $library.Path))
  if(Test-Path -LiteralPath $target -PathType Leaf){
   if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $library.SHA256){throw ($library.Path+' does not match the pinned SHA-256 in link-libraries.json. Delete it and build again to download the pinned copy.')}
   continue
  }
  $url='https://raw.githubusercontent.com/optiscaler/OptiScaler/'+$revision+'/'+$library.Upstream
  # Download outside the repository: the build fingerprint hashes whatever sits at the target.
  $temp=Join-Path ([IO.Path]::GetTempPath()) ('laminr-link-'+[Guid]::NewGuid().ToString('N')+'.lib')
  try{
   try{SaveLaminrLinkLibrary $url $temp}
   catch{throw ('Could not download '+$library.Path+' from '+$url+' ('+$_.Exception.Message+'). Download it by hand and save it as '+$target+' (SHA-256 '+$library.SHA256+').')}
   if((Get-FileHash -LiteralPath $temp -Algorithm SHA256).Hash -ne $library.SHA256){throw ('Downloaded '+$library.Path+' does not match its pinned SHA-256. Nothing was placed.')}
   [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))
   Move-Item -LiteralPath $temp -Destination $target
   Write-Output ('Downloaded '+$library.Path)
  }finally{if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp -Force}}
 }
}
if($MyInvocation.InvocationName -ne '.'){SyncLaminrLinkLibraries $PSScriptRoot}
