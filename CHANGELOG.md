# Changelog

All notable changes to the `Wslc.Testcontainers*` packages.

## Unreleased

### Fixed

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

### Changed

- Tests and examples now use xUnit.net v3 (`xunit.v3` 4.0.1) with the Microsoft Testing Platform: `global.json` opts into MTP, commands use `dotnet test --solution ...`, and test-app options are passed after `--`.
- Lifecycle transitions on `WslContainer` are serialized; commands, copies and processes issued concurrently with `DisposeAsync` may fail with an exception rather than corrupting state.

### Removed

- Public API (unshipped preview): `WslResourceReaper.CleanupAllAsync()` (the reaper never deletes reusable instances), `WslPlatform.MinimumWindowsVersion`, `LogDumper.Dump(IReadOnlyList<LogLine>, ...)`, `IWslProcess.Stdout`/`Stderr`/`LogsAsync`, and `IWslContainer.Stdout`/`Stderr`. Use `container.LogsAsync()` and `ExecResult` for container and process output.

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
