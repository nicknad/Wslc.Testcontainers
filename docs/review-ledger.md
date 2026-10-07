# Review ledger — open work

Remaining findings only. Completed work (all F1-F27 findings, the API review slices
S1-S4/M1-M11/M13/M19, redundancy R1/R3/R8, migration MIG1-MIG7, and new findings N2-N4) is in
git history from `18c89c1` through `8915340`; do not re-litigate it here.

Statuses: **OPEN** (reproduced, not fixed), **UNVERIFIED** (needs reproduction), **PARTIAL**
(some of the item landed), **PROCESS** (planning/observability gap), **ACCEPTED** (recorded
residual, no action planned).

Baseline at `8915340`: C# 254 tests (249 pass, 5 environment skips); C++ `ctest` 196 (179 pass,
17 skips); clang-format and the CI-equivalent clang-tidy build clean; the public surface is
frozen by the `Verify-Package.ps1` promotion gate until the first release promotes
`PublicAPI.Unshipped.txt` to `Shipped`.

## MED — API / quality

| ID | Theme | Status | Notes |
|----|-------|--------|-------|
| M2 | Argument vs runtime exception taxonomy; platform exception nesting | UNVERIFIED | Confirm which failures should be `Argument*` vs `WslException`, and whether the C++ platform exception should nest the original error like C#. |
| M8 | TimeSpan/ms, dict/map, Version parity | PARTIAL | `Inner` removed; remaining: verify C# `TimeSpan`/`IReadOnlyDictionary`/`System.Version` behavior matches C++ `chrono::milliseconds`/`std::map`/`WslVersion` (goldens cover config hashes only). |
| M9 | Reversible builders; `RequireNetwork` | PARTIAL | `SuppressFinalize` resolved via the virtual base `DisposeAsync` that suppresses finalization and disposes the inner container; reversible builders and the `RequireNetwork` concept remain unverified. |
| M12 | Split god classes (1043/1406 lines) | UNVERIFIED | `WslContainer.cs` / `wsl_container.cpp` are still monolithic; decide whether to split before 1.0. |
| M14 | Catch audit; re-enable clang-tidy checks | UNVERIFIED | Review broad catches (`Ignore()`/`Debug`) and whether any disabled clang-tidy checks can return. |
| M15 | Module constants; Doxygen casing; `reserve`; WSLC_ env validation | UNVERIFIED | Module constant extraction, XML/doc param casing, container `reserve` calls, and validation of `WSLC_*` values. |
| M16 | Tarball name/size caps; volume regex; args/cwd caps | PARTIAL | Args/cwd and tarball size caps landed with F18; remaining: validate the tarball image name and the volume-name regex `^[A-Za-z0-9][-_]{0,63}$`. |
| M17 | Inspect isfinite/1 MiB cap; log-auth docs; reuse mutex/jitter/TTL; PID checks; version/HRESULT centralization | UNVERIFIED | Harden Inspect JSON parsing, document log authorization, review reuse locking/jitter/TTL, PID checks in both directions, and centralize version/HRESULT formatting. |
| M18 | README/docs updates | PARTIAL | Root router README, `cpp/README` install notes, Quickstart 3-arg example, version banner in `CHANGELOG.md`; the S1 promotion gate and `Verify-Package` wiring are done. |

## LOW — polish

| ID | Finding | Status |
|----|---------|--------|
| L1 | Interpolate duplicate-port values | UNVERIFIED |
| L2 | Fixture gate + Describe/GetRecentLogs sample | UNVERIFIED |
| L3 | `.editorconfig` max_line_length, warnings, split e2e | UNVERIFIED |
| L4 | Shared `FormatMilliseconds`, parity tests, casing, digest/Stop notes | UNVERIFIED (F12 fixed the unit bug; the shared helper/parity tests remain) |
| L5 | Accepted-risk docs (RW volumes, LAN binds, secrets, tags) | UNVERIFIED |

## Redundancy / dead code

