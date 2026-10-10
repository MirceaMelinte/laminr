. "$PSScriptRoot/json-state.ps1"
. "$PSScriptRoot/store-paths.ps1"
# Read launcher catalogues; never crawl entire disks or start games during discovery.
function ResolveFusionStoreFolder([string]$Folder){
 # Appx InstallLocation can be a protected package path or an Xbox junction.
 # Never install through WindowsApps or relax OwnedPath's link protection.
 if($Folder -notmatch '(?i)[\\/]WindowsApps(?:[\\/]|$)'){return $Folder}
 $help='This is a WindowsApps package path, not an installable game folder. In the Xbox app, open the simulator > Manage > Files, then select its installation folder using Browse. laminr will not change WindowsApps permissions.'
 if($Folder -notmatch '(?i)[\\/]WindowsApps[\\/]Microsoft\.Limitless_[^\\/]+$'){throw $help}
 try{
  $target=ResolveSimulatorStoreTarget $Folder
  $exe='FlightSimulator2024.exe'
  foreach($root in @($target,(Join-Path $target 'Content'))){
   if(-not(Test-Path -LiteralPath (Join-Path $root $exe) -PathType Leaf)){continue}
   $check=$root
   while($check){
    if((Get-Item -LiteralPath $check -Force -ErrorAction Stop).Attributes -band [IO.FileAttributes]::ReparsePoint){throw $help}
    $parent=Split-Path $check -Parent;if($parent -eq $check){break};$check=$parent
   }
   return $root
  }
 }catch{throw $help}
 throw $help
}
function ResolveFusionInstallFolder([string]$Game){
 if([string]::IsNullOrWhiteSpace($Game)){throw 'Choose the simulator installation folder using Browse.'}
 $candidate=[Environment]::ExpandEnvironmentVariables($Game.Trim().Trim('"').Trim())
 if($candidate -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)' -or $candidate.IndexOfAny([IO.Path]::GetInvalidPathChars()) -ge 0 -or $candidate -match '[<>|"*?\x00-\x1F]' ){
  throw 'The installation path is invalid. Use Browse to select the simulator installation folder.'
 }
 try{$folder=[IO.Path]::GetFullPath($candidate).TrimEnd('\')}catch{throw 'The installation path is invalid. Use Browse to select the simulator installation folder.'}
 if([IO.Path]::GetFileName($folder) -in @('FlightSimulator2024.exe','gamelaunchhelper.exe')){$folder=Split-Path $folder -Parent}
 $folder=ResolveFusionStoreFolder $folder
 foreach($candidate in @($folder,(Join-Path $folder 'Content'))){
  if(Test-Path -LiteralPath (Join-Path $candidate 'FlightSimulator2024.exe') -PathType Leaf){return $candidate}
 }
 return $folder
}
function GetMsfsTitle([string]$Game){
 try{$folder=ResolveFusionInstallFolder $Game}catch{return ''}
 if(-not $folder){return ''}
 if(Test-Path -LiteralPath (Join-Path $folder 'FlightSimulator2024.exe') -PathType Leaf){return 'Microsoft Flight Simulator 2024'}
 return ''
}
function AssertFusionPhysicalFolder([string]$Folder){
 $check=$Folder
 while($check){
  if((Get-Item -LiteralPath $check -Force -ErrorAction Stop).Attributes -band [IO.FileAttributes]::ReparsePoint){
   throw 'Select the actual simulator installation folder, not a linked or WindowsApps folder. For Xbox installs, use the location shown under Manage > Files and select its Content folder.'
  }
  $parent=Split-Path $check -Parent;if($parent -eq $check){break};$check=$parent
 }
}
function GetFusionInstallState([string]$Store,[string]$Game){
 $gamePath=ResolveFusionInstallFolder $Game
 foreach($file in Get-ChildItem (Join-Path $Store 'Games') -Filter manifest.json -Recurse -File -ErrorAction SilentlyContinue){
  try{$m=ReadLaminrJson $file.FullName 'Launcher or installation JSON';if([IO.Path]::GetFullPath($m.Game).TrimEnd('\') -ne $gamePath){continue}
   if($m.Status -eq 'Installed'){
    $loaders=@($m.Files|Where-Object {$_.SourcePath -eq 'winmm.dll'})
    foreach($loader in $loaders){
     $loaderPath=[IO.Path]::GetFullPath((Join-Path $gamePath $loader.Path))
     if(-not $loaderPath.StartsWith($gamePath+'\',[StringComparison]::OrdinalIgnoreCase) -or -not(Test-Path -LiteralPath $loaderPath -PathType Leaf)){return 'Installation incomplete - repair required'}
     if((Get-Item -LiteralPath $loaderPath).Length -eq 0){return 'Installation incomplete - repair required'}
    }
    if($m.Downloads -eq 'Pending'){return 'Installed - downloads pending'};return 'Installed'
   }
   if($m.Status -eq 'Restored'){return 'Not installed (restored)'}
   return 'Installation incomplete - repair required'
  }catch{}
 }
 if(Test-Path -LiteralPath (Join-Path $gamePath 'ReShade64.dll')){return 'Graphics files detected - not tracked'}
 return 'Not installed'
}
function FindFusionGames([string]$Store){
 $games=@{}
 function AddGame($name,$folder,$launcher){
  if(-not $folder){return}
  try{
   $folder=ResolveFusionInstallFolder $folder
   AssertFusionPhysicalFolder $folder
   # A catalogue entry alone is not enough: validate the launchable x64 EXE.
   $executable=@(FindFusionExecutable $folder $launcher)
   if($executable.Count -ne 1){return}
  }catch{return}
  $name=GetMsfsTitle $folder
  if(-not $name){return}
  if($name -match 'Steamworks Common|SteamVR|Oasis Driver|Unreal Engine|Fab UE Plugin|Quixel Bridge|Minecraft Launcher'){return}
  if(-not $folder -or -not(Test-Path -LiteralPath $folder -PathType Container)){return}
  $folder=[IO.Path]::GetFullPath($folder).TrimEnd('\');$key=$folder.ToLowerInvariant()
  if(-not $games.ContainsKey($key)){$games[$key]=[pscustomobject]@{Name=$name;Folder=$folder;Launcher=$launcher;State='Not installed';Label=''}}
 }
 $steam=@("${env:ProgramFiles(x86)}\Steam")
 $registered=(Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
 if($registered){$steam+= $registered}
 foreach($key in @('HKLM:\SOFTWARE\WOW6432Node\Valve\Steam','HKLM:\SOFTWARE\Valve\Steam')){
  $registered=(Get-ItemProperty $key -ErrorAction SilentlyContinue).InstallPath
  if($registered){$steam+=$registered}
 }
 $steam=@($steam|ForEach-Object {try{ResolveFusionInstallFolder $_}catch{}}|Select-Object -Unique)
 foreach($base in @($steam)){
  $vdf=Join-Path $base 'steamapps/libraryfolders.vdf'
  try{if(Test-Path -LiteralPath $vdf){foreach($match in [regex]::Matches((Get-Content -LiteralPath $vdf -Raw -Encoding UTF8),'"path"\s+"([^"]+)"')){try{$steam+=ResolveFusionInstallFolder $match.Groups[1].Value.Replace('\\','\')}catch{}}}}catch{}
 }
 foreach($base in ($steam|Select-Object -Unique)){
  foreach($file in Get-ChildItem -LiteralPath (Join-Path $base 'steamapps') -Filter 'appmanifest_*.acf' -File -ErrorAction SilentlyContinue){
   try{
   $data=Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
   $name=[regex]::Match($data,'"name"\s+"([^"]+)"').Groups[1].Value
   $dir=[regex]::Match($data,'"installdir"\s+"([^"]+)"').Groups[1].Value
   if($dir){AddGame $name (Join-Path $base "steamapps/common/$dir") 'Steam'}
   }catch{}
  }
 }
 foreach($drive in Get-PSDrive -PSProvider FileSystem){
  foreach($entry in Get-ChildItem -LiteralPath (Join-Path $drive.Root 'XboxGames') -Directory -ErrorAction SilentlyContinue){AddGame $entry.Name (Join-Path $entry.FullName 'Content') 'Xbox'}
  foreach($entry in Get-ChildItem -LiteralPath (Join-Path $drive.Root 'Xbox games') -Directory -ErrorAction SilentlyContinue){AddGame $entry.Name (Join-Path $entry.FullName 'Content') 'Xbox'}
  # Bounded custom simulator roots; inspect only the root and direct children.
  # This covers layouts such as C:\MSFS\Microsoft Flight Simulator\Content.
  foreach($rootName in @('MSFS','MSFS2024')){
   $custom=Join-Path $drive.Root $rootName
   if(-not(Test-Path -LiteralPath $custom -PathType Container)){continue}
   try{AssertFusionPhysicalFolder $custom}catch{continue}
   AddGame $rootName $custom ''
   foreach($entry in Get-ChildItem -LiteralPath $custom -Directory -ErrorAction SilentlyContinue|Select-Object -First 100){
    if(-not($entry.Attributes -band [IO.FileAttributes]::ReparsePoint)){AddGame $entry.Name $entry.FullName ''}
   }
  }
 }
 # Registered Store packages may live outside the default XboxGames folders.
 # Query only the simulator identity, never enumerate protected disk trees.
 if(Get-Command Get-AppxPackage -ErrorAction SilentlyContinue){
  try{foreach($package in Get-AppxPackage -Name 'Microsoft.Limitless' -ErrorAction Stop){AddGame $package.Name $package.InstallLocation 'Xbox'}}catch{}
 }
 foreach($file in Get-ChildItem (Join-Path $Store 'Games') -Filter manifest.json -Recurse -File -ErrorAction SilentlyContinue){
  try{
   $m=ReadLaminrJson $file.FullName 'Launcher or installation JSON'
   $parent=@($games.Values|Where-Object {$m.Game.StartsWith($_.Folder+'\',[StringComparison]::OrdinalIgnoreCase)}|Sort-Object @{Expression={$_.Folder.Length};Descending=$true})|Select-Object -First 1
   if($parent){$parent.State=$m.Status;if($m.Status -eq 'Installed' -and $m.Downloads -eq 'Pending'){$parent.State='Installed - finish downloads'};$parent|Add-Member -NotePropertyName InstallFolder -NotePropertyValue $m.Game -Force}
   else{AddGame (Split-Path $m.Game -Leaf) $m.Game 'Added by you';$key=([IO.Path]::GetFullPath($m.Game).TrimEnd('\')).ToLowerInvariant();if($games.ContainsKey($key)){$games[$key].State=$m.Status;if($m.Status -eq 'Installed' -and $m.Downloads -eq 'Pending'){$games[$key].State='Installed - finish downloads'}}}
  }catch{}
 }
 $validated=@(foreach($game in $games.Values){
  try{
   $installPath=ResolveFusionInstallFolder $(if($game.InstallFolder){$game.InstallFolder}else{$game.Folder})
   $null=FindFusionExecutable $installPath $game.Launcher
   $game|Add-Member -NotePropertyName InstallFolder -NotePropertyValue $installPath -Force
   $game.State=GetFusionInstallState $Store $installPath
   $game.Label="$($game.Name) / $($game.Launcher) - laminr $($game.State)"
   $game
  }catch{} # A stale installation record must not hide other valid games.
 })
 @($validated|Sort-Object Name)
}
function GetFusionGpu{
  $devices=@(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue)
 $cards=@($devices|ForEach-Object Name)
 $drivers=@(foreach($device in $devices){
  $raw=[string]$device.DriverVersion;$display=$raw
  if($device.Name -match 'NVIDIA' -and $raw -match '^\d+\.\d+\.(\d+)\.(\d{1,4})$'){$number=(([int]$Matches[1]%10)*10000)+[int]$Matches[2];$display=('{0}.{1:00}' -f [math]::Floor($number/100),($number%100))}
  [pscustomobject]@{Name=$device.Name;Version=$display;WindowsVersion=$raw}
 })
 [pscustomobject]@{Names=($cards -join ', ');Nvidia=[bool]($cards -match 'NVIDIA');Known=($cards.Count -gt 0);Drivers=$drivers}
}
function AssertFusionExecutable([string]$File){
 $stream=[IO.File]::OpenRead($File);$reader=New-Object IO.BinaryReader($stream)
 try{if($reader.ReadUInt16() -ne 0x5a4d){throw 'Choose a Windows game EXE.'};$stream.Position=0x3c;$offset=$reader.ReadInt32();$stream.Position=$offset;if($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664){throw 'This preview supports 64-bit Windows games only.'}}finally{$reader.Dispose()}
}
function FindFusionExecutable([string]$Game,[string]$Launcher=''){
 if(-not $Game -or -not(Test-Path -LiteralPath $Game -PathType Container)){return @()}
 $Game=ResolveFusionInstallFolder $Game
 AssertFusionPhysicalFolder $Game
 # MSFS Xbox uses the accessible launch helper beside the protected game EXE.
 # Steam uses the game EXE directly. Both install beside the selected executable.
 foreach($folder in @($Game,(Join-Path $Game 'Content'))){
  $main=Join-Path $folder 'FlightSimulator2024.exe';$helper=Join-Path $folder 'gamelaunchhelper.exe'
  if(Test-Path -LiteralPath $main -PathType Leaf){
   $xbox=$Launcher -eq 'Xbox' -or ($Launcher -ne 'Steam' -and (Test-Path -LiteralPath (Join-Path $folder 'MicrosoftGame.Config')))
   $target=if($xbox){$helper}else{$main}
   if(-not(Test-Path -LiteralPath $target -PathType Leaf)){throw "Microsoft Flight Simulator launcher is missing: $target. Repair the game through its launcher first."}
   AssertFusionExecutable $target
   return @([pscustomobject]@{Path=$target;Score=1000})
  }
 }
 throw 'No supported simulator executable was found. Select the folder containing FlightSimulator2024.exe. For Xbox installs select the real Content folder; laminr selects gamelaunchhelper.exe automatically when required.'
}
function AssertMsfsNvidiaTarget([string]$Exe,$Gpu){
 if(-not $Gpu.Known -or $Gpu.Names -notmatch '(?i)NVIDIA[^,]*\bRTX\s*50\d\d'){throw 'This build supports NVIDIA GeForce RTX 50-series graphics cards only. Restore and uninstall remain available.'}
 if(-not $Exe -or (Split-Path $Exe -Leaf) -notin @('FlightSimulator2024.exe','gamelaunchhelper.exe')){throw 'This edition supports Microsoft Flight Simulator 2024 only.'}
 if(-not (GetMsfsTitle (Split-Path $Exe))){throw 'Choose the simulator executable folder containing FlightSimulator2024.exe.'}
}
