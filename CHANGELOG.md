# Changelog

All notable changes to the `Wslc.Testcontainers*` packages and the C++ port.

## Unreleased

### Added

- `Modules.Kafka` (C#) / `wslc_kafka` (C++): single-node Apache Kafka 4.3.1 in KRaft mode (client
  port 9092, `Kafka Server started` readiness, `GetBootstrapServers()` as `127.0.0.1:<host-port>`).
  Kafka is the one module that reserves a **fixed** loopback host port (a broker tells clients to
  reconnect to its advertised listener, so the port must be known before start) and therefore
  fails fast if that port is already in use — avoid starting several Kafka containers in parallel.
- Fixed host ports: `WithPort(port, hostPort)` and `WithPort(port, hostPort, bindAddress)` reserve a
  specific Windows host port instead of a dynamic one (C# and C++). Needed by services that must
  advertise the host address clients reach them on (for example Kafka's advertised listener); the
  runtime fails fast at start if the fixed port is already in use. A dynamic declaration is upgraded
  when a module pins the host port, and the host port is part of the reuse identity and the shared
  golden hash vectors.

## 0.4.0

Rolls up the 0.3.1 and 0.3.2 changes. 0.3.1 never left the release gate; 0.3.2 was
published and indexed (slowly), then superseded by 0.4.0. The 0.3.1 section below
lists the rest of the content.

### Fixed

- Process exit no longer crashes after a container was used: the C++ cleanup registry is now
  constructed before the `atexit` hook, so the exit handler no longer iterates a destroyed
  registry (heap-use-after-free, caught by ASan and the C++ integration suite).

## 0.3.1

### Fixed

- Reuse instances can be restarted: `StopAsync`/`Stop` now releases the reuse lock, so the
  documented `Stop` then `Start` sequence no longer stalls 30s and fails (C# and C++).
- C++ `CopyFrom` checks the temporary file's write state before replacing the destination, so a
  disk-full write can no longer commit a truncated file.
- C++ teardown no longer deletes the runtime container while a detached attached-start call may
  still be using it; the session teardown reclaims it and a diagnostic is logged.
- `WSLC_TIMEOUT` (and clock-time values) with non-finite or out-of-range numbers now falls back to
  the default instead of throwing `OverflowException` (C#) or invoking undefined behavior (C++).
- C# `IWslProcess.WaitForExitAsync` no longer hangs when the process is disposed while awaited:
  the `Exited` subscription stays active so the SIGKILL completes the exit task.
- C++ `WaitForExit` and `LogStream::Next` no longer miss a stop notification (stop-token aware
  condition waits).
- Instance metadata write failures are no longer silent: C++ `WriteMetadata` throws and both
  ports log the failure when a state update cannot be persisted.
- Module endpoint and connection-URL helpers bracket IPv6 hosts (e.g. `http://[::1]:49153`) in
  both languages.
- C++ port-mapping resolution is synchronized with concurrent readiness probes.

### Changed

- Internal dedup only (no public API changes beyond additive protected module helpers): shared
  `PercentEncode` and endpoint-formatting helpers, a single 1 GiB copy cap, removal of duplicate
  environment/stdin validation passes, shared argv/environment marshalling, a single CI-variable
  list, a common `BuilderLimits` range helper, and registry prune disposal.
- Docs: removed the stale session handover/workflow/review artifacts; `docs/review-ledger.md` now
  tracks the remaining findings.

## 0.3.0

### Added

- Ten more service modules alongside the existing presets, mirrored in C++:
  - `Modules.MongoDb` (C#) / `wslc_mongodb` (C++): MongoDB 8 (port 27017, optional root credentials, driver connection string).
  - `Modules.Nats` (C#) / `wslc_nats` (C++): NATS 2 (port 4222, optional JetStream and credentials, `nats://` URL).
  - `Modules.MailPit` (C#) / `wslc_mailpit` (C++): Mailpit v1.31 (SMTP 1025 + HTTP 8025, `/livez` readiness, `GetSmtpEndpoint()` / `GetHttpEndpoint()`).
  - `Modules.RustFs` (C#) / `wslc_rustfs` (C++): RustFS 1.0.1 S3-compatible object storage (API 9000 + console 9001, `/health` readiness, `AccessKey`/`SecretKey`).
  - `Modules.WireMock` (C#) / `wslc_wiremock` (C++): WireMock 3 (port 8080, `/__admin/health` readiness, `GetEndpoint()`).
  - `Modules.Qdrant` (C#) / `wslc_qdrant` (C++): Qdrant v1.19.2 vector database (HTTP 6333 + gRPC 6334, `/readyz` readiness).
  - `Modules.ClickHouse` (C#) / `wslc_clickhouse` (C++): ClickHouse 26.7 (HTTP 8123 + native 9000, `/ping` readiness, ClickHouse.Client connection string).
  - `Modules.Vault` (C#) / `wslc_vault` (C++): HashiCorp Vault 2.1 dev mode (port 8200, `/v1/sys/health` readiness, `GetAddress()` + `RootToken`).
  - `Modules.Keycloak` (C#) / `wslc_keycloak` (C++): Keycloak 26.8 `start-dev` (port 8080, `/realms/master` readiness, admin credentials).
  - `Modules.Elasticsearch` (C#) / `wslc_elasticsearch` (C++): Elasticsearch 9.5.3 single node (port 9200, `_cluster/health` readiness, security off, 512 MB heap, niofs storage because the session kernel's `vm.max_map_count` is below the mmap bootstrap check).
- Module base: `ReadyMessageOccurrences` may return 0 to skip the log-message wait for services without a stable readiness log line; `WaitTimeout` is available to `Configure` for module-added waits (used by the HTTP readiness checks).
- The module test suite runs sequentially (`Parallelization(Mode = None)`): parallel WSL sessions exhausted the runtime.

### Changed

- Modules now pass the image's service command as the configured command (e.g. `mongod`, `nats-server`) rather than repeating the image entrypoint path; the runtime preserves the image ENTRYPOINT and treats the configured command as its CMD.

## 0.2.0

### Added

- Three more service modules alongside `Modules.PostgreSql` and `Modules.Redis`, mirrored in C++:
  - `Modules.Valkey` (C#) / `wslc_valkey` (C++): Redis-compatible successor (`docker.io/valkey/valkey:8-alpine`, port 6379, `GetEndpoint()`).
  - `Modules.MariaDb` (C#) / `wslc_mariadb` (C++): MariaDB 11.4 LTS (port 3306, `MARIADB_*` credentials, MySqlConnector-style connection string).
  - `Modules.RabbitMq` (C#) / `wslc_rabbitmq` (C++): RabbitMQ 4 (AMQP port 5672, `amqp://` connection URI, readiness via `Server startup complete`).
- `Wslc.Testcontainers.Modules.Tests`: module unit tests run in CI; real-container integration tests are gated by `WSLC_RUN_INTEGRATION=1` (C++ counterparts live in the GoogleTest suite).
- Release packs every module package (`ci.yml`, `release.yml`, `Verify-Package.ps1`), and the CI unit jobs run the module tests on all TFMs.

### Changed

- Development now happens on the `development` branch. `main` only accepts pull requests
  from `development` (enforced by the `main-source-guard` workflow plus the
  `main-protection` repository ruleset; see `docs/branching.md`).

## 0.1.0-preview.1

First public preview of `Wslc.Testcontainers` (+ `Modules.PostgreSql`, `Modules.Redis`).

### Added

- Monorepo layout: the .NET implementation moved to `csharp/`; a full C++23 port now lives in
  `cpp/` and builds on the native `wslcsdk` C API with CMake.
- C++ port parity: mutable `WslContainerBuilder`/`WslContainer`, exec/start-process with captured
  stdio, file copy in/out, log streaming with bounded history, wait strategies (TCP, HTTP,
  process, log message, file, custom), dynamic port mappings, bind mounts and scratch VHD
  volumes, reuse hashing/locking, orphan reaper, and the PostgreSql/Redis modules.
- C++ analysis baseline: `.clang-format` (Microsoft style), `.clang-tidy` (warnings as errors),
  AddressSanitizer option, MSVC `/analyze` option, and CI format/tidy/ASan jobs.
- GoogleTest-based C++ suite (unit tests ported from the C# suite plus real-runtime integration
  tests gated by `WSLC_RUN_INTEGRATION=1`).

- `Wait.ForWsl().Until(name, condition)`: poll a custom readiness delegate with the builder's timeout/retry settings.
- `IWslContainer.GetRecentLogs(int maxLines = 50)` (also on `WslContainer` and module containers): bounded newest-lines snapshot for failure triage without enumerating the infinite `SubscribeLogs` stream.
- `IsReuseEffective` (C# `IWslContainer`/`WslContainer`/module containers, C++ `WslContainer`/`WslModuleContainer`):
  reports whether reuse will actually be used — configured reuse (or `WSLC_REUSE`) that is not suppressed
  under CI (`WSLC_REUSE_IN_CI`). Lets callers detect the silent fallback to an ephemeral instance without
  starting the container (see `docs/reuse.md`).
- `WslEndpoint` (C++): the Windows host/port pair returned by `GetConnectEndpoint`.
- Readiness failures and startup timeouts now hint when no init command was configured: WSLC never runs the image's ENTRYPOINT/CMD automatically.
- XML docs for the module packages (`PostgreSqlBuilder`/`PostgreSqlContainer`/`RedisBuilder`/`RedisContainer`).

### Changed

- Builder and exec inputs are now capped, and the caps are part of the documented contract.
  Exceeding a cap fails before reaching the WSLC runtime with an actionable domain exception
  (`WslException` / `ArgumentOutOfRangeException` in C#, `WslException` in C++); single-value
  caps fail fast in the `With...` setter or exec call, aggregate caps in `Build()`:

  | Input | Maximum | Checked at |
  | --- | --- | --- |
  | Environment variables | 1000 entries | `Build()`, exec options |
  | Environment value | 128 KiB (UTF-8) | `WithEnvironment`/`WithEnvironmentVariables`, exec options |
  | Command arguments | 1000 | `Build()` (`WithCommand`), exec/start-process |
  | File copies | 64 | `Build()` |
  | Volume mounts | 64 | `Build()` |
  | Scratch volumes | 64 | `Build()` |
  | Wait strategies | 16 | `Build()` and `And(...)` composition (composites flatten; a nested composite can hold up to 16 per level) |
  | CPU count | 64 | `WithCpuCount` |
  | Memory | 1048576 MB (1 TiB) | `WithMemoryMegabytes` |
  | Session VHD size | 1 TiB | `WithScratchVolume` |
  | Tarball size | 1 TiB | `FromTarball` |
  | Startup timeout | 24 h | `WithReadinessTimeout` (must also cover the wait-timeout sum) |
  | Exec timeout | 24 h | `ExecOptions.Timeout` |

  Wait-timeout summation saturates instead of overflowing, so an inconsistent startup budget is
  rejected with the domain exception rather than an `OverflowException` or a raw
  `ArgumentOutOfRangeException` from the startup `CancellationTokenSource`.

- `And(...)` composites now consistently keep the left operand's timeout (which bounds the whole sequence) and retry interval; the non-composite + non-composite case previously fell back to the default timeout.
- `CS1591` is no longer globally suppressed: missing XML docs on public members fail the strict build.
- `WslModuleContainer` now implements `IWslContainer`, so module containers are substitutable wherever the interface is expected.
- Pre-start errors are unified on `WslException` (previously `WslNetworkException` for `GetMappedPort` and `InvalidOperationException` for exec/copy/start-process), so `catch (WslException)` is complete.
- `WslReadinessException` is immutable; the container builds the fully populated instance before throwing instead of mutating properties after creation.
- `WslcException` was renamed `WslException` and `WslcEnvironment` `WslEnvironment` (C# and C++),
  so the whole exception/environment hierarchy follows the `Wsl*` prefix; the `WSLC_*` environment
  variable names are unchanged.
- C++ accessors now follow the C# names: `WslReadinessException.Command()`, `Image()`, `Stdout()`
  and `Stderr()` (previously `command()`, `StdoutText()`, `StderrText()`), and `ExecResult.Stdout`/
  `ExecResult.Stderr` (previously `StdoutText`/`StderrText`). `WslReadinessException::WithDiagnostics`
  is no longer public API: the container applies the diagnostic enrichment internally before it
  rethrows the failure.
- `FromTarball` validates that the tarball exists when called (matching `WithFile`/`WithVolume`) instead of at `Build()`.
- `WslEnvironment.DataDirectory` is snapshotted on first use, matching its documentation and `SessionId`.
- `WslPlatform.MinimumWslVersion` is a read-only property instead of a public field.
- `LogLine.Diagnostic` is internal; construct `LogLine` directly for custom lines.
- Reuse identity (`Name`) covers configuration metadata only: `WithFile` source contents are no longer hashed (they are copied on every start), so `Name` performs no file I/O.
- Builders (`WslContainerBuilder`, module builders) are now mutable: every `With...` mutates and returns the same builder; `Build()` snapshots the configuration. Supersedes ADR-0003 (see ADR-0007).
- `WslContainerBuilder.FromImage` renamed `WithImage` (matches module builders and Testcontainers).
- `WslModuleBuilder<TBuilder>.WithStartupTimeout` renamed `WithWaitTimeout`: the value is the per-wait timeout; the startup budget is derived as `2*t+30s`.
- README quickstart now uses `PostgreSqlBuilder`; docs call out that the image ENTRYPOINT/CMD is never started automatically.
- Environment variable names now follow the ASCII rule `[A-Za-z_][A-Za-z0-9_]*` in C# as well as
  C++: names starting with or containing non-ASCII letters/digits (e.g. `café`, `Ωmega`) are
  rejected with `ArgumentException`. This is an intentional breaking change: the Unicode
  `char.IsLetter`/`char.IsLetterOrDigit` checks accepted them while the C++ port already rejected
  them; both languages now report the same message.
- `WithFile` absolutizes the host source path when the builder call runs, matching `WithVolume`
  and the C++ port. A current-directory change between `Build()` and `StartAsync()` can no longer
  redirect the copy, and the reuse hash now matches C++ for the same relative input.
- Exec/process options are split by scope: `ProcessOptions` carries the environment and working
  directory, and `ExecOptions` derives from it with `StandardInput`/`Timeout` (C++:
  `ProcessOptions` / `ExecOptions : ProcessOptions`). `ExecAsync` and `StartProcess` collapse to
  one overload each — `ExecAsync(string command, string[]? arguments = null, ExecOptions? options
  = null, CancellationToken cancellationToken = default)` and `StartProcess(string command,
  string[]? arguments = null, ProcessOptions? options = null, CancellationToken cancellationToken
  = default)` — so an argument array is explicit (`ExecAsync("ps", ["aux"])`) and `ExecAsync("cmd")`
  still works. Because `ExecOptions` derives from `ProcessOptions`, `StartProcess` still accepts
  an `ExecOptions` but rejects one carrying `StandardInput`/`Timeout` (those apply only to
  `ExecAsync`): C# throws `ArgumentException`, C++ throws `WslException` after an RTTI check on
  the now-polymorphic `ProcessOptions` base (`StartProcess` takes `const ProcessOptions&`, so the
  derived type stays visible and nothing is sliced). C++ `ProcessOptions`/`ExecOptions` are no
  longer aggregates because of that virtual base, so designated-initializer syntax such as
  `ExecOptions{ .Timeout = ... }` no longer compiles; use value-initialization plus member
  assignment (`ExecOptions o; o.Timeout = ...;`). C# object initializers are unaffected.
- One connect endpoint replaces the host/port getters in both languages:
  `System.Net.IPEndPoint GetConnectEndpoint(int containerPort)` (C# `IWslContainer`,
  `IWaitTarget`, `WslModuleContainer`, `WslContainer`) and `WslEndpoint GetConnectEndpoint(int
  containerPort)` (C++). It returns the runtime-assigned host port plus the mapping's configured
  bind address (loopback for default/wildcard bindings); wait strategies, module connection
  helpers and diagnostics all use it. Before `StartAsync`/`Start` it throws `WslException`
  (`WslNetworkException` for undeclared/unassigned ports, unchanged).
- `WslContainerBuilder.WithPort(int port, System.Net.IPAddress address)` joins the string overload;
  both normalize the address and conflict-check identical ports the same way.
- Pre-first-release naming batch (C# and C++): `WslWaitBuilder.UntilTcpPortIsAvailable` ->
  `UntilTcpPortIsOpen`, `UntilHttpRequestIsSucceeded` -> `UntilHttpRequestSucceeds`;
  `IWslContainer.LogsAsync` -> `SubscribeLogs` (C++ `WslContainer::Logs` /
  `WslModuleContainer::Logs` -> `SubscribeLogs`); `LogDumper.DumpAsync` -> `DumpHeadAsync`
  (C++ `LogDumper::Dump` -> `DumpHead`) with the docs now saying "head" explicitly;
  `WithSessionVolume` -> `WithScratchVolume` (type `WslSessionVolume` -> `WslScratchVolume`,
  collection `SessionVolumes` -> `ScratchVolumes`); `WithMemoryMB` -> `WithMemoryMegabytes`;
  `WithStartupTimeout` -> `WithReadinessTimeout` (the value still bounds startup; module
  `WithWaitTimeout` is unchanged); `WslModuleBuilder<TBuilder>.WithContainerConfiguration` ->
  `ConfigureContainer`; `ContainerNetworkMode.None` -> `ContainerNetworkMode.Isolated` (numeric
  value stays 1, docs say "fully isolated"). Module constants `ContainerPort` -> `DefaultPort`,
  and Redis `GetConnectionString()` -> `GetEndpoint()` because it returns `host:port` rather
  than a connection string (PostgreSql keeps `GetConnectionString()`).
- `WslModuleContainer.DisposeAsync` is now abstract; derived (sealed) module containers implement it
  with the protected `DisposeInnerAsync` helper. The base is unsealed and has no finalizer, so it no
  longer needs a `GC.SuppressFinalize` pattern (CA1816).
- `ExecResult.ToString()` (C# only) now returns a bounded summary — exit code plus stdout/stderr
  lengths, never their contents — so logging a result cannot dump megabytes of captured output.
  Record equality, `EnsureSuccess` and the payload properties are unchanged; the C++ `ExecResult`
  has no string-formatting or equality surface, so nothing is mirrored there.
- C++ hygiene (no behavior change): `WslContainerBuilder` is `final`; the static-only
  `WslEnvironment`, `WslPlatform` and `WslResourceReaper` delete their constructors; and
  `WslWaitBuilder` is no longer default-constructible publicly (`ForWsl()` is the factory; the C#
  builder already had an internal constructor).
- `scripts/Verify-Package.ps1` now enforces the static release invariants: a version-lockstep check
  (numeric C# `VersionPrefix` vs the C++ CMake project `VERSION`) and a PublicAPI promotion gate
  that refuses to ship an unfrozen surface (unshipped public-surface lines with an empty
  `PublicAPI.Shipped.txt`). `-SkipPromotionCheck` keeps local verification usable before the
  surface is frozen; `-ChecksOnly` runs just the two static checks. `release.yml` runs
  `-ChecksOnly` before packing.

### Removed

- `StartProcessAsync` (all six members) — obsolete before first release; use `StartProcess`.
- `WithVolume(hostPath, containerPath, bool readOnly)` — use the `VolumeAccess` overload (`VolumeAccess.ReadOnly`).
- `WithReadOnlyVolume(hostPath, containerPath)` — use `WithVolume(hostPath, containerPath, VolumeAccess.ReadOnly)`.
- `WslModuleContainer.Inner` (C#) and the protected C++ `WslModuleContainer::Inner()` — module
  containers implement the full container interface, so the inner-container escape hatch is gone;
  use the container members directly or a core `WslContainer` when the wrapper adds nothing.
- Module parameterless `GetMappedPort()` — use `GetConnectEndpoint(DefaultPort).Port` (Redis also
  exposes `GetEndpoint()`).
- `IWslContainer.Host`, `IWslContainer.GetMappedPort(int)` and `IWslContainer.GetMappedHost(int)`
  (also on `WslContainer`/`WslModuleContainer`), and `IWaitTarget.Host`, `GetMappedPort`,
  `GetProbeHost`; the C++ `Host`/`GetMappedPort`/`GetMappedHost`/`GetProbeHost` equivalents; use
  `GetConnectEndpoint` and the returned endpoint's host/port.
- `ExecAsync(string, params string[])`, `ExecAsync(string, ExecOptions?, CancellationToken)` and
  `StartProcess(string, params string[])` (C#); the `ExecOptions`-taking C# `StartProcess` is
  replaced by the `ProcessOptions` one that runtime-rejects exec-only settings. The C++
  `StartProcess(..., ExecOptions, ...)` overload was removed; the new
  `StartProcess(..., const ProcessOptions&, ...)` overload serves all call forms and runtime-rejects
  an `ExecOptions` carrying `StandardInput`/`Timeout`.
