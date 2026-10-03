# Changelog

All notable changes to the `Wslc.Testcontainers*` packages.

## Unreleased

### Added

- `Wait.ForWsl().Until(name, condition)`: poll a custom readiness delegate with the builder's timeout/retry settings.
- `IWslContainer.GetRecentLogs(int maxLines = 50)` (also on `WslContainer` and module containers): bounded newest-lines snapshot for failure triage without enumerating the infinite `LogsAsync` stream.
- `IWslContainer.GetMappedHost(int containerPort)`: effective connect address for a mapped port (the configured bind address; loopback for wildcard/default bindings).
- `ExecAsync(string command, ExecOptions?, CancellationToken)` overload: options (and cancellation) without the `Array.Empty<string>()` argument noise.
- Readiness failures and startup timeouts now hint when no init command was configured: WSLC never runs the image's ENTRYPOINT/CMD automatically.
- XML docs for the module packages (`PostgreSqlBuilder`/`PostgreSqlContainer`/`RedisBuilder`/`RedisContainer`).

### Changed

- `And(...)` composites now consistently keep the left operand's timeout/retry interval; the non-composite + non-composite case previously fell back to the default timeout.
- `CS1591` is no longer globally suppressed: missing XML docs on public members fail the strict build.
- `WslModuleContainer` now implements `IWslContainer`, so module containers are substitutable wherever the interface is expected.
- Pre-start errors are unified on `WslcException` (previously `WslNetworkException` for `GetMappedPort` and `InvalidOperationException` for exec/copy/start-process), so `catch (WslcException)` is complete.
- `WslReadinessException` is immutable; the container builds the fully populated instance before throwing instead of mutating properties after creation.
- Reuse identity (`Name`) covers configuration metadata only: `WithFile` source contents are no longer hashed (they are copied on every start), so `Name` performs no file I/O.
- Builders (`WslContainerBuilder`, module builders) are now mutable: every `With...` mutates and returns the same builder; `Build()` snapshots the configuration. Supersedes ADR-0003 (see ADR-0007).
- `WslContainerBuilder.FromImage` renamed `WithImage` (matches module builders and Testcontainers).
- `WslModuleBuilder<TBuilder>.WithStartupTimeout` renamed `WithWaitTimeout`: the value is the per-wait timeout; the startup budget is derived as `2*t+30s`.
- README quickstart now uses `PostgreSqlBuilder`; docs call out that the image ENTRYPOINT/CMD is never started automatically.

### Removed

- `StartProcessAsync` (all six members) — obsolete before first release; use `StartProcess`.
- `WithVolume(hostPath, containerPath, bool readOnly)` — use the `VolumeAccess` overload or `WithReadOnlyVolume`.

## 0.1.0-preview.1

First public preview of `Wslc.Testcontainers` (+ `Modules.PostgreSql`, `Modules.Redis`).

### Added

- Testcontainers-style API on `Microsoft.WSL.Containers`: `WslContainerBuilder` / `WslContainer`, `ExecAsync`, `StartProcessAsync`, `CopyTo/FromAsync`, `LogsAsync`, port mapping, file/volume mounts, reuse + orphan reaper.
- Readiness strategies via `Wait.ForWsl()` (TCP, HTTP, process, log message, file) with timeout/retry/composite support and `WslReadinessException.Describe()` diagnostics.
- Typed modules: `PostgreSqlBuilder` (+ `GetConnectionString`) and `RedisBuilder`.
- `WslPlatform` guard: `Build()`, `StartAsync()` and host init throw `PlatformNotSupportedException` on non-Windows (or unsupported arch) with remediation, instead of obscure loader/COM errors.
- WSL version gate: startup requires WSL 2.9.3+ (`wsl --update`) with a clear `WslRuntimeException`.
- Resource caps: `WslContainerBuilder.WithCpuCount(uint)` and `WithMemoryMB(uint)` bound the session VM (`SessionSettings.CpuCount`/`MemorySizeInMB`) and participate in the reuse hash.
- Network isolation: `WithNetworkingMode(Bridged | None)`. `None` removes the NIC entirely (no ports or TCP/HTTP waits — rejected at `Build()`); offline containers are driven via `Exec`/`Copy`/`Logs`.
- Port control: `WithPort(port)` and `WithPort(port, bindAddress)` (bind defaults to loopback; pass `"0.0.0.0"` to expose on the LAN); resolve dynamic ports with `GetMappedPort(port)`. Mappings are TCP-only: the WSLC runtime returns `E_NOTIMPL` for UDP, so the protocol-taking overloads and `WithUdpPort` are not shipped rather than exposing methods that can never succeed.
- Session VHD volumes: `WithSessionVolume(name, containerPath, sizeBytes, access, type)` provisions native ext4 scratch inside the session VM (recreated empty every start, never exposed as Windows host files) instead of a Windows bind mount.
- `docs/usage.md`: testing guide (builder, waits, ports, volumes, xUnit fixture patterns) with networking-modes/resources sections and an agent-containment checklist.
- `WslPlatformTests`: cross-platform guard + version-gate coverage; unit suite runs on net8/9/10-windows.
- CI: Windows matrix (net8/9/10), `windows-11-arm` unit job, `ubuntu-latest` early-fail guard job, nightly + reusable real-WSL integration (incl. Postgres module tests).
- Packaging: centralized license/authors/README/tags, deterministic + `snupkg` symbols, tag-driven versions (`v*` → `/p:Version=`), `release.yml` with local-feed smoke test, NuGet trusted publishing and GitHub Release.
- `scripts/Verify-Package.ps1`: local pre-release gate (build, all-TFM units, pack, nupkg + smoke install, optional `-RunIntegration`).

