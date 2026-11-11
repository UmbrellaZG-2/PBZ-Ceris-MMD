param(
  [string]$Out = (Join-Path $PSScriptRoot '..\output\CerisMMD_PBZ_v37_v38.zip')
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$stage = Join-Path $root 'output\stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

function Copy-Into([string]$src, [string]$dstRel) {
  if (-not (Test-Path -LiteralPath $src)) { throw "Missing: $src" }
  $dst = Join-Path $stage $dstRel
  New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
  Copy-Item -LiteralPath $src -Destination $dst
}

$mods = 'ProjectPBZ\Content\Paks\~mods'
foreach ($f in 'zzz_CerisMMD_v37_P.pak', 'zzz_CerisMMD_v37_P.ucas', 'zzz_CerisMMD_v37_P.utoc') {
  Copy-Into (Join-Path $root "release\AssetPack\$f") "$mods\$f"
}

$rt = 'platform\steam\ue4ss\Mods\CerisFullMMD'
Copy-Into (Join-Path $root 'release\Runtime\CerisFullMMD\dlls\main.dll') "$rt\dlls\main.dll"
Copy-Into (Join-Path $root 'release\Runtime\CerisFullMMD\enabled.txt') "$rt\enabled.txt"
Copy-Into (Join-Path $root 'metadata\game_plan.json') "$rt\game_plan.json"
Copy-Into (Join-Path $root 'metadata\expressions.json') "$rt\expressions.json"
Copy-Into (Join-Path $root 'metadata\original_names.json') "$rt\original_names.json"
Copy-Into (Join-Path $root 'model\model.pmx') "$rt\model.pmx"
foreach ($l in Get-ChildItem -LiteralPath (Join-Path $root 'model\licenses') -File) {
  Copy-Into $l.FullName "$rt\licenses\$($l.Name)"
}
Copy-Into (Join-Path $root 'docs\INSTALL.md') 'INSTALL.txt'

New-Item -ItemType Directory -Path (Split-Path $Out -Parent) -Force | Out-Null
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $Out -CompressionLevel Optimal
Remove-Item -Recurse -Force $stage

$item = Get-Item -LiteralPath $Out
$hash = (Get-FileHash -LiteralPath $Out -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host ("PACKED {0} {1} bytes sha256={2}" -f $item.FullName, $item.Length, $hash)
