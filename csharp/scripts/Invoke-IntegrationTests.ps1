<#
.SYNOPSIS
    Runs the real-container tests against the WSLC runtime by executing the built
    test host directly.

.DESCRIPTION
    `dotnet test` is not usable on every machine: its Microsoft.Testing.Platform
    handshake can fail with UnauthorizedAccessException on a named pipe before a
    single test runs, even outside any sandbox. The test assembly is a runnable
    xUnit v3 host, so this script builds the project and invokes that host. No
    named pipe is involved.

    Two MSBuild switches are pinned because this checkout needs them locally:

      NuGetAudit=false            restore must not depend on api.nuget.org
      UseSharedCompilation=false  the shared csc server is cancelled in confined shells

    Select tests with -Class, -Method or -Namespace. With none, the whole assembly
    runs, and tests gated by [IntegrationFact] still skip unless the environment
    variable is set; this script sets it.

    Run this from an elevated Windows Terminal or PowerShell window. The WSLC
    WinRT API is denied to non-elevated processes (UnauthorizedAccessException
    from Microsoft.WSL.Containers), while the wslc.exe CLI still works; CI runs
    the same tests as an elevated runner user. Inside the DSH agent sandbox the
    CLI is additionally denied, and an agent cannot elevate, so verification is
    driven from the operator's elevated terminal.

.PARAMETER Class
    Fully qualified test class, wildcards allowed at either end.

.PARAMETER Method
    Fully qualified test method (class and method).

.PARAMETER Namespace
    Namespace to run, wildcards allowed at either end.

.PARAMETER Project
    Test project relative to the csharp directory. Defaults to the core tests;
    pass tests/Wslc.Testcontainers.Modules.Tests/Wslc.Testcontainers.Modules.Tests.csproj
    for module tests.
#>
[CmdletBinding()]
param(
    [string]$Class,
    [string]$Method,
    [string]$Namespace,
    [switch]$AllowUnelevated,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Framework = "net11.0-windows10.0.19041.0",
    [string]$Project = "tests/Wslc.Testcontainers.Tests/Wslc.Testcontainers.Tests.csproj"
)

$ErrorActionPreference = "Stop"

$csharp = Split-Path -Parent $PSScriptRoot
$wslc = Join-Path $env:ProgramFiles "WSL/wslc.exe"

if (-not (Test-Path $wslc)) {
    throw "wslc.exe was not found at '$wslc'. Install or update WSL: wsl --update."
}

$previousErrorAction = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $wslc info *> $null
$wslcExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorAction

if ($wslcExitCode -ne 0) {
    throw @"
WSLC is not reachable from this shell. If you are inside the DSH agent sandbox,
its process token is refused by the runtime even in full-access mode; run this
script from a normal Windows Terminal or PowerShell window instead.
"@
}

Push-Location $csharp
try {
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        if (-not $AllowUnelevated) {
            throw @"
WSLC's WinRT API is denied for a non-elevated process on this machine: the
Microsoft.WSL.Containers call throws UnauthorizedAccessException before any test
runs. Relaunch from an elevated PowerShell (Run as administrator):

    powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath

Pass -AllowUnelevated only on a host where the API works without elevation.
"@
        }

        Write-Warning "Running unelevated by request; WSLC may deny the WinRT API."
    }

    dotnet build $Project `
        -c $Configuration `
        -f $Framework `
        -p:NuGetAudit=false `
        -p:UseSharedCompilation=false `
        --nologo `
        -v minimal

    if ($LASTEXITCODE -ne 0) {
        throw "Build failed; fix the build before running the tests."
    }

    $projectDirectory = Join-Path $csharp (Split-Path -Parent $Project)
    $outputDirectory = Join-Path $projectDirectory "bin/$Configuration/$Framework"
    $hostExecutable = Get-ChildItem $outputDirectory -Filter *.exe | Select-Object -First 1

    if ($null -eq $hostExecutable) {
        throw "No test host executable found under '$outputDirectory'."
    }

    $testArguments = @()
    if ($Class) { $testArguments += @("-class", $Class) }
    if ($Method) { $testArguments += @("-method", $Method) }
    if ($Namespace) { $testArguments += @("-namespace", $Namespace) }

    $env:WSLC_RUN_INTEGRATION = "1"
    & $hostExecutable.FullName @testArguments

    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
