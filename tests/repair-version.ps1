$ErrorActionPreference='Stop'
. "$PSScriptRoot/../installer/ownership.ps1"
AssertRepairVersion ([pscustomobject]@{Version=(GetBundledLaminrVersion)})
foreach($version in @('P0.20.11-MSFS24','P0.21-MSFS24','', $null)){
 $rejected=$false
 try{AssertRepairVersion ([pscustomobject]@{Version=$version})}catch{$rejected=$true}
 if(-not $rejected){throw "Repair accepted mismatched version: $version"}
}
'PASS: same-version repair accepted; older/newer/missing versions rejected'
$message='';try{AssertRepairVersion ([pscustomobject]@{Version='P0.0.0'})}catch{$message=$_.Exception.Message}
if($message -notlike '*Choose Update laminr in Setup*'){throw 'Mismatched repair did not point to the separate reviewed update action'}
'PASS: mismatched Repair points to the distinct Update action without applying it'
