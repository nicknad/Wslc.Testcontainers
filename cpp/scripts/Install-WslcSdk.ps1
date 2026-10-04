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
  [string]$Destination = (Join-Path $PSScriptRoot '..\.packages')
)

$ErrorActionPreference = 'Stop'

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
