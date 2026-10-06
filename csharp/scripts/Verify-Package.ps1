<#
.SYNOPSIS
  Local pre-release gate: build, unit-test all TFMs, pack, verify nupkg, smoke-install.
  Optionally runs real-WSL integration with -RunIntegration.

.DESCRIPTION
  Also enforces the static release invariants:
  - Version lockstep: the numeric part of the C# VersionPrefix matches the C++ CMake
    project VERSION.
  - PublicAPI promotion: refuses to ship an unfrozen public surface, i.e. an assembly
    whose PublicAPI.Unshipped.txt still has public-surface lines while its
    PublicAPI.Shipped.txt has none. Promote entries (move them, keep them sorted)
    before releasing; use -SkipPromotionCheck only for local verification while the
    surface is still being frozen.

.EXAMPLE
  ./scripts/Verify-Package.ps1
  ./scripts/Verify-Package.ps1 -RunIntegration
  ./scripts/Verify-Package.ps1 -SkipPromotionCheck
  ./scripts/Verify-Package.ps1 -ChecksOnly
#>
param(
  [string]$Version = "",
  [switch]$RunIntegration,
  [switch]$SkipPromotionCheck,
  [switch]$ChecksOnly
)

$ErrorActionPreference = 'Stop'

# The script lives in csharp/scripts; paths below are relative to csharp/.
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location (Split-Path $PSScriptRoot -Parent)
try {

# Native commands (dotnet/wsl) do not throw on non-zero exit under WinPS 5.1,
# so every invocation goes through here. Without this the gate can report
# "passed" while tests actually failed.
function Invoke-Native([string]$context, [scriptblock]$Body) {
  & $Body
  if ($LASTEXITCODE -ne 0) { throw "$context failed with exit code $LASTEXITCODE." }
}

function Step($name) { Write-Host "`n=== $name ===" -ForegroundColor Cyan }

# Returns the public-surface lines of a PublicAPI file: everything that is not a
# '#nullable enable' directive, a comment, or blank.
function Get-PublicApiSurface([string]$path) {
  if (-not (Test-Path -LiteralPath $path)) { return @() }
  return @(Get-Content -LiteralPath $path | Where-Object {
    $line = $_.Trim()
    -not [string]::IsNullOrWhiteSpace($line) -and -not $line.StartsWith('#') -and -not $line.StartsWith(';')
  })
}

# The numeric part of the C# VersionPrefix and the CMake project version must agree;
# the suffix (e.g. -preview.1) is a NuGet-only concern.
function Assert-VersionLockstep {
  $propsPath = (Join-Path $PSScriptRoot '..\Directory.Build.props' | Resolve-Path).Path
  $props = Get-Content -LiteralPath $propsPath -Raw
  if ($props -notmatch '<VersionPrefix>\s*([0-9]+\.[0-9]+\.[0-9]+)') {
    throw "Could not parse a numeric VersionPrefix from $propsPath."
  }
  $csharpVersion = $Matches[1]

  $cmakePath = Join-Path $repoRoot 'cpp/CMakeLists.txt'
  $cmake = Get-Content -LiteralPath $cmakePath -Raw
  if ($cmake -notmatch 'project\s*\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    throw "Could not parse the project VERSION from $cmakePath."
  }
  $cppVersion = $Matches[1]

  if ($csharpVersion -ne $cppVersion) {
    throw "Version lockstep failed: csharp/Directory.Build.props VersionPrefix numeric part '$csharpVersion' does not match cpp/CMakeLists.txt project VERSION '$cppVersion'. Update one to match the other."
  }
  Write-Host "ok: C# VersionPrefix numeric part and C++ CMake project VERSION are both $csharpVersion"
}

# Refuses to ship an unfrozen surface: an assembly with unshipped public-surface lines
# and no shipped baseline must be promoted first. Assemblies that already have a shipped
# baseline may keep accumulating new unshipped entries.
function Assert-PublicApiPromoted {
  $unshippedFiles = @(Get-ChildItem -Path src -Recurse -Filter 'PublicAPI.Unshipped.txt' -File)
  if ($unshippedFiles.Count -eq 0) { throw 'No PublicAPI.Unshipped.txt files found under src/.' }

  $unpromoted = @()
  foreach ($unshipped in $unshippedFiles) {
    $surface = Get-PublicApiSurface $unshipped.FullName
    if ($surface.Count -eq 0) { continue }

    $shipped = Get-PublicApiSurface (Join-Path $unshipped.DirectoryName 'PublicAPI.Shipped.txt')
    if ($shipped.Count -eq 0) {
      $unpromoted += [pscustomobject]@{
        Assembly = Split-Path $unshipped.DirectoryName -Leaf
        Count    = $surface.Count
      }
    }
  }

  if ($unpromoted.Count -eq 0) {
    Write-Host 'ok: every assembly with unshipped entries has a shipped baseline'
    return
  }

  foreach ($entry in $unpromoted) {
    Write-Host "  $($entry.Assembly): $($entry.Count) unshipped public-surface line(s), PublicAPI.Shipped.txt has none." -ForegroundColor Yellow
  }

  throw @"
PublicAPI promotion gate failed: an unfrozen public surface cannot ship.
Promotion instructions:
  1. For each assembly listed above, move every public-surface line from
     PublicAPI.Unshipped.txt into the matching PublicAPI.Shipped.txt.
  2. Leave only '#nullable enable', comments and blank lines in PublicAPI.Unshipped.txt.
  3. Keep the moved entries sorted in PublicAPI.Shipped.txt.
  4. Re-run this script (without -SkipPromotionCheck) to confirm the surface is frozen.
"@
}

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

# 3. Version lockstep (C# VersionPrefix numeric part vs C++ CMake project VERSION)
Step 'Version lockstep'
Assert-VersionLockstep

# 4. PublicAPI promotion gate (refuse to ship an unfrozen surface)
Step 'PublicAPI promotion gate'
if ($SkipPromotionCheck) {
  Write-Host 'skipped (-SkipPromotionCheck)'
} else {
  Assert-PublicApiPromoted
}

if ($ChecksOnly) {
  Write-Host "`nStatic release checks passed." -ForegroundColor Green
  return
}

# 5. Build + unit (all TFMs, no integration)
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
  Invoke-Native "module unit ($tfm)" {
    dotnet test tests/Wslc.Testcontainers.Modules.Tests/Wslc.Testcontainers.Modules.Tests.csproj `
      -c Release -f $tfm --no-build -v minimal
  }
}

# 6. Pack + verify contents
Step 'Pack'
New-Item -ItemType Directory -Force artifacts/packages | Out-Null
foreach ($p in @('src/Wslc.Testcontainers/Wslc.Testcontainers.csproj', 'src/Wslc.Testcontainers.Modules.PostgreSql/Wslc.Testcontainers.Modules.PostgreSql.csproj', 'src/Wslc.Testcontainers.Modules.Redis/Wslc.Testcontainers.Modules.Redis.csproj', 'src/Wslc.Testcontainers.Modules.Valkey/Wslc.Testcontainers.Modules.Valkey.csproj', 'src/Wslc.Testcontainers.Modules.MariaDb/Wslc.Testcontainers.Modules.MariaDb.csproj')) {
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

# 7. Smoke install from local feed (outside the repo so repo MSBuild/CPM is not inherited)
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

# 8. Optional real-WSL integration
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
  Invoke-Native 'module integration' {
    dotnet test tests/Wslc.Testcontainers.Modules.Tests/Wslc.Testcontainers.Modules.Tests.csproj `
      -c Release -f net10.0-windows10.0.19041.0 --no-build -v minimal
  }
  Invoke-Native 'Postgres module' {
    dotnet test examples/Postgres.Tests/Postgres.Tests.csproj -c Release -v minimal
  }
}

Write-Host "`nPre-release gate passed for $Version." -ForegroundColor Green

} finally {
  Pop-Location
}
