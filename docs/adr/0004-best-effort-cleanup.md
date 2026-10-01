# ADR-0004: Best-effort cleanup never throws

Status: Accepted

## Context

Startup can fail halfway, and the process-exit hook runs on a crashing host.
Cleanup that throws hides the original error or crashes the finalizer path.

## Decision

- `CleanupSynchronously`, `StopCoreAsync` helpers (`Ignore`, `TryStep`),
  `UpdateState`, and the reaper swallow only what they document as benign
  (e.g. `ContainerNotRunning`, `ContainerNotFound`) and record the rest in a
  `failures` list that is thrown once, by the outermost async cleanup.
- The sync exit-hook path only calls `Ignore`: signal, stop, delete,
  terminate, dispose, delete storage directory. Each step explains itself as
  `best-effort`.
- Reaping touches only `wslc-*` directories with dead owners; missing
  metadata means "not ours, never delete".

## Consequences

- `DisposeAsync` after a failed `StartAsync` is safe and idempotent.
- `LogBroadcaster.Complete()` is owned by dispose, not by the startup catch.
