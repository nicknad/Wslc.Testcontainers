#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class WireMockContainer;

/// <summary>
/// Testcontainers-style builder for WireMock. Encapsulates the Image, port and readiness wait so
/// tests don't memorize them.
/// </summary>
class WireMockBuilder : public WslModuleBuilder<WireMockBuilder>
{
public:
    /// <summary>Initializes the builder with the default WireMock Image and readiness preset.</summary>
    WireMockBuilder();

    /// <summary>Builds the container. The container is not started.</summary>
    WireMockContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // WireMock's startup log lines are not reliably captured before the HTTP server is up, so
    // readiness is the /__admin/health HTTP check added in Configure instead of a log message.
    int ReadyMessageOccurrences() const override { return 0; }
};

/// <summary>
/// Typed WireMock container: renders the HTTP stub/admin base URL from the dynamic Host port
/// mapping.
/// </summary>
class WireMockContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port WireMock serves stubs and the admin API on by default (8080).</summary>
    static constexpr int DefaultPort = 8080;

    WireMockContainer(const WireMockContainer&) = delete;
    WireMockContainer& operator=(const WireMockContainer&) = delete;
    WireMockContainer(WireMockContainer&&) noexcept = default;
    WireMockContainer& operator=(WireMockContainer&&) noexcept = default;

    /// <summary>Renders the WireMock HTTP base URL (e.g. http://127.0.0.1:49153).</summary>
    std::string GetEndpoint() const;

private:
    friend class WireMockBuilder;
    explicit WireMockContainer(WslContainer inner);
};

} // namespace wslc::modules
