# ADR-0012: Analysis toolchain is pinned as a pair

Status: Accepted

## Context

The C++ gates run two clang tools from one pinned download (LLVM 20.1.8): `clang-format`
for formatting and `clang-tidy` for analysis.

`clang-tidy` parses the MSVC Standard Library headers that the compiler builds against.
That library refuses compilers older than the toolset it shipped with, through
`_EMIT_STL_ERROR(STL1000, ...)` in `<yvals_core.h>`, gated on
`#if __clang_major__ < N`. On 2026-11-19 the `windows-latest` label moved to the
Visual Studio 2026 image (`win25-vs2026`, MSVC 14.52), whose STL requires clang 22 while
the pinned analyzer is clang 20. Every translation unit then failed with the same static
assertion, which reads like a code finding rather than a toolchain mismatch, and it cost a
full 11-minute analysis run to report nothing.

The pinned pair is compatible with the newer toolset only after a version bump, and that
bump is not free: clang 22.1.3 reports 241 additional diagnostics on the unchanged tree
(104 `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`, 96
`modernize-use-scoped-lock`, 41 across four further checks).

## Decision

- The two jobs that run clang — `cpp-lint` in `ci`, and `cpp-analyzer` in the nightly
  `analyze` — pin `runs-on: windows-2025`, the image whose MSVC STL (14.44, requires
  clang 19) matches the pinned LLVM. Every other job keeps following the default image
  because it uses the current MSVC compiler, not clang.
- The LLVM pin stays at 20.1.8 for both tools: the analyzer must stay compatible with the
  image's STL, and the formatting gate must stay reproducible.
- `cpp/scripts/Test-ClangToolchain.ps1` runs before every analysis. It reads the minimum
  clang major out of the installed `<yvals_core.h>` and compares it with the analyzer, so
  a future skew fails in about a second, naming both versions, instead of once per
  translation unit.
- CI keeps one run per branch: a push and its pull request are the same commit, so the
  newer run cancels the older one.

## Consequences

- A runner image rollover can no longer change the analyzer underneath the pin: the image
  is part of the pin.
- Moving to the Visual Studio 2026 image is a deliberate change: bump the image and the
  LLVM pin in one commit and triage the diagnostics the newer clang adds, fixing them or
  disabling the checks with a reason in `.clang-tidy`.
- A toolchain mismatch now fails fast and by name; it no longer consumes an analysis run.
- The analyzer parses an older STL than the compiler the test jobs use. That is acceptable:
  findings depend on the project's own code far more than on STL patch level, and the build
  jobs still exercise the current toolset.
