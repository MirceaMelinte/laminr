function ShowUninstallOptions($Owner){
 [xml]$markup=@'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" Title="Uninstall laminr" Width="640" SizeToContent="Height" WindowStyle="None" ResizeMode="NoResize" AllowsTransparency="True" WindowStartupLocation="CenterOwner" Background="Transparent" Foreground="#F3EFFB" FontFamily="Segoe UI">
 <Border Background="#100E17" BorderBrush="#40314F" BorderThickness="1" CornerRadius="20" Padding="28"><StackPanel>
 <TextBlock Text="Uninstall laminr" FontSize="26" FontWeight="SemiBold"/>
 <TextBlock Text="Choose what to remove from your recorded installations." Foreground="#AA9CB9" Margin="0,12,0,20" TextWrapping="Wrap"/>
 <Border Background="#1D1727" CornerRadius="12" Padding="18"><StackPanel>
 <CheckBox Name="Everything" Content="Remove everything" Margin="0,0,0,20"/>
 <CheckBox Name="AppFiles" Content="Remove app files" IsChecked="True" Margin="0,0,0,14"/>
 <CheckBox Name="IniFiles" Content="Remove INI files" Margin="0,0,0,14"/>
 <CheckBox Name="Snapshots" Content="Remove snapshots"/>
 </StackPanel></Border>
 <TextBlock Text="INI removal covers saved presets in laminrData/Presets. Snapshot removal covers recorded, unchanged laminr captures, across your recorded games. Other photos, game settings, controls and saves are kept. Your downloaded installer EXE is kept. Edited shader sources are preserved in %LOCALAPPDATA%/laminr-Recovery." TextWrapping="Wrap" Foreground="#AA9CB9" Margin="0,16,0,12"/>
 <TextBlock Name="Warning" Visibility="Collapsed" Text="This will remove your INI files and snapshots. Are you sure?" TextWrapping="Wrap" Foreground="#FF8799" FontWeight="SemiBold" Margin="0,8,0,10"/>
 <CheckBox Name="Confirm" Visibility="Collapsed" Content="Yes, remove my INI files and snapshots" Margin="0,0,0,12"/>
 <StackPanel Orientation="Horizontal" HorizontalAlignment="Right" Margin="0,16,0,0"><Button Name="Cancel" Content="Close" Background="#282238" IsCancel="True" Margin="0,0,12,0"/><Button Name="Remove" Content="Remove selected" Background="#8650C8"/></StackPanel>
 </StackPanel></Border>
</Window>
'@
 $dialog=[Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($markup));$dialog.Owner=$Owner
 $dialog.Resources.Add([Windows.Controls.CheckBox],$Owner.FindResource('LaminrSwitch'))
 [xml]$theme=Get-Content "$PSScriptRoot/dialog-theme.xaml" -Raw;$dialog.Resources.MergedDictionaries.Add([Windows.Markup.XamlReader]::Load([Xml.XmlNodeReader]::new($theme)))
 $refresh={
  $all=[bool]$dialog.FindName('Everything').IsChecked
  $danger=$all -or ($dialog.FindName('IniFiles').IsChecked -and $dialog.FindName('Snapshots').IsChecked)
  $dialog.FindName('Warning').Visibility=if($danger){'Visible'}else{'Collapsed'}
  $dialog.FindName('Confirm').Visibility=if($danger){'Visible'}else{'Collapsed'}
  $any=$all -or $dialog.FindName('AppFiles').IsChecked -or $dialog.FindName('IniFiles').IsChecked -or $dialog.FindName('Snapshots').IsChecked
  $dialog.FindName('Remove').IsEnabled=$any -and (-not $danger -or $dialog.FindName('Confirm').IsChecked)
 }
 $dialog.FindName('Everything').Add_Click({
  $all=[bool]$dialog.FindName('Everything').IsChecked
  foreach($name in @('AppFiles','IniFiles','Snapshots')){$dialog.FindName($name).IsChecked=$all;$dialog.FindName($name).IsEnabled=-not $all}
  $dialog.FindName('Confirm').IsChecked=$false;&$refresh
 })
 foreach($name in @('AppFiles','IniFiles','Snapshots','Confirm')){$dialog.FindName($name).Add_Click($refresh)}
 $dialog.FindName('Cancel').Add_Click({$dialog.Close()})
 $dialog.FindName('Remove').Add_Click({$dialog.Tag=@{AppFiles=[bool]$dialog.FindName('AppFiles').IsChecked;IniFiles=[bool]$dialog.FindName('IniFiles').IsChecked;Snapshots=[bool]$dialog.FindName('Snapshots').IsChecked};$dialog.DialogResult=$true})
 &$refresh
 if($dialog.ShowDialog()){return $dialog.Tag}
 return $null
}
