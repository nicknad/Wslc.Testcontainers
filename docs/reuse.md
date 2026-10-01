# Reuse and test isolation

`WithReuse(true)` keeps session storage between runs and names the instance
from the configuration hash (`wslc-reuse-<12 hex>`). Images stay cached, so
the second run skips the pull. Ephemeral instances use `wslc-{session}-{random}`
and are deleted by `DisposeAsync()`.

## When to reuse

- Safe: stateless images (alpine tools, HTTP stubs), or throwaway DBs you
  fully reset per test (`DROP TABLE` / `TRUNCATE` in a fixture, see
  `examples/Postgres.Tests/PostgresFixture.cs`).
- Unsafe: any DB whose schema or rows persist across tests without a reset.
  Shared Postgres without `ResetAsync()` leaks customers from one test to
  the next — the example assertion `Equal(2, ...)` would flake.
- CI: reuse is disabled under CI unless `WSLC_REUSE_IN_CI=1`. Keep it off in
  CI for hermetic runs; use it locally for inner-loop speed.
- Parallel processes: a reuse instance is guarded by a `wslc.lock` file in
  its storage directory. A second process waits up to 30 s for the owner to
  release it before failing startup.

## Modules are the preset

Don't memorize waits per image. Each module encapsulates its own readiness:

- `Wslc.Testcontainers.Modules.PostgreSql`: TCP 5432 + log
  `database system is ready to accept connections`, `GetConnectionString()`
  for Npgsql.
- `Wslc.Testcontainers.Modules.Redis`: TCP 6379 + log
  `Ready to accept connections`, `GetConnectionString()` as `host:port`.

Prefer a new module over `WithPostgresDefaults()` extensions on the core
builder: the core stays generic, presets stay versioned with their image.

## Cleanup

- `StopAsync()` stops processes but keeps storage (restart clears session storage so the runtime starts empty); `DisposeAsync()` deletes
  ephemeral storage. The process-exit hook does best-effort cleanup on crash.
- Orphans from dead owners are reaped on next `StartAsync()` (PID-recycling safe). To force it:
  `await WslResourceReaper.CleanupAsync()`. Corrupt/missing metadata is deleted only after 7 days.
- Reusable instances are preserved by `DisposeAsync()` and by the reaper, even
  when their owner is gone. Reclaim with `await WslResourceReaper.PurgeReuseAsync()` (force, all reuse)
  or `await WslResourceReaper.CleanupIncludingReuseAsync()` (dead owners only; skips instances a
  running process still holds), or delete the printed `wslc-reuse-*` directory to force a clean
  slate (the next start re-pulls the image).
- Storage root: `%LOCALAPPDATA%\Wslc` (`WSLC_DATA_DIRECTORY` overrides).
  Delete an instance directory only for the printed container name.
