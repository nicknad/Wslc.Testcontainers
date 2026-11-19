# ADR-0011: Fault taxonomy — module fit and producer classes

Status: Accepted

## Context

ADR-0009 generalized the fault *mechanism*: `FaultShim`, `FaultMode` and `FaultDirection`
live in the core faults package, the relay is protocol-blind, and every module composes it
through `GetConnectEndpoint` + `FaultShim.Attach`. It deliberately left two questions open:
which modules x failure classes are worth fault tests at all, and which failure classes need
a producer other than a TCP byte relay. This ADR records that audit for all 21 modules.

Every module x class cell was judged by two questions:

1. **Honest producer?** Which mechanism produces the failure the way the real one would
   (per ADR-0009's bar: the peer must observe the genuine thing; synthesized fiction is
   rejected).
2. **Assertable client behavior?** Which documented resilience behavior of the module's
   canonical client would a test prove? No assertable behavior, no fault surface.

## Decision

### Producer classes — only these are honest

- **Transport** — the relay on real sockets: `Reset` (RST), `Close` (half-open FIN),
  `Stall` (silence), `Latency`, each x `FaultDirection`. This is the only
  faults-package API.
- **Lifecycle** — the real container: `StopAsync` (SIGTERM, then SIGKILL after the
  `WithStopTimeout` grace; 1 s floor, 10 s default), an in-container `SIGKILL` via
  `ExecShellAsync` (for modules the init process *is* the service: `mongod`,
  `nats-server`, ...), and `StartAsync` after a stop (documented re-provision). A stopped
  container's mapped port is a genuinely dead port.
- **Service configuration** — module builder options plus `ExecAsync`/`ExecShellAsync`
  against the real service: auth rejection, max-connections, quotas, seal. Never the faults
  package; ADR-0009's precedent (MQTT auth and reason codes stayed broker configuration)
  applies to every protocol.
- **Builder sizing** — `WithMemoryMegabytes` / `WithScratchVolume(..., sizeBytes)`
  produce real memory and disk caps. Honest but nondeterministic (OOM timing, fill time), so
  they stay outside the fault backlog.

### Rejected as dishonest or unavailable

- **Protocol synthesis** (fake CONNACK, HTTP 500, synthesized reason codes) — tests the
  shim's fiction, not the service (ADR-0009's own rejection).
- **Byte duplication, reordering, mid-stream corruption** — TCP guarantees make them
  fiction; real checksum corruption surfaces as RST, which `Reset` already produces.
- **Durability replay across restart** (Postgres WAL, InnoDB, WiredTiger journal, ES
  translog) — ephemeral containers restart with clean storage per the documented lifecycle,
  so a restarted service starts from an empty data dir and replays nothing. Blocked on a
  persistence capability, not on fault API.
- **Per-container cgroup pressure tools** — the runtime guarantees no Linux capabilities
  (ADR-0009); session VM caps exist but timing is nondeterministic (builder sizing above).

### Uniform verdicts

- **Dead port (connection refused):** lifecycle, not the relay. After `StopAsync` the
  container's mapped port is honestly dead.
- **Configuration rejection:** stays module configuration, demand-driven. High-value
  instances worth their own presets: Keycloak realm/client auth and token expiry; Vault seal
  and lease expiry; PostgreSql `max_connections` and auth; Mosquitto password file; Redis
  `requirepass`/`maxclients`; Kafka quotas/SASL; Qdrant API key.
- **Resource exhaustion:** builder sizing lever exists; deterministic tests cannot rely on
  OOM timing, so it is not part of the fault backlog.
- **Readiness/startup faults:** out of scope (ADR-0009: readiness probes the direct
  endpoint; a fault can never mask a failed start).

### Naming — decided now, in the unshipped window

