# Wslc.Testcontainers

**Ephemeral containers for .NET integration tests on Windows, powered by WSL Containers.**

`Wslc.Testcontainers` provides a Testcontainers-style API for starting disposable service containers
directly through Microsoft's [`Microsoft.WSL.Containers`](https://www.nuget.org/packages/Microsoft.WSL.Containers)
API.

The goal is simple:

> **Make container-based integration testing on Windows require less infrastructure.**

Instead of communicating with a Docker Engine, the library uses the WSL Containers runtime directly.

```csharp
using Wslc.Testcontainers.Modules.PostgreSql;

await using var postgres = new PostgreSqlBuilder()
    .WithPassword("secret")
    .Build();

await postgres.StartAsync();

var connectionString = postgres.GetConnectionString();
```

The container is created for the test and disposed when the test finishes.

## Why?

Testcontainers has become a useful abstraction for integration testing: tests can provision their own
PostgreSQL, Redis, RabbitMQ, and other dependencies instead of relying on shared infrastructure.

On Windows, however, this traditionally means introducing a container runtime and its associated
developer-machine setup.

WSL Containers provides another option.

This project explores what happens when the **Testcontainers programming model is backed directly by
the WSL Containers runtime**.

```text
Traditional container-based testing

.NET tests
    │
    ▼
Testcontainers
    │
    ▼
Docker Engine API
    │
    ▼
Container runtime
```

```text
Wslc.Testcontainers

.NET tests
    │
    ▼
Wslc.Testcontainers
    │
    ▼
Microsoft.WSL.Containers
    │
    ▼
WSL Containers
```

The project is intentionally focused on **ephemeral test infrastructure**, not on replacing
general-purpose container tooling.

## Features

- Testcontainers-style builders and lifecycle
- Ephemeral containers
- Direct integration with WSL Containers
- No Docker Engine dependency
- .NET 8, 9 and 10 support
- Windows x64 and ARM64
- Real-runtime integration tests
- Container networking configuration
- Dynamic or fixed host-port mapping
- Host-directory mounts
- Environment variables and port configuration
- Readiness checks
- Reusable service modules
- NuGet packages for common dependencies

### Available modules

The .NET implementation currently includes modules for:

- PostgreSQL
- Redis
- Valkey
- MariaDB
- RabbitMQ
- MongoDB
- NATS
- Mailpit
- RustFS
- WireMock
- Qdrant
- ClickHouse
- Vault
- Keycloak
- Elasticsearch
- Kafka

Modules are intended to make common integration-test dependencies require minimal test setup.

## Installation

Install the core package:

```bash
dotnet add package Wslc.Testcontainers
```

For PostgreSQL:

```bash
dotnet add package Wslc.Testcontainers.Modules.PostgreSql
```

Then:

```csharp
using Wslc.Testcontainers.Modules.PostgreSql;

await using var postgres = new PostgreSqlBuilder()
    .WithPassword("secret")
    .Build();

await postgres.StartAsync();

var connectionString = postgres.GetConnectionString();
```

See the [`csharp/README.md`](csharp/README.md) for the complete .NET API and configuration reference.

## Requirements

### Windows

- Windows 10 version 2004 or newer, or Windows 11
- x64 or ARM64
- WSL 2.9.3 or newer
- WSL Containers support

Install or update WSL with:

```powershell
wsl --install
```

or:

```powershell
wsl --update
```

### .NET

- .NET 8
- .NET 9
- .NET 10

The library currently targets Windows because it depends on the WSL Containers runtime.

### C++

The native port additionally requires:

- Visual Studio 2022 17.10 or newer (C++23)
- CMake 3.25 or newer

## Important: container lifecycle

WSL Containers does not automatically execute an image's `ENTRYPOINT` or `CMD` in the way a
Docker-based workflow might lead you to expect.

Without an explicit command, a keep-alive shell can start instead of the service you intended to run.

As a result, a service readiness check can wait indefinitely.

**Use a module builder or explicitly configure the service command.**

The built-in modules handle this for their respective services.

## Networking

The library exposes the networking capabilities provided by WSL Containers.

For example:

```csharp
.WithNetworkingMode(ContainerNetworkMode.Isolated)
```

