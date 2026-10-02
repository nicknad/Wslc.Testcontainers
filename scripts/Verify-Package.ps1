<#
.SYNOPSIS
  Local pre-release gate: build, unit-test all TFMs, pack, verify nupkg, smoke-install.
  Optionally runs real-WSL integration with -RunIntegration.

.EXAMPLE
  ./scripts/Verify-Package.ps1
  ./scripts/Verify-Package.ps1 -RunIntegration
  ./scripts/Verify-Package.ps1 -Version 0.1.0-preview.1 -RunIntegration
#>
param(
  [string]$Version = "",
  [switch]$RunIntegration
)

$ErrorActionPreference = 'Stop'

# Native commands (dotnet/wsl) do not throw on non-zero exit under WinPS 5.1,
# so every invocation goes through here. Without this the gate can report
# "passed" while tests actually failed.
function Invoke-Native([string]$context, [scriptblock]$Body) {
  & $Body
  if ($LASTEXITCODE -ne 0) { throw "$context failed with exit code $LASTEXITCODE." }
}

function Step($name) { Write-Host "`n=== $name ===" -ForegroundColor Cyan }

# 1. Host checks (early fail mirrors WslPlatform guard; works on WinPS 5.1 + PS7)
Step 'Host checks'
if ([System.Environment]::OSVersion.Platform -ne [System.PlatformID]::Win32NT) {
  throw 'Verify-Package requires Windows 10 build 19041+ (x64/ARM64).'
}
# Use the environment variables rather than RuntimeInformation.ProcessArchitecture:
# that API returns null in some PowerShell hosts (limited/constrained runspaces, older
# .NET Framework) and reports the *process* arch (X86 under WOW64). PROCESSOR_ARCHITEW6432
# carries the real OS arch when this shell is 32-bit; both are AMD64/ARM64 names.
$arch = $env:PROCESSOR_ARCHITEW6432
if ([string]::IsNullOrEmpty($arch)) { $arch = $env:PROCESSOR_ARCHITECTURE }
if ($arch -notin @('AMD64', 'ARM64')) { throw "Unsupported architecture: $arch. Requires x64/ARM64." }
dotnet --version | Write-Output
try { wsl --version | Write-Output } catch { Write-Warning "wsl --version failed: $_" }

# 2. Version
if ([string]::IsNullOrWhiteSpace($Version)) {
  $props = Get-Content Directory.Build.props -Raw
  if ($props -match '<VersionPrefix>([^<]+)</VersionPrefix>') { $Version = $Matches[1].Trim() }
  else { throw 'Could not resolve VersionPrefix from Directory.Build.props.' }
}
Write-Host "Version: $Version"

# 3. Build + unit (all TFMs, no integration)
Step 'Build'
Invoke-Native 'build' { dotnet build Wslc.Testcontainers.slnx -c Release --nologo -v minimal }

Step 'Unit tests (net8/9/10, no integration)'
foreach ($tfm in @('net8.0-windows10.0.19041.0', 'net9.0-windows10.0.19041.0', 'net10.0-windows10.0.19041.0')) {
  Invoke-Native "unit ($tfm)" {
    # Do not pass --nologo to dotnet test under the Microsoft Testing Platform:
    # it makes the test app discover zero tests and exit 5. Options for the test
    # app go after --; -v is a dotnet test option.
    dotnet test tests/Wslc.Testcontainers.Tests/Wslc.Testcontainers.Tests.csproj `
      -c Release -f $tfm --no-build -v minimal -- --filter 'FullyQualifiedName!~Integration'
  }
}

# 4. Pack + verify contents
Step 'Pack'
New-Item -ItemType Directory -Force artifacts/packages | Out-Null
foreach ($p in @('src/Wslc.Testcontainers/Wslc.Testcontainers.csproj', 'src/Wslc.Testcontainers.Modules.PostgreSql/Wslc.Testcontainers.Modules.PostgreSql.csproj', 'src/Wslc.Testcontainers.Modules.Redis/Wslc.Testcontainers.Modules.Redis.csproj')) {
  Invoke-Native "pack ($p)" {
    dotnet pack $p -c Release --no-build -o artifacts/packages /p:Version="$Version" --nologo -v minimal
  }
}

Step 'Verify nupkg'
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($p in Get-ChildItem artifacts/packages/*.nupkg) {
  $entries = [System.IO.Compression.ZipFile]::OpenRead($p.FullName).Entries.Name
  if ('README.md' -notin $entries) { throw "$($p.Name) is missing README.md" }
  Write-Host "ok $($p.Name) ($([math]::Round($p.Length / 1KB)) KB)"
}

# 5. Smoke install from local feed (outside the repo so repo MSBuild/CPM is not inherited)
Step 'Smoke install (local feed)'
$feed = (Resolve-Path artifacts/packages).Path
$smoke = Join-Path ([System.IO.Path]::GetTempPath()) ("wslc-smoke-" + [System.Guid]::NewGuid().ToString('N'))
if (Test-Path $smoke) { Remove-Item -Recurse -Force $smoke }
dotnet new console -o $smoke -f net10.0 --no-restore | Out-Null
$csproj = Get-ChildItem "$smoke/*.csproj" | Select-Object -First 1
(Get-Content $csproj.FullName -Raw) `
  -replace '<TargetFramework>net10\.0</TargetFramework>', '<TargetFramework>net10.0-windows10.0.19041.0</TargetFramework>' `
  | Set-Content $csproj.FullName
Invoke-Native 'smoke add package' { dotnet add $csproj.FullName package Wslc.Testcontainers --version "$Version" --source "$feed" }
Invoke-Native 'smoke build' { dotnet build $csproj.FullName -c Release --nologo -v minimal }

# 6. Optional real-WSL integration
if ($RunIntegration) {
  Step 'Integration (real WSL, pulls images)'
  Invoke-Native 'wsl --update' { wsl --update }
  wsl --version
  $env:WSLC_RUN_INTEGRATION = '1'
  Invoke-Native 'core integration' {
    # No --nologo: see the unit test step comment.
    dotnet test tests/Wslc.Testcontainers.Tests/Wslc.Testcontainers.Tests.csproj `
      -c Release -f net10.0-windows10.0.19041.0 --no-build -v minimal -- --filter 'FullyQualifiedName~Integration'
  }
  Invoke-Native 'Postgres module' {
    dotnet test examples/Postgres.Tests/Postgres.Tests.csproj -c Release -v minimal
  }
}

Write-Host "`nPre-release gate passed for $Version." -ForegroundColor Green
