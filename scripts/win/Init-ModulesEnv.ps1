<#
.SYNOPSIS
    Pins the newest installed MSVC toolset for the C++20-modules Windows bring-up and imports the
    Visual Studio x64 developer environment into the current PowerShell session.

.DESCRIPTION
    Defaults the toolset to 14.51.36231 -- the "18"/2026-generation MSVC (_MSC_VER 1951, CMake
    compiler version 19.51), the newest installed on the reference machine and the module-capable
    toolset the handoff asks us to pin (step W0 of
    docs\technical_notes\cxx20-modules-migration\msvc-modules-handoff.typ).

    vcvarsall only mutates a cmd.exe environment, so this script runs it and imports the resulting
    variables into THIS PowerShell session. Because $env: changes are process-level, cl.exe and the
    pinned toolset stay on PATH for whatever you run next -- no dot-sourcing required:

        .\scripts\win\Init-ModulesEnv.ps1
        cmake --preset windows-msvc-dbg -D SOURCETRAIL_CXX_MODULES=ON

.PARAMETER ToolsetVersion
    MSVC toolset to pin (default 14.51.36231). Override to test another, e.g. -ToolsetVersion 14.44.35207.

.PARAMETER Arch
    vcvarsall architecture argument (default x64).

.NOTES
    Companion: docs\technical_notes\cxx20-modules-migration\msvc-modules-handoff.typ (W0),
    tools\modules-migration\repro-gmf-attachment\run-msvc.ps1 (the risk-1 smoke test).
#>
[CmdletBinding()]
param(
    [string] $ToolsetVersion = '14.51.36231',
    [string] $Arch = 'x64'
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere not found at '$vswhere' - is Visual Studio installed?"
}

# Prefer a stable install that ships the C++ x64 toolset; fall back to prerelease (Insiders).
$req = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
$install = & $vswhere -latest -products * -requires $req -property installationPath | Select-Object -First 1
if (-not $install) {
    $install = & $vswhere -prerelease -latest -products * -requires $req -property installationPath | Select-Object -First 1
}
if (-not $install) { throw "No Visual Studio install with the C++ x64 toolset ($req) was found." }

$toolsetDir = Join-Path $install "VC\Tools\MSVC\$ToolsetVersion"
if (-not (Test-Path $toolsetDir)) {
    $have = (Get-ChildItem (Join-Path $install 'VC\Tools\MSVC') -Directory |
        Select-Object -ExpandProperty Name) -join ', '
    throw "Toolset $ToolsetVersion not present under '$install'. Installed: $have"
}

$vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvarsall.bat'
Write-Host "[modules-env] $install  (toolset $ToolsetVersion, $Arch)"

# Run vcvarsall in cmd (its banner suppressed) and dump the resulting environment; import each
# VAR=VALUE line into this session. Split on the first '=' only (PATH etc. contain '='), and strip
# any trailing CR the cmd pipe leaves behind.
$imported = 0
$envDump = cmd /c "`"$vcvars`" $Arch -vcvars_ver=$ToolsetVersion >nul 2>&1 && set"
foreach ($line in $envDump) {
    $idx = $line.IndexOf('=')
    if ($idx -gt 0) {
        $name = $line.Substring(0, $idx)
        $value = $line.Substring($idx + 1).TrimEnd("`r")
        Set-Item -Path "Env:$name" -Value $value
        $imported++
    }
}
Write-Verbose "[modules-env] imported $imported environment variables from vcvarsall"

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) { throw "cl.exe still not on PATH after importing the VS environment." }
Write-Host "[modules-env] cl.exe -> $($cl.Source)"
Write-Host "[modules-env] VCToolsVersion=$env:VCToolsVersion"
