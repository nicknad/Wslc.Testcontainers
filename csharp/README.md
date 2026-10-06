# Wslc.Testcontainers

Ephemeral, isolated **WSL containers** for .NET integration tests — a Testcontainers-style API built
directly on the official [`Microsoft.WSL.Containers`](https://www.nuget.org/packages/Microsoft.WSL.Containers)
runtime. No Docker daemon required.

```csharp
using Wslc.Testcontainers.Modules.PostgreSql;

await using var postgres = new PostgreSqlBuilder()
    .WithPassword("secret")
    .Build();

await postgres.StartAsync();

var connectionString = postgres.GetConnectionString();
```

> Using the core `WslContainerBuilder` instead? WSLC never runs the image's ENTRYPOINT/CMD
> automatically: without `WithCommand(...)` only a keep-alive shell starts, so a readiness wait
> for the image's service always times out. Declare the service command explicitly or use a
> module builder.

> **Security: defaults are for *trusted* test dependencies, not hostile code.**
> Out of the box this library (and the underlying WSL container runtime) provides
> **no security boundary**: containers run bridged with full egress, processes run as
> the image default user (usually root) with no seccomp/capability/user-namespace
> controls, and every `WithVolume` is a write path onto the Windows host. Do **not**
> run untrusted or agent-generated code with default settings. If you need containment
> (e.g. an agent that may only talk to the API that invoked it), see
> [Agent containment](docs/usage.md#agent-containment-note). `WithNetworkingMode(Isolated)`
> is the boundary that works, while `WithScratchVolume` avoids exposing a Windows
> directory rather than avoiding Windows storage entirely.
>
> **Egress allowlisting is intentionally not provided.** On the current WSLC runtime
> (WSL 3.0.1) containers are not granted `CAP_NET_ADMIN` (the runtime seeds only a
> Docker-like default capability set), so an in-container `iptables` policy can never be
> installed, and the container settings exposed by the SDK contain no outbound policy
> API either. The only enforcement the runtime currently honors is removing the NIC
> entirely (`WithNetworkingMode(Isolated)`). A library cannot fix this from inside the
> container; it needs runtime-side egress policy support.

## Requirements

- Windows 10 2004+ or Windows 11 (x64 / ARM64)
- WSL **2.9.3 or newer** with container support: `wsl --install` (or `wsl --update`)
- .NET 8, 9 or 10 on Windows

## Install

```powershell
dotnet add package Wslc.Testcontainers
dotnet add package Wslc.Testcontainers.Modules.PostgreSql
dotnet add package Wslc.Testcontainers.Modules.Redis
dotnet add package Wslc.Testcontainers.Modules.Valkey
dotnet add package Wslc.Testcontainers.Modules.MariaDb
dotnet add package Wslc.Testcontainers.Modules.RabbitMq
dotnet add package Wslc.Testcontainers.Modules.MongoDb
dotnet add package Wslc.Testcontainers.Modules.Nats
dotnet add package Wslc.Testcontainers.Modules.MailPit
dotnet add package Wslc.Testcontainers.Modules.RustFs
dotnet add package Wslc.Testcontainers.Modules.Keycloak
```

Versions come from `Directory.Build.props` (`VersionPrefix`, see `CHANGELOG.md`).
For local development the `examples/` use `ProjectReference`; consumers use
the `PackageReference` lines above. To pack locally:

```powershell
dotnet pack src/Wslc.Testcontainers -c Release
```

## Core concepts

| Concept               | Description                                                                                   |
| --------------------- | --------------------------------------------------------------------------------------------- |
| `WslContainerBuilder` | Mutable, `With...`/`From...` builder. Every call mutates and returns the same builder; `Build()` snapshots. |
| `WslContainer`        | One disposable container in its own WSL session. Create with `Build()`, start with `StartAsync()`. |
| `Wait`                | Readiness strategies (`IWaitStrategy`) evaluated by `StartAsync()` before it completes.       |
| `IWslProcess`         | A long-running process started with `StartProcess` (must be disposed).                        |

Each `WslContainer` owns a dedicated WSL session with its own storage, so tests are isolated and can
run in parallel. Instances are named `wslc-{session}-{random}` and are destroyed by `DisposeAsync()`.

## Sources

```csharp
.WithImage("docker.io/library/redis:7")          // pull (cached in session storage)
.FromTarball(@"C:\images\rootfs.tar", "app:test") // import a root filesystem tarball
```

If `WSLC_DEFAULT_IMAGE` is set, it is used when no source is configured.

## Builder

| Method                                                            | Purpose                                                            |
| ----------------------------------------------------------------- | ------------------------------------------------------------------ |
| `WithImage(image)`                                                | Use a container image. Pulled on first use.                        |
| `FromTarball(path, imageName?)`                                   | Import a root filesystem tarball as an image.                      |
| `WithCommand(command, params args)`                               | Init process. Defaults to a keep-alive shell so `ExecAsync` works — the image ENTRYPOINT/CMD never runs automatically. |
| `WithWorkingDirectory(path)`                                      | Working directory for the init process and execs.                  |
| `WithEnvironment(name, value)` / `WithEnvironmentVariables(dict)` | Variables scoped to container processes.                           |
| `WithPort(containerPort)` / `WithPort(port, bindAddress)` / `WithPort(port, IPAddress)` | Exposes a Linux TCP port on a dynamic Windows port; the optional per-port Windows bind address defaults to loopback (pass `0.0.0.0` or `IPAddress.Any` to expose on the LAN). UDP is not supported — the WSLC runtime returns `E_NOTIMPL` for UDP mappings. |
| `WithNetworkingMode(mode)`                                      | `Bridged` (default) or `Isolated` (no NIC — no ports or waits allowed; the only containment mode).                     |
| `WithCpuCount(n)` / `WithMemoryMegabytes(n)`                    | Caps for the session VM.                                                                    |
| `WithScratchVolume(name, containerPath, sizeBytes, ...)`          | Scratch VHD volume (ext4, recreated empty every start) instead of a bind mount.             |
| `WithWaitStrategy(strategy)`                                      | Adds a readiness condition. All must pass.                         |
| `WithFile(hostPath, containerPath)`                               | Copies a Windows file (≤1 GiB) into the container. Absolute Linux dest. |
| `WithVolume(hostPath, containerPath)` / `WithVolume(..., VolumeAccess)` | Mounts a Windows directory. Order is host, container. |
| `WithReuse(true)`                                                 | Keeps session storage (and cached images) between runs (see Reuse). |
| `WithReadinessTimeout(timeout)`                                   | Readiness budget that bounds the whole startup (must be ≥ sum of wait timeouts). Default 120 s. |

## Working with the container

```csharp
using Wslc.Testcontainers;

// Execute and capture output
ExecResult result = await container.ExecAsync("ps", ["aux"]);
Console.WriteLine(result.ExitCode);

// Options: environment, working directory, stdin, timeout (pass null for defaults)
var result2 = await container.ExecAsync("psql", ["-c", "SELECT 1"], new ExecOptions
{
    Environment = new Dictionary<string, string> { ["PGPASSWORD"] = "secret" },
    Timeout = TimeSpan.FromSeconds(30),
}, CancellationToken.None);

// Long-running processes (ProcessOptions: working directory + environment only;
// an ExecOptions carrying StandardInput/Timeout is rejected with ArgumentException)
IWslProcess process = container.StartProcess("sleep", ["3600"]);
Console.WriteLine(process.Id);
await process.KillAsync();
await process.DisposeAsync();

// Output from the init process, ExecAsync and StartProcess all flows
// through container.SubscribeLogs() (infinite until cancelled — bound with CTS or LogDumper;
// container.GetRecentLogs() returns a bounded tail snapshot without blocking).

// Files (≤1 GiB each way; container paths must be absolute Linux paths)
await container.CopyToAsync(@".\fixtures\app.conf", "/etc/app/app.conf");
await container.CopyFromAsync("/var/log/app.log", @".\artifacts\app.log");

// Bounded tail snapshot (never blocks; returns the newest lines)
var recent = container.GetRecentLogs(50);

// Logs (infinite stream — always cancel; see docs/troubleshooting.md)
using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
await foreach (var line in container.SubscribeLogs(cts.Token))
{
    Console.WriteLine(line);
}
```

## Readiness

`StartAsync()` returns only after every configured strategy passes (or throws `WslReadinessException`).
On failure, call `ex.Describe()` and inspect `GetRecentLogs()` (bounded tail snapshot) — see [troubleshooting](docs/troubleshooting.md).

```csharp
using Wslc.Testcontainers.Waiting;

.WithWaitStrategy(Wait.ForWsl().UntilTcpPortIsOpen(5432))
.WithWaitStrategy(Wait.ForWsl().UntilHttpRequestSucceeds("/health", 8080))
.WithWaitStrategy(Wait.ForWsl().UntilProcessIsRunning("postgres"))
.WithWaitStrategy(Wait.ForWsl().UntilProcessExits("migration"))
.WithWaitStrategy(Wait.ForWsl().UntilMessageIsLogged("database system is ready"))
.WithWaitStrategy(Wait.ForWsl().UntilFileExists("/tmp/ready"))
```

Strategies support `WithTimeout(...)`, `WithRetryInterval(...)` and can be combined (the
composite keeps the left operand's timeout; nested composites are flattened):

```csharp
var strategy = Wait.ForWsl()
    .WithTimeout(TimeSpan.FromSeconds(60))
    .UntilTcpPortIsOpen(5432)
    .And(Wait.ForWsl().UntilMessageIsLogged("ready to accept connections"));
```

Mapped ports are dynamic (`WindowsPort = 0`): the WSL runtime assigns a free host port and WSLC
resolves it after start, so `GetConnectEndpoint(5432)` never collides between parallel tests.
Mappings are TCP-only: the WSLC runtime returns `E_NOTIMPL` for UDP mappings. The Windows side
binds loopback by default; pass a bind address (e.g. `"0.0.0.0"` or `IPAddress.Any`) to override.
TCP/HTTP readiness probes honor the configured bind address; `GetConnectEndpoint(port)` returns
the effective endpoint (wildcard bindings resolve to loopback: `127.0.0.1` for `0.0.0.0`, `::1`
for `::`).

## Lifecycle and cleanup

```csharp
using Wslc.Testcontainers;

await using var container = new WslContainerBuilder()
    .WithImage("docker.io/library/alpine:latest")
    .Build();

await container.StartAsync();
// tests...
// DisposeAsync stops processes, terminates the session and removes storage.
```

- `StopAsync()` stops processes and terminates the session but keeps storage (restart resets storage so `StartAsync` works again).
- `DisposeAsync()` also deletes ephemeral storage (reuse storage is preserved by design).
- Lifecycle calls are serialized; commands, copies and processes issued concurrently with
  `DisposeAsync` can fail with an exception instead of corrupting state.
- A process-exit hook performs best-effort cleanup if the test host crashes.
- At startup, `WslResourceReaper.CleanupAsync()` deletes ephemeral storage left behind by dead
  owners (PID-recycling safe, 7-day grace for corrupt metadata); `WithReuse(true)` storage is never reaped automatically — use `CleanupIncludingReuseAsync()` or `PurgeReuseAsync()`. WSLC only ever touches
  resources it created.

### Reuse

`WithReuse(true)` makes the instance name a hash of the builder configuration and keeps the session
storage between runs: the session VHD (and its pulled image cache) is reused, so the second run
skips the pull. Scratch volumes are still recreated empty on every start. Reuse is disabled under
CI unless `WSLC_REUSE_IN_CI=1`. See [reuse](docs/reuse.md) for when reuse is safe and how modules
encapsulate presets.

## Modules

Typed builders live in versioned module packages so tests stay declarative:

```csharp
using Wslc.Testcontainers.Modules.PostgreSql;
using Wslc.Testcontainers.Modules.Redis;
using Wslc.Testcontainers.Modules.Valkey;
using Wslc.Testcontainers.Modules.MariaDb;
using Wslc.Testcontainers.Modules.RabbitMq;
using Wslc.Testcontainers.Modules.MongoDb;
using Wslc.Testcontainers.Modules.Nats;
using Wslc.Testcontainers.Modules.MailPit;
using Wslc.Testcontainers.Modules.RustFs;
using Wslc.Testcontainers.Modules.Keycloak;

await using var postgres = new PostgreSqlBuilder().WithPassword("secret").Build();
await postgres.StartAsync();
var npgsql = postgres.GetConnectionString();

await using var redis = new RedisBuilder().Build();
await redis.StartAsync();
var endpoint = redis.GetEndpoint(); // host:port for StackExchange.Redis

await using var valkey = new ValkeyBuilder().Build();
await valkey.StartAsync();
var valkeyEndpoint = valkey.GetEndpoint(); // host:port, same protocol as Redis

await using var mariadb = new MariaDbBuilder().WithPassword("secret").Build();
await mariadb.StartAsync();
var mySql = mariadb.GetConnectionString(); // MySqlConnector format

await using var rabbitmq = new RabbitMqBuilder().Build();
await rabbitmq.StartAsync();
var amqp = rabbitmq.GetConnectionString(); // amqp://user:pass@host:port/

await using var mongodb = new MongoDbBuilder().Build();
await mongodb.StartAsync();
var mongo = mongodb.GetConnectionString(); // mongodb://host:port

await using var nats = new NatsBuilder().WithJetStream().Build();
await nats.StartAsync();
var natsUrl = nats.GetConnectionString(); // nats://host:port

await using var mailpit = new MailPitBuilder().Build();
await mailpit.StartAsync();
var smtp = mailpit.GetSmtpEndpoint();  // host:port
var mailUi = mailpit.GetHttpEndpoint(); // http://host:port

await using var rustfs = new RustFsBuilder().Build();
await rustfs.StartAsync();
var s3 = rustfs.GetEndpoint(); // http://host:port + AccessKey/SecretKey

await using var keycloak = new KeycloakBuilder().Build();
await keycloak.StartAsync();
var issuer = keycloak.GetEndpoint(); // http://host:port; credentials via AdminUsername/AdminPassword
```

See `examples/Postgres/` (console) and `examples/Postgres.Tests/` (shared xUnit
fixture with per-test reset).

## Configuration

| Environment variable  | Meaning                                                     |
| --------------------- | ----------------------------------------------------------- |
| `WSLC_TIMEOUT`        | Default wait timeout (seconds or `TimeSpan`). Default 60 s. |
| `WSLC_DATA_DIRECTORY` | Root for instance storage. Default `%LOCALAPPDATA%\Wslc`.   |
| `WSLC_DEFAULT_IMAGE`  | Image used when none is configured.                         |
| `WSLC_REUSE`          | `1`/`true` enables reuse by default.                        |
| `WSLC_CLEANUP`        | `0`/`false` disables automatic storage cleanup.             |
| `WSLC_REUSE_IN_CI`    | `1`/`true` allows reuse under CI.                           |
| `WSLC_SESSION_ID`     | Overrides the session identifier used in instance names.    |

Inside every container these variables are also available: `WSLC_SESSION_ID`, `WSLC_INSTANCE_ID`,
`WSLC_CREATED_AT`, `WSLC_OWNER_PID`.

## Limitations

- `CopyToAsync`/`CopyFromAsync` and `WithFile` support **single files**; use `WithVolume` for directories.
- Sources are container images; WSLC does not manage regular WSL distributions.
- Port mappings require bridged networking, which WSLC enables automatically.

## Development

Building and testing require the .NET 10 SDK (`global.json`), which also opts into the
Microsoft Testing Platform; test-app options are passed after `--`.

```powershell
dotnet build Wslc.Testcontainers.slnx
dotnet test --solution Wslc.Testcontainers.slnx

# Real-runtime tests (pull public images, require WSL container support)
$env:WSLC_RUN_INTEGRATION = "1"
dotnet test --solution Wslc.Testcontainers.slnx

# Subset of the unit suite
dotnet test --project tests/Wslc.Testcontainers.Tests -f net10.0-windows10.0.19041.0 `
  -- --filter "FullyQualifiedName~WslPlatform"
```

## License

[MIT](LICENSE)
