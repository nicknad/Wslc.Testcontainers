# ADR-0006: Typed module bases

Status: Accepted

## Context

The PostgreSql and Redis modules ship as separate packages, but their typed
container wrappers and builders duplicated the same delegation, readiness
presets and timeout handling. The layers had to agree, yet nothing shared the
contract, and a third module would copy the pattern again.

## Decision

- `WslModuleContainer` is the public abstract base for typed wrappers. It
  forwards `Name`, `Host`, lifecycle, logs and dispose to `IWslContainer`,
  and exposes `GetMappedPort(int)` to derived types.
- `WslModuleBuilder<TBuilder>` is the public fluent base. It owns the default
  image, exposed port, readiness waits and startup timeout, and calls the
  `Configure(WslContainerBuilder)` hook for module-specific settings.
- Module builders are mutable like `WslContainerBuilder` (ADR-0007): they hold
  module defaults, not user-visible container configuration, and their terminal
  `Build()` still snapshots the immutable core configuration.
- Modules must derive these bases instead of copying the delegation and wait
  presets. Public surface changes are tracked by PublicApiAnalyzers
  (ADR-0005).

## Consequences

- A new module implements one `Configure` override plus its connection
  string; the shared readiness order (TCP, then log message, then timeout)
  stays consistent.
- The public API grows by two base types, so the `PublicAPI.Unshipped.txt`
  files list them until the first release.