### Changed

- The public API no longer exposes `Microsoft.WSL.Containers` enum types: `WithNetworkingMode` takes the package-owned `ContainerNetworkMode` and `WithSessionVolume` takes `VhdAllocationType`, mapped internally to the SDK. Port APIs are TCP-only and take no protocol parameter.
- `WithNamedVolume` was renamed `WithSessionVolume`: the volume is scratch space recreated empty on every start, not a Docker-style persistent named volume.
- `IWaitTarget` gained `GetProbeHost(int)` so probes can target the port's bind address; external implementations must add it (package is still `0.1.0-preview`).
- Tests and examples now use xUnit.net v3 (`xunit.v3` 4.0.1) with the Microsoft Testing Platform: `global.json` opts into MTP, commands use `dotnet test --solution ...`, and test-app options are passed after `--`.
- Lifecycle transitions on `WslContainer` are serialized; commands, copies and processes issued concurrently with `DisposeAsync` may fail with an exception rather than corrupting state.
- Test and example projects target net8/9/10-windows (tests) and are no longer locked to `win-x64`, enabling ARM64 runs.

### Fixed

- Postgres module now waits for the second "database system is ready to accept connections" log line. The image entrypoint starts a temporary server for initialization, logs readiness, then restarts; accepting the first occurrence let tests connect and get dropped mid-initialization (`UntilMessageIsLogged(message, occurrences)` added).
- Real-runtime example tests (`examples/Postgres.Tests`) now skip unless `WSLC_RUN_INTEGRATION=1`, matching the documented opt-in and the CI integration job; previously `dotnet test --solution` pulled and started Postgres unconditionally.
- Reuse now keeps the session VHD (`storage.vhdx`) between runs, so pulled images stay cached and the second run skips the pull; the storage directory was previously deleted on every start, silently defeating the cache. Named session volumes are still recreated empty on every start.
- Log capture no longer splits lines quadratically: `LineAssembler` resumes scanning and compacts once per chunk instead of removing each line from a `StringBuilder`, and capture buffers are fixed-size rings that drop the oldest bytes in place.
- Log snaphots are cached until the next publish, so readiness polls (`GetRecentLogs`) no longer allocate a full history copy every retry; subscriber channels are bounded (1k, DropOldest).
- Long-running child processes are registered before they start and exited entries are pruned, so a concurrent `StopAsync`/`DisposeAsync` can no longer miss and leak one.
- Lifecycle state is published safely; `DisposeAsync` always completes log streams, including when the process-exit hook already ran, and no longer races queued lifecycle callers with a disposed semaphore.
- Container inspect host ports are parsed by JSON kind (string or number) with invariant culture instead of throwing on numeric values.
- Reuse identity is hashed with a length-prefixed binary encoding; the previous delimiter-based text encoding was not injective (values containing newline/`\u001f` could collide and reuse the wrong instance). Existing reuse cache names change once.
- Metadata writes use a unique temp file under a lock, so state transitions racing cleanup cannot clobber each other or leave `.tmp` litter.
- Cached image matching canonicalizes references (implicit `docker.io`/`library`/`latest`), removing cross-registry false positives like `evil/team/postgres:15` matching `team/postgres:15`.
- `CleanupIncludingReuseAsync` skips reusable instances another live process holds via `wslc.lock`; owner-liveness probes treat access-denied (`Win32Exception`) as alive.
- Cleanup paths abort processes after a 2s grace period instead of stalling up to 10s per child; HTTP wait paths that cannot form a valid URI fail the check instead of throwing.
- Constructing a reuse container defers configuration hashing (and `WithFile` content hashing) until the name is needed, and one metadata write per start was removed.
- TCP and HTTP readiness probes now honor the per-port bind address (`IWaitTarget.GetProbeHost`, wildcard bindings probe loopback), so `WithPort(port, bindAddress)` no longer times out when combined with network wait strategies.
- Modules now start their server: `PostgreSqlBuilder` / `RedisBuilder` launch the image entrypoint (`docker-entrypoint.sh postgres` / `redis-server`) as the container init process. Previously the keep-alive shell was always used, so no server ever ran and startup timed out.
- `WslModuleBuilder.Configure` returns the updated `WslContainerBuilder` and `BuildContainer` uses it. Previously the result was discarded, silently dropping module settings (`POSTGRES_USER/PASSWORD/DB` never reached the container).
- `scripts/Verify-Package.ps1` now fails on any `dotnet`/`wsl` non-zero exit (was silently reporting success when integration tests failed).
