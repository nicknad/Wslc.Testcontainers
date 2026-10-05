# Usage — containers for testing

This guide shows how to use `Wslc.Testcontainers` for .NET integration tests.
It covers the core container, the Postgres/Redis modules, and the xUnit patterns
used in `examples/`. For failure triage see `troubleshooting.md`; for caching
semantics see `reuse.md`.

> Scope: hermetic **test dependencies** (databases, HTTP stubs, CLI tools).
> Running **untrusted agent code** inside a container is a different threat model —
> see [Agent containment](#agent-containment-note) at the end.

## Security model (read this first)

The defaults in this guide are for **trusted testcontainers only**. Neither this
library nor the underlying WSL container runtime offers an out-of-the-box security
boundary against hostile code running inside a container:

- **Full egress by default.** Bridged containers can reach the internet, the LAN and
  the Windows host. The runtime exposes no outbound filter, and the library cannot
  install one from inside the container (no `CAP_NET_ADMIN`), so the only enforced
  choice is the NIC-less `WithNetworkingMode(Isolated)`.
- **Root by default, no syscall filtering.** Processes run as the image default user
  (usually root). WSLC exposes no seccomp profiles, capability drops, user namespaces
  or read-only rootfs.
- **Mounts are host write paths.** Every writable `WithVolume` lets the container
  create/modify/delete files on the Windows host. The container cannot see host files
  it was not explicitly given — but anything mounted writable is fully exposed.
- **No image trust.** Images are pulled without signature verification; pin digests
  and prefer minimal images for anything sensitive.
- **Shared-host resources.** Without `WithCpuCount`/`WithMemoryMegabytes` a container can
  consume unbounded CPU/memory.

Per-container WSL sessions do isolate test dependencies from *each other* (own VM,
own storage VHD, own network namespace), which is what makes parallel trusted tests
safe. That is **test isolation, not a hostile-code sandbox**. To contain untrusted or
agent-generated code so it can only interact with the API that invoked it, you must
opt into the hardening primitives (`WithNetworkingMode(Isolated)`, `WithScratchVolume`,
loopback bind addresses, resource caps, short-lived tokens) —
see [Agent containment](#agent-containment-note). Egress filtering is not offered:
see [Egress policy](#egress-policy-not-offered).

## Requirements

- Windows 10 build 19041+ (x64/ARM64), WSL **2.9.3+** with container support.
  Verify with `wsl --status` / `wsl --version`; in code `StartAsync()` fails fast
  with `WslRuntimeException` when components are missing.
- .NET 8, 9 or 10 on Windows. Building the repo needs the .NET 10 SDK
  (`global.json`); test options are passed after `--` (Microsoft Testing Platform).

```powershell
dotnet build Wslc.Testcontainers.slnx
dotnet test --solution Wslc.Testcontainers.slnx

# Real-runtime tests (pull public images, require WSL container support)
$env:WSLC_RUN_INTEGRATION = "1"
dotnet test --solution Wslc.Testcontainers.slnx
```

Install the packages you need:

```powershell
dotnet add package Wslc.Testcontainers
dotnet add package Wslc.Testcontainers.Modules.PostgreSql
dotnet add package Wslc.Testcontainers.Modules.Redis
```

## Quickstart

Minimal round-trip (`examples/Quickstart/Program.cs`):

```csharp
using Wslc.Testcontainers;

await using var container = new WslContainerBuilder()
    .WithImage("docker.io/library/alpine:latest")
    .WithCommand("/bin/sh", "-c", "while true; do sleep 3600; done")
    .WithEnvironment("HELLO", "wslc")
    .WithReadinessTimeout(TimeSpan.FromMinutes(2))
    .Build();

await container.StartAsync();

var whoami = await container.ExecAsync(
    "sh", ["-c", "echo $HELLO && uname -a"], cancellationToken: CancellationToken.None);
whoami.EnsureSuccess();
Console.WriteLine($"exit={whoami.ExitCode} stdout={whoami.Stdout.Trim()}");
```

Postgres with the typed module (declarative, no manual waits):

```csharp
using Wslc.Testcontainers.Modules.PostgreSql;

await using var postgres = new PostgreSqlBuilder().WithPassword("secret").Build();
await postgres.StartAsync();
var connectionString = postgres.GetConnectionString(); // Host=127.0.0.1;Port=<dynamic>;...
```

Redis:

```csharp
using Wslc.Testcontainers.Modules.Redis;

await using var redis = new RedisBuilder().Build();
await redis.StartAsync();
var endpoint = redis.GetEndpoint(); // host:port for StackExchange.Redis
```

## Core concepts

| Concept | Description |
| --- | --- |
| `WslContainerBuilder` | Mutable builder. Every `With...`/`From...` mutates and returns the same builder; `Build()` snapshots. |
| `WslContainer` / `IWslContainer` | One disposable container in its own WSL session. `Build()` creates, `StartAsync()` provisions + waits. |
| `Wait` / `IWaitStrategy` | Readiness conditions. `StartAsync()` returns only after all pass. |
| `IWslProcess` | Long-running process from `StartProcess` — caller must dispose it. |
| Modules (`PostgreSqlBuilder`, `RedisBuilder`) | Versioned presets: image + port + waits + connection helpers (`GetConnectionString()` / `GetEndpoint()`). Prefer over hand-rolled builder chains. |

Isolation model: each `WslContainer` owns a dedicated WSL **session** with its own
storage (`%LOCALAPPDATA%\Wslc\instances\<wslc-name>\storage`), so parallel tests do
not share files, ports, or processes. Instances are named `wslc-{session}-{random}`
(ephemeral) or `wslc-reuse-<hash>` (reuse). See Architecture below.

## Image sources

```csharp
.WithImage("docker.io/library/redis:7")            // pulled on first use, cached in session storage
.FromTarball(@"C:\images\rootfs.tar", "app:test")  // import a rootfs tarball as an image
```

- If `WSLC_DEFAULT_IMAGE` is set it is used when no source is configured.
- `FromTarball` throws `WslException` when the tarball does not exist (same
  call-time check as `WithFile`/`WithVolume`); `Build()` throws when no source
  is configured. Pin tags/digests in tests; avoid `:latest` for reproducibility.

## Builder reference

| Method | Purpose / notes |
| --- | --- |
| `WithImage(image)` / `FromTarball(path, imageName?)` | Mutually exclusive source. |
| `WithCommand(cmd, params args)` | Init process. WSLC never runs the image's ENTRYPOINT/CMD automatically; the default is a keep-alive shell (`/bin/sh -c "while true; do sleep 3600; done"`) so `ExecAsync` works. Modules override this with the image entrypoint (e.g. `docker-entrypoint.sh postgres`) — do not override it for modules. |
| `WithWorkingDirectory(path)` | Working dir for init + execs. |
| `WithEnvironment(k, v)` / `WithEnvironmentVariables(dict)` | Scoped to container processes only. Names must be `[_A-Za-z][_A-Za-z0-9]*`. Inside every container `WSLC_SESSION_ID`, `WSLC_INSTANCE_ID`, `WSLC_OWNER_PID`, `WSLC_CREATED_AT` are also set. |
| `WithPort(containerPort)` / `WithPort(port, bindAddress)` / `WithPort(port, IPAddress)` | Declare each Linux TCP port you probe or connect to. Host port is dynamic (`0` → runtime-assigned); resolve the address and port with `GetConnectEndpoint(containerPort)`. UDP mappings are not supported — the WSLC runtime returns `E_NOTIMPL` for them. The Windows side binds loopback (`127.0.0.1`) by default; pass a bind address (e.g. `"0.0.0.0"` or `IPAddress.Any`) to override. TCP/HTTP readiness probes honor the configured bind address. |
| `WithNetworkingMode(mode)` | `Bridged` (default) or `Isolated` (no NIC — full isolation; no ports or network waits allowed; the only containment mode the runtime enforces). |
| `WithCpuCount(n)` / `WithMemoryMegabytes(n)` | Caps for the session VM (megabytes). Null (default) leaves the runtime default. |
| `WithScratchVolume(name, containerPath, sizeBytes, ...)` | Session VHD volume (native ext4, recreated empty every start). Prefer over bind mounts when data must not be exposed as Windows host files (the VHD still lives under the session storage directory). |
| `WithWaitStrategy(s)` | Add a readiness condition. All must pass, run sequentially. |
| `WithFile(hostPath, containerPath)` | Copy one Windows **file** (≤1 GiB, must exist) to an absolute Linux dest at startup. |
| `WithVolume(host, container)` / `WithVolume(..., VolumeAccess)` | Mount an existing Windows **directory** (host, container order, like `docker run -v`). Prefer read-only unless the test must write back. |
| `WithReuse(true)` | Keep session storage (and cached images) between runs; name is a config hash. Disabled under CI unless `WSLC_REUSE_IN_CI=1`. See `reuse.md`. |
| `WithReadinessTimeout(t)` | Whole-`StartAsync` budget. Must be ≥ sum of wait timeouts (validated at `Build()`). Default 120 s. Module builders set per-wait timeouts with `WithWaitTimeout(t)` and derive startup as `2*t+30 s`. |

## Lifecycle and cleanup

```csharp
await using var container = new WslContainerBuilder()
    .WithImage("docker.io/library/alpine:latest")
    .Build();

await container.StartAsync();
// ... tests ...
await container.StopAsync();  // optional: stop but keep storage
 // DisposeAsync: stop + terminate session + delete ephemeral storage
```

- `StartAsync()` may be called again after `StopAsync()`. Ephemeral instances restart with
  clean storage; `WithReuse(true)` instances reuse the session VHD (cached images) and only
  their scratch volumes are recreated empty.
- Lifecycle calls are serialized; `Exec`/`Copy`/`StartProcess` issued concurrently
  with `DisposeAsync` fail fast instead of corrupting state.
- A process-exit hook does best-effort cleanup on crash; next `StartAsync()` reaps
  orphaned ephemeral storage from dead owners (PID-recycling safe, 7-day grace for
  corrupt metadata). Reuse storage is never auto-reaped — see `reuse.md` and
  `WslResourceReaper.CleanupAsync()` / `PurgeReuseAsync()` /
  `CleanupIncludingReuseAsync()`.
- Storage root is `%LOCALAPPDATA%\Wslc` (`WSLC_DATA_DIRECTORY` overrides). Only
  `wslc-*` directories are ever touched.

## Readiness (`Wait`)

`StartAsync()` throws `WslReadinessException` when a strategy never passes. Call
`ex.Describe()` and dump `SubscribeLogs()` — see `troubleshooting.md`.

```csharp
using Wslc.Testcontainers.Waiting;

.WithWaitStrategy(Wait.ForWsl().UntilTcpPortIsOpen(5432))
.WithWaitStrategy(Wait.ForWsl().UntilHttpRequestSucceeds("/health", 8080))
.WithWaitStrategy(Wait.ForWsl().UntilProcessIsRunning("postgres"))
.WithWaitStrategy(Wait.ForWsl().UntilProcessExits("migration"))
.WithWaitStrategy(Wait.ForWsl().UntilMessageIsLogged("database system is ready"))
.WithWaitStrategy(Wait.ForWsl().UntilFileExists("/tmp/ready"))
```

Options and composition:

```csharp
var strategy = Wait.ForWsl()
    .WithTimeout(TimeSpan.FromSeconds(60))           // per-strategy (default from WSLC_TIMEOUT or 60 s)
    .WithRetryInterval(TimeSpan.FromMilliseconds(200))
    .UntilTcpPortIsOpen(5432)
    .And(Wait.ForWsl().UntilMessageIsLogged("ready to accept connections"));
```

Rules that bite:

- `WithPort(n)` every port you probe — `GetConnectEndpoint(n)` throws `WslNetworkException`
  for an undeclared/unassigned port, and `WslException` before `StartAsync()`.
- `And(...)` flattens nested composites and keeps the left operand's timeout, which bounds the
  whole sequence: two composed 60 s waits get a 60 s budget, not 120 s. Set the budget on the
  left operand. The composite keeps the left retry interval too, but it does not poll itself —
  each child keeps its own interval.
- Custom conditions: `Until("name", (target, ct) => ...)` polls a delegate. Exceptions are not
  retried: `OperationCanceledException` becomes a `WslReadinessException` timeout unless the
  caller cancelled, and other exceptions surface from `StartAsync` as `WslProvisioningException`
  (original as `InnerException`). Use `WithTimeout`/`WithRetryInterval` to bound and pace it.
- `WithReadinessTimeout` ≥ sum of wait timeouts. A 5 s strategy inside a 120 s startup
  still fails at 5 s.
- `UntilMessageIsLogged` is ordinal substring, case-sensitive, ignores `LogSource.System`
  diagnostics (no regex). Postgres needs `occurrences: 2` — the entrypoint logs readiness
  once from a temp init server before the real one (handled by `PostgreSqlBuilder`).
- `UntilHttpRequestSucceeds(pathAndQuery, port)` needs an absolute path (`/health`),
  not a URL; 2xx–4xx succeed, 5xx retry.
- File/container paths must be absolute Linux paths (`/tmp/ready`).

## Ports and connecting

Host ports are dynamic. Never hardcode them:

```csharp
var endpoint = postgres.GetConnectEndpoint(PostgreSqlContainer.DefaultPort);
var connectionString =
    $"Host={endpoint.Address};Port={endpoint.Port};Username=postgres;Password=secret";
// endpoint.Address is the mapping's bind address; wildcard/default bindings resolve to
// loopback (127.0.0.1 for 0.0.0.0, ::1 for ::).
```

- `GetConnectEndpoint(containerPort)` returns an `IPEndPoint`: the runtime-assigned
  host port plus the address the mapping is actually bound to (IPv4 `127.0.0.1` for
  `0.0.0.0`, IPv6 `::1` for `::`). Mappings are TCP-only because the WSLC runtime
  returns `E_NOTIMPL` for UDP.
- Only declared ports are mapped; each `WslContainer` gets its own mapping so
  parallel tests never collide. UDP mapping support must come from the runtime.
- The Windows side binds loopback (`127.0.0.1`) by default. Override it only when you
  need LAN exposure, e.g. `WithPort(8080, "0.0.0.0")` or `WithPort(8080, IPAddress.Any)`.
- Published ports are reachable via loopback from Windows. Treat them as test-only
  listeners, not public endpoints.

## Networking modes, resources, named volumes

```csharp
// Fully offline container: no NIC, no ports, no egress. Drive it via Exec/Copy.
await using var offline = new WslContainerBuilder()
    .WithImage("docker.io/library/alpine:latest")
    .WithNetworkingMode(ContainerNetworkMode.Isolated)
    .Build();

// Bounded session VM + loopback-only port + native-Linux scratch disk.
await using var agent = new WslContainerBuilder()
    .WithImage("my-registry/agent:latest")
    .WithCpuCount(2)
    .WithMemoryMegabytes(2048)
    .WithPort(8080, "127.0.0.1")
    .WithScratchVolume("scratch", "/scratch", sizeBytes: 10UL * 1024 * 1024 * 1024)
    .Build();
```

- `Isolated` rejects `WithPort` and TCP/HTTP waits at `Build()` time —
  use process/log/file waits instead.
- Scratch VHD volumes are ext4 inside the session VM (no VirtioFS/Windows round-trip)
  and are recreated empty on every start: size-limited scratch, not persistence.
  They are never exposed as Windows host files, but the backing VHD does live under
  the session storage directory. Volume names must be unique per container.
- `WithCpuCount`/`WithMemoryMegabytes` cap the session VM; they are part of the reuse hash.

## Egress policy (not offered)

The library cannot enforce a destination allowlist on this runtime, so it does not
expose an API that pretends to:

- On the current WSLC runtime (WSL 3.0.1), containers are started without
  `CAP_NET_ADMIN` (the capability set is the Docker default minus network admin).
  `iptables`/`nftables` inside the container can neither read nor modify the ruleset,
  and because the capability is also absent from the bounding set, no in-container
  workaround (`setcap`, setuid, a different backend) can add it.
- The SDK's container settings expose ingress (`PortMappings`, `NetworkingMode`) but
  no outbound policy. Egress can therefore only be enforced by the runtime outside the
  container's security context — network-namespace rules applied by WSLC, or the
  user-mode session network proxy — which does not exist today.

Until the runtime provides it, the only enforced containment is
`WithNetworkingMode(Isolated)`: no NIC, so neither egress nor ingress is possible. Drive
the workload from the outside with `Exec`/`Copy`/`Logs`. If a runtime-side policy API
appears, egress belongs there, not inside the container.

## Exec, processes, files, logs

```csharp
// One-shot command; captures exit code + stdout/stderr (each capped ~1 MiB).
ExecResult r = await container.ExecAsync("ps", ["aux"]);
r.EnsureSuccess(); // throws WslProcessException with truncated output on failure

// With options (env, cwd, stdin, timeout). Pass null for arguments/options to use defaults.
var r2 = await container.ExecAsync("psql", ["-c", "SELECT 1"], new ExecOptions
{
    Environment = new Dictionary<string, string> { ["PGPASSWORD"] = "secret" },
    WorkingDirectory = "/tmp",
    StandardInput = "optional stdin text",
    Timeout = TimeSpan.FromSeconds(30),
}, CancellationToken.None);

// Long-running process. ProcessOptions carries working directory + environment only.
IWslProcess proc = container.StartProcess("sleep", ["3600"]);
Console.WriteLine(proc.Id);
await proc.KillAsync();          // SIGTERM → SIGKILL
await proc.DisposeAsync();       // must dispose; container also kills leftovers on Stop/Dispose

// Files (≤1 GiB each way; container paths must be absolute Linux paths)
await container.CopyToAsync(@".\fixtures\app.conf", "/etc/app/app.conf");
await container.CopyFromAsync("/var/log/app.log", @".\artifacts\app.log");

// Logs: infinite stream — always bound it (CTS or LogDumper).
using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
await foreach (var line in container.SubscribeLogs(cts.Token))
    Console.WriteLine(line.ToString());

// Bounded tail snapshot (never blocks): the right first look after a failure.
var recent = container.GetRecentLogs(100);

// One-liner without an xUnit dependency: dumps the OLDEST 100 lines of the stream.
using Wslc.Testcontainers.Testing;
await LogDumper.DumpHeadAsync(container.SubscribeLogs(ct), output.WriteLine, maxLines: 100, ct);
```

Notes:

- `ExecAsync` timeout throws `WslTimeoutException` and kills the process.
- `StartProcess` takes `ProcessOptions` (working directory + environment only); `StandardInput`/`Timeout` exist only on `ExecOptions` for `ExecAsync`, and passing an `ExecOptions` with either set to `StartProcess` throws `ArgumentException`.
- `CopyToAsync` requires the host file to exist; `CopyFromAsync` creates parent dirs.
- Init-process output, exec output and `StartProcess` output all flow through `SubscribeLogs()`.

## Volumes — prefer copy over mount

```csharp
// Seed config at startup (no host write-back):
.WithFile(@"./fixtures/app.conf", "/etc/app/app.conf")

// Share a directory only when the test needs live file exchange:
.WithVolume(@"C:\data\in", "/workspace/in")                       // read-write
.WithVolume(@"C:\data\in", "/workspace/in", VolumeAccess.ReadOnly)
.WithVolume(@"C:\data\seed", "/seed", VolumeAccess.ReadOnly)       // preferred for fixtures
```

Under the hood mounts use VirtioFS (`/mnt` → bind mount into the container).
For databases prefer named data to stay inside the VM; for fixtures prefer
`WithFile`/`CopyToAsync` over a writable mount so the container cannot mutate the
repo. Mounts are the main host-escape surface — never mount `C:\` or a repo root
writable in agent scenarios (see below).

## xUnit patterns

`examples/Postgres.Tests/` is the reference: one shared container per collection,
reset state per test.

```csharp
// Fixture: share one Postgres for the whole collection (~60–120 s cold start).
public sealed class PostgresFixture : IAsyncLifetime
{
    private readonly PostgreSqlContainer _postgres = new PostgreSqlBuilder()
        .WithImage("docker.io/library/postgres:15-alpine")
        .Build();

    public string ConnectionString => _postgres.GetConnectionString();
    public DbConnectionProvider Provider => new(ConnectionString);

    public ValueTask InitializeAsync() => new(_postgres.StartAsync());
    public ValueTask DisposeAsync() => _postgres.DisposeAsync();
}

[CollectionDefinition(Name)] public sealed class PostgresCollection : ICollectionFixture<PostgresFixture> { }
```

```csharp
[Collection(PostgresCollection.Name)]
public sealed class CustomerServiceTest
{
    [Fact] // or [IntegrationFact] when gating on WSLC_RUN_INTEGRATION=1
    public async Task ShouldReturnTwoCustomers()
    {
        await new CustomerService(_fixture.Provider)
            .ResetAsync(TestContext.Current.CancellationToken); // DROP/CREATE per test
        // ... arrange/act/assert ...
    }
}
```

Guidance:

- Share one container per `[Collection]`, reset with `TRUNCATE`/`DROP TABLE` per test
  (see `CustomerService.ResetAsync`). One container per test class is too slow for
  Postgres; reuse (`WithReuse(true)`) is only safe locally with a reset, never as a
  substitute for the reset.
- Gate real-runtime tests with `[IntegrationFact]` (`WSLC_RUN_INTEGRATION=1`) so unit
  runs stay green on machines without WSL. See `IntegrationFactAttribute` and
  `troubleshooting.md`.
- On failure dump `ex.Describe()` + `container.GetRecentLogs(100)` (bounded tail) + builder chain.
- `ConfigureContainer(b => b.WithReuse(true).WithReadinessTimeout(...))` is the
  escape hatch for core settings a module does not expose (extra ports/waits/volumes).

## Configuration

| Variable | Meaning (code-level builder wins) |
| --- | --- |
| `WSLC_TIMEOUT` | Default wait timeout (seconds or `TimeSpan`). Default 60 s. |
| `WSLC_DATA_DIRECTORY` | Storage root. Default `%LOCALAPPDATA%\Wslc`. Snapshotted on first use. |
| `WSLC_DEFAULT_IMAGE` | Image used when none is configured. |
| `WSLC_REUSE` | `1`/`true` enables reuse by default. |
| `WSLC_CLEANUP` | `0`/`false` disables automatic cleanup + orphan reaper. |
| `WSLC_REUSE_IN_CI` | `1`/`true` allows reuse under CI (otherwise forced off). |
| `WSLC_SESSION_ID` | Overrides session id in `wslc-{session}-{random}` names. Snapshotted per process. |

Truthy = `1/true/yes/on`; falsy = `0/false/no/off` (case-insensitive). Anything else
is treated as unset.

## Architecture (why tests are isolated)

From the [WSLC architecture deep dive](https://devblogs.microsoft.com/commandline/wslc-architecture-deep-dive/)
(`Microsoft.WSL.Containers` 3.0.1, `wslcsdk.h`):

- **Session model.** `wslservice.exe` (privileged) spawns one `wslcsession.exe` per
  session running as the calling user. WSLC creates **one session per container**
  (`SessionSettings(Name, storagePath)`), so sessions live in different processes
  with a reduced-privilege boundary. A kernel/container exploit is scoped to that
  session's VM, not to all test containers.
- **Storage.** Each session has its own storage VHD (`.../instances/<name>/storage`).
  Windows-path volumes are VirtioFS shares (mounted under `/mnt` in the VM, then bind-
  mounted into the container — ~2× plan9). `WithScratchVolume` provisions a scratch VHD
  volume instead: native ext4 inside the VM, recreated empty every start.
- **Networking ("Consommé").** VM traffic goes as Ethernet frames over a virtio queue
  to a user-mode Windows process that provides DNS, TCP/UDP routing and port mapping.
  Egress leaves Windows **as the session owner**, so VPNs/firewalls see it as a normal
  user process. WSLC defaults to `ContainerNetworkMode.Bridged` with dynamic TCP port
  mappings onto Windows loopback (optionally pinned with a bind address);
  `WithNetworkingMode(Isolated)` removes the NIC (the only enforced containment). There is
  no egress policy mechanism in WSLC 3.0.1: containers do not receive `CAP_NET_ADMIN`
  and the runtime exposes no outbound filter.

## Agent containment note

The defaults above are for **trusted test dependencies**, not untrusted agents.
Bridged networking gives the container full egress (internet, LAN and host-side
services); any `WithVolume` gives it a write path onto Windows; processes run as the
container's default user (root in most images) with no seccomp/capability/user-namespace
controls exposed by WSLC.

If you run agent-generated code, contain it so it can only interact with the API
that invoked it:

1. **Strongest: no network.** `WithNetworkingMode(Isolated)` removes the NIC entirely —
   drive the agent via `ExecAsync`/`StartProcess`/`CopyTo`/`CopyFrom`/`SubscribeLogs`.
   This is also the only containment the runtime can enforce today; an egress
   allowlist is not available (see [Egress policy](#egress-policy-not-offered)).
2. **No mounts by default.** Seed with `WithFile`/`CopyToAsync`, extract with
   `CopyFromAsync`; for scratch that must not be exposed as Windows host files use
   `WithScratchVolume`; if a bind mount is unavoidable use `VolumeAccess.ReadOnly` on the
   narrowest directory. Never mount a repo root or `C:\` writable.
3. **Bound the blast radius.** `WithCpuCount`/`WithMemoryMegabytes`, minimal pinned-digest
   image, minimal `WithCommand`, per-run short-lived tokens (never host credentials
   in `WithEnvironment`).
4. **Observe.** `SubscribeLogs` + `Exec` audit trail is your record; `DisposeAsync`
   destroys the session VHD/scratch on completion.

Not yet exposed by the SDK surface in WSLC: custom bind defaults beyond per-port
`bindAddress`, non-root users, seccomp/capability controls, read-only rootfs, image
signing, or any outbound network policy. Until those exist, treat `Isolated` + mount/port
hygiene as layered defenses, and re-evaluate before trusting agents with
production-adjacent access.
