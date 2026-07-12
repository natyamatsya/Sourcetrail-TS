<#
.SYNOPSIS
    Build and register the Sourcetrail MCP server (sourcetrail-mcp) with an MCP
    client (Claude Code by default) on Windows.

.DESCRIPTION
    One server drives many app checkouts — it spawns/attaches instances via the
    start_instance / list_instances tools, each namespaced by its git label — so
    you install it ONCE, not per checkout. The connect-time handshake
    (get_instance_info) flags any protocol skew between this server and an older
    app build. See context/DESIGN_AGENT_UI_CONTROL.md (Protocol handshake,
    Registration & multi-checkout).

.PARAMETER Prefix
    Copy the built .exe to this directory and register THAT path (stable, decoupled
    from this checkout's target\). Default: register the in-place target\release exe.

.PARAMETER Name
    MCP server name to register as. Default: sourcetrail

.PARAMETER Scope
    claude mcp scope: local | user | project. Default: user (available in every
    project, which suits one-server-many-checkouts).

.PARAMETER NoRegister
    Build (and optionally copy) only; skip client registration.

.PARAMETER NoBuild
    Skip the cargo build; use the existing binary.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\install-mcp.ps1
    powershell -ExecutionPolicy Bypass -File scripts\install-mcp.ps1 -Prefix "$env:LOCALAPPDATA\Programs\sourcetrail-mcp"

.NOTES
    Requires: cargo (rustup toolchain). Optional: the `claude` CLI for auto-registration
    (otherwise a ready-to-paste config snippet is printed).
#>
[CmdletBinding()]
param(
    [string] $Prefix = "",
    [string] $Name = "sourcetrail",
    [ValidateSet("local", "user", "project")]
    [string] $Scope = "user",
    [switch] $NoRegister,
    [switch] $NoBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Manifest = Join-Path $RepoRoot "src\agent_mcp_bridge\Cargo.toml"

if (-not $NoBuild) {
    if (-not (Get-Command cargo -ErrorAction SilentlyContinue)) {
        throw "cargo not found on PATH (install rustup)"
    }
    Write-Host ">> building sourcetrail-mcp (release, --features mcp)"
    & cargo build --release --manifest-path $Manifest --bin sourcetrail-mcp --features mcp
    if ($LASTEXITCODE -ne 0) { throw "cargo build failed ($LASTEXITCODE)" }
}

$Bin = Join-Path $RepoRoot "src\agent_mcp_bridge\target\release\sourcetrail-mcp.exe"
if (-not (Test-Path $Bin)) {
    throw "server binary not found at $Bin (run without -NoBuild)"
}

# Optionally copy to a stable location so registration does not pin to this checkout.
if ($Prefix -ne "") {
    New-Item -ItemType Directory -Force -Path $Prefix | Out-Null
    $dest = Join-Path $Prefix "sourcetrail-mcp.exe"
    Copy-Item -Force $Bin $dest
    $Bin = $dest
    Write-Host ">> installed binary at $Bin"
}

Write-Host ">> server binary: $Bin"

if ($NoRegister) {
    Write-Host ">> -NoRegister: skipping client registration"
    exit 0
}

if (Get-Command claude -ErrorAction SilentlyContinue) {
    Write-Host ">> registering with Claude Code as '$Name' (scope: $Scope)"
    # Idempotent: drop any existing registration of the same name/scope first.
    & claude mcp remove $Name -s $Scope 2>$null | Out-Null
    & claude mcp add $Name -s $Scope $Bin
    Write-Host ">> done. Verify with: claude mcp get $Name"
    Write-Host ">> Open a NEW Claude Code session to pick up the tools."
} else {
    $json = @"
>> 'claude' CLI not found — register manually.

Claude Code:
    claude mcp add $Name -s $Scope "$Bin"

Claude Desktop — add to %APPDATA%\Claude\claude_desktop_config.json:

    {
      "mcpServers": {
        "$Name": {
          "command": "$($Bin -replace '\\','\\')",
          "args": []
        }
      }
    }

Then restart the client.
"@
    Write-Host $json
}
