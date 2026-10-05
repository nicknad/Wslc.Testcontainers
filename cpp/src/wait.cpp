#include "wslc/waiting/wait.hpp"

#include "internal/limits.hpp"
#include "internal/tcp_http.hpp"
#include "internal/util.hpp"
#include "wslc/environment.hpp"
#include "wslc/exceptions.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace wslc::waiting
{

namespace
{

constexpr int c_maxIterations = 100'000;
constexpr std::size_t c_maxLogLines = 50;

std::vector<LogLine> TakeLast(const std::vector<LogLine>& Logs, std::size_t maxLines)
{
    if (Logs.size() <= maxLines)
    {
        return Logs;
    }

    return std::vector<LogLine>(Logs.end() - static_cast<std::ptrdiff_t>(maxLines), Logs.end());
}

std::string JoinLast(const std::vector<LogLine>& Logs, LogSource Source, std::size_t maxLines)
{
    std::vector<std::string> selected;
    for (const auto& line : Logs)
    {
        if (line.Source != Source)
        {
            continue;
        }

        selected.push_back(line.Text);
        if (selected.size() > maxLines)
        {
            selected.erase(selected.begin());
        }
    }

    return internal::join(selected, "\n");
}

/// <summary>Requests Stop on a derived Stop source when the Timeout elapses; cancels on destruction.</summary>
class StopTimer
{
public:
    StopTimer(std::stop_source& source, std::chrono::milliseconds timeout)
        : m_thread(
              [&source, timeout](std::stop_token timer_token)
              {
                  if (internal::SleepFor(timeout, timer_token))
                  {
                      source.request_stop();
                  }
              })
    {
    }

private:
    std::jthread m_thread;
};

class CompositeStrategy;

/// <summary>Shared Timeout/retry configuration and diagnostics for Wait Strategies.</summary>
class WaitStrategyBase : public IWaitStrategy, public std::enable_shared_from_this<WaitStrategyBase>
{
public:
    std::chrono::milliseconds Timeout() const override { return m_timeout; }
    std::chrono::milliseconds RetryInterval() const override { return m_retryInterval; }

    std::shared_ptr<IWaitStrategy> WithTimeout(std::chrono::milliseconds Timeout) const override
    {
        if (Timeout <= std::chrono::milliseconds::zero())
        {
            throw WslException("Timeout must be positive.");
        }

        auto copy = Clone();
        copy->m_timeout = Timeout;
        return copy;
    }

    std::shared_ptr<IWaitStrategy> WithRetryInterval(std::chrono::milliseconds retryInterval) const override
    {
        if (retryInterval <= std::chrono::milliseconds::zero())
        {
            throw WslException("Retry interval must be positive.");
        }

        auto copy = Clone();
        copy->m_retryInterval = retryInterval;
        return copy;
    }

    std::shared_ptr<IWaitStrategy> And(std::shared_ptr<IWaitStrategy> other) const override;

    void SetTimeout(std::chrono::milliseconds Timeout) { m_timeout = Timeout; }
    void SetRetryInterval(std::chrono::milliseconds retryInterval) { m_retryInterval = retryInterval; }

protected:
    virtual std::shared_ptr<WaitStrategyBase> Clone() const = 0;

    WslReadinessException CreateTimeout(IWaitTarget& target, std::chrono::milliseconds Elapsed) const
    {
        std::string message = "Timed out after " + internal::FormatMilliseconds(Elapsed) + "s waiting for " + Name() +
                              " on '" + target.Name() + "'.";
        const std::string detail = BuildFailureDetail(target);
        if (!internal::IsBlank(detail))
        {
            message += "\n" + detail;
        }

        return WslReadinessException(message, Name(), m_timeout, TakeLast(target.GetRecentLogs(), c_maxLogLines));
    }

    virtual std::string BuildFailureDetail(IWaitTarget&) const { return {}; }

    std::chrono::milliseconds m_timeout = WslEnvironment::DefaultWaitTimeout();
    std::chrono::milliseconds m_retryInterval = std::chrono::milliseconds(250);
};

/// <summary>A Wait strategy that polls a condition until it holds or the Timeout elapses.</summary>
class PollingStrategy final : public WaitStrategyBase
{
public:
    using Check = std::function<bool(IWaitTarget&, std::stop_token)>;

    PollingStrategy(std::string Name, Check check, bool network = false)
        : m_name(std::move(Name)), m_check(std::move(check)), m_network(network)
    {
    }

    std::string Name() const override { return m_name; }

    /// <summary>True for built-in TCP/HTTP waits, which need networking.</summary>
    bool IsNetwork() const noexcept { return m_network; }

    void Wait(IWaitTarget& target, std::stop_token caller_token) const override
    {
        std::stop_source timeout_source;
        std::stop_callback callback(caller_token, [&timeout_source] { timeout_source.request_stop(); });
        StopTimer timer(timeout_source, m_timeout);
        const std::stop_token token = timeout_source.get_token();
        const auto deadline = std::chrono::steady_clock::now() + m_timeout;
        const auto started = std::chrono::steady_clock::now();

        for (int attempt = 0; attempt < c_maxIterations; attempt++)
        {
            if (caller_token.stop_requested())
            {
                throw OperationCanceledException();
            }

            bool satisfied = false;
            try
            {
                satisfied = m_check(target, token);
            }
            catch (const OperationCanceledException&)
            {
                if (!caller_token.stop_requested())
                {
                    throw CreateTimeout(target, Elapsed(started));
                }

                throw;
            }

            if (satisfied)
            {
                return;
            }

            if (std::chrono::steady_clock::now() >= deadline)
            {
                throw CreateTimeout(target, Elapsed(started));
            }

            if (!internal::SleepFor(m_retryInterval, token))
            {
                if (!caller_token.stop_requested())
                {
                    throw CreateTimeout(target, Elapsed(started));
                }

                throw OperationCanceledException();
            }
        }

        throw CreateTimeout(target, Elapsed(started));
    }

protected:
    std::shared_ptr<WaitStrategyBase> Clone() const override
    {
        auto copy = std::make_shared<PollingStrategy>(m_name, m_check, m_network);
        copy->m_timeout = m_timeout;
        copy->m_retryInterval = m_retryInterval;
        return copy;
    }

private:
    static std::chrono::milliseconds Elapsed(std::chrono::steady_clock::time_point started)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    }

    std::string m_name;
    Check m_check;
    bool m_network = false;
};

/// <summary>Requires several Strategies to be satisfied sequentially within the composite Timeout.</summary>
class CompositeStrategy final : public WaitStrategyBase
{
public:
    explicit CompositeStrategy(std::vector<std::shared_ptr<IWaitStrategy>> Strategies)
        : m_strategies(std::move(Strategies))
    {
    }

    std::string Name() const override
    {
        std::vector<std::string> names;
        names.reserve(m_strategies.size());
        for (const auto& strategy : m_strategies)
        {
            names.push_back(strategy->Name());
        }

        return internal::join(names, " and ");
    }

    void Wait(IWaitTarget& target, std::stop_token caller_token) const override
    {
        std::stop_source timeout_source;
        std::stop_callback callback(caller_token, [&timeout_source] { timeout_source.request_stop(); });
        StopTimer timer(timeout_source, m_timeout);
        const std::stop_token token = timeout_source.get_token();
        const auto started = std::chrono::steady_clock::now();

        try
        {
            for (const auto& strategy : m_strategies)
            {
                strategy->Wait(target, token);
                if (caller_token.stop_requested())
                {
                    throw OperationCanceledException();
                }
            }
        }
        catch (const OperationCanceledException&)
        {
            if (!caller_token.stop_requested())
            {
                throw CreateTimeout(target, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - started));
            }

            throw;
        }
    }

    const std::vector<std::shared_ptr<IWaitStrategy>>& Strategies() const noexcept { return m_strategies; }

protected:
    std::shared_ptr<WaitStrategyBase> Clone() const override
    {
        auto copy = std::make_shared<CompositeStrategy>(m_strategies);
        copy->m_timeout = m_timeout;
        copy->m_retryInterval = m_retryInterval;
        return copy;
    }

private:
    std::vector<std::shared_ptr<IWaitStrategy>> m_strategies;
};

