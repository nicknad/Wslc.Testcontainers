# Review ledger — open work

Remaining findings only. Completed work lives in git history: the F1-F27/API/migration
series through `8915340`, Review 2.0 slop (REV2-R5 `TakeLast`/`JoinLast`, REV2-CPP-FMT
endpoint helpers, REV2-R6-CHECK) in `129cda4`, and the Review 3.0 bug/dedup batch on
`chore/slop-cleanup-2.0` (reuse-lock release on stop, `CopyFrom` write check, detached
start-thread handle lifetime, timeout parse guards, metadata-write visibility,
`PortMapping` locking, IPv6 bracketing, shared `PercentEncode`/format helpers, 1 GiB
constant, duplicate validation passes, argv/env marshalling, CI-variable list,
`BuilderLimits` helper, registry prune disposal). The 0.3.2 release also fixed the
exit-time `CleanupRegistry` use-after-free (`container_host.cpp`, found by ASan during
the 0.3.1 release gate). Do not re-litigate those.

Statuses: **OPEN** (reproduced, not fixed), **UNVERIFIED** (needs reproduction),
**PARTIAL** (some of the item landed), **PROCESS** (planning/observability gap),
**ACCEPTED** (recorded residual, no action planned).

Baseline at 2026-10-08 (post-Review-3.0): C# 265 unit tests pass (5 environment skips)
plus 25 module tests pass (15 integration skips); C++ `ctest` 218/218 (26 integration
and reparse skips); clang-format clean. The ASan job runs in CI (locally the ASan
binary needs the runtime DLL).

---

## Review 3.0 — remaining

| ID | Finding | Status | Notes |
|----|---------|--------|-------|
| R3-1 | C# `CleanupSynchronously` diverges from `StopCoreAsync`: 2s vs 10s SIGTERM grace, does not drain `_processes` or clear `_network`, separate `UpdateState` | OPEN | `WslContainer.cs:297-339` vs `:800-834`; unify or document the exit-hook differences |
| R3-2 | C++ `ReadTextFile` reads `wslc.json` with no size cap | OPEN | `instance_store.cpp:23-34`; bound it like the other caps |
| R3-3 | C# sync-over-async on the `StartProcess` failure path | OPEN | `WslContainer.cs:158`; benign today, blocks if dispose ever becomes truly async |
| R3-4 | `WslInstanceMetadata.Owner` is write-only | OPEN | set at `WslContainer.cs:427`, never read; drop or surface |
| R3-5 | `"wslc.lock"` and lifecycle state strings duplicated | OPEN | `WslContainer.cs:445` / `WslResourceReaper.cs:169`; `"Creating"/"Running"/"Stopped"` literals |
| R3-6 | C# `WslcHost` fetches version and install-error text twice | OPEN | `WslcHost.cs:16-25` vs `:53-65` |
| R3-7 | Dead branches/parameters: `CleanupAsync(throwOnError: true)` unreachable (`:226/:383` → `:794`); `StopCoreAsync` CT and C++ `StopLocked` token unused; `WslPlatform` revision fallback | OPEN | former R9/R13 |
| R3-8 | Test-only seams in production types: `WslContainer` internal ctor/`Configuration`; reaper store overloads | OPEN | former R10 residual |
| R3-9 | `EnsureStorageAndMetadata` ignores `create_directories` failure but marks storage created | OPEN | `wsl_container.cpp:335-337`; mirror C# which throws |
| R3-10 | God classes: `WslContainer.cs` (~1026 lines), `wsl_container.cpp` (~1476 lines) | OPEN | former M12/REV2-M12; decide before 1.0 |
| R3-11 | MariaDB/PostgreSQL builders near-clones; Redis/Valkey clones (both languages) | OPEN | ADR-0006 covers containers; builder duplication remains |
| R3-12 | C# MariaDB connection string stays hand-rolled (`Server=`/`User ID=`) | ACCEPTED | genuine format difference from `FormatConnectionString` (`Host=`/`Username=`) |
| R3-13 | C++ test structure not mirrored to the C# folder layout | OPEN | former REV2-TEST-STRUCT |
| R3-14 | No SBOM/dependency-update coverage for C++ gtest | OPEN | former REV2-SBOM/N5 |
| R3-15 | `IntegrationFactAttribute` duplicated across test projects; `mklink /J` helper duplicated | OPEN | `Support/IntegrationFactAttribute.cs` ×2; `HostFileTests.cs:131-151`, `WslInstanceStoreTests.cs:149-163` |
| R3-16 | Seven module tests copy-paste HttpClient/CancellationTokenSource setup | OPEN | ClickHouse/Elasticsearch/Keycloak/MailPit/Qdrant/Vault/WireMock |
| R3-17 | Redis is the only module with no test in either language | OPEN | at least a builder/connection-string test |
| R3-18 | Several wait-strategy tests assert only "did not throw" | OPEN | add probe/handler call counts |
| R3-19 | Fixed 3s log-drain windows in integration tests | OPEN | can truncate on slow CI |

