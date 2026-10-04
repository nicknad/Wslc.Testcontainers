<#
.SYNOPSIS
  Downloads and expands the Microsoft.WSL.Containers NuGet package for the C++ build.

.DESCRIPTION
  The native SDK ships in the Microsoft.WSL.Containers nupkg (wslcsdk.h/.lib/.dll and
  cmake/Microsoft.WSL.ContainersConfig.cmake). CMake consumes the expanded package root
  via -DWSLC_SDK_ROOT. Local development usually finds the package in the NuGet cache
  already; this script is for clean machines and CI.

.EXAMPLE
  ./cpp/scripts/Install-WslcSdk.ps1
  ./cpp/scripts/Install-WslcSdk.ps1 -Version 3.0.1 -Destination cpp/.packages
#>
param(
  [string]$Version = '3.0.1',
  [string]$Destination = (Join-Path $PSScriptRoot '..\.packages'),
  [string]$Sha512 = ''
)

$ErrorActionPreference = 'Stop'

# SHA512 pin for each supported SDK version (hex, verified against nuget.org).
$pinnedHashes = @{
  '3.0.1' = 'AE6836D4AEF1CAF172A8782BB42EF1B2B3980240B818F61A23229CB0A8B286D6FDD3998F86FBE9ADDBAA4D24F63BCF7919FD75235ADFD5E749C9B750869FDDEC'
}

if (-not $Sha512) {
  $Sha512 = $pinnedHashes[$Version]
}

if (-not $Sha512) {
  throw "No pinned SHA512 for Microsoft.WSL.Containers $Version. Pass -Sha512 <hex> to verify the download."
}

$root = Join-Path $Destination "Microsoft.WSL.Containers.$Version"
if (Test-Path (Join-Path $root 'cmake/Microsoft.WSL.ContainersConfig.cmake')) {
  Write-Host "Microsoft.WSL.Containers $Version already present at $root"
  Write-Output $root
  exit 0
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$url = "https://api.nuget.org/v3-flatcontainer/microsoft.wsl.containers/$Version/microsoft.wsl.containers.$Version.nupkg"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) "wslc-sdk-$Version-$(New-Guid).zip"
Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $temp

$actual = (Get-FileHash -LiteralPath $temp -Algorithm SHA512).Hash
if ($actual -ne $Sha512.ToUpperInvariant()) {
  Remove-Item -Force $temp -ErrorAction SilentlyContinue
  throw "SHA512 mismatch for $url`nExpected: $($Sha512.ToUpperInvariant())`nActual:   $actual"
}

try {
  Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
  Expand-Archive -LiteralPath $temp -DestinationPath $root
}
finally {
  Remove-Item -Force $temp -ErrorAction SilentlyContinue
}

if (-not (Test-Path (Join-Path $root 'cmake/Microsoft.WSL.ContainersConfig.cmake'))) {
  throw "Expanded package at $root does not look like Microsoft.WSL.Containers."
}

Write-Host "Microsoft.WSL.Containers $Version expanded to $root"
Write-Output $root
