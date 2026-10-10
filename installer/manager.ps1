param([string]$Payload,[string]$Installer)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/ownership.ps1"
. "$PSScriptRoot/nvidia.ps1"
. "$PSScriptRoot/neural-download.ps1"
. "$PSScriptRoot/library.ps1"
. "$PSScriptRoot/consent.ps1"
. "$PSScriptRoot/compatibility.ps1"
. "$PSScriptRoot/recovery.ps1"
. "$PSScriptRoot/menu-settings.ps1"
. "$PSScriptRoot/uninstall-options.ps1"
Add-Type -AssemblyName PresentationFramework,PresentationCore,WindowsBase,System.Windows.Forms
$store=if($env:LAMINR_STORE){$env:LAMINR_STORE}else{Join-Path $env:LOCALAPPDATA 'laminr'}
$script:cachePath=Join-Path $store 'msfs-2020-2024-cache.json'
[xml]$xaml=Get-Content "$PSScriptRoot/manager.xaml" -Raw -Encoding UTF8
$form=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $xaml))
$path=$form.FindName('GamePath');$status=$form.FindName('Status')
$buttons=@('Browse','Install','Repair','Restore','Runtime','Retry','Uninstall','Play','GamePath','Method','HomeNav','SetupNav','SettingsNav','KeybindsNav','ChangeMenuKey','ChangeHotSwapKey','ClearHotSwapKey','ChangeSnapshotKey','ClearSnapshotKey','BrowseSnapshots','OpenLibrary','CheckCompatibility','TroubleshootingNav','ResetDefaults','ResetDownloadCache','RecoveryRepair','RecoveryRestore','SaveMenuKeys','DefaultMenuKeys','LoadMenuKeys','OwnIniMode','ChooseOwnIni','ApplyFxChoice')|ForEach-Object {$form.FindName($_)}
$form.Icon=[Windows.Media.Imaging.BitmapFrame]::Create([uri](Join-Path $PSScriptRoot 'app.ico'))
$form.FindName('BrandIcon').Source=[Windows.Media.Imaging.BitmapFrame]::Create([uri](Join-Path $PSScriptRoot 'app-logo.png'))
$form.FindName('BrandIcon').Cursor='SizeAll'
$form.FindName('BrandIcon').Add_MouseLeftButtonDown({if($_.ChangedButton -eq 'Left'){$form.DragMove()}})
$form.FindName('TitleBar').Add_MouseLeftButtonDown({if($_.ChangedButton -eq 'Left'){$form.DragMove()}})
$form.Add_PreviewMouseLeftButtonDown({
 $e=$_;$bottom=$form.FindName('TitleBar').TranslatePoint([Windows.Point]::new(0,$form.FindName('TitleBar').ActualHeight),$form).Y
 if($e.GetPosition($form).Y -gt $bottom){return}
 $node=$e.OriginalSource
 while($node -is [Windows.DependencyObject]){if($node -is [Windows.Controls.Primitives.ButtonBase]){return};if($node -isnot [Windows.Media.Visual]){break};$node=[Windows.Media.VisualTreeHelper]::GetParent($node)}
 if($e.LeftButton -eq 'Pressed'){$e.Handled=$true;$form.DragMove()}
})
$form.FindName('Close').Add_Click({if(-not $script:busy){$form.Close()}})
$form.FindName('Minimize').Add_Click({$form.WindowState='Minimized'})
$script:busy=$false
function RunAction([scriptblock]$action){
 if($script:startupResult -and -not $script:startupResult.IsCompleted){$status.Text='Finishing the hardware check. Please try again in a moment.';return}
 if($script:busy){return};$script:busy=$true
 foreach($control in $buttons){if($control){$control.IsEnabled=$false}}
 try{if($path.Text){$path.Text=ResolveFusionInstallFolder $path.Text};& $action;$status.Foreground='#BDA0F4'}catch{$status.Text=$_.Exception.Message;$status.Foreground='#FFBE83';WriteInstallerLog ($_.Exception.Message+' | '+$_.ScriptStackTrace)}
 finally{$form.FindName('InstallProgress').IsIndeterminate=$false;$form.FindName('InstallProgress').Visibility='Collapsed';$script:busy=$false;foreach($control in $buttons){if($control){$control.IsEnabled=$true}};if(-not $script:uninstallDone){RefreshLibrary}}
}

