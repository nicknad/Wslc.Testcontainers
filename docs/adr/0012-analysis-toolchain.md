# ADR-0012: The analyzer follows the toolchain; clang-format is pinned

Status: Accepted

## Context

The C++ gates run two clang tools: `clang-format` for formatting and `clang-tidy` for
analysis. Both used to come from one pinned download (LLVM 20.1.8).

`clang-tidy` parses the MSVC Standard Library headers that the compiler builds against.
That library refuses compilers older than the toolset it shipped with, through
`_EMIT_STL_ERROR(STL1000, ...)` in `<yvals_core.h>`, gated on
`#if __clang_major__ < N`. When the runner image moved to the Visual Studio 2026 toolset
(MSVC 14.52, requires clang 22), the pinned clang 20 could not parse any translation unit:
the gate reported 152 identical static assertions that read like code findings, and the
nightly analysis spent 11 minutes failing every file. Pinning the image label does not
help: `windows-latest` and `windows-2025` both resolve to the `win25-vs2026` image.

`clang-format` has the opposite requirement: its output is an artifact of the tool
version, so following the image would reformat the tree whenever the image rolls.

## Decision

- `clang-tidy` comes from the Visual Studio installation in the runner image
  (`VC\Tools\Llvm\x64\bin`), exported as `WSLC_CLANG_TIDY`. Microsoft ships it
  matched to the STL in that image, so the analyzer can always parse the headers the build
  uses, and the pair can never drift apart.
- `clang-format` stays pinned to LLVM 20.1.8, installed from the Actions cache or
  Chocolatey, because the formatting gate must be reproducible.
- Every analysis run begins with `cpp/scripts/Test-ClangToolchain.ps1`, which reads the
  minimum clang major from the installed `<yvals_core.h>` and fails in a second, naming
  both versions, when the analyzer is older. This is what turned the incident into one
  actionable line while it was being fixed.
- CI runs one workflow per branch: a push and its pull request are the same commit, so the
  newer run cancels the older one.

## Consequences

- The analyzer version moves with the runner image. A newer clang can enable checks that
  fire on existing code; the response is a deliberate triage, not a silent reformat of the
  baseline. The 2026-11-23 triage (clang 22.1.3) produced 241 diagnostics and disabled six
  checks: `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` (104),
  `modernize-use-scoped-lock` (96), `readability-container-contains` (21),
  `modernize-avoid-c-style-cast` (8), `readability-use-concise-preprocessor-directives`
  (7), `readability-ambiguous-smartptr-reset-call` (5). Each flags code whose intent the
  surrounding bounds check or single mutex already states; re-examine them with
  `Run-ClangTidy.ps1 -All` when touching those areas.
- Findings describe the STL the compiler actually uses, so the analyzer and the compiler
  cannot disagree about the standard library.
- Bumping `clang-format` remains a separate, explicit change; an image rollover cannot
  reformat the repository.
- A toolchain skew fails fast and by name instead of consuming a full analysis run.