std::shared_ptr<IWaitStrategy> WaitStrategyBase::And(std::shared_ptr<IWaitStrategy> other) const
{
    if (!other)
    {
        throw WslException("The other Wait strategy must not be null.");
    }

    std::vector<std::shared_ptr<IWaitStrategy>> combined;
    const auto self_composite = dynamic_cast<const CompositeStrategy*>(this);
    if (self_composite != nullptr)
    {
        combined = self_composite->Strategies();
    }
    else
    {
        combined.push_back(std::const_pointer_cast<WaitStrategyBase>(shared_from_this()));
    }

    const auto other_composite = dynamic_cast<const CompositeStrategy*>(other.get());
    if (other_composite != nullptr)
    {
        const auto& nested = other_composite->Strategies();
        combined.insert(combined.end(), nested.begin(), nested.end());
    }
    else
    {
        combined.push_back(std::move(other));
    }

    // Composites flatten their children, so the top-level Build count cannot see nested
    // strategies; enforce the combined cap here instead.
    internal::RequireWaitStrategyCount(combined.size());
    auto composite = std::make_shared<CompositeStrategy>(std::move(combined));
    composite->SetTimeout(m_timeout);
    composite->SetRetryInterval(m_retryInterval);
    return composite;
}

int ValidatePort(int port)
{
    if (port < 1 || port > 65535)
    {
        throw WslException("Port must be between 1 and 65535.");
    }

    return port;
}

