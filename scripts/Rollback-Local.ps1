param([string]$BackupPath=$PSScriptRoot)
$ErrorActionPreference='Stop'
$backup=[IO.Path]::GetFullPath($BackupPath)
$m=Get-Content -LiteralPath (Join-Path $backup 'manifest.json') -Raw | ConvertFrom-Json
$workspace=[IO.Path]::GetFullPath($m.Workspace).TrimEnd('\')
if(!$backup.StartsWith($workspace+'\analysis\evidence\',[StringComparison]::OrdinalIgnoreCase)){throw 'Backup must be within the named workspace evidence directory.'}
if(Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -in @('unlockfps_nc','YuanShen','GenshinImpact') }){throw 'Close game/unlocker first; no processes will be terminated.'}
$target=Join-Path $workspace 'unlockfps_nc.exe'
$source=Join-Path $backup 'unlockfps_nc.previous.exe'
$stub=Join-Path $workspace 'UnlockerStub.dll'
if((Get-FileHash -LiteralPath $source).Hash -ne $m.OldExeSha256){throw 'Previous EXE is not intact.'}
$current=(Get-FileHash -LiteralPath $target).Hash
if($current -eq $m.OldExeSha256){'Already restored; latest user configuration was retained.';return}
if($current -ne $m.NewExeSha256){throw 'EXE has changed since this update; preserve it instead of overwriting.'}
if(Test-Path -LiteralPath $stub){
    if((Get-FileHash -LiteralPath $stub).Hash -ne $m.NativeSha256){throw 'Generated DLL is not the expected custom worker; inspect it first.'}
    Move-Item -LiteralPath $stub -Destination (Join-Path $backup ('custom-stub-'+[guid]::NewGuid().ToString('N')+'.dll'))
}
$stage=Join-Path $workspace 'unlockfps_nc.exe.rollback.tmp'
if(Test-Path -LiteralPath $stage){throw 'Rollback stage already exists; inspect it.'}
Copy-Item -LiteralPath $source -Destination $stage
[IO.File]::Replace($stage,$target,(Join-Path $backup ('custom-exe-'+[guid]::NewGuid().ToString('N')+'.exe')))
if($m.PreviousStub){Copy-Item -LiteralPath (Join-Path $backup 'UnlockerStub.previous.dll') -Destination $stub}
if((Get-FileHash -LiteralPath $target).Hash -ne $m.OldExeSha256){throw 'Rollback integrity check failed.'}
'Restored original upstream EXE. Current user configuration and shortcut were not overwritten.'
