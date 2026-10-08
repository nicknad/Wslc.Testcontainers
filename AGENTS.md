# Project Conventions

## Layout

- `csharp/` — .NET solution (sources, tests, examples, packaging). Build files
  (`Directory.Build.props/targets`, `Directory.Packages.props`, `global.json`) live here.
- `cpp/` — CMake/C++23 port. `include/wslc` public surface, `src/` implementation,
  `modules/` typed modules, `tests/` GoogleTest.
- `docs/` — shared operational docs; `docs/adr/` — decisions applying to both
  implementations (bounded loops, comments, strict builds, best-effort cleanup).
- Root CI builds and tests both languages.

## Ponytail

Before writing code, stop at the first rung that holds:

1. Does this need to exist at all? Speculative need = skip it, say so in one line. (YAGNI)
2. Already in this codebase? Reuse the helper/util/type/pattern. Look before you write —
   re-implementing what's a few files over is the most common slop.
3. Stdlib does it? Use it.
4. Native platform feature covers it? `<input type="date">` over a picker lib, CSS over JS,
   DB constraint over app code.
5. Already-installed dependency solves it? Use it. Never add a new one for what a few
   lines can do.
6. Can it be one line? One line.
7. Only then: the minimum code that works.

## C++

- Microsoft style: PascalCase types/functions, camelCase params/locals, `m_` members,
  `c_` file-scope constants, Allman braces. Enforced by `cpp/.clang-format`.
- Strict build: `/W4 /WX /permissive- /utf-8 /EHsc`. No warning suppressions without a
  comment explaining why.
- Analysis baseline: clang-tidy (`.clang-tidy`, warnings are errors), ASan
  (`-DWSLC_ENABLE_SANITIZERS=ON`), MSVC `/analyze` (`-DWSLC_ENABLE_CODE_ANALYSIS=ON`).
  Keep them clean.
- Windows header order: `winsock2.h` before `windows.h`; `windows.h` before
  `bcrypt.h`/`wslcsdk.h`. `.clang-format` keeps `SortIncludes: Never`.
- Public headers document the consumer API with `<summary>` XML comments.
  Implementation comments explain the current snippet/abstraction, not history.

## API design / maintainability

- Minimize public surface. Public API is a long-term compatibility commitment; prefer
  reusing existing public concepts over near-duplicate types.
- Optimize for the call site: judge quality from the consumer's view. Simple call sites
  are worth a more complex declaration.
- Progressive disclosure: the common case is immediately understandable; advanced
  capability exists without complicating basic use.
- Design the consumer API first — names, required args, defaults, returns, failure
  behavior — from the caller's perspective.
- Minimize dependencies: don't add one for convenience when a little local code is
  simpler. Weigh transitive count, supply chain, size, startup cost, compatibility.
- Expose capabilities, not implementation. No concrete types/state/details when a
  smaller capability suffices; prefer opaque representations that preserve encapsulation.
- Make expensive operations explicit: I/O, network, synchronization, heavy computation,
  allocation must not look like cheap property access. Prefer methods or documented semantics.
- Errors are part of the API: types, codes, exceptions/results, messages, failure
  semantics. Consumers must distinguish expected/recoverable from programmer/config/system
  failures.
- Avoid weakly typed boundaries: domain types/enums/structured data over strings, numbers,
  dicts, loose JSON. No wrapper type just for typing when a primitive is genuinely right.
- Semantic consistency: similar concepts share naming, parameter order, nullability,
  lifetime/ownership, error and async behavior.
- No surprising behavior: name and signature imply the behavior. Scrutinize hidden I/O,
  mutation, global state, blocking, retries, network calls, cost.
- Preserve room to evolve: don't expose details or overly specific types that constrain
  future implementations.
- Fewer concepts: before adding a public abstraction, check whether an existing type,
  interface, option, overload, or composition already expresses it.
- Discoverability: the primary path should be understandable from names, signatures,
  docs, and types — without reading the implementation.
- Don't optimize for declaration elegance alone: complexity is fine when it buys a
  simpler, safer consumer experience, and not otherwise.

## C# / .NET

- Minimize public types and members: classes, interfaces, records, enums, overloads,
  extension methods, properties.
- Prefer strong .NET types where semantics matter: `Uri`, `DateTimeOffset`, `TimeSpan`,
  `Guid`, enums, value objects, `CancellationToken`, structured options. Don't wrap
  `string`/`int` without a concrete API benefit.
- Nullable annotations are contract: make them match runtime behavior.
- Avoid ambiguous overload sets; prefer options objects when parameters grow or
  combinations multiply.
- Async semantics explicit: `Task`/`ValueTask`-returning, consistently named, no
  sync-over-async, accept `CancellationToken` where cancellation is meaningful.
- No expensive work behind properties: a property is cheap state access.
- Exceptions are API surface: document meaningful ones; don't make consumers parse
  messages; prefer structured failure info where callers need programmatic discrimination.
- Don't leak implementation dependencies: EF Core, database-specific, internal
  infrastructure, framework, or third-party types unless intentionally part of the contract.
- Collections: expose the least powerful abstraction needed; never return mutable
  internals. Choose `IReadOnlyCollection`/`IReadOnlyList`/`IEnumerable`/`IAsyncEnumerable`
  by actual semantics.
- Binary/source compatibility: review removals, signature changes, sealing/unsealing,
  virtual members, interface changes.
- No interfaces for convention or hypothetical mocking — only meaningful capability or
  abstraction boundaries.
- Extension methods sparingly: they fragment discoverability and add import requirements.
- Keep options cohesive: no huge parameter lists or dozens of loosely related properties;
  group by concept.
- Explicit ownership/lifetime for `IDisposable`/`IAsyncDisposable`: who owns and disposes
  a returned resource must be unambiguous.
