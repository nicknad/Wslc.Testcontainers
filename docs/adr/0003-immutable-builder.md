# ADR-0003: Immutable builder, single-copy handoff

Status: Accepted

## Context

`WslContainerBuilder` is immutable: every `With...` returns a new builder.
Naive LINQ (`Append().ToArray()`, copying dictionaries without capacity)
turns chained calls into O(n²) allocations and hides a double copy of the
command line (`List` in `WslContainer`, then `ToList()` in the runner).

## Decision

- Keep the immutable public API. Internally append with a manual copy of
  known size (`WslContainerBuilder.Append`), and size dictionaries from
  `Count` (`WithEnvironment`, `BuildEnvironment`).
- `WslContainer` builds one `List<string>` with exact capacity and hands
  ownership to `WslcProcessRunner.CreateSettings`, which reuses a
  `List<string>` as-is instead of calling `ToList()` again.
- `WslcPortMapping` pre-sizes its lists and exposes `UnresolvedCount` so the
  50ms poll loop does not allocate a throwaway array per iteration.

## Consequences

- Builder chains stay allocation-proportional, exec/start stays at one list
  plus one dictionary per call.
- Callers must treat the list passed to `CreateSettings` as transferred.
