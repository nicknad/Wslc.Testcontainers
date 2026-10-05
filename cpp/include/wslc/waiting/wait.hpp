#pragma once

#include "wslc/waiting/i_wait_strategy.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace wslc::waiting
{

/// <summary>Fluent factory for built-in Wait Strategies and custom <c>until</c> conditions.</summary>
class WslWaitBuilder
{
public:
    /// <summary>Sets the Timeout applied to every strategy created from this builder.</summary>
    WslWaitBuilder& WithTimeout(std::chrono::milliseconds timeout);

    /// <summary>Sets the retry interval applied to every strategy created from this builder.</summary>
    WslWaitBuilder& WithRetryInterval(std::chrono::milliseconds retryInterval);

    /// <summary>Waits until a Linux TCP port accepts connections.</summary>
    std::shared_ptr<IWaitStrategy> UntilTcpPortIsAvailable(int port) const;

    /// <summary>Waits until an HTTP GET against the given Linux port succeeds (2xx-4xx; 5xx retries).</summary>
    std::shared_ptr<IWaitStrategy> UntilHttpRequestIsSucceeded(std::string pathAndQuery, int port) const;

    /// <summary>Waits until an HTTP GET against Linux port 80 succeeds.</summary>
    std::shared_ptr<IWaitStrategy> UntilHttpRequestIsSucceeded(std::string pathAndQuery) const;

    /// <summary>Waits until a process with the given Name is running.</summary>
    std::shared_ptr<IWaitStrategy> UntilProcessIsRunning(std::string processName) const;

    /// <summary>Waits until no process with the given Name is running.</summary>
    std::shared_ptr<IWaitStrategy> UntilProcessExits(std::string processName) const;

    /// <summary>Waits until a message appears in captured stdout/stderr (ordinal, case-sensitive).</summary>
    std::shared_ptr<IWaitStrategy> UntilMessageIsLogged(std::string message, int occurrences = 1) const;

    /// <summary>Waits until an absolute Linux path exists inside the Environment.</summary>
    std::shared_ptr<IWaitStrategy> UntilFileExists(std::string path) const;

    /// <summary>
    /// Waits until a custom condition returns true. The condition receives the Environment and
    /// a token linked to the startup Timeout, so it should observe the token. Exceptions are not
    /// retried: OperationCanceledException is reported as a readiness Timeout unless the caller
    /// cancelled, and through Start() other exceptions surface as WslProvisioningException.
    /// </summary>
    std::shared_ptr<IWaitStrategy> Until(std::string Name,
                                         std::function<bool(IWaitTarget&, std::stop_token)> condition) const;

private:
    std::shared_ptr<IWaitStrategy> Configure(std::shared_ptr<IWaitStrategy> strategy) const;

    std::optional<std::chrono::milliseconds> m_timeout;
    std::optional<std::chrono::milliseconds> m_retryInterval;
};

/// <summary>Starts configuring Wait Strategies for a WSLC Environment.</summary>
WslWaitBuilder ForWsl();

} // namespace wslc::waiting
