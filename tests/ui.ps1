$ErrorActionPreference='Stop'
Add-Type -AssemblyName PresentationFramework
[xml]$xaml=Get-Content "$PSScriptRoot/../installer/manager.xaml" -Raw -Encoding UTF8
$form=[Windows.Markup.XamlReader]::Load((New-Object Xml.XmlNodeReader $xaml))
foreach($name in @('MsfsCopies','Install','Restore','Runtime','SettingsPage','TroubleshootingNav','TroubleshootingPage','ResetDefaults','RecoveryRepair','RecoveryRestore')){if(-not $form.FindName($name)){throw "Missing $name"}}
$tokens=$null;$errors=$null
[void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../installer/manager.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw ($errors|Out-String)}
$source=Get-Content "$PSScriptRoot/../installer/manager.ps1" -Raw
foreach($match in [regex]::Matches($source,'\$form\.FindName\(''([^'']+)''\)')){if(-not $form.FindName($match.Groups[1].Value)){throw ('Missing handler control: '+$match.Groups[1].Value)}}
$form.FindName('Intro').Visibility='Collapsed'
$form.FindName('HomePage').Visibility='Collapsed'
$form.FindName('TroubleshootingPage').Visibility='Visible'
$form.Content.Measure([Windows.Size]::new(1100,780));$form.Content.Arrange([Windows.Rect]::new(0,0,1100,780));$form.Content.UpdateLayout()
foreach($name in @('TroubleshootingNav','ResetDefaults','RecoveryRestore')){
 $control=$form.FindName($name);$point=$control.TranslatePoint([Windows.Point]::new(0,0),$form.Content)
 if($point.Y+$control.ActualHeight -gt 780 -or $control.ActualHeight -lt 1){throw ('Control cropped: '+$name)}
}
$form.FindName('TroubleshootingPage').Visibility='Collapsed'
$form.FindName('HomePage').Visibility='Visible'
$form.FindName('HomeDetection').Text='Not detected - select folder in Setup'
$form.FindName('Hardware').Text="NVIDIA GeForce RTX 5090, AMD Radeon(TM) Graphics | 63 GB RAM | Microsoft Windows 11 Pro"
foreach($width in @(1000,1100)){
 $form.Content.Measure([Windows.Size]::new($width,740));$form.Content.Arrange([Windows.Rect]::new(0,0,$width,740));$form.Content.UpdateLayout()
 $control=$form.FindName('OpenLibrary');$point=$control.TranslatePoint([Windows.Point]::new(0,0),$form.Content)
 if($point.X+$control.ActualWidth -gt $width -or $control.ActualHeight -lt 100){throw "Home game button clipped at $width"}
}
$bitmap=[Windows.Media.Imaging.RenderTargetBitmap]::new(1100,740,96,96,[Windows.Media.PixelFormats]::Pbgra32);$bitmap.Render($form.Content)
$encoder=[Windows.Media.Imaging.PngBitmapEncoder]::new();$encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
$imagePath=Join-Path $PSScriptRoot '../test-run/home-page.png';$stream=[IO.File]::Create($imagePath);try{$encoder.Save($stream)}finally{$stream.Dispose()}
$form.Close()
'PASS: WPF layout loaded, edition controls present, manager syntax valid'
