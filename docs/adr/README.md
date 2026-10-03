# Architecture Decision Records

Decisions that constrain day-to-day code in this repo.
Code comments must explain the current snippet or the overall abstraction.
When a comment refers to one of these rules, cite it as `per ADR-XXXX <slug>`.

| ADR | Rule |
|-----|------|
| [0001](0001-bounded-loops.md) | Bounded loops — we don't allow `while(true)` |
| [0002](0002-comments.md) | Commenting rules and styles |
| [0003](0003-immutable-builder.md) | Superseded by [0007](0007-mutable-builders.md) |
| [0004](0004-best-effort-cleanup.md) | Best-effort cleanup never throws |
| [0005](0005-strict-build.md) | Strict build with zero warnings |
| [0006](0006-module-bases.md) | Typed module bases |
| [0007](0007-mutable-builders.md) | Mutable builders, single-copy handoff |
| [0008](0008-service-interfaces.md) | `IWslContainer`/`IWaitTarget` are library-implemented |
