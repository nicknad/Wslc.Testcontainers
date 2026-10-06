# Reuse and test isolation

`WithReuse(true)` keeps the session storage VHD between runs and names the
instance from the configuration hash (`wslc-reuse-<12 hex>`). Pulled images
stay cached in that VHD, so the second run skips the pull. Scratch volumes are
still recreated empty on every start. Ephemeral instances use
`wslc-{session}-{random}` and are deleted by `DisposeAsync()`.

The reuse name covers configuration metadata (paths and settings) only, not
`WithFile` source contents — those are copied into the container on every
start, and hashing them would put file I/O behind the public `Name` property.

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

## Is reuse actually active?

Configured reuse can be suppressed: reuse is forced off under CI unless
`WSLC_REUSE_IN_CI` is truthy (see the detection rules in
`WslEnvironment.IsContinuousIntegrationVariable`). Check the resolved policy
after building the container instead of inferring it from `WithReuse`:

- C#: `IWslContainer.IsReuseEffective` (also on `WslContainer` and module
  containers) is `true` only when reuse is configured (or `WSLC_REUSE` is
  truthy) and the environment allows it.
- C++: `WslContainer::IsReuseEffective()` / `WslModuleContainer::IsReuseEffective()`.

The property is cheap state (no I/O); it does not start the container, so it
can be asserted in tests that want to skip when CI silently disabled reuse.

## Modules are the preset

Don't memorize waits per image. Each module encapsulates its own readiness:

- `Wslc.Testcontainers.Modules.PostgreSql`: TCP 5432 + log
  `database system is ready to accept connections`, `GetConnectionString()`
  for Npgsql.
- `Wslc.Testcontainers.Modules.Redis`: TCP 6379 + log
  `Ready to accept connections`, `GetEndpoint()` as `host:port`.
- `Wslc.Testcontainers.Modules.Valkey`: TCP 6379 + log
  `Ready to accept connections`, `GetEndpoint()` as `host:port` (Redis-compatible).
- `Wslc.Testcontainers.Modules.MariaDb`: TCP 3306 + log `ready for connections`,
  `GetConnectionString()` for MySqlConnector.
- `Wslc.Testcontainers.Modules.RabbitMq`: TCP 5672 + log `Server startup complete`,
  `GetConnectionString()` as an `amqp://` URI.
- `Wslc.Testcontainers.Modules.MongoDb`: TCP 27017 + log `Waiting for connections`
  (twice when root credentials are configured), `GetConnectionString()` for the driver.
- `Wslc.Testcontainers.Modules.Nats`: TCP 4222 + log `Server is ready`, `GetConnectionString()`
  as `nats://`; `WithJetStream()` opts into persistence.
- `Wslc.Testcontainers.Modules.MailPit`: SMTP 1025 + HTTP 8025, readiness via `/livez`,
  `GetSmtpEndpoint()` / `GetHttpEndpoint()`.
- `Wslc.Testcontainers.Modules.RustFs`: S3 API 9000 + console 9001, readiness via `/health`,
  `GetEndpoint()` plus `AccessKey`/`SecretKey`.
- `Wslc.Testcontainers.Modules.Vault`: HTTP 8200 in dev mode, readiness via `/v1/sys/health`,
  `GetAddress()` plus `RootToken`.

Prefer a new module over `WithPostgresDefaults()` extensions on the core
builder: the core stays generic, presets stay versioned with their image.

## Cleanup

- `StopAsync()` stops processes but keeps storage. Restarting reuses the session VHD for
  reuse instances (image cache intact) and starts ephemeral instances with clean storage;
  scratch volumes are recreated empty either way. `DisposeAsync()` deletes ephemeral
  storage (reuse storage is preserved). The process-exit hook does best-effort cleanup on crash.
- Orphans from dead owners are reaped on next `StartAsync()` (PID-recycling safe). To force it:
  `await WslResourceReaper.CleanupAsync()`. Corrupt/missing metadata is deleted only after 7 days.
- Reusable instances are preserved by `DisposeAsync()` and by the reaper, even
  when their owner is gone. Reclaim with `await WslResourceReaper.PurgeReuseAsync()` (force, all reuse)
  or `await WslResourceReaper.CleanupIncludingReuseAsync()` (dead owners only; skips instances a
  running process still holds), or delete the printed `wslc-reuse-*` directory to force a clean
  slate (the next start re-pulls the image).
- Storage root: `%LOCALAPPDATA%\Wslc` (`WSLC_DATA_DIRECTORY` overrides).
  Delete an instance directory only for the printed container name.