`FaultDirection.ClientToBroker`/`BrokerToClient`/`Both` rename to
`ClientToTarget`/`TargetToClient`/`Both` in both ports before first ship. The code
already speaks "target" (`FaultConnection` holds `_target`; the `FaultDirection` docs
say "to the target (broker)"), and `BrokerToClient` is wrong for Postgres, S3 and gRPC.
The faults namespace is still unshipped (`PublicAPI.Unshipped.txt`), so the rename is
free; after first release it becomes a compatibility commitment. Mechanical diff: enum
members, doc comments, tests, usage docs, `PublicAPI.Unshipped.txt`, both languages.

### Per-module verdicts

`K` keep — the named client behavior earns one composition test. `T` thin — only when
the test brings its own resilience logic. `D` drop — no fault surface. Dead port,
resource exhaustion and durability replay are uniform (above) and omitted from the table.

| Module | Transport (R/C/S/L) assert | Crash + stop | Rebirth | Notes |
| --- | --- | --- | --- | --- |
| Mosquitto | K: the proven shape — raw MQTT frames over the relay; reconnect, keepalive, last-will, QoS 1 redelivery after `Clear` (per ADR-0009) | K | K | MosquittoFaultTests / mosquitto_fault_tests |
| HiveMq | K: same asserts against HiveMQ | K | K | |
| RabbitMq | K: automatic recovery — topology redeclare, resubscribe, confirm timeout; heartbeat death | K | K | Heartbeat is a client-side setting; use a short one |
| Artemis | K: client reconnect + redelivery | K | K | |
| Kafka | K: producer delivery timeout; consumer session timeout -> rebalance on silent link | K | K | Advertised listener must point at the relay — pin in the first test |
| Nats | K: reconnect/resubscribe, pending-flush errors, fast stale detection | K | K | Ping interval is a client-side setting |
| Redis | K: `ConnectionRestored`, sync-op `TimeoutException` then recovery | K | K | Config: `requirepass`, `maxclients` |
| Valkey | K: same client, same asserts | K | K | |
| PostgreSql | K: pool eviction, "server closed the connection", reconnect | K | K | WAL replay -> boundary; config: `max_connections` |
| MariaDb | K: pool eviction/reconnect | K | K | InnoDB recovery -> boundary |
| SqlServer | K: pool eviction/reconnect | K | K | Heavy image; rebirth test is slow |
| MongoDb | K: SDAM half-open detection, server-selection timeout, retryable writes | K | K | Journal replay -> boundary |
| Elasticsearch | K: connection-pool death and resurrection, operation timeout | K | K | Slow restart; keep one rebirth test |
| Qdrant | K: gRPC deadline exceeded on stall, channel reconnect | K | K | Config: API key |
| Azurite | K: Azure SDK exponential retry, then success | K | K | No real auth (dev key by design) |
| RustFs | K: S3 retry of idempotent PUTs and multipart parts after a mid-body fault | K | K | Config: access keys |
| Keycloak | T: proves only a caller-supplied retry policy against a real RST | T | D | High-value config: realm/client auth, token expiry |
| Vault | T: lease-renewal retry is test-driven | T | D | Dev-mode default; high-value config: seal via `ExecShellAsync`, lease expiry |
| ClickHouse | T: no canonical client retry; surfaces caller error handling only | T | D | Config: user quotas |
| WireMock | D: inverted — WireMock is itself an application-level fault injector (delays, resets, malformed responses) | D | D | No fault surface by design |
| MailPit | D: SMTP test senders have no resilience path; the module asserts arrival, not survival | D | D | No fault surface by design |

New modules are graded against this matrix at review time: `K` requires a canonical
client with documented resilience behavior; anything else is `T` or `D`.

### Proof contract (inherited from ADR-0009)

- `FaultShimTests` (echo server, every build) and `MosquittoFaultTests` /
  `mosquitto_fault_tests` (integration fact) keep passing unchanged and remain the
  pattern.
- Every `K` earns exactly one composition test behind the integration fact, asserting the
  behavior named in the matrix, red to green and deterministic. No fault test without the
  assert; no assert without the test.
