# Troubleshooting

## Preflight (run before first test)

```powershell
wsl --status
wsl --version
# Need WSL 2.9.3+ with container support. If missing:
wsl --install
# or
wsl --update
```

In code, `StartAsync()` calls `WslcService.GetMissingComponents()` first.
If it throws `WslRuntimeException`, install/update WSL and retry — don't
debug ports or waits until the runtime is present.

## Enable integration tests

Unit tests run everywhere. Real-runtime tests are opt-in:

```powershell
$env:WSLC_RUN_INTEGRATION = "1"
dotnet test --solution Wslc.Testcontainers.slnx

# Or run only the real-runtime tests
dotnet test --project tests/Wslc.Testcontainers.Tests -- --filter "FullyQualifiedName~Integration"
```

Without the variable, `[IntegrationFact]` tests skip with the reason printed.
Test-app options go after `--` under the Microsoft Testing Platform.

## Readiness timeout triage

`StartAsync()` throws `WslReadinessException` when a wait strategy never
passes. Triage in order:

1. Print the full report — it already includes image, command, expected
   condition, timeout, exit code, and last stdout/stderr:

```csharp
try
{
    await container.StartAsync();
}
catch (WslReadinessException ex)
{
    output.WriteLine(ex.Describe());
    throw;
}
```

2. Dump the container logs. `LogsAsync()` replays the bounded history (up to
   the last 10,000 lines) and then streams live; the step 1 report already
   includes the last 50 stdout/stderr lines:

```csharp
await foreach (var line in container.LogsAsync(testCancellationToken))
{
    output.WriteLine(line.ToString());
}
```

Or use the one-liner helper (no xUnit dependency):

```csharp
using Wslc.Testcontainers.Testing;

await LogDumper.DumpAsync(container.LogsAsync(ct), output.WriteLine, maxLines: 100, ct);
```

3. Check the usual causes:
   - Wrong port: `GetMappedPort(n)` throws `WslNetworkException` when `n`
     wasn't declared with `WithPort(n)` or the runtime hasn't assigned it yet.
     Always `WithPort()` every port you probe or map.
   - Wait too strict: `UntilMessageIsLogged` is ordinal substring, case-sensitive and ignores
     `LogSource.System` diagnostics (no regex). Copy the exact container log line.
   - HTTP path: `UntilHttpRequestIsSucceeded(pathAndQuery, port)` requires an absolute
     path like `/health`, not a full URL.
   - File path: `UntilFileExists`/`WithFile`/`WithVolume`/`Copy*` require absolute Linux
     paths starting with `/`; swapped host/container order fails fast.
   - Two timeouts: `WithStartupTimeout` bounds the whole `StartAsync` and must be ≥ sum of
     wait timeouts (validated at `Build()`); each strategy has its own `WithTimeout`/`WithRetryInterval`.
     A 5s strategy inside a 120s startup still fails at 5s. Modules derive startup as `2*timeout+30s`.
   - Image pull: first start pulls the image into session storage. Re-run
     once before blaming the wait.

## Ports

- Mapped ports are dynamic. Never hardcode the host port — always
  `$"Host={container.Host};Port={container.GetMappedPort(5432)}"`.
- `GetMappedPort()` before `StartAsync()` throws by design.
- Parallel tests are isolated (one WSL session per container), so distinct
  `GetMappedPort()` values across tests are expected.

## Storage and orphans

- Ephemeral storage lives under `%LOCALAPPDATA%\Wslc` (override with
  `WSLC_DATA_DIRECTORY`). `DisposeAsync()` deletes it; `StopAsync()` keeps it. Restart reuses
  the session VHD for `WithReuse(true)` instances (images stay cached) and starts ephemeral
  instances with clean storage; session volumes are recreated empty either way.
- Crashed test hosts leave storage behind. Next `StartAsync()` runs
  `WslResourceReaper.CleanupAsync()` for dead owners automatically (7-day grace for corrupt metadata).
- Reusable instances (`WithReuse(true)`) are never reaped automatically, even
  when their owner is gone. Reclaim with `PurgeReuseAsync()` / `CleanupIncludingReuseAsync()` or delete their `wslc-reuse-*` directory manually.
- Copies are capped at 1 GiB each way (`WithFile`/`CopyTo`/`CopyFrom`); logs keep 10k lines history + 1k per subscriber (DropOldest) + 1 MiB per capture stream.
- Manual cleanup: call `WslResourceReaper.CleanupAsync()` once at startup,
  or delete the instance directory for a printed container name.
- `WithReuse(true)` keeps storage between runs (images stay cached). Only
  use it for stateless images or throwaway DBs you truncate per test —
  otherwise data leaks between tests. Reuse is disabled under CI unless
  `WSLC_REUSE_IN_CI=1`.

## Still stuck?

Include in the report: `ex.Describe()` output, last 100 log lines,
`wsl --version` output, and the builder chain (image, ports, waits).
