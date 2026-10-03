# Changelog

All notable changes to the `Wslc.Testcontainers*` packages.

## Unreleased

### Added

- Resource caps: `WslContainerBuilder.WithCpuCount(uint)` and `WithMemoryMB(uint)` bound the session VM (`SessionSettings.CpuCount`/`MemorySizeInMB`) and participate in the reuse hash.
- Network isolation: `WithNetworkingMode(Bridged | None)`. `None` removes the NIC entirely (no ports or TCP/HTTP waits — rejected at `Build()`); offline containers are driven via `Exec`/`Copy`/`Logs`.
- Port control: `WithPort(port)` and `WithPort(port, bindAddress)` (bind defaults to loopback; pass `"0.0.0.0"` to expose on the LAN); resolve dynamic ports with `GetMappedPort(port)`. Mappings are TCP-only: the WSLC runtime returns `E_NOTIMPL` for UDP, so the protocol-taking overloads and `WithUdpPort` are not shipped rather than exposing methods that can never succeed.
- Session VHD volumes: `WithSessionVolume(name, containerPath, sizeBytes, access, type)` provisions native ext4 scratch inside the session VM (recreated empty every start, never exposed as Windows host files) instead of a Windows bind mount.
- `docs/usage.md`: testing guide (builder, waits, ports, volumes, xUnit fixture patterns) with networking-modes/resources sections and an agent-containment checklist.

### Changed

- The public API no longer exposes `Microsoft.WSL.Containers` enum types: `WithNetworkingMode` takes the package-owned `ContainerNetworkMode` and `WithSessionVolume` takes `VhdAllocationType`, mapped internally to the SDK. Port APIs are TCP-only and take no protocol parameter.
- `WithNamedVolume` was renamed `WithSessionVolume`: the volume is scratch space recreated empty on every start, not a Docker-style persistent named volume.
- `IWaitTarget` gained `GetProbeHost(int)` so probes can target the port's bind address; external implementations must add it (package is still `0.1.0-preview`).
- Tests and examples now use xUnit.net v3 (`xunit.v3` 4.0.1) with the Microsoft Testing Platform: `global.json` opts into MTP, commands use `dotnet test --solution ...`, and test-app options are passed after `--`.
- Lifecycle transitions on `WslContainer` are serialized; commands, copies and processes issued concurrently with `DisposeAsync` may fail with an exception rather than corrupting state.

### Removed

- In-container egress allowlisting (`WithEgressAllowlist`, `IWslContainer.ApplyEgressAllowlistAsync`, `EgressAllowlistOptions`). On WSL 3.0.1, containers are not granted `CAP_NET_ADMIN` (the SDK's container `Privileged` setting does not change the capability set), so an `iptables` policy could never be installed from inside the container, and the SDK's container settings expose no outbound policy API. Shipping the API would promise enforcement it cannot deliver; use `WithNetworkingMode(None)` for containment. See the README security note and `docs/usage.md#egress-policy-not-offered`.
- The `PortProtocol` enum and the protocol-taking `WithPort`/`GetMappedPort` overloads: only `Tcp` was ever accepted (UDP returns `E_NOTIMPL`), so the protocol parameter was dead surface. UDP can be re-added additively if the runtime implements it.
- The removals above also change the reuse configuration hash for every configuration (the old encoding wrote the port protocol for each mapping and a trailing egress marker), so instances cached by earlier preview builds are no longer matched and are left to the reaper. This is a one-time, expected consequence of dropping the fields.
- Public API (unshipped preview): `WslResourceReaper.CleanupAllAsync()` (the reaper never deletes reusable instances), `WslPlatform.MinimumWindowsVersion`, `LogDumper.Dump(IReadOnlyList<LogLine>, ...)`, `IWslProcess.Stdout`/`Stderr`/`LogsAsync`, and `IWslContainer.Stdout`/`Stderr`. Use `container.LogsAsync()` and `ExecResult` for container and process output.

### Fixed

- Postgres module now waits for the second "database system is ready to accept connections" log line. The image entrypoint starts a temporary server for initialization, logs readiness, then restarts; accepting the first occurrence let tests connect and get dropped mid-initialization (`UntilMessageIsLogged(message, occurrences)` added).
- Real-runtime example tests (`examples/Postgres.Tests`) now skip unless `WSLC_RUN_INTEGRATION=1`, matching the documented opt-in and the CI integration job; previously `dotnet test --solution` pulled and started Postgres unconditionally.
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

## 0.1.0-preview.1

First public preview of `Wslc.Testcontainers` (+ `Modules.PostgreSql`, `Modules.Redis`).

### Added

- Testcontainers-style API on `Microsoft.WSL.Containers`: `WslContainerBuilder` / `WslContainer`, `ExecAsync`, `StartProcessAsync`, `CopyTo/FromAsync`, `LogsAsync`, port mapping, file/volume mounts, reuse + orphan reaper.
- Readiness strategies via `Wait.ForWsl()` (TCP, HTTP, process, log message, file) with timeout/retry/composite support and `WslReadinessException.Describe()` diagnostics.
- Typed modules: `PostgreSqlBuilder` (+ `GetConnectionString`) and `RedisBuilder`.
- `WslPlatform` guard: `Build()`, `StartAsync()` and host init throw `PlatformNotSupportedException` on non-Windows (or unsupported arch) with remediation, instead of obscure loader/COM errors.
- WSL version gate: startup requires WSL 2.9.3+ (`wsl --update`) with a clear `WslRuntimeException`.
- `WslPlatformTests`: cross-platform guard + version-gate coverage; unit suite runs on net8/9/10-windows.
- CI: Windows matrix (net8/9/10), `windows-11-arm` unit job, `ubuntu-latest` early-fail guard job, nightly + reusable real-WSL integration (incl. Postgres module tests).
- Packaging: centralized license/authors/README/tags, deterministic + `snupkg` symbols, tag-driven versions (`v*` → `/p:Version=`), `release.yml` with local-feed smoke test, NuGet trusted publishing and GitHub Release.
- `scripts/Verify-Package.ps1`: local pre-release gate (build, all-TFM units, pack, nupkg + smoke install, optional `-RunIntegration`).

### Fixed

- Modules now start their server: `PostgreSqlBuilder` / `RedisBuilder` launch the image entrypoint (`docker-entrypoint.sh postgres` / `redis-server`) as the container init process. Previously the keep-alive shell was always used, so no server ever ran and startup timed out.
- `WslModuleBuilder.Configure` returns the updated `WslContainerBuilder` and `BuildContainer` uses it. Previously the result was discarded, silently dropping module settings (`POSTGRES_USER/PASSWORD/DB` never reached the container).
- `scripts/Verify-Package.ps1` now fails on any `dotnet`/`wsl` non-zero exit (was silently reporting success when integration tests failed).

### Changed

- Test and example projects target net8/9/10-windows (tests) and are no longer locked to `win-x64`, enabling ARM64 runs.
