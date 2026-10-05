param([switch]$TestsOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$gxx = (Get-Command g++ -ErrorAction Stop).Source
$gcc = (Get-Command gcc -ErrorAction Stop).Source
function CheckExit([string]$stage) { if ($LASTEXITCODE -ne 0) { throw "$stage failed ($LASTEXITCODE)" } }

# Pure policy tests do not load/inject the DLL or touch any game process.
& $gxx -std=c++23 -O2 -static -static-libgcc -static-libstdc++ `
    -DNOMINMAX (Join-Path $root 'tests\native_policy.cpp') (Join-Path $root 'UnlockerStub\Utils.cpp') `
    -o (Join-Path $out 'native_policy_tests.exe') -luser32
CheckExit 'Native policy compile'
& (Join-Path $out 'native_policy_tests.exe')
CheckExit 'Native policy tests'
dotnet test (Join-Path $root 'tests\Managed\Managed.csproj') --nologo
CheckExit 'Managed protocol tests'
if ($TestsOnly) { return }

$native = Join-Path $root 'UnlockerStub'
& $gcc -std=c11 -O2 -DZYDIS_STATIC_BUILD -DZYCORE_STATIC_BUILD -I $native `
    -c (Join-Path $native 'Zydis.c') -o (Join-Path $out 'Zydis.o')
CheckExit 'Zydis compile'
& $gxx -std=c++23 -O2 -shared -static -static-libgcc -static-libstdc++ -DNOMINMAX `
    -DZYDIS_STATIC_BUILD -DZYCORE_STATIC_BUILD -I $native `
    (Join-Path $native 'dllmain.cpp') (Join-Path $native 'Utils.cpp') (Join-Path $out 'Zydis.o') `
    -o (Join-Path $out 'UnlockerStub.dll') -lntdll -ldbghelp -luser32 -lkernel32
CheckExit 'Native worker compile'
$fixture = Join-Path $out 'native-fixture'
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
& $gxx -std=c++23 -O2 -static -static-libgcc -static-libstdc++ -DNOMINMAX `
    (Join-Path $root 'tests\native_fixture.cpp') -o (Join-Path $fixture 'GenshinImpact.exe')
CheckExit 'Synthetic fixture compile'
foreach ($mode in @('normal-stop','bad-protocol','controller-exit')) {
    & (Join-Path $fixture 'GenshinImpact.exe') --fixture (Join-Path $out 'UnlockerStub.dll') $mode
    CheckExit "Synthetic fixture $mode"
}
Copy-Item -LiteralPath (Join-Path $out 'UnlockerStub.dll') -Destination (Join-Path $root 'unlockfps_nc\Resources\UnlockerStub.dll')
dotnet publish (Join-Path $root 'unlockfps_nc\unlockfps_nc.csproj') -c Release -r win-x64 `
    --self-contained false -p:PublishSingleFile=true -p:PublishReadyToRun=false -o (Join-Path $out 'publish') --nologo
CheckExit 'Managed publish'
# Never run the output EXE here: the user's existing AutoStart setting may launch the game.
