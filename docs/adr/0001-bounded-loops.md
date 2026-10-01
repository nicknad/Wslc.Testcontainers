# ADR-0001: Bounded loops — we don't allow `while(true)`

Status: Accepted

## Context

Polling and streaming code wants open-ended retries: port resolution, wait
strategies, stdout copy. An unbounded loop hangs the host when a timeout,
clock, or stream misbehaves.

## Decision

Every loop has a statically visible bound. We don't allow `while(true)`.
Use a `for` loop with a named cap that derives from a timeout or size limit:

- Port resolution: `for (attempt < 200)` covers 10s @ 50ms
  (`src/Wslc.Testcontainers/WslContainer.cs` → `ResolveMappedPortsAsync`).
- Wait strategies: `for (attempt < 100_000)` caps polling even when
  `Timeout` / `RetryInterval` are misconfigured
  (`src/Wslc.Testcontainers/Waiting/WaitStrategyBase.cs` → `WaitAsync`).
- Stdout copy: `for (chunk < MaxChunks)` where
  `MaxChunks = 1GiB / 64KiB + 1`, plus a byte total that fails over 1 GiB
  (`src/Wslc.Testcontainers/Runtime/WslcProcessRunner.cs` → `CopyFromAsync`).

Small scans over an in-memory buffer (`LinkedList` trim, newline scan) are
allowed with `while` because the bound is the buffer length, but prefer `for`
when a count is known.

## Consequences

- Loops always terminate without relying on wall-clock math alone.
- Tests can assert the cap instead of timing out.
- Comments cite this rule as `per ADR bounded loops we don't allow while(true)`
  and must still explain the concrete numbers for that snippet.