can be used when a test dependency should not have normal network access.

Networking behavior is ultimately constrained by the underlying WSL Containers runtime.

## Security considerations

**These containers should be treated as test infrastructure, not as a security boundary.**

The default configuration is intended for trusted test dependencies.

In particular:

- containers use bridged networking by default
- network egress is available by default
- processes run as the image's configured user, commonly `root`
- the runtime does not currently provide the usual seccomp/capability/user-namespace controls
- host-directory mounts can provide write access to the Windows host
- the library does not provide a mechanism for arbitrary in-container egress allowlisting

Do **not** use the default configuration to execute untrusted or agent-generated code.

For stronger network isolation, use:

```csharp
.WithNetworkingMode(ContainerNetworkMode.Isolated)
```

The security properties of a container are determined by the underlying runtime. This library does
not attempt to manufacture a security boundary that WSLC itself does not provide.

### Why no egress allowlist?

A tempting approach would be to configure `iptables` inside the container.

The current WSLC environment does not provide the necessary `CAP_NET_ADMIN` capability, and the SDK
does not expose a runtime-level outbound policy API.

Consequently, an in-container firewall cannot be relied upon as an enforcement mechanism.

Proper egress policy requires support from the container runtime itself.

See [`docs/`](docs/) for the detailed operational and security documentation.

## Architecture

The repository contains two implementations based on the same concepts.

### C# / .NET

```text
csharp/
├── Wslc.Testcontainers
├── Wslc.Testcontainers.Modules.*
├── tests/
└── examples/
```

The .NET implementation is distributed through NuGet. See [`csharp/README.md`](csharp/README.md)
for the complete API reference.

### C++23

The repository also contains a native implementation using the WSL Containers SDK:

```text
cpp/
├── include/wslc
├── src/
├── modules/
├── tests/
└── examples/
```

The C++ implementation is useful for native applications and for validating the underlying WSLC
integration independently of .NET. See [`cpp/README.md`](cpp/README.md) for the native API.

Both implementations share the same core concepts, defaults, environment variables and on-disk
layout.

## Testing

There are two classes of tests.

### Unit tests

The normal test suite does not require pulling container images.

```bash
dotnet test --solution csharp/Wslc.Testcontainers.slnx
```

For C++:

```bash
cmake -S cpp -B cpp/build
cmake --build cpp/build --config Release
ctest --test-dir cpp/build -C Release --output-on-failure
```

### Real-runtime integration tests

Tests that exercise the actual WSL Containers runtime can be enabled with:

```powershell
$env:WSLC_RUN_INTEGRATION="1"
```

These tests pull public container images and create real WSL containers.

See the language-specific READMEs for the complete integration-test setup.

## Repository structure

```text
.
├── csharp/       .NET implementation, modules, tests and examples
├── cpp/          Native C++23 implementation
├── docs/         Operational documentation and architecture decisions
├── tests/        Shared test fixtures
└── CHANGELOG.md
```

Architecture decisions are documented in [`docs/adr/`](docs/adr/).

## What this project is — and isn't

### This project is

- an integration-testing library
- an alternative backend for ephemeral test dependencies on Windows
- a .NET-friendly API over WSL Containers
- an exploration of using WSLC as test infrastructure

### This project isn't

- a Docker replacement
- a general-purpose container orchestrator
- a production container runtime
- a security sandbox for untrusted workloads
- a compatibility layer for every Docker feature

The project deliberately stays close to the capabilities exposed by the WSL Containers runtime.

## Project status

This project is actively being developed.

The API and runtime integration are still subject to change as the WSL Containers ecosystem evolves.

If you are using WSL Containers for development or integration testing, feedback and real-world
testing are particularly valuable.

## Contributing

Contributions are welcome.

Useful areas include:

- additional service modules
- Windows/WSL compatibility testing
- integration-test reliability
- API improvements
- documentation
- networking experiments
- runtime capability investigations
- performance measurements

If you are participating in **Hacktoberfest**, this repository is intentionally open to contributions
that improve the library or help establish where its boundaries should be.

Before opening a pull request, please read the development documentation and architecture decisions
in [`docs/`](docs/).

## License

[MIT](LICENSE)
