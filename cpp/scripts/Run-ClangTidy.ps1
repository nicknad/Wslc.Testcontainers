<#
.SYNOPSIS
  Runs clang-tidy over the C++ project, in full or only over files changed since a git ref.

.DESCRIPTION
  Prefers LLVM's run-clang-tidy runner (needs Python) for parallel analysis against the
  Ninja compile_commands.json; falls back to invoking clang-tidy once per translation unit.
  When changed headers are found, the translation units that include them are analyzed too,
  so diagnostics in headers are still reported. Warnings are errors via the tidy config.

.EXAMPLE
  ./cpp/scripts/Run-ClangTidy.ps1 -Changed
  ./cpp/scripts/Run-ClangTidy.ps1 -Changed -Base origin/main
  ./cpp/scripts/Run-ClangTidy.ps1 -All
  ./cpp/scripts/Run-ClangTidy.ps1 -All -ConfigFile cpp/.clang-tidy-analyzer
#>
param(
  [switch]$All,
  [switch]$Changed,
  [string]$Base = 'HEAD',
  [string]$BuildDir = 'cpp/build',
  [string]$ConfigFile = '',
  [string]$Checks = '',
  [int]$Jobs = [Environment]::ProcessorCount,
  [switch]$Fix
)

$ErrorActionPreference = 'Stop'

# Git writes warnings to stderr (e.g. line-ending conversions); keep them from terminating
# the script under $ErrorActionPreference = 'Stop'.
function Invoke-Git {
  param([string[]]$Arguments)
  $previous = $ErrorActionPreference
  $ErrorActionPreference = 'Continue'
  try { return @(& git @Arguments 2>$null) }
  finally { $ErrorActionPreference = $previous }
}

if ($All -and $Changed) { throw 'Specify either -All or -Changed, not both.' }
if (-not $All) { $Changed = $true }

$repoRoot = (Invoke-Git -Arguments @('rev-parse', '--show-toplevel') | Select-Object -First 1).Trim()
if (-not $repoRoot) { throw 'Run this script from inside the git repository.' }

if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repoRoot $BuildDir }
if ($ConfigFile -and -not [IO.Path]::IsPathRooted($ConfigFile)) { $ConfigFile = Join-Path $repoRoot $ConfigFile }
$dbPath = Join-Path $BuildDir 'compile_commands.json'
if (-not (Test-Path -LiteralPath $dbPath)) {
  throw "No compile_commands.json under '$BuildDir'. Configure a Ninja build first; CMAKE_EXPORT_COMPILE_COMMANDS is on by default."
}

$clangTidy = (Get-Command clang-tidy -ErrorAction Stop).Source

# Every first-party translation unit in the compile database. Sources under _deps are
# third-party (GoogleTest) and are never analyzed; only this repo's targets are.
function Get-AnalysisSources {
  $database = Get-Content -LiteralPath $dbPath -Raw | ConvertFrom-Json
  return @($database | ForEach-Object {
      $full = if ([IO.Path]::IsPathRooted($_.file)) { $_.file } else { Join-Path $_.directory $_.file }
      [IO.Path]::GetFullPath($full)
    } | Where-Object { $_ -notmatch '[\\/]_deps[\\/]' } | Sort-Object -Unique)
}

# Select translation units (absolute paths): changed sources plus every source that
# includes a changed header.
$sources = @()
if ($Changed) {
  if ($Base -match '^0{40,}$') {
    Write-Host 'clang-tidy: no previous revision; analyzing all translation units.'
    $sources = Get-AnalysisSources
  } else {
    $changedFiles = @(Invoke-Git -Arguments @('diff', '--name-only', '--diff-filter=ACMR', $Base, '--', 'cpp'))
    if ($LASTEXITCODE -ne 0) { throw "'git diff $Base' failed; pass a valid -Base revision." }
    $sources = @($changedFiles | Where-Object { $_ -match '\.cpp$' })
    foreach ($header in ($changedFiles | Where-Object { $_ -match '\.hpp$' })) {
      $name = [IO.Path]::GetFileName($header)
      $including = @(Invoke-Git -Arguments @('grep', '-l', '-F', '--', $name, '--', 'cpp') | Where-Object { $_ -match '\.cpp$' })
      if ($including.Count -gt 0) {
        Write-Host "clang-tidy: $header is included by $($including.Count) translation unit(s)."
        $sources += $including
      }
    }
    $sources = @($sources | Sort-Object -Unique)
    if ($sources.Count -eq 0) {
      Write-Host 'clang-tidy: no changed C++ sources; nothing to analyze.'
      exit 0
    }
    $sources = @($sources | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $repoRoot $_)) })
    Write-Host "clang-tidy: analyzing changed files ($($sources.Count) translation unit(s))."
  }
} else {
  Write-Host 'clang-tidy: analyzing all translation units.'
  $sources = Get-AnalysisSources
}

# MSVC compile lines carry flags clang does not consume; cl driver mode plus -Qunused-arguments
# keep the parser in cl.exe semantics without erroring on /MP, /Zc:*, and /external:*.
$extraArgs = @('-extra-arg=-Qunused-arguments', '-extra-arg-before=--driver-mode=cl')

$binDir = Split-Path -Parent $clangTidy
$runner = Get-ChildItem -LiteralPath $binDir -Filter 'run-clang-tidy*' -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Extension -eq '.exe' -or $_.Extension -eq '' } | Select-Object -First 1
$python = Get-Command python -ErrorAction SilentlyContinue

if ($runner -and ($runner.Extension -eq '.exe' -or $python)) {
  $runnerArgs = @('-p', $BuildDir, '-j', $Jobs, '-quiet') + $extraArgs
  $exe = $runner.FullName
  if ($runner.Extension -ne '.exe') {
    $exe = $python.Source
    $runnerArgs = @($runner.FullName) + $runnerArgs
  }
  if ($ConfigFile) { $runnerArgs += @('-config-file', $ConfigFile) }
  if ($Checks) { $runnerArgs += @('-checks', $Checks) }
  if ($Fix) { $runnerArgs += '-fix' }
  $runnerArgs += @($sources | ForEach-Object {
    $normalized = $_ -replace '\\', '/'
    [regex]::Escape($normalized) -replace '/', '[\\/]'
  })
  & $exe @runnerArgs
  exit $LASTEXITCODE
}

Write-Warning 'run-clang-tidy was not found (it needs Python); falling back to serial clang-tidy.'

$failed = $false
foreach ($file in $sources) {
  Write-Host "clang-tidy: $file"
  $tidyArgs = @('-p', $BuildDir, '--extra-arg=-Qunused-arguments', '--extra-arg-before=--driver-mode=cl', '-quiet')
  if ($ConfigFile) { $tidyArgs += "--config-file=$ConfigFile" }
  if ($Checks) { $tidyArgs += "--checks=$Checks" }
  if ($Fix) { $tidyArgs += '--fix' }
  & $clangTidy @tidyArgs $file
  if ($LASTEXITCODE -ne 0) { $failed = $true }
}
if ($failed) { exit 1 }
