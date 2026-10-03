# ADR-0008: Library-implemented service interfaces

Status: Accepted

## Context

`IWslContainer` and `IWaitTarget` are public so consumers can program against
capabilities (and custom wait strategies can observe the environment), but the
library is the only intended implementer. Additive members after 1.0 would
break any external implementer, and the package tracks its surface with
PublicApiAnalyzers, so the compatibility question needs an explicit answer
before the first release.

## Decision

- `IWslContainer` and `IWaitTarget` are service interfaces implemented by the
  library only. Consumers program against them but do not implement them.
- Extension points are `IWaitStrategy` (including `WslWaitBuilder.Until`),
  module builders, and the builders' configuration hooks.
- Additive interface members are acceptable while the package is pre-1.0.
  After 1.0, prefer new capabilities on concrete types; introduce a new
  interface only if external implementation is genuinely required.

## Consequences

- `WslModuleContainer` implements `IWslContainer` (ADR-0006), so new interface
  members flow to module containers automatically.
- External test doubles should wrap the container/wait target they own or
  implement `IWaitStrategy`, not fake these interfaces.
- The policy is enforced by review and `PublicAPI.Unshipped.txt`, not by
  sealing (interfaces cannot be sealed).
