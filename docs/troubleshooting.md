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
```

Without the variable, `[IntegrationFact]` tests skip with the reason printed.

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

2. Dump recent logs (bounded to the last 50 lines per stream by the library):

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
   - Wait too strict: `UntilMessageIsLogged` is case-sensitive and ignores
     `LogSource.System` diagnostics. Copy the exact container log line.
   - Two timeouts: `WithStartupTimeout` bounds the whole `StartAsync`;
     each strategy has its own `WithTimeout`/`WithRetryInterval`.
     A 5s strategy inside a 120s startup still fails at 5s.
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
  `WSLC_DATA_DIRECTORY`). `DisposeAsync()` deletes it; `StopAsync()` keeps it.
- Crashed test hosts leave storage behind. Next `StartAsync()` runs
  `WslResourceReaper.CleanupAsync()` for dead owners automatically.
- Manual cleanup: call `WslResourceReaper.CleanupAsync()` once at startup,
  or delete the instance directory for a printed container name.
- `WithReuse(true)` keeps storage between runs (images stay cached). Only
  use it for stateless images or throwaway DBs you truncate per test —
  otherwise data leaks between tests. Reuse is disabled under CI unless
  `WSLC_REUSE_IN_CI=1`.

## Still stuck?

Include in the report: `ex.Describe()` output, last 100 log lines,
`wsl --version` output, and the builder chain (image, ports, waits).
