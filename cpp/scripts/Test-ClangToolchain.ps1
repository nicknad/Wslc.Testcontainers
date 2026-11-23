<#
.SYNOPSIS
  Fails fast when clang-tidy cannot parse the MSVC STL installed with Visual Studio.

.DESCRIPTION
  The MSVC STL rejects compilers older than the toolset it shipped with through
  _EMIT_STL_ERROR(STL1000, ...) in <yvals_core.h>, gated on `#if __clang_major__ < N`.
  Reaching that assertion costs a whole analysis run: every translation unit fails to
  parse with the same diagnostic. This check reads N from the installed header and
  compares it with the clang-tidy that will run, so a version skew prints one actionable
  line instead of dozens of identical errors.

  Run it after the MSVC developer environment is initialized: the header is located
  through INCLUDE, the same way clang-tidy finds it.

.PARAMETER ClangTidy
  clang-tidy executable to check. Defaults to WSLC_CLANG_TIDY, then to clang-tidy on
  PATH, which is how Run-ClangTidy.ps1 resolves its analyzer.

.EXAMPLE
  ./cpp/scripts/Test-ClangToolchain.ps1
#>
param(
  [string]$ClangTidy = ''
)

$ErrorActionPreference = 'Stop'

# The STL gates its compiler check on the clang major version. A header without the gate
# parses with any compiler, so no requirement means no check.
function Get-RequiredClangMajor {
  param([string[]]$IncludeDirs)

  foreach ($dir in $IncludeDirs) {
    $header = Join-Path $dir 'yvals_core.h'
    if (-not (Test-Path -LiteralPath $header)) { continue }
    $gate = [regex]::Match((Get-Content -LiteralPath $header -Raw), '#if\s+__clang_major__\s*<\s*(\d+)')
    if (-not $gate.Success) { return 0 }
    return [int]$gate.Groups[1].Value
  }
  return $null
}

function Get-ClangMajor {
  param([string]$Exe)

  $line = (& $Exe --version | Select-Object -First 2) -join ' '
  $version = [regex]::Match($line, '(?:clang|LLVM) version (\d+)\.')
  if (-not $version.Success) {
    throw "Could not read a clang version from '$Exe --version': $line"
  }
  return [int]$version.Groups[1].Value
}

$tidy = if ($ClangTidy) {
  $ClangTidy
} elseif ($env:WSLC_CLANG_TIDY) {
  $env:WSLC_CLANG_TIDY
} else {
  (Get-Command clang-tidy -ErrorAction Stop).Source
}
if (-not (Test-Path -LiteralPath $tidy)) { throw "clang-tidy was not found at '$tidy'." }

$required = Get-RequiredClangMajor -IncludeDirs @($env:INCLUDE -split ';' | Where-Object { $_ })
if ($null -eq $required) {
  Write-Warning 'No yvals_core.h on INCLUDE; MSVC STL compatibility was not checked.'
  exit 0
}

$actual = Get-ClangMajor -Exe $tidy
if ($actual -lt $required) {
  throw ("clang-tidy $actual cannot parse this image's MSVC STL, which requires clang $required or newer. " +
    'Move the runner image and the LLVM pin together (per ADR-0012), or point the analyzer at a ' +
    'clang that matches the toolset.')
}
Write-Host "clang-tidy $actual can parse the MSVC STL (requires clang >= $required)."
