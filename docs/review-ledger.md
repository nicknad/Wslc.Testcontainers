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
| F1 | `wait.cpp` timeout token never fires on `m_timeout`; checks/SleepFor can overshoot; composite deadline | FIXED | A file-local `StopTimer` (jthread) stops the derived token at the strategy deadline in `PollingStrategy::Wait` and `CompositeStrategy::Wait`; deadline backstops kept. Blocking token-aware checks abort in ~200 ms, and a composite with 5s children now bounds at the composite timeout (~0.2 s). Residual: checks that ignore the token can still overshoot. |
| F2 | `Run()` never closes stdin before wait | FIXED | `stdinHandle` is scoped to the `if (StandardInput)` block (`process_runner.cpp:175-197`); `CopyTo` resets explicitly at `:292`. |
| F3 | `WaitForExitFor` uses `wait` not `wait_for` | FIXED | `wait_for(lock, Timeout, ...)` with a comment naming the bug (`container_process.cpp:214-216`). |
| F4 | Registry `Snapshot()` then `Clear()` drops concurrently added processes | FIXED | `TakeAll()` prunes, collects, and clears under one lock (C# `ProcessRegistry.cs:43-52`, C++ `process_registry.hpp:52-59`); stop paths iterate it and no longer call `Clear()`. Stress tests prove no loss/duplication; the old pattern fails 500/500 in a replica harness. Residual: a start registered after the final drain during dispose is never drained (N8). |
| F5 | Build checks `Exists`/`Length` then Start re-opens; symlink swap can exfiltrate | FIXED | Builder rejects reparse-point file/volume sources (`WslContainerBuilder.cs:196-206`, `wsl_container_builder.cpp:234-247`). CopyTo opens without following links and validates the same handle: C# `Internal/HostFile.OpenRead` uses `CreateFileW` + `FILE_FLAG_OPEN_REPARSE_POINT` + `File.GetAttributes(SafeFileHandle)`; C++ `ProcessRunner::CopyTo` uses `GetFileInformationByHandle`/`GetFileSizeEx`. Residual: reparse points in ancestor path components are still accepted by both. |
| F6 | Reaper/instance delete recurses through junctions | FIXED | Reparse points are unlinked, never recursed (`WslInstanceStore.cs:110-130`, `instance_store.cpp:235-254`); reaper skips directories whose metadata `InstanceId` does not match the name. Verification could not reproduce recursive junction deletion on .NET 8/10 or MSVC 14.43, so this lands as defense-in-depth. Residual: reparse points in ancestor components and identity-mismatched dirs (fail-safe leak). |
| F7 | `CopyFrom` truncates destination before exit verification | OPEN | C# `WslcProcessRunner.cs:192`, C++ `process_runner.cpp:336`. Slice 2. |
| F8 | C# `CopyTo` pre-checks size only, no streaming accumulator | FIXED | Handle length at open plus a 64 KiB accumulator during the stream (`WslcProcessRunner.cs`); C++ CopyTo now accumulates too. |
| F9 | Missing `createdAt` deserializes to 0001/epoch, bypassing 7-day grace | FIXED | `CreatedAt` is nullable/optional; missing or unparseable timestamps route through the directory-age grace gate and the PID-recycle compare is skipped (`WslResourceReaper.cs:79-91,243-247`, `resource_reaper.cpp:115-122,188-199`). Residual: a present-but-absurd old timestamp (e.g. 1970) still trips the PID-recycle check (tracked as N7). |
| F10 | SDK download and gtest FetchContent unpinned | OPEN | `cpp/scripts/Install-WslcSdk.ps1:30-33`, `cpp/tests/CMakeLists.txt:10-12`. Slice 3. |
| F11 | `JENKINS_URL`/`TEAMCITY_VERSION` parsed as booleans, so CI detection misses them | FIXED | Shared internal classifier: boolean for `CI`/`TF_BUILD`/`GITHUB_ACTIONS`, non-blank presence for the two URL/version variables (`WslcEnvironment.cs:69-75`, `util.cpp:637-650`). Tests in both languages. Residual: NBSP-only values classify differently (Unicode vs C-locale whitespace). |
| F12 | `FormatMilliseconds` emits seconds but `"ms"` is appended in one caller | FIXED | `process_runner.cpp:205` now appends `"s."`; all call sites audited. Static-only: the message needs a live WSLC process. |
| F13 | `CopyFrom` reads ignore cancellation while blocked | FIXED | C# awaits `reader.LoadAsync(ChunkSize).AsTask(cancellationToken)` in the extracted `CopyStdoutAsync` (`WslcProcessRunner.cs:252-273`) so a cancelled copy throws `OperationCanceledException` promptly and the existing catch kills/disposes the process. C++ reads through `internal::ReadPipeAvailable` (`pipe_reader.cpp:19-59`), which polls `PeekNamedPipe`, reads only when bytes are available, returns 0 on broken pipe/EOF, and waits in 100 ms `SleepFor` slices that wake on stop; `CopyFrom` keeps the 1 GiB accumulator and error mapping (`process_runner.cpp:375`). Unit tests: `CopyCancel.*` (`copy_cancel_tests.cpp`, anonymous pipe: pre-cancelled token, data intact + EOF, idle-read cancellation ~104 ms) and `WslcProcessRunnerCopyTests` (empty output on cancel, data intact). Residual: the read unblocks immediately, but a cancelled public `CopyFrom` can still take up to the ~4s abort grace to return (SIGTERM+SIGKILL windows), and real silent-container cancellation remains WSL-gated. |
| F14 | Broadcaster `Subscribe()` replays up to 10k into a 1k-bounded subscriber | FIXED | C++ `Subscribe()` replays only the newest 1'000 (`log_broadcaster.cpp:66-70`); C# completed `StreamAsync` fast path now caps with `TakeLast(Snapshot(), MaxSubscriberBuffered)`, so both replay paths match. Ring-wrap and cap tests added in both languages. |
| F15 | HTTP path allows `\r\n`, SP, TAB; raw socket injects headers | OPEN | `wait.cpp:326-340`, `Wait.cs:127-141`, `tcp_http.cpp:221`. Slice 1. |
| F16 | Data directory verbatim; session id unbounded | FIXED | Configured data dir is absolutized and UNC/`\\?\`/`\\.\` paths are rejected; configured session id must match `[A-Za-z0-9_-]{1,64}` or throws, generated ids are sanitized/truncated; instance directories are containment-checked and trailing dots/spaces are replaced so names cannot alias the root (`WslcEnvironment.cs`, `util.cpp:326-435`, `instance_store.cpp:116-148`). `Default` is a `Lazy` so invalid config surfaces `WslcException` without an init race. Residuals: lexical-only containment (reparse escape), getters throw on invalid config. |
| F17 | Container path validation is only `StartsWith('/')`; no deny list, `..`, NUL | OPEN | Six sites: `WslContainer.cs:974`, `WslContainerBuilder.cs:481`, `Wait.cs:143`; `wsl_container.cpp:86`, `wsl_container_builder.cpp:36`, `wait.cpp:342`. Slice 1. |
| F18 | Builders uncapped (VHD, args, files, waits, CPUs, memory, timeouts) | FIXED | Central `BuilderLimits`/`limits.hpp` enforce: CPU<=64, memory<=1 TiB, VHD<=1 TiB, startup/exec timeout<=24h, waits<=16 (including `And` flattening), args<=1000, env<=1000x128 KiB, files<=64, volumes<=64, session volumes<=64, tarball<=1 TiB. The >49.7-day timeout now fails at Build instead of a raw `CancellationTokenSource` exception at Start; exec args/env are capped too. Residual: files+volumes combined can reach 128, tarball size is TOCTOU, custom `IWaitStrategy` counts are unbounded (F24). |
| F19 | `HttpWaitStrategy` follows container redirects by default | OPEN | The explicit `AllowAutoRedirect = true` was removed, but `new HttpClient()` (`HttpWaitStrategy.cs:9`) still follows redirects via the default handler. |
| F20 | `ci.yml`/`integration.yml` have no `permissions:` | FIXED | Top-level `permissions: contents: read` added to both workflows; YAML parsed and verified. |
| F21 | Container retry loop catches `IOException` only | FIXED | Reuse-lock acquisition retries `IOException` and `UnauthorizedAccessException` through `IsRetryableReuseLockError` with the same 100x300ms budget; cancellation and non-transient errors still propagate. C++ already retried every failure. |
| F22 | UTF-16 ordinal compare, `strtol`/`double` truncation, silent `{}` hashing | FIXED | UTF-16-compare and silent-encode claims refuted (5/5 digests matched). Added a shared 14-vector golden fixture at `tests/fixtures/config_hash_vectors.json` consumed by both suites (mutation-tested: both fail by case name; digests independently reproduced). C++ port parsing now requires `^[0-9]+$` strings and finite integral JSON numbers in [1,65535]; the lax `strtol` cases are rejected. Residual: C++ accepts numerically integral floats like `4514.0` because the JSON parser stores only doubles (unreachable from the SDK's string `HostPort`); tarball fields are hashed but unexercised. |
| F23 | `char.IsLetter` vs `isalpha`; Build vs Start absolutize timing | FIXED | Env names use the ASCII rule `[A-Za-z_][A-Za-z0-9_]*` in both languages (C# explicit range checks; C++ `isalpha` in the C locale with messages aligned). `WithFile` stores `Path.GetFullPath` at builder time in C#, matching C++ `absolute()`, so a CWD change after Build cannot redirect the copy and relative-path reuse hashes match across languages. Intentional breaking change for C# Unicode env names, documented in CHANGELOG. Residual: C++ relies on the C locale (host `setlocale` could diverge). |
| F24 | Custom waits bypass the no-network rule and ignore CT past startup | UNVERIFIED | `WslContainerBuilder.cs:390-404`, `wsl_container_builder.cpp:383-396`. |
| F25 | Assembler state mutated from native threads without a lock | UNVERIFIED | `ContainerProcess.cs:165-287`, `line_assembler.hpp`, `container_process.cpp:376-410`. |
| F26 | stdin unbounded; `written == 0` can spin | FIXED | 64 MiB cap validated before any process exists in both languages (`WslcProcessRunner.cs:15,118`, `util.hpp:31-32`, `process_runner.cpp:170-173`); C# writes 64 KiB chunks with cancellation checks; C++ `Run` and `CopyTo` write loops throw on a zero-byte successful write. Zero-write paths need a live WSL handle to exercise. |
| F27 | `Describe` can emit ~12 MiB including secrets/paths | FIXED | `Describe()` caps the body at 64 KiB total, 8 KiB per section (head + `... [N bytes omitted] ...` + tail), 4 KiB per line, UTF-8-safe, header never truncated (`Internal/TextTruncation.cs`, `internal/text_truncation.*`). Captured output is documented as untrusted. Residual: a pathological header can exceed 64 KiB; the global cap is defense-in-depth; C# uses `Environment.NewLine` vs C++ `\n` (pre-existing). |

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
| R3 | 1 GiB copy cap and host-file check in three layers | PARTIAL | The bypassable `file_size` pre-check is gone from `WslContainer`/`wsl_container.cpp`; builder still fails fast and the runner enforces on the open handle. |
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
| N7 | Present-but-absurd `createdAt` (e.g. 1970) still trips the PID-recycle check and deletes a live instance | OPEN | Same failure class as F9; sanity-bound the timestamp against directory creation time or require `createdAt` to be plausible before trusting the PID compare. |
| N8 | A process registered after the final `TakeAll` during a disposing stop is never drained by any later path | OPEN | `StartProcess` does not check disposal under the lifecycle lock; `CleanupSynchronously` never drains. Narrow window, wrapper leak rather than data loss. |

## Slice plan

1. **Injection (F15, F17, F19, R1)** — one internal validation helper per language; reject
   control characters in HTTP paths; reject `..`, control characters, and `/proc`, `/sys`,
   `/dev` in container paths; require absolute working directories; stop following redirects.
2. **CopyFrom atomicity (F7)** — temp file + atomic replace, reparse-point rejection,
   destination untouched on failure.
3. **Hash pins (F10, N5)** — SHA512 for `Install-WslcSdk.ps1`, `URL_HASH` for gtest.

## C++23 migration review (2026-10-04)

Scope: `cpp/` moved from C++20 to C++23 (`CMakeLists.txt` 3.25 floor, `cxx_std_23`), VS 2022
17.10+ is the documented toolchain floor, GoogleTest is bumped to v1.18.0 (SHA512 pinned), CI
LLVM is pinned to 20.1.8, and the docs state the new floors. Reviewed against `18c89c1` plus
the in-flight slice work in the working tree. Statuses: FIXED / ACCEPTED (deliberate no-change)
/ PROCESS.

### Phase 1 — standard and toolchain floor

| Change | Location | Evidence |
|--------|----------|----------|
| CMake floor 3.24 -> 3.25 | `cpp/CMakeLists.txt:1` | Sane VS 2022 17.6+ floor for `/std:` feature mapping |
| `CMAKE_CXX_STANDARD` 20 -> 23, `cxx_std_20` -> `cxx_std_23` | `cpp/CMakeLists.txt:9,57` | MSVC 14.43 emits `/std:c++latest` (`stdcpplatest`); MSVC has no `/std:c++23` switch, so CMake maps `cxx_std_23` to `c++latest` |
| clang-format `Standard: c++20 -> Latest` | `cpp/.clang-format:4` | `c++23` is not a clang-format enum value before LLVM 22 (`LS_Cpp23` exists only on `main`); LLVM 20 (the pin) rejects `c++23`/`c++2b`, so `Latest` is the correct value |
| GTest v1.15.2 -> v1.18.0 | `cpp/tests/CMakeLists.txt:6-12` | 2026-08 release; SHA512 computed and verified locally before pinning; ASan compile flags re-validated under C++23 |
| CI LLVM pinned 20.1.8 | `.github/workflows/ci.yml:81-100` | Matches the `windows-latest` image (LLVM 20.1.8); a version check installs the pin when the image floats; `$GITHUB_PATH` covers fresh installs |
| Docs floors | `README.md:14,55`, `cpp/README.md:39`, `AGENTS.md:7`, `CHANGELOG.md:9` | VS 2022 17.10+ / CMake 3.25+ / C++23 |

Verification (VS 17.14 / MSVC 14.43, x64): Release build clean; `ctest` 125/125 (integration
skipped without `WSLC_RUN_INTEGRATION`, `AtomicFile.EnsureReplaceableRejectsReparsePoints`
skipped without symlink privilege 1314); clang-tidy Ninja build warnings-as-errors clean; ASan
build + `ctest` 125/125; `/analyze` build exit 0; `clang-format --dry-run --Werror` clean over
84 files.

### Phase 2 — mechanical modernization

| Item | Locations | Evidence |
|------|-----------|----------|
| `std::format` | `log_line.cpp` (`ToString`), `internal/api.cpp` (`HresultHex`), `exceptions.cpp` (`Describe`), `internal/util.cpp` (`FormatIso8601`, `FormatMilliseconds`), `internal/json.cpp` (`Escape`), `internal/instance_store.cpp` (`RenderMetadata`) | Replaces `snprintf`/`ostringstream`/`+` chains; format specifiers preserve zero padding and widths, so rendered output is unchanged; `<cstdio>`/`<sstream>` dropped where no longer used |
| `contains()` | `internal/util.cpp` (`ValidateHttpPath`), `internal/image_resolver.cpp` (x2), `internal/tcp_http.cpp`, `tests/integration_tests.cpp` | `find(...) != npos` -> `contains(...)` |
| `std::unreachable()` | `internal/api.cpp` (`CreateException`) only | Internal closed enum, all call sites pass literal enumerators. `log_line.cpp` (`SourceName`) was reverted to a `"system"` fallback after review: `LogSource` is public, so an out-of-range cast made the old path undefined behavior |
| `std::jthread` | `tests/support/tiny_http_server.{hpp,cpp}`, `tests/wait_strategy_tests.cpp`, `tests/integration_tests.cpp` | Cancellation/timer threads; manual `join()` and the atomic stop flag removed in favor of `request_stop()` and member RAII joining |

Already idiomatic and left alone: the startup timer is a `jthread` (`wsl_container.cpp:312`)
and `SleepFor` uses `std::stop_callback` (`util.cpp`).

### Phase 3 — deliberate no-change

| Item | Decision | Rationale |
|------|----------|-----------|
| `std::expected` | ACCEPTED (no) | Would break public API; `optional` + throw is the documented contract (`json::Parse`, `ParseIso8601`, `TryReadMetadata`); `LogStream::Next` uses `nullopt` as an end sentinel, not an error |
| `std::flat_map` | ACCEPTED (no) | No profiling data; `std::map` / flat `vector<pair>` are right-sized today; would churn internal/public headers |
| `std::mdspan` | ACCEPTED (no) | No 2-D data; 1-D `std::span` is the correct abstraction |
| chrono `tzdb` / `zoned_time` | ACCEPTED (no) | Hand-rolled ISO-8601 keeps UTC-only semantics and avoids an OS time-zone database dependency and binary growth |
| `std::print` / `std::stacktrace` | ACCEPTED (no) | The library writes through its log sinks; stdout coupling and stack traces have no consumer |
| `[[assume]]` | ACCEPTED (no) | No profiling evidence; a violated assumption is a silent miscompile. Parser bounds are already checked |
| `views::join_with` | ACCEPTED (no) | `join()` reads clearer and avoids extra `<ranges>` machinery; no call site benefits |
| `jthread` for `start_thread` | ACCEPTED (no) | `JoinStartThread` waits with a timeout then deliberately **detaches** when the blocking attached native call does not return (`wsl_container.cpp:721-741`); a `jthread` destructor would block or terminate instead |
| `std::to_underlying` | ACCEPTED (no) | No enum-index arithmetic sites; converting switches adds ceremony with no behavior change |
| SDK-enum / parse-switch defaults | ACCEPTED (no) | `ProgressStatusName` (external `WslcImageProgressStatus`) and the JSON escape switch keep explicit fallbacks: those values come from outside the program's type system, so unknown/error handling is deliberate, not unreachable code |

### Phase 4 — lint/format re-baseline

- `.clang-tidy` unchanged: `modernize-use-std-format` is already inside the enabled
  `modernize-*` group (no `fmt`/`absl` sites, so no-op); `modernize-use-ranges` and
  `modernize-use-starts-ends-with` stay disabled (no broad ranges adoption, `contains` used
  selectively). `WarningsAsErrors: "*"` holds under the new C++23 compile flag.
- Two pre-existing findings blocked the first tidy pass; both fixed, one with a justified
  suppression:
  - `HttpGetSucceeds` defined its parameters in snake_case while the header declared camelCase
    (`readability-inconsistent-declaration-parameter-name`) — definition renamed.
  - `BCryptHashData` takes a non-const `PUCHAR` although it does not modify the input
    (`cppcoreguidelines-pro-type-const-cast`) — scoped `NOLINTNEXTLINE` plus comment.
- `clang-format` check is green with the C++23 style on LLVM 20.1.0; no source reformat needed.

### Migration findings

| ID | Finding | Status | Notes |
|----|---------|--------|-------|
| MIG1 | clang-format `Standard: c++23` is invalid before LLVM 22 | FIXED | Use `Latest`; switch to `c++23` if the LLVM pin reaches 22+ |
| MIG2 | MSVC has no `/std:c++23`; `cxx_std_23` compiles as `/std:c++latest` | ACCEPTED | Unavoidable CMake/MSVC mapping; `/Zc:__cplusplus` keeps feature detection honest |
| MIG3 | ASan test runs fail with `0xc0000135` unless the MSVC ASan runtime DLL is on PATH | PROCESS | Local-only: run the ASan `ctest` under `vcvars64.bat`; CI's `msvc-dev-cmd` already does. Add a note if local ASan instructions are published |
| MIG4 | Cosmetic: remaining startup-diagnostic `+` concatenation (`wsl_container.cpp`) | FIXED | 9 `publish_diagnostic` sites converted to `std::format` with byte-identical rendered text; build/tests/format clean |
| MIG5 | New tests from the in-flight slice work were included in all four verification builds | PROCESS | Release/tidy/ASan/`analyze` all green together on the same tree |
| MIG6 | `SourceName` used `std::unreachable()` for an out-of-range public `LogSource` cast (UB, crash at -O2) | FIXED | Reverted to the `"system"` fallback; regression test `LogLine.UnknownSourceFallsBackToSystem` |
| MIG7 | CI LLVM pin could not resolve a fresh install in-step and lacked downgrade allowance | FIXED | `$env:PATH` prepend after install plus `--allow-downgrade` |
