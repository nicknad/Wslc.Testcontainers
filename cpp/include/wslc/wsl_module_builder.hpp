#pragma once

#include "wslc/exceptions.hpp"
#include "wslc/waiting/wait.hpp"
#include "wslc/wsl_container.hpp"
#include "wslc/wsl_container_builder.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wslc
{

/// <summary>
/// Base class for typed module builders. Owns the settings shared by the built-in modules:
/// Image, exposed port, readiness waits and startup Timeout. Like
/// <see cref="WslContainerBuilder"/>, module builders are mutable: every <c>with...</c>
/// mutates and returns the concrete builder. <c>Build()</c> snapshots the configuration.
/// </summary>
/// <typeparam Name="TDerived">The concrete builder type returned by fluent calls.</typeparam>
template <typename TDerived> class WslModuleBuilder
{
public:
    /// <summary>Uses a container image. The image is pulled on first use.</summary>
    TDerived& WithImage(std::string image)
    {
        if (image.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            throw WslException("Image must not be empty.");
        }

        m_image = std::move(image);
        return Derived();
    }

    /// <summary>
    /// Overrides the per-wait readiness timeout applied to both the TCP and log-message waits.
    /// The overall startup budget is derived as 2 * timeout + 30s so sequential waits cannot
    /// outlive startup.
    /// </summary>
    TDerived& WithWaitTimeout(std::chrono::milliseconds timeout)
    {
        if (timeout <= std::chrono::milliseconds::zero())
        {
            throw WslException("Wait timeout must be positive.");
        }

        m_timeout = timeout;
        return Derived();
    }

    /// <summary>Enables reuse for the built module (see core <c>WithReuse</c> for CI semantics).</summary>
    TDerived& WithReuse(bool reuse = true)
    {
        m_reuse = reuse;
        return Derived();
    }

    /// <summary>
    /// Escape hatch for core settings the module does not expose (Volumes, Environment, extra
    /// waits/ports). Applied after module defaults so it can override them. Module-level
    /// <c>WithReuse</c> is applied last and wins over a customizer that sets Reuse.
    /// </summary>
    TDerived& ConfigureContainer(std::function<void(WslContainerBuilder&)> customize)
    {
        if (!customize)
        {
            throw WslException("Customizer must not be null.");
        }

        m_customizers.push_back(std::move(customize));
        return Derived();
    }

    virtual ~WslModuleBuilder() = default;

protected:
    /// <summary>Initializes the builder for a module container.</summary>
    WslModuleBuilder(std::string defaultImage, int port, std::string readyMessage)
        : m_image(std::move(defaultImage)), m_port(port), m_readyMessage(std::move(readyMessage))
    {
    }

    /// <summary>
    /// Applies module-specific settings to the core builder. The core builder is mutable, so
    /// overrides can mutate and return it (e.g. <c>builder.WithCommand(...)</c>).
    /// </summary>
    virtual WslContainerBuilder& Configure(WslContainerBuilder& builder) { return builder; }

    /// <summary>
    /// How many times the ready message must appear before the module is considered ready.
    /// Modules whose entrypoint starts a temporary server (Postgres, MariaDB, MongoDB with
    /// credentials) override this. Return 0 for services without a stable readiness log line;
    /// such modules add their own wait (e.g. HTTP) in <c>Configure</c>.
    /// </summary>
    virtual int ReadyMessageOccurrences() const { return 1; }

    /// <summary>Per-wait readiness timeout configured by <c>WithWaitTimeout</c>, for module waits added in
    /// <c>Configure</c>.</summary>
    std::chrono::milliseconds WaitTimeout() const { return m_timeout; }

    /// <summary>Builds the core container with the module readiness waits applied.</summary>
    WslContainer BuildContainer()
    {
        using namespace wslc::waiting;
        WslContainerBuilder builder;
        builder.WithImage(m_image)
            .WithPort(m_port)
            .WithWaitStrategy(ForWsl().WithTimeout(m_timeout).UntilTcpPortIsOpen(m_port))
            .WithReadinessTimeout(ComputeStartupTimeout(m_timeout));
        if (ReadyMessageOccurrences() > 0)
        {
            builder.WithWaitStrategy(
                ForWsl().WithTimeout(m_timeout).UntilMessageIsLogged(m_readyMessage, ReadyMessageOccurrences()));
        }

        Configure(builder);
        for (auto& customizer : m_customizers)
        {
            customizer(builder);
        }

        if (m_reuse.has_value())
        {
            builder.WithReuse(*m_reuse);
        }

        return builder.Build();
    }

    /// <summary>Sequential waits need sum(wait timeouts) + provisioning slack.</summary>
    static std::chrono::milliseconds ComputeStartupTimeout(std::chrono::milliseconds perWaitTimeout)
    {
        constexpr auto max = std::chrono::milliseconds::max();
        if (perWaitTimeout > (max - std::chrono::seconds(30)) / 2)
        {
            return max;
        }

        return perWaitTimeout * 2 + std::chrono::seconds(30);
    }

private:
    TDerived& Derived() { return static_cast<TDerived&>(*this); }

    std::string m_image;
    int m_port;
    std::string m_readyMessage;
    std::chrono::milliseconds m_timeout = std::chrono::minutes(2);
    std::vector<std::function<void(WslContainerBuilder&)>> m_customizers;
    std::optional<bool> m_reuse;
};

} // namespace wslc