$form.Add_Closing({param($sender,$e) if($script:busy){$e.Cancel=$true}})
$progress={param($text,[int]$completed=0,[int]$total=0)
 $status.Text=$text;$bar=$form.FindName('InstallProgress');$bar.Visibility='Visible'
 $bar.IsIndeterminate=($total -le 0)
 if($total -gt 0){$bar.Maximum=$total;$bar.Value=[Math]::Min($completed,$total)}
 $form.Dispatcher.Invoke([Action]{},[Windows.Threading.DispatcherPriority]::Background)
}
$form.FindName('Browse').Add_Click({$d=New-Object Windows.Forms.FolderBrowserDialog;if($d.ShowDialog() -eq 'OK'){$path.Text=$d.SelectedPath};$d.Dispose()})
$script:ownIni=''
$form.FindName('OwnIniMode').Add_Checked({$form.FindName('OwnIniPicker').Visibility='Visible'})
$form.FindName('OwnIniMode').Add_Unchecked({$form.FindName('OwnIniPicker').Visibility='Collapsed'})
$form.FindName('ChooseOwnIni').Add_Click({$d=New-Object Windows.Forms.OpenFileDialog;$d.Filter='ReShade look preset (*.ini)|*.ini';try{if($d.ShowDialog() -eq 'OK'){$script:ownIni=$d.FileName;$form.FindName('OwnIniName').Text=[IO.Path]::GetFileName($d.FileName)}}finally{$d.Dispose()}})
$form.FindName('ApplyFxChoice').Add_Click({RunAction {
 AssertClosed $path.Text;$mp=ManifestPath $store $path.Text;$m=ReadLaminrState $mp
 if($m.Status -ne 'Installed'){throw 'Complete the laminr installation first.'}
 $relative=SaveOwnPreset $script:ownIni $path.Text (Join-Path $PSScriptRoot 'EffectPackages.ini')
 $m|Add-Member -NotePropertyName FxPresetRelative -NotePropertyValue $relative -Force;WriteState $m $mp
 FinishOptionalDownloads $m (CheckGameCompatibility $m.LaunchExe)
}})
$form.FindName('Install').Add_Click({RunAction {
 if((GetFusionInstallState $store $path.Text) -match '^Installed'){
  $mp=ManifestPath $store $path.Text;$previous=ReadLaminrState $mp
  if(-not(TestBundledUpdateVersion $previous)){throw 'This manager does not contain a newer version. Use the manager matching the installed version for Repair.'}
  if([Windows.MessageBox]::Show($form,'Update this installation using the files in this manager? Saved presets, settings and original backups are kept. Close the simulator first.','Update laminr','YesNo','Question','No') -ne 'Yes'){return}
  $proxy=@($previous.Files|Where-Object SourcePath -eq 'winmm.dll'|Select-Object -First 1).Path
  if(-not $proxy){throw 'The recorded loader is unknown. Update stopped.'}
  $mp=InstallFusion $path.Text $Payload $store $Installer $proxy @(FindFusionConflicts $path.Text) -ReplaceExisting $true -PreserveConfiguration $true
  $updated=ReadLaminrState $mp
  foreach($key in @('LaunchExe','Downloads','OptionalDlss','FxPresetRelative')){if($previous.PSObject.Properties[$key]){$updated|Add-Member -NotePropertyName $key -NotePropertyValue $previous.$key -Force}}
  WriteState $updated $mp;$status.Text='laminr updated from this manager. Settings and presets kept. No Restore is required.';return
 }
 if((GetFusionInstallState $store $path.Text) -match '^Installation incomplete'){throw 'An incomplete installation was found. Repair using the matching manager first.'}
 if($form.FindName('OwnIniMode').IsChecked){TestOwnPreset $script:ownIni $path.Text (Join-Path $PSScriptRoot 'EffectPackages.ini')}
 $exe=ChooseGameExe; if(-not $exe){return}; AssertFusionExecutable $exe
 AssertMsfsNvidiaTarget $exe (GetFusionGpu)
 $proxy=[string]$form.FindName('Method').SelectedItem.Tag
 $plan=CheckGameCompatibility $exe
 if(-not(ConfirmNvidiaDriver $form $script:gpu)){$status.Text='Installation cancelled. Update your NVIDIA driver, then run setup again.';return}
 if($proxy -eq 'auto'){$proxy=$plan.Proxy}
 $optionalDlss=if($plan.DownloadNvidia){ConfirmOptionalDlss $form $true}else{$false}
 if($null -eq $optionalDlss){return}
 if(-not $plan.PossibleInput){if([Windows.MessageBox]::Show($form,(FormatFusionCompatibility $plan)+"`n`nContinue with image effects?",'Image effects only','YesNo','Information','No') -ne 'Yes'){return}}
 $replace=@(FindFusionConflicts $path.Text)
 $existing=Test-Path -LiteralPath (ManifestPath $store $path.Text)
 $existing=$existing -or (Test-Path -LiteralPath (Join-Path $path.Text 'laminrData'))
 if($replace.Count -or $existing){
  $message="Files already in this game folder:`n"+(($replace|ForEach-Object { $_.Path+' - '+$(if($_.Recognised){$_.Description}else{'Unidentified loader; may belong to the game or another tool'}) }) -join "`n")+"`n`nReplace this setup with laminr? Conflicting loaders will be removed from the game folder. Existing graphics loaders are backed up before replacement and restored by Restore. Avoid replacing an intentionally working mod setup. Existing shader folders and presets are kept. Unidentified loaders may be needed by the game; choose No if unsure."
  $message="ReShade / laminr files detected. Remove conflicting files from the game folder, then install laminr? Replaced files will be included in the restore backup. Saved INI files are kept.`n`n"+$message
  if([Windows.MessageBox]::Show($form,$message,'Reinstall or replace graphics mods','YesNo','Question','No') -ne 'Yes'){return}
 }
 $script:manifest=InstallFusion $path.Text $Payload $store $Installer $proxy $replace -ReplaceExisting $existing
 $m=ReadLaminrState $script:manifest;$m|Add-Member -NotePropertyName LaunchExe -NotePropertyValue $exe -Force;$m|Add-Member -NotePropertyName Downloads -NotePropertyValue 'Pending' -Force;$m|Add-Member -NotePropertyName OptionalDlss -NotePropertyValue ([bool]$optionalDlss) -Force;WriteState $m $script:manifest
 SaveFusionCompatibility $path.Text $plan
 if($form.FindName('OwnIniMode').IsChecked){$relative=SaveOwnPreset $script:ownIni $path.Text (Join-Path $PSScriptRoot 'EffectPackages.ini');$m|Add-Member -NotePropertyName FxPresetRelative -NotePropertyValue $relative -Force;WriteState $m $script:manifest}
 FinishOptionalDownloads $m $plan
}})
$form.FindName('Restore').Add_Click({RunAction {$m=ManifestPath $store $path.Text;if(-not(Test-Path $m)){throw 'No recorded installation for this game.'};RestoreFusion $m;$status.Text='Game restored. Original files are back and laminr game files are removed.';$restored=ReadLaminrState $m;if($restored.RecoveredShaders){$status.Text+=' Edited shaders saved in: '+$restored.RecoveredShaders}}})
$form.FindName('Runtime').Add_Click({RunAction {$m=ManifestPath $store $path.Text;if(-not(Test-Path $m)){throw 'Install laminr first.'};$installed=ReadLaminrState $m;$plan=CheckGameCompatibility $installed.LaunchExe;if(-not $plan.DownloadNvidia){throw $plan.NeuralRendering};$d=New-Object Windows.Forms.OpenFileDialog;$d.Title='Choose '+$plan.NeuralRuntime.Expected;$d.Filter='NVIDIA NR model (not the helper)|nvngx_dlssnr.dll';if($d.ShowDialog() -eq 'OK'){ImportNrRuntime $m $d.FileName;$installed|Add-Member -NotePropertyName OptionalDlss -NotePropertyValue $true -Force;$latest=ReadLaminrState $m;$latest|Add-Member -NotePropertyName OptionalDlss -NotePropertyValue $true -Force;WriteState $latest $m;$status.Text='Model file added. It is not running yet: a supported game connection is still required.'};$d.Dispose()}})
$form.FindName('Uninstall').Add_Click({RunAction {
 $choice=ShowUninstallOptions $form;if(-not $choice){return}
 RemoveSelectedLaminrData $store $Installer $PSScriptRoot -AppFiles $choice.AppFiles -IniFiles $choice.IniFiles -Snapshots $choice.Snapshots
 if($choice.AppFiles){$script:uninstallDone=$true;$status.Text='laminr removed. Your original installer EXE is kept.';$script:busy=$false;$form.Close()}
 else{$status.Text='Selected laminr files removed. The app remains installed.'}
}})
$form.FindName('Retry').Add_Click({RunAction {$m=ReadLaminrState (ManifestPath $store $path.Text);if($m.Status -ne 'Installed'){throw 'Install laminr first.'};AssertClosed $path.Text;$plan=CheckGameCompatibility $m.LaunchExe;SaveFusionCompatibility $path.Text $plan;FinishOptionalDownloads $m $plan}})
$form.FindName('Repair').Add_Click({RunAction {
 $mp=ManifestPath $store $path.Text
 if(-not(Test-Path -LiteralPath $mp)){throw 'No installation record. Use Install to remove conflicting mods and install laminr.'}
 $previous=ReadLaminrState $mp
 if($previous.Status -notin @('Installed','Installing')){throw 'No repairable installation record. Restore the recorded installation first, then install again.'}
 AssertRepairVersion $previous
 if([Windows.MessageBox]::Show($form,'Repair laminr from this installer? Core files will be replaced. Existing configuration and menu keys are kept. Your saved INI presets are kept. Original pre-install backups are preserved.','Repair laminr','YesNo','Question','No') -ne 'Yes'){return}
 $proxy=@($previous.Files|Where-Object SourcePath -eq 'winmm.dll'|Select-Object -First 1).Path
 if(-not $proxy){throw 'The recorded loader is unknown. Repair was stopped.'}
 $mp=InstallFusion $path.Text $Payload $store $Installer $proxy @(FindFusionConflicts $path.Text) -ReplaceExisting $true -PreserveConfiguration $true
 $repaired=ReadLaminrState $mp
 foreach($key in @('LaunchExe','Downloads','OptionalDlss','FxPresetRelative')){if($previous.PSObject.Properties[$key]){$repaired|Add-Member -NotePropertyName $key -NotePropertyValue $previous.$key -Force}}
 WriteState $repaired $mp
 $status.Text='laminr repaired from the manager. Saved looks and original backups are kept. You can close the manager and start your game.'
}})
function WriteInstallerLog([string]$Message){try{New-Item -ItemType Directory -Path $store -Force|Out-Null;((Get-Date -Format o)+' '+$Message)|Add-Content -LiteralPath (Join-Path $store 'Installer.log') -Encoding UTF8}catch{}}
function FinishOptionalDownloads($Manifest,$Plan){
 $presetWarning=''
 $issues=New-Object 'System.Collections.Generic.List[string]'
 if(UseOptionalDlss $Manifest $Plan){try{EnsureNeuralRuntime (ManifestPath $store $path.Text) (GetFusionGpu) $progress}catch{$issues.Add('Neural model: '+$_.Exception.Message)};try{InstallNvidia $path.Text $progress}catch{$issues.Add('NVIDIA files: '+$_.Exception.Message)}}
 if($Manifest.PSObject.Properties['FxPresetRelative'] -and $Manifest.FxPresetRelative){try{$progress.Invoke('Installing the selected INI shader dependencies...');$presetWarning=InstallPresetDependencies (OwnedPath $path.Text $Manifest.FxPresetRelative) $path.Text (Join-Path $PSScriptRoot 'EffectPackages.ini')}catch{$issues.Add('Preset FX: '+$_.Exception.Message)}}
 if($issues.Count){
  $mp=ManifestPath $store $path.Text;$m=ReadLaminrState $mp
  $m|Add-Member -NotePropertyName Downloads -NotePropertyValue 'Pending' -Force;WriteState $m $mp
  foreach($issue in $issues){WriteInstallerLog $issue}
  $status.Text='laminr is installed. Some optional downloads are unfinished; installed effects can still be used. Choose Retry unfinished downloads. Details: %LOCALAPPDATA%\laminr\Installer.log'
 }else{CompleteDownloads;if($Manifest.PSObject.Properties['FxPresetRelative'] -and $Manifest.FxPresetRelative){$status.Text='Preset installed. Select '+[IO.Path]::GetFileName($Manifest.FxPresetRelative)+' in game under Image effects > Preset 1.'}}
 WriteInstallerLog $status.Text
 try{$updated=CheckGameCompatibility $Manifest.LaunchExe;SaveFusionCompatibility $path.Text $updated;if((UseOptionalDlss $Manifest $Plan) -and $updated.NeuralRuntime.State -ne 'Verified file'){$status.Text='laminr image effects are installed. Neural rendering is NOT ready: '+$updated.NeuralRuntime.Message+' Choose Retry unfinished downloads to retry the GPU-matched model download.';if($issues.Count){$status.Text+=' Some optional downloads also failed; use Retry unfinished downloads.'}}}catch{WriteInstallerLog ('Post-install diagnostics: '+$_.Exception.Message)}
 if($presetWarning){$status.Text+=' '+$presetWarning;WriteInstallerLog $presetWarning}
}

