# ADR-0005: Strict build with zero warnings

Status: Accepted

## Context

`Directory.Build.props` already enables `Nullable` and `ImplicitUsings`, but
nothing failed the build on new warnings.

## Decision

- `TreatWarningsAsErrors`, `WarningLevel 9999`, and `EnforceCodeStyleInBuild`
  are on for the whole repo. The build must stay at zero warnings and zero
  errors on all TFMs (`net8/net9/net10-windows`).
- Full `AnalysisMode:All` stays off: several `All` rules conflict with
  intentional API shape (exception ctors, `And()` name, `CancellationToken`
  position for overload compatibility) and with `net8` APIs
  (`Convert.ToHexStringLower`, `ReadBytes(Span)`). Revisit per-rule if needed.
  `AnalysisMode:Recommended` runs with scoped `.editorconfig` suppressions for
  the documented conflicts (`CA1068`, `CA1716`) and for xUnit naming in tests.
- Package projects track their public surface with PublicApiAnalyzers:
  new API goes into `PublicAPI.Unshipped.txt`, and entries move to
  `PublicAPI.Shipped.txt` on release.
- New code pre-sizes collections and checks `TryWrite`/`TryComplete` results
  with an explicit discard. Postconditions are covered by tests and analyzers
  rather than `Debug.Assert`.

## Consequences

- Any new warning breaks CI; fix or justify with a scoped suppression.
- Docs stay in `GenerateDocumentationFile`; `CS1591` remains suppressed for
  internal members only.
