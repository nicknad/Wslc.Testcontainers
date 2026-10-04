# Review ledger

Status of the findings from the two 2026-10 hardening reviews, rebased against `main` at
`d603bfb`. Statuses mean:

- **FIXED** — the described behavior is gone in HEAD; evidence points at the current code.
- **OPEN** — reproduced by reading HEAD (tests where noted).
- **UNVERIFIED** — plausible but not reproduced on HEAD; do the targeted test before work.
- **FALSE POSITIVE** — the finding does not hold for HEAD.
- **PROCESS** — a plan/evaluation gap rather than a code defect.

Ground truth on HEAD: C++ `ctest` 116 tests (103 pass, 13 WSL-gated skips, 0 fail). C# 133
tests pass via the CI command (`dotnet test tests/Wslc.Testcontainers.Tests/...csproj -f
<tfm> -- --filter "FullyQualifiedName!~Integration"`); `dotnet test Wslc.Testcontainers.slnx`
runs zero tests (N1). The two source lists predate the monorepo split and several of their
items are already fixed here; verify against this ledger before starting.

## P0 / HIGH

| ID | Finding | Status | Evidence / notes |
|----|---------|--------|------------------|
| F1 | `wait.cpp` timeout token never fires on `m_timeout`; checks/SleepFor can overshoot; composite deadline | OPEN | `PollingStrategy` has a steady-clock deadline backstop (`wait.cpp:136-182`) but the derived stop token is only caller-cancel. Needs runtime overshoot test; composite path unverified. |
| F2 | `Run()` never closes stdin before wait | FIXED | `stdinHandle` is scoped to the `if (StandardInput)` block (`process_runner.cpp:175-197`); `CopyTo` resets explicitly at `:292`. |
| F3 | `WaitForExitFor` uses `wait` not `wait_for` | FIXED | `wait_for(lock, Timeout, ...)` with a comment naming the bug (`container_process.cpp:214-216`). |
| F4 | Registry `Snapshot()` then `Clear()` drops concurrently added processes | OPEN | `cpp/src/internal/process_registry.hpp:42-58`, `csharp/.../Internal/ProcessRegistry.cs:31-43`. |
| F5 | Build checks `Exists`/`Length` then Start re-opens; symlink swap can exfiltrate | OPEN | `WslContainerBuilder.cs:191-238`, `wsl_container_builder.cpp:238-281`. |
| F6 | Reaper/instance delete recurses through junctions | OPEN | `WslInstanceStore.cs:110-122` (`Directory.Delete recursive`), `instance_store.cpp:233-247` (`remove_all`); no reparse check, no `InstanceId == directory` check. |
| F7 | `CopyFrom` truncates destination before exit verification | OPEN | C# `WslcProcessRunner.cs:192`, C++ `process_runner.cpp:336`. Slice 2. |
| F8 | C# `CopyTo` pre-checks size only, no streaming accumulator | OPEN | `WslcProcessRunner.cs:147-165`; C++ has the accumulator. |
| F9 | Missing `createdAt` deserializes to 0001/epoch, bypassing 7-day grace | OPEN | `WslInstanceStore.cs:73-95` + `WslResourceReaper.cs:223`; `instance_store.cpp:190-200`. |
| F10 | SDK download and gtest FetchContent unpinned | OPEN | `cpp/scripts/Install-WslcSdk.ps1:30-33`, `cpp/tests/CMakeLists.txt:10-12`. Slice 3. |
| F11 | `JENKINS_URL`/`TEAMCITY_VERSION` parsed as booleans, so CI detection misses them | OPEN | `WslcEnvironment.cs:60-81`, `environment.cpp:55`. |
| F12 | `FormatMilliseconds` emits seconds but `"ms"` is appended in one caller | OPEN | `process_runner.cpp:205` vs `util.cpp:451-460`; all other callers append `"s"`. |
| F13 | `CopyFrom` reads ignore cancellation while blocked | OPEN | C# `reader.LoadAsync` without token (`WslcProcessRunner.cs:207`); C++ `ReadFile` (`process_runner.cpp:348`). |
| F14 | Broadcaster `Subscribe()` replays up to 10k into a 1k-bounded subscriber | OPEN | `log_broadcaster.cpp:62-69` vs `MaxHistory=10'000` / `MaxSubscriberBuffered=1'000` (`log_broadcaster.hpp:33-34`). |
| F15 | HTTP path allows `\r\n`, SP, TAB; raw socket injects headers | OPEN | `wait.cpp:326-340`, `Wait.cs:127-141`, `tcp_http.cpp:221`. Slice 1. |
| F16 | Data directory verbatim; session id unbounded | OPEN | `WslcEnvironment.cs:42-53`, `environment.cpp:17-46`. |
| F17 | Container path validation is only `StartsWith('/')`; no deny list, `..`, NUL | OPEN | Six sites: `WslContainer.cs:974`, `WslContainerBuilder.cs:481`, `Wait.cs:143`; `wsl_container.cpp:86`, `wsl_container_builder.cpp:36`, `wait.cpp:342`. Slice 1. |
| F18 | Builders uncapped (VHD, args, files, waits, CPUs, memory, timeouts) | UNVERIFIED | Some caps may exist; enumerate per property before changing. |
| F19 | `HttpWaitStrategy` follows container redirects by default | OPEN | The explicit `AllowAutoRedirect = true` was removed, but `new HttpClient()` (`HttpWaitStrategy.cs:9`) still follows redirects via the default handler. |
| F20 | `ci.yml`/`integration.yml` have no `permissions:` | OPEN | Only `release.yml:13` declares them. |
| F21 | Container retry loop catches `IOException` only | OPEN | `WslContainer.cs:471`; ACL failures are not retried. |
| F22 | UTF-16 ordinal compare, `strtol`/`double` truncation, silent `{}` hashing | UNVERIFIED | Hash/port parity claims need golden vectors. |
| F23 | `char.IsLetter` vs `isalpha`; Build vs Start absolutize timing | UNVERIFIED | Decide one ASCII rule and one validation point. |
| F24 | Custom waits bypass the no-network rule and ignore CT past startup | UNVERIFIED | `WslContainerBuilder.cs:390-404`, `wsl_container_builder.cpp:383-396`. |
| F25 | Assembler state mutated from native threads without a lock | UNVERIFIED | `ContainerProcess.cs:165-287`, `line_assembler.hpp`, `container_process.cpp:376-410`. |
| F26 | stdin unbounded; `written == 0` can spin | OPEN | C# writes the whole string in one call (`WslcProcessRunner.cs:107`); C++ loop only exits on failure (`process_runner.cpp:183-196`). |
| F27 | `Describe` can emit ~12 MiB including secrets/paths | UNVERIFIED | `Exceptions.cs:110-157`, `exceptions.cpp:52-96`. |

