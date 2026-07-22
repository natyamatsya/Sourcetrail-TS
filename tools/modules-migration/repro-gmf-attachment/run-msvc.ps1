<#
.SYNOPSIS
    MSVC port of run.sh -- the risk-1 attachment smoke test from the C++20-modules MSVC handoff.

.DESCRIPTION
    Translates clang's `--precompile -x c++-module` / `-fmodule-file=` steps into cl `/interface` +
    `/reference` (per the mapping table in
    docs\technical_notes\cxx20-modules-migration\msvc-modules-handoff.typ) and runs the 8-compile
    reproducer against the same .cppm/.h/.inl fixtures run.sh uses.

    This is a FINDINGS probe, not a pass/fail gate. clang uses STRONG module ownership and rejects the
    final m:f shape; MSVC uses WEAK ownership and may accept every step, including the one clang
    rejects (handoff Risk 0). The script records per-step results and never asserts an outcome; the
    macOS clang build remains the semantic referee.

    Run scripts\win\Init-ModulesEnv.ps1 first (or use a VS 18 x64 developer prompt) so cl.exe / the
    pinned toolset (14.51.36231) is on PATH.

.NOTES
    clang baseline (README.md): steps 1-7 pass; step 8 FAILS with
    "declaration 'trim' attached to named module 'a:s' cannot be attached to other modules".
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-Location -Path $PSScriptRoot

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "cl.exe not on PATH. Run scripts\win\Init-ModulesEnv.ps1 first, or use a VS 18 x64 developer prompt."
}

Remove-Item *.ifc, *.obj -ErrorAction SilentlyContinue

$flags = @('/std:c++latest', '/nologo', '/EHsc', '/c', '/interface', '/TP')
$results = [System.Collections.Generic.List[object]]::new()

function Invoke-Step {
    param(
        [int]      $N,
        [string]   $Desc,
        [string[]] $ClArgs,
        [switch]   $ClangFails
    )
    Write-Host ""
    Write-Host "+ [step $N] $Desc"
    Write-Host "  cl $($ClArgs -join ' ')"
    & cl.exe @ClArgs 2>&1 | ForEach-Object { Write-Host "    $_" }
    $ok = ($LASTEXITCODE -eq 0)
    Write-Host ("  -> step {0} {1} (exit {2})" -f $N, $(if ($ok) { 'ok' } else { 'FAILED' }), $LASTEXITCODE)
    $results.Add([pscustomobject]@{
        Step = $N; Ok = $ok; Exit = $LASTEXITCODE; ClangFails = [bool]$ClangFails; Desc = $Desc
    })
}

Invoke-Step 1 'a:s owner (defines trim inline via s.h)' ($flags + @('a_s.cppm', '/ifcOutput', 'a-s.ifc'))
Invoke-Step 2 'primary module a (export import :s)' ($flags + @('a.cppm', '/reference', 'a-s.ifc', '/ifcOutput', 'a.ifc'))
Invoke-Step 3 'plain module b2 (GMF textual parse of s.h)' ($flags + @('b2.cppm', '/ifcOutput', 'b2.ifc'))

Write-Host "`n== contrast 1 (clang accepts silently, ill-formed NDR): consumer of a+b2 that USES trim"
Invoke-Step 4 'c4 consumer of a+b2' ($flags + @('c4.cppm', '/reference', 'a.ifc', '/reference', 'a-s.ifc', '/reference', 'b2.ifc', '/ifcOutput', 'c4.ifc'))

Write-Host "`n== contrast 2 (clang accepts): carrier partition m:t WITHOUT 'import a'"
Invoke-Step 5 'm:t carrier, no import a' ($flags + @('m_t2.cppm', '/ifcOutput', 'm-t2.ifc'))
Invoke-Step 6 'm:f sibling via m-t2' ($flags + @('m_f.cppm', '/reference', 'a.ifc', '/reference', 'a-s.ifc', '/reference', 'm-t2.ifc', '/ifcOutput', 'm-f2.ifc'))

Write-Host "`n== THE SHAPE clang REJECTS: m:t parses s.h in its GMF AND imports a; sibling m:f imports :t and a"
Invoke-Step 7 'm:t carrier WITH import a' ($flags + @('m_t.cppm', '/reference', 'a.ifc', '/reference', 'a-s.ifc', '/ifcOutput', 'm-t.ifc'))
Invoke-Step 8 'm:f sibling via m-t  <== clang fails HERE' ($flags + @('m_f.cppm', '/reference', 'a.ifc', '/reference', 'a-s.ifc', '/reference', 'm-t.ifc', '/ifcOutput', 'm-f.ifc')) -ClangFails

Write-Host "`n==================== SUMMARY ===================="
Write-Host "MSVC toolset: $env:VCToolsVersion"
$results | Format-Table Step, Ok, Exit, ClangFails, Desc -AutoSize | Out-String | Write-Host

$divergent = $results | Where-Object { $_.ClangFails -and $_.Ok }
if ($divergent) {
    Write-Host "NOTE: step 8 compiled under MSVC but FAILS on clang -- expected under MSVC weak ownership"
    Write-Host "      (handoff Risk 0). macOS clang remains the semantic referee."
}
