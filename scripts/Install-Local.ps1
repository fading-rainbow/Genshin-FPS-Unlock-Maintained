param([Parameter(Mandatory)][string]$WorkspacePath)
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath($WorkspacePath).TrimEnd('\')
$project=Split-Path -Parent $PSScriptRoot
$candidate=Join-Path $project 'build\publish\unlockfps_nc.exe'
$target=Join-Path $workspace 'unlockfps_nc.exe'
$config=Join-Path $workspace 'fps_config.json'
$stub=Join-Path $workspace 'UnlockerStub.dll'
$stage=Join-Path $workspace 'unlockfps_nc.exe.update.tmp'
function Idle {
    if(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in @('unlockfps_nc','YuanShen','GenshinImpact') }) {
        throw 'Close the game and unlocker first; no process will be terminated.'
    }
}
Idle
if(!(Test-Path -LiteralPath $target) -or !(Test-Path -LiteralPath $config)) {throw 'Workspace is not the existing unlocker installation.'}
if(Test-Path -LiteralPath $stage) {throw 'An update stage already exists; preserve and inspect it.'}
$oldHash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
if($oldHash -ne 'AB383E989A6EA0EA58024889BE36E622FC2DC75B0A3F84230DC907716A990C10') {throw 'Installed EXE differs from the reviewed upstream baseline; do not overwrite it.'}
if((Get-Item -LiteralPath $candidate).VersionInfo.ProductVersion -ne '3.5.0-local.1') {throw 'Unexpected candidate version.'}
$newHash=(Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash
$configHash=(Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash
$nativeHash=(Get-FileHash -LiteralPath (Join-Path $project 'build\UnlockerStub.dll') -Algorithm SHA256).Hash
$backup=Join-Path $workspace ('analysis\evidence\local-build-20261005-'+[guid]::NewGuid().ToString('N').Substring(0,8))
if(![IO.Path]::GetFullPath($backup).StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backup escaped workspace.'}
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'unlockfps_nc.previous.exe')
Copy-Item -LiteralPath $config -Destination (Join-Path $backup 'fps_config.previous.json')
if((Get-FileHash -LiteralPath (Join-Path $backup 'unlockfps_nc.previous.exe')).Hash -ne $oldHash){throw 'Backup verification failed.'}
Copy-Item -LiteralPath $candidate -Destination $stage
if((Get-FileHash -LiteralPath $stage).Hash -ne $newHash){throw 'Stage verification failed.'}
Idle
$hadStub=Test-Path -LiteralPath $stub
if($hadStub){Move-Item -LiteralPath $stub -Destination (Join-Path $backup 'UnlockerStub.previous.dll')}
try {[IO.File]::Replace($stage,$target,(Join-Path $backup 'replaced-original.exe'))}
catch {
    if($hadStub -and !(Test-Path -LiteralPath $stub)){Move-Item -LiteralPath (Join-Path $backup 'UnlockerStub.previous.dll') -Destination $stub}
    throw
}
if((Get-FileHash -LiteralPath $target).Hash -ne $newHash -or (Get-FileHash -LiteralPath $config).Hash -ne $configHash){throw 'Post-update integrity check failed; keep backups and inspect.'}
$manifest=[ordered]@{Workspace=$workspace;OldExeSha256=$oldHash;NewExeSha256=$newHash;NativeSha256=$nativeHash;ConfigSha256=$configHash;PreviousStub=$hadStub;Version='3.5.0-local.1';TargetExecuted=$false}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Rollback-Local.ps1') -Destination (Join-Path $backup 'Rollback-Local.ps1')
"Installed 3.5.0-local.1; configuration unchanged; rollback folder: $backup"