## Carried over — unverified

| ID | Finding | Status |
|----|---------|--------|
| M2 | Argument vs runtime exception taxonomy; platform exception nesting | UNVERIFIED |
| M8 | TimeSpan/ms, dict/map, Version parity beyond config-hash goldens | PARTIAL |
| M9 | Reversible builders; `RequireNetwork` concept | PARTIAL |
| M14 | Catch audit; re-enable disabled clang-tidy checks | UNVERIFIED |
| M15 | Module constants; Doxygen casing; `reserve`; `WSLC_*` env validation | UNVERIFIED |
| M16 | Tarball image-name and volume-name regex validation | PARTIAL |
| M17 | Inspect JSON hardening; log auth docs; reuse locking/jitter/TTL; PID checks; version/HRESULT centralization | UNVERIFIED |
| M18 | README/docs updates; version banner in `CHANGELOG.md` | PARTIAL |
| L1 | Interpolate duplicate-port values | UNVERIFIED |
| L2 | Fixture gate + `Describe`/`GetRecentLogs` sample | UNVERIFIED |
| L3 | `.editorconfig` max_line_length; warnings; split e2e | UNVERIFIED |
| L4 | Shared `FormatMilliseconds`; parity tests; casing; digest/Stop notes | UNVERIFIED |
| L5 | Accepted-risk docs (RW volumes, LAN binds, secrets, tags) | UNVERIFIED |
| R6 | Five near-identical C++ RAII handle wrappers | OPEN |
| R7 | `WaitForExit` vs `WaitForExitFor` duplicate wait machinery | OPEN |
| R11 | Other C++ dead members (symbol-level confirmation) | UNVERIFIED |
| R12 | Example dead code (`CustomerService`, `DbConnectionProvider`, `PostgresFixture`) | UNVERIFIED |
| R14 | Broadcaster `m_hasSnapshot` duplicate state | UNVERIFIED |
| N1 | `dotnet test <slnx>` runs zero tests (exit 5) while per-project works | OPEN |
| N6 | No threat model, PR slicing, or effort/owner sizing | PROCESS |
| N7 | Present-but-absurd `createdAt` (e.g. 1970) trips the PID-recycle check | OPEN |
| N8 | A process registered after the final `TakeAll` during a disposing stop is never drained | OPEN |
| N9 | Version-lockstep gate ignores the tag version | OPEN |

## Accepted risks (no action planned)

- F5/F6: reparse points in **ancestor** path components are accepted by both languages.
- F1: custom checks that ignore the stop token can still overshoot their deadline.
- F13: a cancelled `CopyFrom` can take up to the ~4s abort grace to return.
- F16: containment is lexical only (a junction inside `instances` can still redirect at open).
- F18: files+volumes can total 128; tarball size is checked at build time (TOCTOU).
- F22: C++ accepts numerically integral floats (`4514.0`) because the JSON parser stores doubles (unreachable from the SDK's string `HostPort`).
- F23: C++ env-name validation depends on the C locale (host `setlocale` could diverge).
- F27: a pathological header can push `Describe()` past 64 KiB; the global cap is defense-in-depth.
- M11: internal guard-type names (`WslcHost`, `RuntimeGuard`/`LifecycleTracker`) are unchanged.
