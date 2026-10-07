# Wslc.Testcontainers (C++)

Ephemeral, isolated **WSL containers** for C++ integration tests — a Testcontainers-style API
built directly on the native [`Microsoft.WSL.Containers`](https://www.nuget.org/packages/Microsoft.WSL.Containers)
SDK (`wslcsdk.h`). No Docker daemon required. This is the C++ port of the
[C# library](../csharp/README.md); both implementations share the same concepts, defaults,
environment variables and on-disk layout.

```cpp
#include <wslc/wslc.hpp>

wslc::WslContainerBuilder builder;
auto postgres = builder.WithImage("docker.io/library/postgres:15-alpine")
                    .WithPort(5432)
                    .WithWaitStrategy(wslc::waiting::ForWsl()
                                          .UntilTcpPortIsOpen(5432)
                                          ->And(wslc::waiting::ForWsl()
                                                    .UntilMessageIsLogged("database system is ready to accept connections", 2)))
                    .WithCommand("/usr/local/bin/docker-entrypoint.sh", {"postgres"})
                    .Build();

postgres.Start();
const auto result = postgres.Exec("/bin/sh", {"-c", "psql -U postgres -c 'SELECT 1'"});
```

> WSLC never runs the image's ENTRYPOINT/CMD automatically: without `WithCommand(...)` only a
> keep-alive shell starts, so a readiness wait for the image's service always times out.
> Declare the service command explicitly or use a module builder.

> **Security: defaults are for *trusted* test dependencies, not hostile code.** The underlying
> WSL container runtime provides no security boundary by default (bridged networking with full
> egress, image default user, host write paths for volumes). See the
> [shared security notes](../README.md) and `WithNetworkingMode(ContainerNetworkMode::Isolated)`.

## Requirements

- Windows 10 2004+ or Windows 11 (x64 / ARM64)
- WSL **2.9.3 or newer** with container support (`wsl --install` / `wsl --update`)
- Visual Studio 2022 17.10+ with the C++ workload (C++23), CMake 3.25+, and the
  `Microsoft.WSL.Containers` SDK

## Build

The SDK ships in the `Microsoft.WSL.Containers` NuGet package (`include/wslcsdk.h`,
`runtimes/win-*/wslcsdk.lib|.dll`, and a CMake config). CMake finds it in the NuGet cache
automatically; on a clean machine fetch it explicitly:

```powershell
./cpp/scripts/Install-WslcSdk.ps1 -Destination cpp/.packages
cmake -S cpp -B cpp/build -DWSLC_SDK_ROOT="$PWD/cpp/.packages/Microsoft.WSL.Containers.3.0.1"
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

Integration tests pull public images and need a real runtime; they are skipped unless enabled:

```powershell
$env:WSLC_RUN_INTEGRATION = "1"
ctest --test-dir cpp/build -C Release -R "Integration" --output-on-failure
```

### Build options

| Option | Default | Purpose |
| --- | --- | --- |
| `WSLC_BUILD_TESTS` | `ON` | Build the GoogleTest suite. |
| `WSLC_BUILD_EXAMPLES` | `ON` | Build `wslc_quickstart` and `wslc_postgres`. |
| `WSLC_ENABLE_CLANG_TIDY` | `OFF` | Run `clang-tidy` (uses `cpp/.clang-tidy`; warnings are errors). Requires a Ninja generator. |
| `WSLC_ENABLE_SANITIZERS` | `OFF` | MSVC AddressSanitizer (Clang: ASan + UBSan) for tests. |
| `WSLC_ENABLE_CODE_ANALYSIS` | `OFF` | MSVC `/analyze` while compiling. |

The build is strict by default: `/W4 /WX /permissive- /utf-8 /EHsc`, plus
`/external:anglebrackets /external:W0` so SDK/STL headers do not break the build.

### Analysis

The inner loop is the compiler and sanitizer: build with the strict flags, and run debug
tests with `-DWSLC_ENABLE_SANITIZERS=ON`. clang-tidy is opt-in and runs against a Ninja
compile database; `cpp/.clangd` points editors at `cpp/build/compile_commands.json` for
on-open/on-save diagnostics.

```powershell
# One-time Ninja configure; exports compile_commands.json for editors and clang-tidy
cmake -S cpp -B cpp/build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Only translation units touched since a revision (fast, pre-commit friendly)
./cpp/scripts/Run-ClangTidy.ps1 -Changed                 # vs HEAD
./cpp/scripts/Run-ClangTidy.ps1 -Changed -Base origin/main

# Whole project against the fast baseline in .clang-tidy
./cpp/scripts/Run-ClangTidy.ps1 -All

# Diff-only pre-commit hook (skipped when cpp/build is not configured)
./cpp/scripts/Install-GitHooks.ps1
```

`Run-ClangTidy.ps1` uses LLVM's `run-clang-tidy` (needs Python) for parallel analysis and
falls back to serial `clang-tidy` without it. Changed headers pull in the sources that
include them, so header diagnostics are still reported.

CI runs `Run-ClangTidy.ps1 -Changed` on pull requests (zero warnings, `WarningsAsErrors: "*"`),
and the scheduled `analyze` workflow runs the whole project with `cpp/.clang-tidy-analyzer`,
which adds the path-sensitive `clang-analyzer-*` family that is too slow for the PR loop.

## Core API

The C++ port mirrors the C# surface with Microsoft C++ naming (PascalCase methods and types,
`m_` members, `c_` file-scope constants). The API is **synchronous and blocking**; cancellation
uses `std::stop_token` instead of `CancellationToken`, and process handles are RAII-owned.

| C# | C++ |
| --- | --- |
| `new WslContainerBuilder()` | `wslc::WslContainerBuilder builder;` |
| `WithImage` / `FromTarball` / `WithCommand` / `WithWorkingDirectory` | same names |
| `WithEnvironment(name, value)` / `WithEnvironmentVariables(dict)` | `WithEnvironment(name, value)` / `WithEnvironmentVariables(map)` |
| `WithPort(port)` / `WithPort(port, bindAddress)` | same names |
| `WithWaitStrategy(strategy)` | `WithWaitStrategy(std::shared_ptr<IWaitStrategy>)` |
| `WithFile` / `WithVolume` / `WithScratchVolume` | same names |
| `WithNetworkingMode` / `WithCpuCount` / `WithMemoryMegabytes` | same names |
| `WithReuse` / `WithReadinessTimeout` | same names |
| `Build()` → `WslContainer` | `Build()` → `wslc::WslContainer` (move-only) |
| `StartAsync` / `StopAsync` / `DisposeAsync` | `Start(token)` / `Stop(token)` / `Dispose()` |
| `ExecAsync(command, args, options, token)` | `Exec(command, args, options, token)` |
| `StartProcess(...)` → `IWslProcess` | `StartProcess(...)` → `std::unique_ptr<IWslProcess>` |
| `CopyToAsync` / `CopyFromAsync` | `CopyTo` / `CopyFrom` |
| `SubscribeLogs(token)` → `IAsyncEnumerable<LogLine>` | `SubscribeLogs()` → `LogStream` (blocking `Next(token)`) |
| `GetRecentLogs(maxLines)` | `GetRecentLogs(maxLines)` |
| `Wait.ForWsl()` | `wslc::waiting::ForWsl()` |
| `strategy.And(other)` | `strategy->And(other)` |
| `Wait.ForWsl().Until(name, condition)` | `ForWsl().Until(name, std::function<bool(IWaitTarget&, std::stop_token)>)` |
| `PostgreSqlBuilder` / `RedisBuilder` / `ValkeyBuilder` / `MariaDbBuilder` / `RabbitMqBuilder` / `MongoDbBuilder` / `NatsBuilder` / `MailPitBuilder` / `RustFsBuilder` / `WireMockBuilder` / `QdrantBuilder` / `ClickHouseBuilder` / `VaultBuilder` | `wslc::modules::PostgreSqlBuilder` / `RedisBuilder` / `ValkeyBuilder` / `MariaDbBuilder` / `RabbitMqBuilder` / `MongoDbBuilder` / `NatsBuilder` / `MailPitBuilder` / `RustFsBuilder` / `WireMockBuilder` / `QdrantBuilder` / `ClickHouseBuilder` / `VaultBuilder` |

Behavioral notes:

- `WslContainer` is move-only; its destructor performs the same cleanup as `Dispose()`.
- `IWslProcess` is owned by the caller; destroying it terminates a still-running process.
- `LogStream::Next` blocks until a line arrives, the stream completes, or the stop token fires
  (then it returns `std::nullopt`). Let the object go out of scope to release the subscription.
- Exceptions mirror the C# hierarchy: `WslException`, `WslRuntimeException`,
  `WslProvisioningException`, `WslProcessException`, `WslTimeoutException`,
  `WslReadinessException` (with `Describe()`), `WslNetworkException`, `WslCleanupException`,
  plus `PlatformNotSupportedException` and `OperationCanceledException`.

## Modules

Typed module builders live in `cpp/modules/`:

```cpp
#include <wslc/modules/clickhouse.hpp>
#include <wslc/modules/mailpit.hpp>
#include <wslc/modules/mariadb.hpp>
#include <wslc/modules/mongodb.hpp>
#include <wslc/modules/nats.hpp>
#include <wslc/modules/postgresql.hpp>
#include <wslc/modules/qdrant.hpp>
#include <wslc/modules/rabbitmq.hpp>
#include <wslc/modules/redis.hpp>
#include <wslc/modules/rustfs.hpp>
#include <wslc/modules/valkey.hpp>
#include <wslc/modules/wiremock.hpp>
#include <wslc/modules/vault.hpp>

wslc::modules::PostgreSqlBuilder postgresBuilder;
auto postgres = postgresBuilder.WithPassword("secret").Build();
postgres.Start();
const std::string npgsql = postgres.GetConnectionString();

wslc::modules::RedisBuilder redisBuilder;
auto redis = redisBuilder.Build();
redis.Start();
const std::string endpoint = redis.GetEndpoint();

wslc::modules::ValkeyBuilder valkeyBuilder;
auto valkey = valkeyBuilder.Build();
valkey.Start();
const std::string valkeyEndpoint = valkey.GetEndpoint();

wslc::modules::MariaDbBuilder mariadbBuilder;
auto mariadb = mariadbBuilder.WithPassword("secret").Build();
mariadb.Start();
const std::string mysql = mariadb.GetConnectionString();

wslc::modules::RabbitMqBuilder rabbitMqBuilder;
auto rabbitmq = rabbitMqBuilder.Build();
rabbitmq.Start();
const std::string amqp = rabbitmq.GetConnectionString();

wslc::modules::MongoDbBuilder mongoBuilder;
auto mongodb = mongoBuilder.Build();
mongodb.Start();
const std::string mongo = mongodb.GetConnectionString();

wslc::modules::NatsBuilder natsBuilder;
natsBuilder.WithJetStream();
auto nats = natsBuilder.Build();
nats.Start();
const std::string natsUrl = nats.GetConnectionString();

wslc::modules::MailPitBuilder mailPitBuilder;
auto mailpit = mailPitBuilder.Build();
mailpit.Start();
const std::string smtp = mailpit.GetSmtpEndpoint();
const std::string mailUi = mailpit.GetHttpEndpoint();

wslc::modules::RustFsBuilder rustFsBuilder;
auto rustfs = rustFsBuilder.Build();
rustfs.Start();
const std::string s3 = rustfs.GetEndpoint();

wslc::modules::WireMockBuilder wireMockBuilder;
auto wiremock = wireMockBuilder.Build();
wiremock.Start();
const std::string stubs = wiremock.GetEndpoint();
wslc::modules::QdrantBuilder qdrantBuilder;
auto qdrant = qdrantBuilder.Build();
qdrant.Start();
const std::string qdrantHttp = qdrant.GetEndpoint(); // gRPC via GetConnectEndpoint(QdrantContainer::GrpcPort)
wslc::modules::ClickHouseBuilder clickHouseBuilder;
auto clickhouse = clickHouseBuilder.Build();
clickhouse.Start();
const std::string clickHouseClient = clickhouse.GetConnectionString();
wslc::modules::VaultBuilder vaultBuilder;
auto vault = vaultBuilder.Build();
vault.Start();
const std::string vaultAddress = vault.GetAddress();
const std::string vaultRootToken = vault.RootToken();
```

## Configuration

The same environment variables as the C# library apply: `WSLC_TIMEOUT`, `WSLC_DATA_DIRECTORY`,
`WSLC_DEFAULT_IMAGE`, `WSLC_REUSE`, `WSLC_CLEANUP`, `WSLC_REUSE_IN_CI`, `WSLC_SESSION_ID`.
Inside every container: `WSLC_SESSION_ID`, `WSLC_INSTANCE_ID`, `WSLC_CREATED_AT`,
`WSLC_OWNER_PID`. Instance metadata (`wslc.json`) and the session VHD layout are compatible
between the C# and C++ implementations, so a reused instance created by one can be picked up by
the other.

## Analysis

- `.clang-format` — Microsoft style (Allman braces, 4-space indent, PascalCase API).
- `.clang-tidy` — bugprone/performance/modernize/readability/Core Guidelines with opinionated
  checks disabled; `WarningsAsErrors: "*"`.
- AddressSanitizer via `-DWSLC_ENABLE_SANITIZERS=ON` (plus UBSan when building with Clang).
- MSVC `/analyze` via `-DWSLC_ENABLE_CODE_ANALYSIS=ON`.
- CI runs format, clang-tidy, unit, and ASan jobs (`.github/workflows/ci.yml`).

## License

[MIT](../LICENSE)
