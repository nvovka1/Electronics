<#
.SYNOPSIS
    Runs the host unit tests in test/test_native with MSVC.

.DESCRIPTION
    `pio test -e native` needs gcc on PATH. This machine has Visual Studio but
    no gcc, so this script does the same job through cl.exe: it compiles
    everything under lib/ plus the test file plus Unity, runs the binary, and
    returns its exit code.

    Both routes build the identical sources, so a green run here means the same
    thing as a green `pio test -e native` on a machine that has gcc.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\run_native_tests.ps1
#>

[CmdletBinding()]
param(
    [switch]$KeepIntermediates
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$unitySrc = Join-Path $projectRoot '.pio\libdeps\native\Unity\src'
$outDir = Join-Path $projectRoot '.pio\build\native-msvc'
$exePath = Join-Path $outDir 'tests.exe'

# --- Unity ----------------------------------------------------------------
# It is a PlatformIO dependency, so let PlatformIO fetch it rather than
# vendoring a copy that then drifts.
if (-not (Test-Path (Join-Path $unitySrc 'unity.c'))) {
    Write-Host 'Unity not found, installing the native environment packages...'
    $pio = Join-Path $HOME '.platformio\penv\Scripts\pio.exe'
    if (-not (Test-Path $pio)) { throw "pio.exe not found at $pio" }
    & $pio pkg install -e native
    if (-not (Test-Path (Join-Path $unitySrc 'unity.c'))) {
        throw "Unity still missing at $unitySrc"
    }
}

# --- Visual Studio --------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere" }

$vsRoot = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsRoot) { throw 'No Visual Studio installation with the C++ tools was found.' }

$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

# --- sources --------------------------------------------------------------
$sources = @(
    Get-ChildItem -Path (Join-Path $projectRoot 'lib') -Recurse -Filter '*.cpp' |
        Select-Object -ExpandProperty FullName
    Join-Path $projectRoot 'test\test_native\test_main.cpp'
    Join-Path $unitySrc 'unity.c'
)

$includeDirs = @(
    Get-ChildItem -Path (Join-Path $projectRoot 'lib') -Directory |
        Select-Object -ExpandProperty FullName
    $unitySrc
)

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

# MSVC accepts forward slashes, and they sidestep the classic /Fo"dir\" trap
# where the trailing backslash escapes the closing quote and cl loses the rest
# of its command line.
$outDirFwd = $outDir.Replace('\', '/')
$exeFwd = $exePath.Replace('\', '/')

# /std:c++17 matches the -std=gnu++17 the native env uses.
# UNIT_TEST and PROTO_VERSION mirror the build_flags in platformio.ini.
$clArgs = @(
    '/nologo', '/std:c++17', '/EHsc', '/W3', '/permissive-'
    '/DUNIT_TEST=1', '/DPROTO_VERSION=1', '/D_CRT_SECURE_NO_WARNINGS'
) + ($includeDirs | ForEach-Object { "/I`"$_`"" }) + @(
    "/Fo`"$outDirFwd/`"", "/Fe`"$exeFwd`""
) + ($sources | ForEach-Object { "`"$_`"" })

$command = "call `"$vcvars`" >nul 2>nul && cl $($clArgs -join ' ')"

Write-Host "Compiling $($sources.Count) source files with MSVC..."
& cmd.exe /c $command
if ($LASTEXITCODE -ne 0) { throw "Compilation failed with exit code $LASTEXITCODE" }

if (-not $KeepIntermediates) {
    Get-ChildItem -Path $outDir -Filter '*.obj' -ErrorAction SilentlyContinue |
        Remove-Item -Force
}

Write-Host ''
& $exePath
$testExit = $LASTEXITCODE

Write-Host ''
if ($testExit -eq 0) {
    Write-Host 'Host tests PASSED' -ForegroundColor Green
} else {
    Write-Host "Host tests FAILED (exit $testExit)" -ForegroundColor Red
}
exit $testExit
