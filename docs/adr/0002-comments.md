# ADR-0002: Commenting rules and styles

Status: Accepted

## Context

The codebase mixes public XML docs, inline `//` comments, and numeric
tuning values (timeouts, caps, chunk sizes). Without shared style, comments
either restate the code or force readers to leave the file to understand it.

## Decision

1. Comment the why, not the what.
   - `///` XML docs describe public API and overall abstractions.
   - `//` explains the current snippet: why this branch exists, what
     invariant holds, what a value derives from. Never restate the code.
   - Internal helpers get `//` only when the reason is non-obvious
     (WinRT quirks, ownership transfer, fast paths, best-effort swallows).

2. Style.
   - Leading comments above the code, full sentences, one idea per block.
   - Keep trailing `//` for short bounds only when it fits on the line;
     otherwise use a leading block.
   - When a snippet exists because of another ADR, cite it as
     `per ADR <slug>` and still explain the snippet.

3. Magic numbers must be explained by comment.
   - No bare literals for limits, timeouts, sizes, or counts. Introduce a
     named `const` and add a comment showing the derivation.
   - The comment states the math and the unit, e.g. what duration and delay
     produce the attempt count, or what byte cap produces the chunk count.

Good:

```csharp
// Per ADR bounded loops we don't allow while(true): poll the inspect payload
// with a fixed attempt count. 200 attempts cover 10s @ 50ms delay.
const int MaxAttempts = 200;
```

```csharp
// Copy container stdout in fixed chunks up to a total cap.
// 1 GiB / 64 KiB + 1 bounds both bytes and iterations.
const ulong MaxBytes = 1024u * 1024u * 1024u;
const uint ChunkSize = 64 * 1024;
const int MaxChunks = (int)(MaxBytes / ChunkSize) + 1;
```

Bad:

```csharp
const int MaxAttempts = 200; // retry
```

```csharp
// NASA #2
while (true) { ... }
```

## Consequences

- New tuning values require a named constant plus a derivation comment.
- `grep NASA` must stay empty in `src/`; rule references use `per ADR <slug>`.
- Review rejects what-comments and unexplained literals.
