# Wslc.Testcontainers

A monorepo for ephemeral, isolated **WSL containers** in integration tests — a
Testcontainers-style API built directly on the official
[`Microsoft.WSL.Containers`](https://www.nuget.org/packages/Microsoft.WSL.Containers) runtime.
No Docker daemon required.

Two peer implementations share the same concepts, defaults, environment variables and on-disk
layout:

| Implementation | Folder | Guide | Artifacts |
| --- | --- | --- | --- |
| C# / .NET 8/9/10 | [`csharp/`](csharp/) | [`csharp/README.md`](csharp/README.md) | NuGet: `Wslc.Testcontainers` (+ `Modules.PostgreSql`, `Modules.Redis`) |
| C++23 (native `wslcsdk`) | [`cpp/`](cpp/) | [`cpp/README.md`](cpp/README.md) | CMake static libraries `wslc`, `wslc_postgresql`, `wslc_redis` |

```csharp
using Wslc.Testcontainers.Modules.PostgreSql;

await using var postgres = new PostgreSqlBuilder().WithPassword("secret").Build();
await postgres.Start();
var connectionString = postgres.GetConnectionString();
```

```cpp
#include <wslc/modules/postgresql.hpp>

wslc::modules::PostgreSqlBuilder builder;
auto postgres = builder.WithPassword("secret").Build();
postgres.Start();
const std::string connectionString = postgres.GetConnectionString();
```

> WSLC never runs the image's ENTRYPOINT/CMD automatically: without a configured command only a
> keep-alive shell starts, so a readiness wait for the image's service always times out.
> Declare the service command explicitly or use a module builder.

> **Security: defaults are for *trusted* test dependencies, not hostile code.**
> Out of the box both libraries (and the underlying WSL container runtime) provide **no security
> boundary**: containers run bridged with full egress, processes run as the image default user
> (usually root) with no seccomp/capability/user-namespace controls, and every host-directory
> volume is a write path onto the Windows machine. Do **not** run untrusted or agent-generated
> code with default settings. `WithNetworkingMode(None)` (C#) /
> `WithNetworkingMode(ContainerNetworkMode::None)` (C++) is the containment mode that works.
>
> **Egress allowlisting is intentionally not provided.** On the current WSLC runtime containers
> are not granted `CAP_NET_ADMIN`, so an in-container `iptables` policy can never be installed,
> and the SDK exposes no outbound policy API. Removing the NIC is the only enforcement the
> runtime honors; proper egress policy needs runtime-side support.

## Requirements

- Windows 10 2004+ or Windows 11 (x64 / ARM64)
- WSL **2.9.3 or newer** with container support: `wsl --install` (or `wsl --update`)
- C#: .NET 8, 9 or 10 on Windows
- C++: Visual Studio 2022 17.10+ (C++23) and CMake 3.25+

## Repository layout

```
csharp/   .NET solution, sources, tests, examples, packaging scripts
cpp/      CMake project, native sources, modules, GoogleTest suite, examples
docs/     shared usage/operations documentation and ADRs
```

Each language folder owns its build files (`csharp/Directory.Build.*`, `cpp/CMakeLists.txt`);
shared operational docs live in [`docs/`](docs/) and architecture decisions in
[`docs/adr/`](docs/adr/).

## Development

```powershell
# C#
dotnet build csharp/Wslc.Testcontainers.slnx
dotnet test --solution csharp/Wslc.Testcontainers.slnx

# C++
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

Real-runtime tests (pull public images) run with `WSLC_RUN_INTEGRATION=1` in both languages.
See the per-language READMEs for details.

## License

[MIT](LICENSE)