| ID | Finding | Status | Notes |
|----|---------|--------|-------|
| R2 | Env-var name validator duplicated verbatim | OPEN | `WslContainer.cs` vs `WslContainerBuilder.cs`; `wsl_container.cpp` vs `wsl_container_builder.cpp`. |
| R4 | Environment dictionary validated twice per exec/start | UNVERIFIED | `WslContainer.cs` then again in the runner; same in C++. |
| R5 | C++ `TakeLast`/`JoinLast` copied | OPEN | `wait.cpp` and `wsl_container.cpp`; a verifier also saw a dead duplicate `JoinLast` in `wait.cpp` under standalone clang-tidy. |
| R6 | Five near-identical C++ RAII handle wrappers | OPEN | `internal/api.hpp`, `process_runner.cpp` (`IoHandle` moved to `pipe_reader.hpp`), `tcp_http.cpp`. |
| R7 | `WaitForExit` vs `WaitForExitFor` duplicate wait machinery | OPEN | `container_process.cpp`. |
| R9 | `CleanupAsync` failure path unreachable; unused CT in `StopCoreAsync` | UNVERIFIED | `WslContainer.cs`. |
| R10 | Unused CT / test-only reaper methods | PARTIAL | `Inner` removed; `WslImageResolver.ResolveAsync` CT and the test-only `WslResourceReaper` methods remain. |
| R11 | Other C++ "dead" members | UNVERIFIED | `LogDumper` is intentional public testing surface; `release()` methods, JSON members, etc. still need symbol-level confirmation. |
| R12 | Example dead code | UNVERIFIED | `CustomerService.Create/GetCustomers`, `DbConnectionProvider.ConnectionString`, `PostgresFixture.ConnectionString`. |
| R13 | Redundant state/branches | UNVERIFIED | `_disposed`, `_normalizedPath`, platform revision fallback, empty-list branch. |
| R14 | Broadcaster `m_hasSnapshot` duplicate state | UNVERIFIED | `log_broadcaster.cpp`. |
| R15 | Test boilerplate duplication | PARTIAL | Low value until the suite is restructured. |

## New findings / residuals

| ID | Finding | Status | Notes |
|----|---------|--------|-------|
| N1 | `dotnet test <slnx>` runs zero tests (exit 5) while per-project works | OPEN | Document or fix the local invocation; CI already uses the per-project command. |
| N5 | No SBOM or C++ dependency update coverage | OPEN | Dependabot covers NuGet only; gtest is fetched at configure time. |
| N6 | No threat model, PR slicing, or effort/owner sizing | PROCESS | Useful before the remaining UNVERIFIED rows. |
| N7 | Present-but-absurd `createdAt` (e.g. 1970) still trips the PID-recycle check | OPEN | Same class as F9; sanity-bound the timestamp against directory creation time. |
| N8 | A process registered after the final `TakeAll` during a disposing stop is never drained | OPEN | Narrow window; wrapper leak rather than data loss. |
| N9 | Version-lockstep gate ignores the tag version | OPEN | `release.yml` packs with `/p:Version=<tag>` while the gate compares `VersionPrefix` to CMake; pass the tag version in or derive both from one source. |

## Accepted risks (no action planned)

- F5/F6: reparse points in **ancestor** path components are accepted by both languages.
- F1: custom checks that ignore the stop token can still overshoot their deadline.
- F13: a cancelled `CopyFrom` can take up to the ~4s abort grace to return.
- F16: containment is lexical only (a junction inside `instances` can still redirect at open).
- F18: files+volumes can total 128; tarball size is checked at build time (TOCTOU).
- F22: C++ accepts numerically integral floats (`4514.0`) because the JSON parser stores doubles (unreachable from the SDK's string `HostPort`).
- F23: C++ env-name validation depends on the C locale (host `setlocale` could diverge).
- F27: a pathological header can push `Describe()` past 64 KiB; the global cap is defense-in-depth.
- M11: internal guard-type names (`WslcHost`, `RuntimeGuard`/`LifecycleTracker`) are unchanged.
