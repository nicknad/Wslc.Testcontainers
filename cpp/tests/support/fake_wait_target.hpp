#pragma once

#include "wslc/waiting/i_wait_target.hpp"

#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace wslc::test
{

/// <summary>Scriptable IWaitTarget used by the wait-strategy unit tests.</summary>
class FakeWaitTarget : public waiting::IWaitTarget
{
public:
    const std::string& Name() const override { return TargetName; }

    WslEndpoint GetConnectEndpoint(int) const override { return ConnectEndpoint; }

    ExecResult Exec(std::string command, std::vector<std::string> arguments, std::stop_token token) override
    {
        if (ExecHandler)
        {
            return ExecHandler(std::move(command), std::move(arguments), token);
        }

        return ExecResult{};
    }

    bool IsTcpPortOpen(int port, std::stop_token token) override
    {
        return PortHandler ? PortHandler(port, token) : false;
    }

    bool IsProcessRunning(std::string processName, std::stop_token token) override
    {
        return ProcessHandler ? ProcessHandler(std::move(processName), token) : false;
    }

    std::vector<LogLine> GetRecentLogs() const override { return Logs; }

    std::string TargetName = "fake-container";
    WslEndpoint ConnectEndpoint{"127.0.0.1", 15000};
    std::function<ExecResult(std::string, std::vector<std::string>, std::stop_token)> ExecHandler;
    std::function<bool(int, std::stop_token)> PortHandler;
    std::function<bool(std::string, std::stop_token)> ProcessHandler;
    std::vector<LogLine> Logs;
};

} // namespace wslc::test
