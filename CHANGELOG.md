# Changelog

All notable changes to the `Wslc.Testcontainers*` packages.

## Unreleased

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
