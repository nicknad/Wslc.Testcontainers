# ADR-0009: Fault relay on the Windows host

Status: Accepted

## Context

MQTT clients only reveal their interesting behavior — reconnect/backoff, keepalive
timeouts, QoS 1 redelivery, last-will — when the transport misbehaves. Docker-based
suites usually reach for Toxiproxy or `tc netem`, but this runtime gives each container
a single init process, no sidecars, no user-defined networks, and no guaranteed Linux
capabilities. Fabricating MQTT packets (a fake CONNACK, an auth rejection) would test the
library's fiction rather than the client's real reconnect path, and a container wrapper
that advertises a faulting mapped port would misrepresent what the container actually
exposes.

The RST/FIN/stall semantics we need can only be produced honestly at a TCP endpoint the
test process controls.

## Decision

- **Relay on the Windows host.** `FaultShim` is an in-process TCP relay that binds an
  ephemeral loopback port and dials the target endpoint once per accepted connection.
  RST, half-close and stall are applied to real sockets in the test process, so the peer
  sees genuine transport behavior and one relay connection maps 1:1 to one container
  connection.
- **Protocol-blind.** The relay forwards bytes. It never parses, synthesizes, duplicates
  or reorders protocol messages; MQTT-specific outcomes (auth rejection, reason codes,
  redelivery) stay broker configuration.
- **Opt-in.** The container's direct endpoint remains the default. Readiness probes
  target the direct endpoint, so a relay can never mask a broker that failed to start.
  The relay is not part of the reuse configuration hash and never pins a host port.
- **Core package placement.** `FaultShim`, `FaultMode` and `FaultDirection` live in
  `Wslc.Testcontainers.Faults` in the core package, alongside the existing test helpers,
  so any module can use them without another dependency.
- **Fixed semantics.** `Reset` sets linger-to-zero and closes both legs (peer observes
  RST, not FIN); `Close` send-shuts only the selected direction (true half-open);
  `Stall` parks forwarding in the selected direction while both sockets stay open, and
  `Clear` resumes it without dropping the held chunk; `Latency` delays every forwarded
  chunk by a fixed duration. `Arm` applies a fault at accept time to exactly the next N
  connections. `Inject` returns the affected live-connection count and returns zero —
  not an error — when nothing is connected.
- **Lifecycle and failure semantics.** Attach after start; dispose the relay before the
  container. Disposal is idempotent, best-effort and never throws (ADR-0004); use after
  disposal throws `ObjectDisposedException`. `LiveConnections` is cheap, lock-protected
  state with no I/O.
- **Bounds.** At most 255 concurrently relayed connections, a 16 KiB per-direction
  buffer, and a 1 GiB / 16 KiB chunk cap per pump, so no loop is unbounded (ADR-0001).

## Rejected alternatives

- **In-container proxy (Toxiproxy, socat, or a custom sidecar).** The runtime gives each
  container one init process and no sidecars or user-defined networks; requiring an extra
  process and port would exclude exactly the environments this library targets.
- **Container decorator that reports a faulting mapped port.** It would lie about what
  the container exposes: readiness, log triage and direct connections would all
  disagree with the reported endpoint.
- **Protocol-aware shim.** Synthesizing CONNACK/SUBACK or auth failures tests the shim,
  not the broker, couples the relay to MQTT forever, and contradicts keeping the fault
  area broker-agnostic for future modules.

## Consequences

- The relay adds one loopback hop, so latency-sensitive assertions should use the direct
  endpoint; the fault API is for behavior, not throughput measurement.
- A stalled relay is intentionally indistinguishable from a broker-side silent drop.
  `Inject` returning zero tells "nothing was connected" apart from "the fault fired", and
  the documented triage rule is to clear faults and connect directly before concluding
  the broker is dead.
- The fault code is testable without Windows container support: the unit and echo-server
  tests run on every build, while the per-broker composition tests stay behind the
  integration fact.