function CheckGameCompatibility([string]$exe){
 $script:gpu=GetFusionGpu
 AssertMsfsNvidiaTarget $exe $script:gpu
 $root=$path.Text
 if($script:gameRoot -and $root.StartsWith($script:gameRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$root=$script:gameRoot}
 $plan=GetFusionCompatibility $root $exe $script:gpu $script:gameLauncher
 $plan|Add-Member -NotePropertyName DriverVersions -NotePropertyValue @($script:gpu.Drivers) -Force
 $plan|Add-Member -NotePropertyName NeuralRuntime -NotePropertyValue (GetNeuralRuntimeStatus $path.Text $script:gpu) -Force
 $installed=@(GetFusionInstalledVersions $path.Text)
 $plan|Add-Member -NotePropertyName InstalledRuntimes -NotePropertyValue $installed
 $form.FindName('Compatibility').Text=FormatFusionCompatibility $plan
 foreach($runtime in $installed){$form.FindName('Compatibility').Text+="`nStored: $($runtime.File) - $($runtime.Version) (not proof it is running)"}
 return $plan
}
$form.FindName('CheckCompatibility').Add_Click({RunAction {$exe=ChooseGameExe;if($exe){$plan=CheckGameCompatibility $exe;$status.Text='Check complete. Found files are clues, not confirmation a feature works in game.'}}})
$path.Add_TextChanged({$form.FindName('Compatibility').Text='Check this game before installing. Automatic setup also checks again at install time.';RefreshHomeState})
$form.FindName('ResetDownloadCache').Add_Click({RunAction {$status.Text=ResetLaminrDownloadCache $store}})
function CompleteDownloads{$mp=ManifestPath $store $path.Text;$m=ReadLaminrState $mp;$m|Add-Member -NotePropertyName Downloads -NotePropertyValue 'Complete' -Force;WriteState $m $mp;$status.Text='Install complete. You can close the manager and start your game.'}
function ShowPage([string]$name){
 if($name -ne 'Keybinds'){CancelSnapshotCapture;$script:capturingHotSwap=$false;$form.FindName('ChangeHotSwapKey').Content='Change'}
 if($name -ne 'Keybinds' -and $script:capturingMenuKey){$script:capturingMenuKey=$false;$form.FindName('ChangeMenuKey').Content='Change'}
 foreach($page in @('Home','Setup','Keybinds','Settings','Troubleshooting')){$form.FindName($page+'Page').Visibility=if($page -eq $name){'Visible'}else{'Collapsed'}}
}
function RefreshHomeState{
 try{$state=if($path.Text){GetFusionInstallState $store $path.Text}else{'Not installed'}}catch{$state='Invalid path'}
 $installed=$state -match '^Installed';$incomplete=$state -match '^Installation incomplete'
 $upgrade=$false
 if($installed){try{$upgrade=TestBundledUpdateVersion (ReadLaminrState (ManifestPath $store $path.Text))}catch{}}
 $install=$form.FindName('Install');$install.Content=if($upgrade){'Update laminr'}elseif($installed){'Already installed'}elseif($incomplete){'Repair required'}else{'Install laminr'}
 $install.IsEnabled=(-not $script:busy -and (-not $installed -or $upgrade) -and -not $incomplete -and $state -ne 'Invalid path' -and -not [string]::IsNullOrWhiteSpace($path.Text))
 $form.FindName('ApplyFxChoice').Visibility=if($installed){'Visible'}else{'Collapsed'}
 $title=GetMsfsTitle $path.Text
 if($title){$form.FindName('SelectedTitle').Text=$title}
 $homeCopy=@($form.FindName('MsfsCopies').ItemsSource)|Select-Object -First 1
 $form.FindName('HomeDetection').Text=if($title -or $homeCopy){'MSFS 2024 detected'}else{'MSFS 2024 not detected'}
 $homeState=if($title){$state}elseif($homeCopy){$homeCopy.State}else{'Not installed'}
 $form.FindName('HomeInstallState').Text=if($homeState -match '^Installed'){'laminr installed'}elseif($homeState -match 'incomplete'){'laminr needs repair'}else{'Install laminr'}
}
function RefreshLibrary{
 $selector=$form.FindName('MsfsCopies');$items=@($selector.ItemsSource)
 foreach($game in $items){
  $installPath=if($game.InstallFolder){$game.InstallFolder}else{$game.Folder}
  $state=GetFusionInstallState $store $installPath
  $game.State=$state;$game.Label="$($game.Name) / $($game.Launcher) - laminr $state"
 }
 $selected=$selector.SelectedIndex;$savedPath=$path.Text
 $selector.ItemsSource=$null;$selector.ItemsSource=$items;$selector.SelectedIndex=$selected;$path.Text=$savedPath
 RefreshHomeState
}
$form.FindName('HomeNav').Add_Click({RefreshHomeState;ShowPage 'Home'})
$form.FindName('OpenLibrary').Add_Click({
 $copy=@($form.FindName('MsfsCopies').ItemsSource)|Select-Object -First 1
 if($copy){$form.FindName('MsfsCopies').SelectedItem=$copy;$path.Text=$copy.InstallFolder}
 else{$form.FindName('MsfsCopies').SelectedIndex=-1;$script:gameRoot='';$script:gameLauncher='';$path.Text=''}
 $form.FindName('SelectedTitle').Text='Microsoft Flight Simulator 2024'
 $status.Text='Select your MSFS 2024 copy below.'
 ShowPage 'Setup'
})
$form.FindName('KeybindsNav').Add_Click({RefreshMenuKeys;ShowPage 'Keybinds'})
$form.FindName('SettingsNav').Add_Click({ShowPage 'Settings'})
$form.FindName('SetupNav').Add_Click({ShowPage 'Setup'})
function ChooseGameExe{
 $launcher=if($script:gameRoot -and (($path.Text -eq $script:gameRoot) -or $path.Text.StartsWith($script:gameRoot+'\',[StringComparison]::OrdinalIgnoreCase))){$script:gameLauncher}else{''};$files=@(FindFusionExecutable $path.Text $launcher)
 if($files.Count -eq 1 -or ($files.Count -gt 1 -and $files[0].Score -gt $files[1].Score)){$path.Text=Split-Path $files[0].Path;return $files[0].Path}
 $dialog=New-Object Windows.Forms.OpenFileDialog;$dialog.Filter='Game executable|*.exe';$dialog.Title='Choose the actual game EXE';$dialog.InitialDirectory=$path.Text;if($files.Count){$dialog.InitialDirectory=Split-Path $files[0].Path;$dialog.FileName=$files[0].Path}
 try{if($dialog.ShowDialog() -eq 'OK'){$path.Text=Split-Path $dialog.FileName;return $dialog.FileName}}finally{$dialog.Dispose()}
 return $null
}
$form.FindName('Play').Add_Click({RunAction {
 $mp=ManifestPath $store $path.Text;$exe=$null
 if(Test-Path -LiteralPath $mp){$m=ReadLaminrState $mp;$exe=$m.LaunchExe}
 if(-not $exe -or -not(Test-Path -LiteralPath $exe)){$exe=ChooseGameExe}
 if($exe){AssertFusionExecutable $exe;Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe);$status.Text='Launch requested. Restore only after closing the game.'}
}})
# Hardware and launcher discovery run in another runspace so the intro remains skippable.
$script:gpu=[pscustomobject]@{Names='Checking';Nvidia=$false;Known=$false}
$script:startup=[PowerShell]::Create()
function SaveStartupCache($data){
 New-Item -ItemType Directory -Path $store -Force|Out-Null
 WriteState $data $script:cachePath
}
[void]$script:startup.AddScript({param($lib,$store,$cache)
 . $lib
 $gpu=GetFusionGpu;$ram=(Get-CimInstance Win32_ComputerSystem -ErrorAction SilentlyContinue).TotalPhysicalMemory;$os=(Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue).Caption
 [pscustomobject]@{Version=1;Gpu=$gpu;Ram=[math]::Round($ram/1GB);OS=$os;Games=@(FindFusionGames $store)}
}).AddArgument("$PSScriptRoot/library.ps1").AddArgument($store).AddArgument($script:cachePath)
$script:startupResult=$script:startup.BeginInvoke()
$script:introStart=Get-Date
$script:startupTimer=New-Object Windows.Threading.DispatcherTimer
$script:startupTimer.Interval=[TimeSpan]::FromMilliseconds(150)

$script:startupTimer.Add_Tick({
 $elapsed=((Get-Date)-$script:introStart).TotalSeconds
 if($elapsed -ge 10){$form.FindName('Intro').Visibility='Collapsed'}
 if($script:startupResult -and $script:startupResult.IsCompleted -and $elapsed -ge 6){
  try{
   $result=@($script:startup.EndInvoke($script:startupResult))[-1];$script:gpu=$result.Gpu;$script:cachedSystem=$result;SaveStartupCache $result
   $form.FindName('Hardware').Text="$($script:gpu.Names) | $($result.Ram) GB RAM | $($result.OS)"
   $form.FindName('IntroStatus').Text='Your setup is ready.'
   $form.FindName('IntroProgress').IsIndeterminate=$false;$form.FindName('IntroProgress').Value=100
   $copies=@($result.Games)
   $form.FindName('MsfsCopies').ItemsSource=$copies
   if($copies.Count){$form.FindName('MsfsCopies').SelectedIndex=0}else{$path.Text=''}
   $status.Text='Simulator detection complete. Open Setup to install, play or restore.'
  }catch{$form.FindName('Hardware').Text='Hardware detection was unavailable. Choose settings in game after checking your graphics card.'}
  finally{$script:startup.Dispose();$script:startupResult=$null;$script:startupTimer.Stop();RefreshHomeState;$form.FindName('Intro').Visibility='Collapsed'}
 }
})
$form.FindName('MsfsCopies').Add_SelectionChanged({$copy=$form.FindName('MsfsCopies').SelectedItem;if($copy){$script:gameRoot=$copy.Folder;$script:gameLauncher=$copy.Launcher;$path.Text=if($copy.InstallFolder){$copy.InstallFolder}else{$copy.Folder};$form.FindName('SelectedTitle').Text=$copy.Name;RefreshHomeState}})

$form.FindName('TroubleshootingNav').Add_Click({ShowPage 'Troubleshooting'})
$form.FindName('RecoveryRepair').Add_Click({$form.FindName('Repair').RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Button]::ClickEvent))})
$form.FindName('RecoveryRestore').Add_Click({$form.FindName('Restore').RaiseEvent([Windows.RoutedEventArgs]::new([Windows.Controls.Button]::ClickEvent))})
$form.FindName('ResetDefaults').Add_Click({RunAction {
 if([Windows.MessageBox]::Show($form,'Back up and reset laminr Performance and image-effect settings? This selects a new empty look. Saved presets and MSFS settings are kept. Close MSFS first.','Reset laminr settings','YesNo','Question','No') -ne 'Yes'){return}
 $saved=ResetLaminrSettings $path.Text $Payload $store;$status.Text='Defaults restored; effects are off. Previous settings saved at '+$saved
}})
function CancelSnapshotCapture {$script:capturingSnapshot=$false;$form.FindName('ChangeSnapshotKey').Content='Change'}
function RefreshMenuKeys {
 CancelSnapshotCapture
 $script:capturingHotSwap=$false;$form.FindName('ChangeHotSwapKey').Content='Change'
 $script:capturingMenuKey=$false;$form.FindName('ChangeMenuKey').Content='Change'
 $form.FindName('KeybindsGame').Text=if([string]::IsNullOrWhiteSpace($path.Text)){'Select your installation on Setup.'}else{'Selected game: '+$path.Text}
 $keys=@{ShortcutKey=45;BackupShortcutKey=79};$script:menuKeyCandidate=0
 try{if(-not [string]::IsNullOrWhiteSpace($path.Text)){$keys=GetMenuSettings $path.Text}}
 catch{$form.FindName('PrimaryMenuKey').Text='Unavailable';$form.FindName('KeyCaptureHint').Text='Unable to read shortcuts: '+$_.Exception.Message;return}
 $script:snapshotCandidate=[int]$keys.SnapshotKey
 $form.FindName('SnapshotKey').Text=if($script:snapshotCandidate -gt 0){([Windows.Forms.Keys]$script:snapshotCandidate).ToString()}else{'Not set'}
 $script:hotSwapCandidate=[int]$keys.PresetHotSwapKey
 $form.FindName('HotSwapKey').Text=if($script:hotSwapCandidate -gt 0){([Windows.Forms.Keys]$script:hotSwapCandidate).ToString()}else{'Not set'}
 $script:menuKeyCandidate=[int]$keys.ShortcutKey
 $form.FindName('PrimaryMenuKey').Text=([Windows.Forms.Keys]$script:menuKeyCandidate).ToString()
 $form.FindName('KeyCaptureHint').Text='Click Change, press one key, then Save shortcuts. Escape cancels. Turn Num Lock on for number-pad digits.'
 $form.FindName('BackupMenuHint').Text='Recovery shortcut: Ctrl+Shift+'+([Windows.Forms.Keys][int]$keys.BackupShortcutKey).ToString()+' (no Insert or numpad needed). Saving the primary key keeps this shortcut.'
}
function BeginMenuKeyCapture {
 CancelSnapshotCapture
 $script:capturingHotSwap=$false;$form.FindName('ChangeHotSwapKey').Content='Change'
 $script:capturingMenuKey=$true;$form.FindName('ChangeMenuKey').Content='Press a key...'
 $form.FindName('KeyCaptureHint').Text='Press one key for the menu. Escape cancels; modifier combinations are not supported for the primary key.'
 [void]$form.FindName('ChangeMenuKey').Focus()
}
function CaptureMenuKey($event) {
 if(-not $script:capturingMenuKey){return};$event.Handled=$true
 $key=$event.Key;if($key -eq [Windows.Input.Key]::System){$key=$event.SystemKey}
 if($key -eq [Windows.Input.Key]::Escape){$script:capturingMenuKey=$false;$form.FindName('ChangeMenuKey').Content='Change';$form.FindName('KeyCaptureHint').Text='Cancelled. The selected key is unchanged.';return}
 $code=[Windows.Input.KeyInterop]::VirtualKeyFromKey($key)
 if([Windows.Input.Keyboard]::Modifiers -ne [Windows.Input.ModifierKeys]::None -or $code -notin (@(33..40)+@(45,46)+@(48..57)+@(65..90)+@(96..111)+@(112..123))){$form.FindName('KeyCaptureHint').Text='Choose a letter, number, function or navigation key without modifiers. Escape cancels.';return}
 $script:menuKeyCandidate=$code;$script:capturingMenuKey=$false
 $form.FindName('PrimaryMenuKey').Text=([Windows.Forms.Keys]$code).ToString();$form.FindName('ChangeMenuKey').Content='Change'
 $form.FindName('KeyCaptureHint').Text='Not saved yet. Click Save shortcuts to apply this key after restarting the game.'
}
$form.FindName('ChangeMenuKey').Add_Click({BeginMenuKeyCapture})
$form.Add_PreviewKeyDown({CaptureMenuKey $_})
$form.Add_Deactivated({if($script:capturingMenuKey){$script:capturingMenuKey=$false;$form.FindName('ChangeMenuKey').Content='Change';$form.FindName('KeyCaptureHint').Text='Key capture cancelled when the manager lost focus.'}})
$form.FindName('ChangeHotSwapKey').Add_Click({CancelSnapshotCapture;$script:capturingMenuKey=$false;$form.FindName('ChangeMenuKey').Content='Change';$script:capturingHotSwap=$true;$form.FindName('ChangeHotSwapKey').Content='Press a key...';$form.FindName('KeyCaptureHint').Text='Press a hotswap key. Escape cancels; Backspace clears. Then Save shortcuts. Turn Num Lock on for number-pad digits.'})
$form.FindName('ClearHotSwapKey').Add_Click({$script:capturingHotSwap=$false;$script:hotSwapCandidate=0;$form.FindName('HotSwapKey').Text='Not set';$form.FindName('ChangeHotSwapKey').Content='Change';$form.FindName('KeyCaptureHint').Text='Click Save shortcuts to keep this change.'})
$form.Add_Deactivated({$script:capturingHotSwap=$false;$form.FindName('ChangeHotSwapKey').Content='Change'})
$form.FindName('ChangeSnapshotKey').Add_Click({$script:capturingMenuKey=$false;$script:capturingHotSwap=$false;$form.FindName('ChangeMenuKey').Content='Change';$form.FindName('ChangeHotSwapKey').Content='Change';$script:capturingSnapshot=$true;$form.FindName('ChangeSnapshotKey').Content='Press a key...';$form.FindName('KeyCaptureHint').Text='Press a SnapShot key. Escape cancels; Backspace clears. Then Save shortcuts. Turn Num Lock on for number-pad digits.'})
$form.FindName('ClearSnapshotKey').Add_Click({CancelSnapshotCapture;$script:snapshotCandidate=0;$form.FindName('SnapshotKey').Text='Not set'})
function CaptureActionKey($event,[bool]$KeyUp=$false) {
 if(-not $script:capturingSnapshot -and -not $script:capturingHotSwap){return}
 $key=$event.Key;if($key -eq [Windows.Input.Key]::System){$key=$event.SystemKey}
 $code=[Windows.Input.KeyInterop]::VirtualKeyFromKey($key)
 # Windows may send only key-up for Print Screen. A down that completed capture
 # already clears the flag, so its later key-up cannot bind or trigger twice.
 if($KeyUp -and $code -ne 44){return}
 $event.Handled=$true
 $isSnapshot=[bool]$script:capturingSnapshot
 $button=if($isSnapshot){'ChangeSnapshotKey'}else{'ChangeHotSwapKey'}
 $label=if($isSnapshot){'SnapshotKey'}else{'HotSwapKey'}
 if($key -eq [Windows.Input.Key]::Escape){$script:capturingSnapshot=$false;$script:capturingHotSwap=$false;$form.FindName($button).Content='Change';$form.FindName('KeyCaptureHint').Text='Cancelled. The selected key is unchanged.';return}
 if($code -eq 8){$code=0}
 elseif([Windows.Input.Keyboard]::Modifiers -ne [Windows.Input.ModifierKeys]::None -or $code -notin (@(33..40)+@(44,45,46)+@(48..57)+@(65..90)+@(96..111)+@(112..123))){$form.FindName('KeyCaptureHint').Text='Unsupported key. Choose a letter, number, function, navigation or Print Screen key without modifiers. Turn Num Lock on for number-pad digits.';return}
 $other=if($isSnapshot){$script:hotSwapCandidate}else{$script:snapshotCandidate}
 try{$conflict=GetActionKeyConflict $path.Text $code $script:menuKeyCandidate $other}catch{$form.FindName('KeyCaptureHint').Text='Unable to check shortcuts: '+$_.Exception.Message;return}
 if($conflict){$form.FindName('KeyCaptureHint').Text='That key is already assigned to '+$conflict+'. Choose another key. Turn Num Lock on for number-pad digits.';return}
 if($isSnapshot){$script:snapshotCandidate=$code}else{$script:hotSwapCandidate=$code}
 $script:capturingSnapshot=$false;$script:capturingHotSwap=$false;$form.FindName($button).Content='Change'
 $form.FindName($label).Text=if($code -eq 44){'Print Screen'}elseif($code){([Windows.Forms.Keys]$code).ToString()}else{'Not set'}
 $form.FindName('KeyCaptureHint').Text='Click Save shortcuts to keep this change. Turn Num Lock on for number-pad digits.'
}
$form.Add_PreviewKeyDown({CaptureActionKey $_})
$form.Add_PreviewKeyUp({CaptureActionKey $_ $true})
$form.Add_Deactivated({CancelSnapshotCapture})
$form.FindName('BrowseSnapshots').Add_Click({RunAction {if([string]::IsNullOrWhiteSpace($path.Text)){throw 'Select your game in Setup first.'};$parent=$path.Text;$preferences=OwnedPath $path.Text 'laminrData/Snapshot-settings.ini';if(Test-Path -LiteralPath $preferences){if((Get-Item -LiteralPath $preferences).Length -gt 65536){throw 'Snapshot settings are oversized.'};foreach($line in [IO.File]::ReadAllLines($preferences)){if($line -match '^Directory=(.+)$'){$parent=$Matches[1]}}};$folder=OwnedPath $parent 'laminr Snapshots';[IO.Directory]::CreateDirectory($folder)|Out-Null;Start-Process -FilePath (Join-Path $env:WINDIR 'explorer.exe') -ArgumentList ('"'+$folder+'"')}})
$form.FindName('LoadMenuKeys').Add_Click({RunAction {RefreshMenuKeys;$status.Text='Showing shortcuts for the selected game folder.'}})
$form.FindName('SaveMenuKeys').Add_Click({RunAction {
 if($script:capturingMenuKey -or -not $script:menuKeyCandidate){throw 'Press a primary menu key first.'}
 $keys=GetMenuSettings $path.Text
 if($script:capturingSnapshot){throw 'Finish choosing the SnapShot key first.'}
 if($script:capturingHotSwap){throw 'Finish choosing the hotswap key first.'}
 SetMenuSettings $path.Text $script:menuKeyCandidate ([int]$keys.BackupShortcutKey) $script:hotSwapCandidate $script:snapshotCandidate
 RefreshMenuKeys;$status.Text='Shortcuts saved. Restart the game to use them. Your recovery shortcut is unchanged.'
}})
$form.FindName('DefaultMenuKeys').Add_Click({RunAction {SetMenuSettings $path.Text 45 79 0 0;RefreshMenuKeys;$status.Text='Default shortcuts saved: Insert and Ctrl+Shift+O. Restart MSFS.'}})
$path.Add_TextChanged({RefreshMenuKeys})
RefreshMenuKeys
ShowPage 'Home'
# Raise the shared splash/installer window once, without keeping it above other apps.
$form.Add_Loaded({$form.Topmost=$true;[void]$form.Activate()})
$form.Add_ContentRendered({$form.Topmost=$false;[void]$form.Activate();$script:introStart=Get-Date;$form.FindName('IntroStatus').Text=if(Test-Path -LiteralPath $script:cachePath){'Loading your saved setup...'}else{'Checking graphics hardware and finding Microsoft Flight Simulator installations...'};$status.Text=$form.FindName('IntroStatus').Text;$script:startupTimer.Start()})
$form.Add_Closed({$script:startupTimer.Stop();if($script:startupResult){$script:startup.BeginStop($null,$null)|Out-Null}})
[void]$form.ShowDialog()