## MED / API and quality

The MED paragraph is one long list; it is tracked here by theme. All are UNVERIFIED on HEAD
and must be checked against `PublicAPI.Unshipped.txt` before the first shipped snapshot.

| ID | Theme | Status |
|----|-------|--------|
| M1 | `ExecAsync` overload collapse + `StartProcessOptions` split | UNVERIFIED |
| M2 | Argument vs runtime exception taxonomy; platform exception nesting | UNVERIFIED |
| M3 | `WithPort(IPAddress)`; one `GetConnectEndpoint` replacing host getters; unify `containerPort` | UNVERIFIED |
| M4 | `StartProcessAsync` / honor CT | UNVERIFIED |
| M5 | `DumpAsync(container, tail)`; `And()` timeout sum | UNVERIFIED |
| M6 | Drop `WithReadOnlyVolume` / parameterless `GetMappedPort` | UNVERIFIED |
| M7 | `IsReuseEffective` | UNVERIFIED |
| M8 | `Inner` visibility; TimeSpan/ms, dict/map, Version parity | UNVERIFIED |
| M9 | Reversible builders; drop `SuppressFinalize`/`RequireNetwork` | UNVERIFIED |
| M10 | Clamp sums; unify pre-start behavior; `127.0.0.1` vs `::1` | UNVERIFIED |
| M11 | Naming batch (Host, guard types, WithMemoryMegabytes, Until*, SubscribeLogs/GetEndpoint, ScratchVolume, Isolated, ToString, InnerContainer, ConfigureContainer, WithReadinessTimeout, enum scoping, DefaultPort, Command casing, Stdout) | UNVERIFIED; land before first `PublicAPI.Shipped.txt` |
| M12 | Split god classes (1043/1406 lines) | UNVERIFIED |
| M13 | Limits/Timeouts/Validation hubs | Slice 1 starts Validation (F15-F17) |
| M14 | Catch audit; re-enable clang-tidy checks | UNVERIFIED |
| M15 | Module constants; Doxygen casing; `reserve`; WSLC_ env validation | UNVERIFIED |
| M16 | Tarball name/size caps; volume regex; args/cwd caps | UNVERIFIED |
| M17 | Inspect isfinite/1 MiB cap; log-auth docs; reuse mutex/jitter/TTL; PID checks; version/HRESULT centralization | UNVERIFIED |
| M18 | README/docs updates (root router, cpp README, Quickstart, version banner, Verify-Package) | UNVERIFIED |
| M19 | Wire ctest and per-finding tests | In progress with this ledger's slices |

## LOW polish

| ID | Finding | Status |
|----|---------|--------|
| L1 | Interpolate duplicate-port values | UNVERIFIED |
| L2 | Fixture gate + Describe/GetRecentLogs sample | UNVERIFIED |
| L3 | `.editorconfig` max_line_length, warnings, split e2e | UNVERIFIED |
| L4 | Shared `FormatMilliseconds`, parity tests, casing, digest/Stop notes | UNVERIFIED (F12 is the bug half) |
| L5 | Accepted-risk docs (RW volumes, LAN binds, secrets, tags) | UNVERIFIED |

