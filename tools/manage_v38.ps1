param([ValidateSet('Validate','Install','Rollback')][string]$Action='Validate')
$ErrorActionPreference='Stop'
$taskRoot=$PSScriptRoot
$workspaceRoot=[IO.Path]::GetFullPath((Join-Path $taskRoot '..\..\..'))
$runtimeRoot='E:\SteamLibrary\steamapps\common\PBZ\platform\steam\ue4ss\Mods\CerisFullMMD'
$loaderRoot=Split-Path (Split-Path $runtimeRoot -Parent) -Parent
$modsRoot='E:\SteamLibrary\steamapps\common\PBZ\ProjectPBZ\Content\Paks\~mods'
$receiptPath=Join-Path $taskRoot 'install_receipt.json'
function ReadJson([string]$p){Get-Content -LiteralPath $p -Raw -Encoding UTF8 | ConvertFrom-Json}
function HashOf([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Contained([string]$p,[string]$root){
 $resolved=[IO.Path]::GetFullPath($p);$allowed=[IO.Path]::GetFullPath($root).TrimEnd('\')+'\'
 if(!$resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw "Unexpected path: $resolved"};return $resolved
}
function AssertStopped{
 if(Get-Process -ErrorAction SilentlyContinue | Where-Object {$_.ProcessName -match '^(ProjectPBZ|PBZ)(-Win64-Shipping)?$'}){throw 'Exit the game before install or rollback.'}
}
function NewBackup([string]$label){
 $root=Join-Path $workspaceRoot 'modwork\quarantine'
 $p=Contained (Join-Path $root ((Get-Date -Format 'yyyy-MM-dd_HHmmssfff')+'_'+$label)) $root
 New-Item -ItemType Directory -Path $p | Out-Null;return $p
}
function CheckPackage{
 foreach($f in $base.files){
  if($f.file -notmatch '^zzz_CerisMMD_v37_P\.(pak|utoc|ucas)$'){throw 'Unexpected package name'}
  if((HashOf (Contained (Join-Path $modsRoot $f.file) $modsRoot)) -ne $f.sha256){throw "v37 package changed: $($f.file)"}
 }
}
$base=ReadJson (Join-Path $taskRoot '..\v37_full_mmd_2026-11-10\install_receipt.json')
if((HashOf (Join-Path $loaderRoot 'UE4SS.dll')) -ne 'e1fb9a7e1d868afef6a091c2012e711c3ec47f05d100ba37f9631a4f75f31df2'){throw 'Installed UE4SS ABI changed'}
CheckPackage
$paths=@('dlls/main.dll','game_plan.json')
if($Action -eq 'Rollback'){
 AssertStopped
 $receipt=ReadJson $receiptPath
 if($receipt.version -ne 38 -or $receipt.runtime_dir -ne $runtimeRoot){throw 'Receipt mismatch'}
 $backup=Contained $receipt.backup_dir (Join-Path $workspaceRoot 'modwork\quarantine')
 foreach($f in $receipt.files){
  if($f.path -notin $paths){throw 'Unexpected runtime path'}
  if((HashOf (Contained (Join-Path $runtimeRoot $f.path) $runtimeRoot)) -ne $f.sha256){throw 'Installed file changed; refusing to overwrite'}
  if((HashOf (Contained (Join-Path $backup $f.path) $backup)) -ne $f.previous_sha256){throw 'Backup hash mismatch'}
 }
 $keep=NewBackup 'v38_rollback_preserved'
 foreach($f in $receipt.files){
  $destination=Contained (Join-Path $runtimeRoot $f.path) $runtimeRoot
  $kept=Contained (Join-Path $keep $f.path) $keep
  New-Item -ItemType Directory -Path (Split-Path $kept -Parent) -Force | Out-Null
  Copy-Item -LiteralPath $destination -Destination $kept
  Copy-Item -LiteralPath (Join-Path $backup $f.path) -Destination $destination -Force
  if((HashOf $destination) -ne $f.previous_sha256){throw 'Rollback verification failed'}
 }
 [ordered]@{restored_runtime_version=37;preserved_v38=$keep;at=(Get-Date -Format o)} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskRoot 'rollback_receipt.json') -Encoding UTF8
 Write-Output 'Restored v37 runtime. v38 files preserved.';return
}
$validation=ReadJson (Join-Path $taskRoot 'grip_validation.json')
$transfer=ReadJson (Join-Path $taskRoot 'ue_integration_postprocess.json')
if(!$validation.ok -or !$transfer.ok -or !$transfer.post_process){throw 'Grip or UE post-process validation failed'}
foreach($f in $validation.validated_files){if((HashOf $f.path) -ne $f.sha256){throw "Validation is stale: $($f.path)"}}
$plan=ReadJson (Join-Path $taskRoot 'game_plan.json')
$oldPlan=ReadJson (Join-Path $runtimeRoot 'game_plan.json')
if($plan.runtime_version -ne '38.0' -or $plan.grips.Count -ne 2 -or $plan.input_names.Count -ne 51){throw 'Grip plan schema mismatch'}
if(($plan.input_names -join ',') -ne ($oldPlan.input_names -join ',')){throw 'Blueprint input ABI changed'}
$sources=@((Join-Path $workspaceRoot 'modwork\mmd_runtime\build\main.dll'),(Join-Path $taskRoot 'game_plan.json'))
$files=@()
for($i=0;$i -lt $paths.Count;$i++){
 $destination=Contained (Join-Path $runtimeRoot $paths[$i]) $runtimeRoot
 $files += [ordered]@{path=$paths[$i];source=$sources[$i];bytes=(Get-Item -LiteralPath $sources[$i]).Length;sha256=(HashOf $sources[$i]);previous_sha256=(HashOf $destination)}
}
foreach($f in $base.native_files){
 $relative=$f.path -replace '^CerisFullMMD/',''
 if($relative -notin $paths -and $relative -ne 'expressions.json'){
  if((HashOf (Contained (Join-Path $runtimeRoot $relative) $runtimeRoot)) -ne $f.sha256){throw "Original runtime data changed: $relative"}
 }
}
if($Action -eq 'Validate'){Write-Output 'v38 grip runtime, palm constraints, UE transfer and original data checks passed.';return}
AssertStopped
if(Test-Path -LiteralPath $receiptPath){throw 'Installation receipt already exists'}
foreach($f in $files){
 $old=$base.native_files | Where-Object {$_.path -eq ('CerisFullMMD/'+$f.path)}
 if($old.sha256 -ne $f.previous_sha256){throw 'Destination is not the expected v37 runtime'}
}
$backup=NewBackup 'v38_grip_install_v37_runtime_backup'
$expressionHash=HashOf (Join-Path $runtimeRoot 'expressions.json')
if(Test-Path -LiteralPath (Join-Path $runtimeRoot 'runtime.log')){Copy-Item -LiteralPath (Join-Path $runtimeRoot 'runtime.log') -Destination (Join-Path $backup 'runtime_before_v38.log')}
foreach($f in $files){
 $saved=Contained (Join-Path $backup $f.path) $backup
 New-Item -ItemType Directory -Path (Split-Path $saved -Parent) -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $runtimeRoot $f.path) -Destination $saved
 if((HashOf $saved) -ne $f.previous_sha256){throw 'Backup verification failed'}
}
try{
 foreach($f in $files){
  $destination=Contained (Join-Path $runtimeRoot $f.path) $runtimeRoot
  Copy-Item -LiteralPath $f.source -Destination $destination -Force
  if((HashOf $destination) -ne $f.sha256){throw 'Installation verification failed'}
 }
 CheckPackage
 if((HashOf (Join-Path $runtimeRoot 'expressions.json')) -ne $expressionHash){throw 'Expression settings changed'}
 [ordered]@{version=38;asset_package_version=37;installed_at=(Get-Date -Format o);runtime_dir=$runtimeRoot;backup_dir=$backup;files=$files;in_game_verified=$false;status='installed_pending_game_validation';palm_validation=$validation.after} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
 Write-Output 'Installed v38 grip runtime. Hashes verified.'
 Write-Output "v37 runtime backup: $backup"
}catch{
 $failure=$_
 foreach($f in $files){Copy-Item -LiteralPath (Join-Path $backup $f.path) -Destination (Contained (Join-Path $runtimeRoot $f.path) $runtimeRoot) -Force}
 throw $failure
}