std::string RequireText(std::string value, const char* what)
{
    if (internal::IsBlank(value))
    {
        throw WslException(std::string(what) + " must not be empty.");
    }

    return value;
}

} // namespace

WslWaitBuilder& WslWaitBuilder::WithTimeout(std::chrono::milliseconds Timeout)
{
    if (Timeout <= std::chrono::milliseconds::zero())
    {
        throw WslException("Timeout must be positive.");
    }

    m_timeout = Timeout;
    return *this;
}

WslWaitBuilder& WslWaitBuilder::WithRetryInterval(std::chrono::milliseconds retryInterval)
{
    if (retryInterval <= std::chrono::milliseconds::zero())
    {
        throw WslException("Retry interval must be positive.");
    }

    m_retryInterval = retryInterval;
    return *this;
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::UntilTcpPortIsOpen(int port) const
{
    const int validated = ValidatePort(port);
    return Configure(std::make_shared<PollingStrategy>(
        "TCP port " + std::to_string(validated) + " to be available",
        [validated](IWaitTarget& target, std::stop_token token) { return target.IsTcpPortOpen(validated, token); },
        true));
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::UntilHttpRequestSucceeds(std::string pathAndQuery, int port) const
{
    internal::ValidateHttpPath(pathAndQuery);
    const std::string path = std::move(pathAndQuery);
    const int validated = ValidatePort(port);
    return Configure(std::make_shared<PollingStrategy>(
        "HTTP request to '" + path + "' on port " + std::to_string(validated) + " to succeed",
        [path, validated](IWaitTarget& target, std::stop_token token)
        {
            const WslEndpoint endpoint = target.GetConnectEndpoint(validated);
            return internal::HttpGetSucceeds(endpoint.Host, endpoint.Port, path, token);
        },
        true));
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::UntilHttpRequestSucceeds(std::string pathAndQuery) const
{
    return UntilHttpRequestSucceeds(std::move(pathAndQuery), 80);
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::UntilProcessIsRunning(std::string processName) const
{
    const std::string Name = RequireText(std::move(processName), "Process Name");
    return Configure(std::make_shared<PollingStrategy>("process '" + Name + "' to be running",
                                                       [Name](IWaitTarget& target, std::stop_token token)
                                                       { return target.IsProcessRunning(Name, token); }));
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::UntilProcessExits(std::string processName) const
{
    const std::string Name = RequireText(std::move(processName), "Process Name");
    return Configure(std::make_shared<PollingStrategy>("process '" + Name + "' to have exited",
                                                       [Name](IWaitTarget& target, std::stop_token token)
                                                       { return !target.IsProcessRunning(Name, token); }));
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::UntilMessageIsLogged(std::string message, int occurrences) const
{
    if (occurrences < 1)
    {
        throw WslException("Occurrences must be at least 1.");
    }

    const std::string Text = RequireText(std::move(message), "Message");
    const std::string Name = occurrences <= 1
                                 ? "log message '" + Text + "' to be logged"
                                 : "log message '" + Text + "' to be logged " + std::to_string(occurrences) + " times";
    return Configure(std::make_shared<PollingStrategy>(Name,
                                                       [Text, occurrences](IWaitTarget& target, std::stop_token)
                                                       {
                                                           const std::vector<LogLine> Logs = target.GetRecentLogs();
                                                           int seen = 0;
                                                           for (const auto& line : Logs)
                                                           {
                                                               if (line.Source == LogSource::System)
                                                               {
                                                                   continue;
                                                               }

                                                               std::size_t index = line.Text.find(Text);
                                                               while (index != std::string::npos)
                                                               {
                                                                   seen++;
                                                                   if (seen >= occurrences)
                                                                   {
                                                                       return true;
                                                                   }

                                                                   index = line.Text.find(Text, index + Text.size());
                                                               }
                                                           }

                                                           return false;
                                                       }));
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::UntilFileExists(std::string path) const
{
    internal::ValidateContainerPath(path);
    const std::string ContainerPath = std::move(path);
    return Configure(std::make_shared<PollingStrategy>("path '" + ContainerPath + "' to exist",
                                                       [ContainerPath](IWaitTarget& target, std::stop_token token)
                                                       {
                                                           const ExecResult result =
                                                               target.Exec("test", {"-e", ContainerPath}, token);
                                                           return result.ExitCode == 0;
                                                       }));
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::Until(std::string Name,
                                                     std::function<bool(IWaitTarget&, std::stop_token)> condition) const
{
    if (!condition)
    {
        throw WslException("The condition must not be null.");
    }

    return Configure(
        std::make_shared<PollingStrategy>(RequireText(std::move(Name), "Condition Name"), std::move(condition)));
}

std::shared_ptr<IWaitStrategy> WslWaitBuilder::Configure(std::shared_ptr<IWaitStrategy> strategy) const
{
    if (m_timeout)
    {
        strategy = strategy->WithTimeout(*m_timeout);
    }

    if (m_retryInterval)
    {
        strategy = strategy->WithRetryInterval(*m_retryInterval);
    }

    return strategy;
}

WslWaitBuilder ForWsl()
{
    return WslWaitBuilder{};
}

namespace detail
{

namespace
{

std::optional<std::string> FindNetworkWait(const std::shared_ptr<IWaitStrategy>& strategy)
{
    if (const auto* polling = dynamic_cast<const PollingStrategy*>(strategy.get()))
    {
        if (polling->IsNetwork())
        {
            return polling->Name();
        }

        return std::nullopt;
    }

    if (const auto* composite = dynamic_cast<const CompositeStrategy*>(strategy.get()))
    {
        for (const auto& child : composite->Strategies())
        {
            if (auto nested = FindNetworkWait(child))
            {
                return nested;
            }
        }
    }

    return std::nullopt;
}

} // namespace

std::optional<std::string> FindNetworkWait(const std::vector<std::shared_ptr<IWaitStrategy>>& Strategies)
{
    for (const auto& strategy : Strategies)
    {
        if (auto found = FindNetworkWait(strategy))
        {
            return found;
        }
    }

    return std::nullopt;
}

} // namespace detail

} // namespace wslc::waiting