## Redundancy / dead code review

| ID | Finding | Status | Notes |
|----|---------|--------|-------|
| R1 | Absolute-path validator implemented 3x per language | OPEN -> fixed by slice 1 | C# `WslContainer.cs:974`, `WslContainerBuilder.cs:481`, `Wait.cs:143`; C++ `wsl_container.cpp:86`, `wsl_container_builder.cpp:36`, `wait.cpp:342`. |
| R2 | Env-var name validator duplicated verbatim | OPEN | `WslContainer.cs:1017` vs `WslContainerBuilder.cs:506`; C++ `wsl_container.cpp:45` vs `wsl_container_builder.cpp:45`. |
| R3 | 1 GiB copy cap and host-file check in three layers | OPEN | `WslContainer.cs:180`, `WslContainerBuilder.cs:200`, `WslcProcessRunner.cs:147`; C++ `wsl_container.cpp:1204`, `process_runner.cpp:240`. |
| R4 | Environment dictionary validated twice per exec/start | UNVERIFIED | `WslContainer.cs:999` then `:719`; `wsl_container.cpp:71` then `:1062`. |
| R5 | C++ `TakeLast`/`JoinLast` copied | OPEN | `wait.cpp:25,35` and `wsl_container.cpp:95,105`. |
| R6 | Five near-identical C++ RAII handle wrappers | OPEN | `internal/api.hpp:49-220`, `process_runner.cpp:22`, `tcp_http.cpp:39`. |
| R7 | `WaitForExit` vs `WaitForExitFor` duplicate wait machinery | OPEN | `container_process.cpp:121-296`. |
| R8 | `IntegrationFactAttribute` byte-identical in two assemblies | FALSE POSITIVE | Per-assembly xunit attribute is the normal pattern; extracting it would add a shared test package for no behavior gain. |
| R9 | `CleanupAsync` failure path unreachable; unused CT in `StopCoreAsync` | UNVERIFIED | `WslContainer.cs:781-808`. |
| R10 | Unused CT/test-only reaper methods/`WslModuleContainer.Inner` | UNVERIFIED | `WslImageResolver.cs:20`, `WslResourceReaper.cs:132,152`, `WslModuleContainer.cs:16`. |
| R11 | C++ `LogDumper` and other "dead" members | FALSE POSITIVE / UNVERIFIED | `LogDumper` is public testing surface (`PublicAPI.Unshipped.txt`, docs). `release()` methods, JSON members, etc. still need symbol-level confirmation. |
| R12 | Example dead code | UNVERIFIED | `CustomerService.Create/GetCustomers`, `DbConnectionProvider.ConnectionString`, `PostgresFixture.ConnectionString`. |
| R13 | Redundant state/branches (`_disposed`, `_normalizedPath`, revision fallback, empty list) | UNVERIFIED | `WslContainer.cs:44`, `HttpWaitStrategy.cs:11`, `WslPlatform.cs:59`, `WslcProcessRunner.cs`. |
| R14 | Broadcaster `m_hasSnapshot` etc. duplicate state | UNVERIFIED | `log_broadcaster.cpp:45`. |
| R15 | Test boilerplate duplication | PARTIAL | RAII already disposes in C++ fixtures; de-duplication is low value until the suite is restructured. |

## New findings from the 2026-10-04 runtime pass

| ID | Finding | Status | Notes |
|----|---------|--------|-------|
| N1 | `dotnet test <slnx>` runs zero tests (exit 5) while per-project works | OPEN | CI already uses the per-project command; document/fix the local path so the stated verify command is honest. |
| N2 | Both review lists are stale relative to HEAD | PROCESS | Several P0 items are fixed here (F2, F3, F19 partial). Always rebase before implementing. |
| N3 | The prior "cpp/tests absent" and "LogDumper dead" claims were wrong | PROCESS | The missing `cpp/tests` observation matches `pr-2`, a pre-monorepo branch, not `main`. |
| N4 | No shared golden-vector corpus across C#/C++ | OPEN | Parity is asserted by parallel hand-written tests; the plan's "golden vectors" requirement is unmet. |
| N5 | No SBOM or C++ dependency update coverage | OPEN | Dependabot covers NuGet only; gtest is fetched at configure time. |
| N6 | No threat model, PR slicing, or effort/owner sizing | PROCESS | Needed before attacking UNVERIFIED rows. |

## Slice plan

1. **Injection (F15, F17, F19, R1)** — one internal validation helper per language; reject
   control characters in HTTP paths; reject `..`, control characters, and `/proc`, `/sys`,
   `/dev` in container paths; require absolute working directories; stop following redirects.
2. **CopyFrom atomicity (F7)** — temp file + atomic replace, reparse-point rejection,
   destination untouched on failure.
3. **Hash pins (F10, N5)** — SHA512 for `Install-WslcSdk.ps1`, `URL_HASH` for gtest.
