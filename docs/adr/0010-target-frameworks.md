# ADR-0010: Target framework policy

Status: Accepted

## Context

The shipped target frameworks were written out in full in every project file, in both CI
matrices, in `scripts/Verify-Package.ps1`, and in prose (README, usage docs, ADR-0005).
Adding a framework meant finding and editing every copy, and a missed copy failed only in
release.

.NET 11 shipped on 2026-11-10. On that date the existing list lost two entries at once: .NET 8
(LTS) and .NET 9 (STS) both reached end of support, leaving .NET 10 (LTS) as the only
supported framework we shipped. The support windows are taken from the release notes under
[`dotnet/core/release-notes`](https://github.com/dotnet/core/tree/main/release-notes) and the
[.NET support policy](https://dotnet.microsoft.com/platform/support/policy/dotnet-core):

| Framework | Channel | Supported until |
|-----------|---------|-----------------|
| .NET 8    | LTS     | 2026-11-10 (ended) |
| .NET 9    | STS     | 2026-11-10 (ended) |
| .NET 10   | LTS     | 2028-11-14 |
| .NET 11   | STS     | 2028-11-14 |

The Windows platform floor is `10.0.19041.0` in the TFM, `SupportedOSPlatformVersion`, the
`[assembly: SupportedOSPlatform]` attribute, the `WslPlatform` guard, and the documented
host requirement. `WindowsSdkPackageVersion` is pinned to `10.0.26100.80`, independent of
the TFM platform version. `Microsoft.WSL.Containers` 3.0.3 ships a single
`net8.0-windows10.0.19041.0` projection (lib and MSBuild targets); net9 and net10 projects
already resolve it through NuGet's compatibility fallback, and net11 resolves it the same
way.

## Decision

- **One framework list.** `WslcTargetFrameworks` in `csharp/Directory.Build.props` is the
  only place the list is written. Projects reference `$(WslcTargetFrameworks)`; CI derives
  its test matrix from the property instead of keeping a second copy; `Verify-Package.ps1`
  parses it.
- **The shipped set is net8, net10 and net11**, all at `net*.0-windows10.0.19041.0`:
  the current LTS (net10), the current release (net11), and the previous LTS (net8) kept
  for its grace window.
- **.NET 9 is dropped.** It ended support on the same day as .NET 8, and net10 — which
  .NET 9 consumers can move to without leaving the rolling release — already covers them.
  Shipping it would mean shipping an unpatched framework. Dropping a TFM is a breaking
  change for consumers, so it is recorded in `CHANGELOG.md` rather than done silently.
- **The Windows platform floor stays `10.0.19041.0`.** Raising it to `10.0.26100.0` would
  make the package uninstallable for consumers targeting `windows10.0.19041.0` (NuGet
  requires the consumer's platform version to be at least the package's) and would
  contradict the runtime guard and the documented host requirement. The 26100 pin that
  matters is the Windows SDK projection version, which is independent of the TFM.
- **The SDK band is pinned.** `global.json` requires `11.0.100` with
  `rollForward: latestPatch`: the 11.0.1xx band and its patches, no other band. CI installs
  the same band (`11.0.1xx`).
- **Examples ship single-target on the newest framework** (`net11.0-windows...`). They are
  consumer-shaped samples, not a matrix redeclaration; the test projects are what prove
  every TFM.
- **Trimming/AOT analysis is out of scope** for this change and stays a separate decision.

## Rejected alternatives

- **Keep .NET 9** to avoid a breaking change. It would ship a framework that gets no
  security fixes, and net10 already covers the same consumers.
- **Raise every TFM to `10.0.26100.0`** to match the Windows SDK pin. It buys nothing the
  build needs (verified: `net11.0-windows10.0.19041.0` builds clean) and costs every
  Windows 10 host.
- **Hand-kept CI matrices next to the MSBuild property.** Two copies drift silently; the
  matrix job is cheap and reads the property directly.
- **`rollForward: latestMajor`** so a missing SDK never blocks a build. It also lets a
  future major SDK change the build without a decision.

## Consequences

- A framework bump is a one-line change plus a CI run. Adding one also needs a decision on
  dropping one, recorded here.
- Consumers on .NET 9 must move to net10 or net11 to pick up 0.9.0; this is the one
  intentional break in the release.
- The `tfms` job adds roughly half a minute to CI and is a new required job for the
  `packages` aggregate.
- Examples track the newest shipped framework, so they stop compiling if that TFM is
  removed; the test matrix, not the examples, is the compatibility canary.
