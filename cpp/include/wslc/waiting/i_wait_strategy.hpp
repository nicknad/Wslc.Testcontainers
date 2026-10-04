#pragma once

#include "wslc/waiting/i_wait_target.hpp"

#include <chrono>
#include <memory>
#include <stop_token>
#include <string>

namespace wslc::waiting
{

/// <summary>A readiness condition evaluated after the Environment has been provisioned.</summary>
class IWaitStrategy
{
public:
    virtual ~IWaitStrategy() = default;

    /// <summary>Gets a human-readable description of the awaited condition.</summary>
    virtual std::string Name() const = 0;

    /// <summary>Gets the maximum time to Wait.</summary>
    virtual std::chrono::milliseconds Timeout() const = 0;

    /// <summary>Gets the interval between checks.</summary>
    virtual std::chrono::milliseconds RetryInterval() const = 0;

    /// <summary>Returns a copy of this strategy with a different Timeout (must be positive).</summary>
    virtual std::shared_ptr<IWaitStrategy> WithTimeout(std::chrono::milliseconds timeout) const = 0;

    /// <summary>Returns a copy of this strategy with a different retry interval (must be positive).</summary>
    virtual std::shared_ptr<IWaitStrategy> WithRetryInterval(std::chrono::milliseconds retryInterval) const = 0;

    /// <summary>
    /// Combines this strategy with another one; both must be satisfied sequentially.
    /// Nested composites are flattened; the returned composite keeps this strategy's Timeout
    /// and retry interval, which bounds the whole sequence.
    /// </summary>
    virtual std::shared_ptr<IWaitStrategy> And(std::shared_ptr<IWaitStrategy> other) const = 0;

    /// <summary>Waits until the condition is satisfied or the Timeout elapses.</summary>
    virtual void Wait(IWaitTarget& target, std::stop_token token) const = 0;
};

} // namespace wslc::waiting
