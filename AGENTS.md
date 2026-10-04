# Project Conventions

## Repository layout

- `csharp/` — the .NET solution (sources, tests, examples, packaging). Build files
  (`Directory.Build.props/targets`, `Directory.Packages.props`, `global.json`) live here.
- `cpp/` — the CMake/C++20 port. `include/wslc` is the public surface, `src/` the
  implementation, `modules/` the typed modules, `tests/` the GoogleTest suite.
- `docs/` — shared operational documentation; `docs/adr/` — architecture decisions that apply
  to both implementations (bounded loops, comments, strict builds, best-effort cleanup).
- Root CI workflows build and test both languages.

## C++ conventions

- Microsoft C++ style: PascalCase types and functions, camelCase parameters/locals, `m_` class
  members, `c_` file-scope constants, Allman braces. Formatting is enforced by `cpp/.clang-format`.
- The build is strict: `/W4 /WX /permissive- /utf-8 /EHsc`; do not add warning suppressions
  without a comment explaining why.
- clang-tidy (`.clang-tidy`, warnings are errors), AddressSanitizer
  (`-DWSLC_ENABLE_SANITIZERS=ON`), and MSVC `/analyze` (`-DWSLC_ENABLE_CODE_ANALYSIS=ON`) are
  part of the analysis baseline. Keep them clean.
- Windows header ordering matters (`winsock2.h` before `windows.h`; `windows.h` before
  `bcrypt.h`/`wslcsdk.h`); `.clang-format` must keep `SortIncludes: Never`.
- Public headers document the consumer API with `<summary>` XML comments; implementation
  comments explain the current snippet or abstraction, not history.

## API Design / Maintainability

- **Minimize public API surface:**
  Prefer the smallest set of public types, methods, properties, and concepts needed by consumers.
  Treat public API as a long-term compatibility commitment.
  Prefer reusing existing public concepts over introducing near-duplicate types.

- **Optimize for the call site:**
  Judge API quality primarily from the consumer's perspective.
  Prefer simple, obvious, discoverable call sites even if the underlying declaration/implementation is more complex.

- **Progressive disclosure:**
  The common/simple use case should be immediately understandable.
  Advanced capabilities should be available without making basic usage complicated.

- **Design the consumer API first:**
  Define how the API should look and behave for its consumers before designing its implementation.
  Review names, required arguments, defaults, return values, and failure behavior from the caller's perspective.

- **Minimize dependencies:**
  Avoid introducing dependencies solely for convenience when a small amount of local implementation is simpler.
  Consider transitive dependency count, supply-chain exposure, binary/package size, startup cost, and compatibility.

- **Expose capabilities, not implementation:**
  Do not expose concrete types, state, or implementation details when consumers only require a smaller capability.
  Prefer abstractions/opaque representations where they preserve useful encapsulation.

- **Make expensive operations explicit:**
  Operations involving I/O, network access, synchronization, significant computation, allocation, or other potentially expensive work should not look like cheap field/property access.
  Prefer methods or clearly documented semantics.

- **Treat errors as part of the API:**
  Error types, error codes, exceptions/results, messages, and failure semantics are part of the contract.
  Consumers should be able to distinguish expected/recoverable failures from programmer/configuration/system failures.

- **Avoid weakly typed API boundaries:**
  Prefer domain-specific types/enums/structured data over arbitrary strings, numbers, dictionaries, or loosely structured JSON when the value has defined semantics.
  Do not introduce a wrapper type merely for the sake of typing when a primitive is genuinely appropriate.

- **Prefer semantic consistency:**
  Similar concepts should have consistent naming, parameter ordering, nullability, lifetime/ownership semantics, error behavior, and async behavior across the API.

- **Avoid surprising behavior:**
  A method/property should behave according to what its name and signature imply.
  Hidden I/O, mutation, global state, blocking, retries, network calls, or expensive computation deserve particular scrutiny.

- **Preserve room for evolution:**
  Avoid exposing implementation details or overly specific types that unnecessarily constrain future implementations.
  Consider whether consumers can be changed without breaking the API.

- **Prefer fewer concepts over more concepts:**
  Before introducing a new public abstraction, check whether an existing type, interface, option, overload, or composition mechanism can express the same behavior clearly.

- **Review discoverability:**
  A consumer should be able to understand the primary API path from names, signatures, documentation, and types without reading the implementation.

- **Do not optimize solely for declaration elegance:**
  A complicated declaration can be acceptable when it produces a substantially simpler and safer consumer experience.
  Conversely, do not accept declaration complexity that provides no meaningful benefit to consumers.

## C# / .NET External API

- **Minimize public types and members:**
  Check whether public classes, interfaces, records, enums, overloads, extension methods, and properties are actually necessary.

- **Prefer strong .NET types:**
  Prefer Uri, DateTimeOffset, TimeSpan, Guid, enums, dedicated value objects, cancellation tokens, and structured options where their semantics matter.
  Do not replace ordinary string/int usage with wrapper types without a concrete API benefit.

- **Use nullable reference types deliberately:**
  Public nullability annotations are part of the API contract.
  Ensure nullable/non-nullable annotations accurately represent runtime behavior.

- **Avoid ambiguous overload sets:**
  Too many overloads can make APIs difficult to discover and can create overload-resolution surprises.
  Prefer options/configuration objects when parameters grow or combinations become complex.

- **Make async semantics explicit:**
  Async I/O should generally be exposed as Task/ValueTask-returning methods and named consistently.
  Avoid APIs that synchronously block on asynchronous work.
  Accept CancellationToken where cancellation is meaningful.

- **Do not hide expensive work behind properties:**
  A property should generally represent cheap state access.
  Flag properties that perform I/O, database access, network calls, blocking, or significant computation.

- **Treat exceptions as API surface:**
  Document meaningful exceptions and avoid requiring consumers to parse exception messages.
  Prefer structured failure information where callers need programmatic discrimination.

- **Avoid leaking implementation dependencies:**
  Do not expose EF Core types, database-specific types, internal infrastructure types, framework-specific implementation details, or third-party types unless they are intentionally part of the API contract.

- **Be careful with collections:**
  Expose the least powerful collection abstraction required.
  Avoid returning mutable internal collections.
  Consider IReadOnlyCollection/IReadOnlyList/IEnumerable/IAsyncEnumerable based on actual semantics.

- **Consider binary/source compatibility:**
  Public APIs should be reviewed for compatibility implications of removing members, changing signatures, sealing/unsealing types, changing virtual members, and modifying interfaces.

- **Avoid unnecessary interfaces:**
  Do not introduce an interface solely because it is conventional or for hypothetical mocking.
  Interfaces should represent a meaningful capability or abstraction boundary.

- **Avoid excessive extension methods:**
  Extension methods can fragment discoverability and create namespace/import requirements.
  Use them when they provide a coherent capability over an existing type.

- **Keep options cohesive:**
  Avoid APIs with large parameter lists or dozens of loosely related configuration properties.
  Group configuration according to meaningful concepts.

- **Prefer explicit ownership/lifetime semantics:**
  For IDisposable/IAsyncDisposable resources, make ownership and disposal responsibilities clear.
  Avoid APIs where it is ambiguous who owns or disposes a returned resource.