- The first crash test pins the real observables (signal to the real process, reset
  connection, dead port while stopped) before the pattern is reused.
- Any new transport mode (the open `Refuse` candidate) gets a container-free echo-server
  test on every build before any composition test uses it.
- C# and C++ stay in semantic lockstep; a doc written against one reads true against the
  other.

## Rejected alternatives

- **Per-module fault options on builders** (e.g. `WithFault(...)`) — contradicts
  ADR-0009's composition style; a builder knob would hide the relay hop and couple modules
  to fault vocabulary.
- **Growing `FaultMode` with protocol-shaped modes** (auth-reject, slow-query, quota) —
  those are service configuration; a transport enum carrying them synthesizes fiction.
- **Docker-style `Kill`/`Restart` primitives now** — lifecycle already produces honest
  crash, stop and rebirth; a convenience wrapper is a sequencing choice, not a correctness
  one (see Open decisions).
- **Toxiproxy-class network emulation** — ADR-0009 already rejected sidecars; `tc`/cgroups
  need capabilities the runtime does not guarantee.

## Resolutions (maintainer, 2026-11-19)

Executed in the order 2, 3, 4, 1. Each branch resolved as follows:

2. **Kill primitive added; Restart stays a composition.** `KillAsync`/`Kill` sends SIGKILL
   to the init process — a genuine crash with no SIGTERM phase (`WithStopTimeout` does not
   apply), with storage preserved exactly as `StopAsync` preserves it, so a subsequent
   `StartAsync`/`Start` re-provisions the same instance. This closes the inexpressibility
   gap without the fragile `ExecShellAsync` pid-guessing composition. `RestartAsync` stays
   out: rebirth remains the documented `StopAsync`/`KillAsync` → `StartAsync` composition
   (no gap, no API).
3. **Refuse is standalone listener state, not a `FaultMode` value.** Honesty forces the
   branch: an accepted connection cannot be "refused" (that would be `Reset`), and genuine
   refusal requires a closed listener, so it cannot be an armed per-connection mode.
   `FaultShim` gains `RefuseNewConnections()` (new connects observe a dead port; live
   connections keep flowing) and `AcceptNewConnections()` (rebinds the same endpoint or
   throws when the port was taken while refused).
4. **Both ports together.** Lockstep is the proof contract: the rename
   (`ClientToTarget`/`TargetToClient`) and both additions land in C# and C++ in the same
   change, with the `PublicAPI` files updated.
1. **Rollout order pinned by wire-protocol cost:** tier 1 — Redis, Valkey, Elasticsearch,
   Azurite, RustFs (RESP/HTTP: raw clients are a few lines); tier 2 — PostgreSql, Nats,
   HiveMq (moderate wire protocols; HiveMq reuses the MQTT shape); tier 3 — RabbitMq,
   Artemis, Kafka, MongoDb, Qdrant, SqlServer (heavy protocols or slow images).
   `RedisFaultTests` is the first composition test, following the `MosquittoFaultTests`
   shape.

## Consequences

- 16 modules gain fault composition tests over time with zero new fault API; this matrix is
  the backlog and the review gate.
- Keycloak, Vault and ClickHouse get transport fault surface only when their tests bring
  resilience logic; their high-value failures remain configuration work.
- WireMock and MailPit carry no fault surface by design.
- Durability-replay scenarios wait on a persistence capability, not on fault API.
- The pre-ship window was used: `FaultDirection` renamed to `ClientToTarget`/`TargetToClient`,
  `KillAsync`/`Kill` and `RefuseNewConnections`/`AcceptNewConnections` added — both ports
  together, with the `PublicAPI` files updated — and `RedisFaultTests` opened the tier-1
  rollout. Container-free fault suites pass in both ports (C++ 16, C# 15 tests); `Kill`'s
  live-runtime behavior and the Redis composition tests run behind the integration gate
  (`WSLC_RUN_INTEGRATION=1`).
