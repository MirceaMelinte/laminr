function ConfirmOptionalDlss($Owner,[bool]$Supported){
 [xml]$markup=@"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Title="laminr setup" Width="550" SizeToContent="Height" ResizeMode="NoResize" WindowStartupLocation="CenterOwner" Background="#17121F" Foreground="#F3EFFB" FontFamily="Segoe UI" ShowInTaskbar="False">
 <StackPanel Margin="28"><TextBlock Text="Would you like to install DLSS 5?" FontSize="23" FontWeight="SemiBold" TextWrapping="Wrap"/>
 <TextBlock Margin="0,16,0,12" Text="Yes downloads the RTX 50 files. DLSS/Streamline come from NVIDIA; the NVIDIA-signed neural model comes from a pinned community GitHub release (RHI). Downloads are hash-verified. Installing files does not switch the effect on." TextWrapping="Wrap" Foreground="#C2B0D7"/>
 <TextBlock Text="No installs laminr with image effects and presets, without adding DLSS 5 files. Your game's built-in DLSS stays untouched." TextWrapping="Wrap" Foreground="#C2B0D7"/>
 <TextBlock Name="Availability" Margin="0,14,0,0" Foreground="#FFBE83" TextWrapping="Wrap"/>
 <StackPanel Orientation="Horizontal" HorizontalAlignment="Right" Margin="0,24,0,0"><Button Name="Yes" Content="Yes" MinWidth="100" Margin="0,0,12,0"/><Button Name="No" Content="No" MinWidth="100" IsDefault="True"/></StackPanel>
 </StackPanel>
</Window>
"@
 $dialog=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $markup));$dialog.Owner=$Owner
 foreach($name in @('Yes','No')){$dialog.FindName($name).Style=$Owner.FindResource([Windows.Controls.Button])}
 $dialog.FindName('Yes').IsEnabled=$Supported
 if(-not $Supported){$dialog.FindName('Availability').Text='The current hardware or game check does not support this option. You can still choose No and install image effects.'}
 $dialog.FindName('Yes').Add_Click({$dialog.Tag='Yes';$dialog.Close()})
 $dialog.FindName('No').Add_Click({$dialog.Tag='No';$dialog.Close()})
 [void]$dialog.ShowDialog()
 if($dialog.Tag -eq 'Yes'){return $true};if($dialog.Tag -eq 'No'){return $false};return $null
}
function UseOptionalDlss($Manifest,$Plan){return ($Manifest.OptionalDlss -eq $true -and $Plan.DownloadNvidia -eq $true)}

function GetNvidiaDriverWarning($Gpu){
 if(-not $Gpu -or (-not $Gpu.Nvidia -and $Gpu.Names -notmatch '(?i)NVIDIA')){return ''}
 $drivers=@($Gpu.Drivers|Where-Object {$_.Name -match '(?i)NVIDIA'})
 if(-not $drivers.Count){return 'The NVIDIA driver version could not be detected. Compatibility is unknown. Please update to NVIDIA driver 616.92 or newer to help avoid possible issues.'}
 $messages=@(foreach($driver in $drivers){
  $version=[string]$driver.Version
  if($version -notmatch '^\d{3,4}\.\d{2}$'){'NVIDIA driver version unknown for '+$driver.Name+'. Please update to NVIDIA driver 616.92 or newer to help avoid possible issues.'}
  elseif([version]$version -lt [version]'616.92'){"Driver $version detected for $($driver.Name). Compatibility with this driver is unknown. Please update to 616.92 or newer to help avoid possible issues."}
 })
 return ($messages -join "`n`n")
}
function ConfirmNvidiaDriver($Owner,$Gpu){
 $message=GetNvidiaDriverWarning $Gpu
 if(-not $message){return $true}
 [xml]$markup=@"
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Title="NVIDIA driver recommendation" Width="560" SizeToContent="Height" ResizeMode="NoResize" WindowStartupLocation="CenterOwner" Background="#17121F" Foreground="#F3EFFB" FontFamily="Segoe UI" ShowInTaskbar="False"><StackPanel Margin="28">
<TextBlock Text="Check your graphics driver" FontSize="23" FontWeight="SemiBold" TextWrapping="Wrap"/>
<TextBlock Name="DriverDetails" Foreground="#FFBE83" TextWrapping="Wrap" Margin="0,16,0,14"/>
<TextBlock Text="This is laminr's recommended baseline, not a guarantee of compatibility. Cancel to update your driver, or continue with the current version." Foreground="#C2B0D7" TextWrapping="Wrap"/>
<StackPanel Orientation="Horizontal" HorizontalAlignment="Right" Margin="0,24,0,0"><Button Name="Cancel" Content="Cancel installation" IsCancel="True" IsDefault="True" Margin="0,0,12,0"/><Button Name="Continue" Content="Continue anyway"/></StackPanel>
</StackPanel></Window>
"@
 $dialog=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup));$dialog.Owner=$Owner
 $dialog.FindName('DriverDetails').Text=$message
 foreach($name in @('Cancel','Continue')){$dialog.FindName($name).Style=$Owner.FindResource([Windows.Controls.Button])}
 $dialog.FindName('Cancel').Add_Click({$dialog.DialogResult=$false}.GetNewClosure())
 $dialog.FindName('Continue').Add_Click({$dialog.DialogResult=$true}.GetNewClosure())
 return $dialog.ShowDialog() -eq $true
}
