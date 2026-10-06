<#
.SYNOPSIS
  Points git at the repository's .githooks directory.

.DESCRIPTION
  Installs the diff-only clang-tidy pre-commit hook for this clone. The hook skips itself
  when cpp/build/compile_commands.json is missing, so it never blocks an unconfigured tree.
#>
$ErrorActionPreference = 'Stop'

$root = (& git rev-parse --show-toplevel).Trim()
if (-not $root) { throw 'Run this script from inside the git repository.' }

& git -C $root config core.hooksPath .githooks
if ($LASTEXITCODE -ne 0) { throw 'Failed to set core.hooksPath.' }

Write-Host "core.hooksPath set to .githooks; pre-commit runs Run-ClangTidy.ps1 -Changed."
