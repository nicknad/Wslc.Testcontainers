# ADR-0007: Mutable builders

Status: Accepted

Supersedes: [ADR-0003](0003-immutable-builder.md)

## Context

ADR-0003 made `WslContainerBuilder` immutable: every `With...` returned a new
builder. Its rationale was allocation behavior (naive chained copies), not the
call-site contract. In practice the immutable shape is the outlier for the
Testcontainers family this library mirrors, and it carries a DX trap: dropping a
`With...` result silently builds a different container (`builder.WithPort(80);`
compiles and does nothing), surfacing minutes later as a readiness timeout.
Module builders already mutated module-owned defaults and `WslWaitBuilder` was
mutable, so the builder surface was also inconsistent.

## Decision

- All public builders (`WslContainerBuilder`, `WslModuleBuilder<TBuilder>` and
  the module builders, `WslWaitBuilder`) mutate in place and return the same
  instance for chaining.
- `Build()`/`BuildContainer()` snapshot the accumulated immutable configuration
  record, so containers already built are unaffected by later builder changes.
- Builders are not thread-safe; do not share one across threads.
- The single-copy handoff and allocation rules from ADR-0003 remain: appends
  copy with known size, dictionaries are sized from `Count`, and settings lists
  transfer ownership to the runner.

## Consequences

- Ignoring a `With...` return value is harmless.
- Callers can no longer branch a builder (`base.WithX()` / `base.WithY()`);
  create a second builder instead. Tests and docs that demonstrated branching
  were updated.
- ADR-0003 is superseded; its allocation guidance is folded into this decision.
