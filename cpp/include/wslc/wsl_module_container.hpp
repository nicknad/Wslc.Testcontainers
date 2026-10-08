#pragma once

#include "wslc/wsl_container.hpp"

#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace wslc
{

/// <summary>
/// Base class for typed module containers. Forwards lifecycle, Exec, copy and log operations so
/// module users are not blocked when they need ad-hoc initialization (e.g. seeding a database).
/// </summary>
class WslModuleContainer
{
public:
    WslModuleContainer(const WslModuleContainer&) = delete;
    WslModuleContainer& operator=(const WslModuleContainer&) = delete;
    WslModuleContainer(WslModuleContainer&&) noexcept = default;
    WslModuleContainer& operator=(WslModuleContainer&&) noexcept = default;
    virtual ~WslModuleContainer() = default;

    /// <summary>Gets the unique Name of the underlying WSLC instance.</summary>
    const std::string& Name() const { return m_inner.Name(); }

    /// <summary>Gets the container Image reference, when one was configured.</summary>
    std::optional<std::string> Image() const { return m_inner.Image(); }

    /// <summary>Gets a value indicating whether the container has been started.</summary>
    bool IsStarted() const { return m_inner.IsStarted(); }

    /// <summary>
    /// Gets a value indicating whether Reuse will actually be used for the wrapped container:
    /// Reuse must be configured and not suppressed by the Environment (forced off under CI
    /// unless WSLC_REUSE_IN_CI is truthy).
    /// </summary>
    bool IsReuseEffective() const { return m_inner.IsReuseEffective(); }

    /// <summary>
    /// Gets the Windows endpoint to connect to for a mapped Linux TCP port: the runtime-assigned
    /// Host port and the port's configured bind address. The default/wildcard binding resolves to
    /// loopback: IPv4 127.0.0.1 for 0.0.0.0 and IPv6 ::1 for ::.
    /// </summary>
    WslEndpoint GetConnectEndpoint(int containerPort) const { return m_inner.GetConnectEndpoint(containerPort); }

    /// <summary>Creates and provisions the Environment.</summary>
    void Start(std::stop_token token = {}) { m_inner.Start(token); }

    /// <summary>Stops the Environment and its processes (storage preserved for restart).</summary>
    void Stop(std::stop_token token = {}) { m_inner.Stop(token); }

    /// <summary>Executes a command and captures its exit code, stdout and stderr.</summary>
    ExecResult Exec(std::string command, std::vector<std::string> arguments = {}, ExecOptions options = {},
                    std::stop_token token = {})
    {
        return m_inner.Exec(std::move(command), std::move(arguments), std::move(options), token);
    }

    /// <summary>Starts a long-running process inside the Environment.</summary>
    /// <exception cref="WslException">options is an ExecOptions carrying StandardInput or Timeout;
    /// those apply only to Exec().</exception>
    std::unique_ptr<IWslProcess> StartProcess(std::string command, std::vector<std::string> arguments = {},
                                              const ProcessOptions& options = {}, std::stop_token token = {})
    {
        return m_inner.StartProcess(std::move(command), std::move(arguments), options, token);
    }

    /// <summary>Copies a Windows file into the Environment.</summary>
    void CopyTo(const std::filesystem::path& HostPath, std::string ContainerPath, std::stop_token token = {})
    {
        m_inner.CopyTo(HostPath, std::move(ContainerPath), token);
    }

    /// <summary>Copies a Linux file out of the Environment.</summary>
    void CopyFrom(std::string ContainerPath, const std::filesystem::path& HostPath, std::stop_token token = {})
    {
        m_inner.CopyFrom(std::move(ContainerPath), HostPath, token);
    }

    /// <summary>Subscribes to all Logs captured by the Environment.</summary>
    LogStream SubscribeLogs() { return m_inner.SubscribeLogs(); }

    /// <summary>Returns a bounded Snapshot of the most recent log lines, oldest first.</summary>
    std::vector<LogLine> GetRecentLogs(int maxLines = 50) const { return m_inner.GetRecentLogs(maxLines); }

    /// <summary>Stops and releases the container, deleting ephemeral storage. Idempotent.</summary>
    void Dispose() { m_inner.Dispose(); }

protected:
    /// <summary>Initializes a wrapper around the given container.</summary>
    explicit WslModuleContainer(WslContainer inner) : m_inner(std::move(inner)) {}

    /// <summary>Brackets IPv6 literals so host:port stays a valid URL authority.</summary>
    static std::string FormatHost(const std::string& host)
    {
        return host.find(':') != std::string::npos ? "[" + host + "]" : host;
    }

    /// <summary>Percent-encodes a credential for the userinfo section of a connection URL.
    /// RFC 3986 unreserved characters pass through; the rest become %XX (uppercase).</summary>
    static std::string PercentEncode(std::string_view value)
    {
        constexpr char c_hexDigits[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size());
        for (const char character : value)
        {
            const auto byte = static_cast<unsigned char>(character);
            const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                    (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.' ||
                                    byte == '~';
            if (unreserved)
            {
                encoded.push_back(character);
            }
            else
            {
                encoded.push_back('%');
                encoded.push_back(c_hexDigits[byte >> 4]);
                encoded.push_back(c_hexDigits[byte & 0x0F]);
            }
        }

        return encoded;
    }

    /// <summary>Renders an endpoint as host:port.</summary>
    static std::string FormatEndpoint(WslEndpoint endpoint)
    {
        return FormatHost(endpoint.Host) + ":" + std::to_string(endpoint.Port);
    }

    /// <summary>Renders an HTTP endpoint as http://host:port.</summary>
    static std::string FormatHttpEndpoint(WslEndpoint endpoint) { return "http://" + FormatEndpoint(endpoint); }

    /// <summary>Renders a connection string with host, port, username, password, and database.</summary>
    static std::string FormatConnectionString(WslEndpoint endpoint, const std::string& username,
                                              const std::string& password, const std::string& database)
    {
        return "Host=" + endpoint.Host + ";Port=" + std::to_string(endpoint.Port) + ";Username=" + username +
               ";Password=" + password + ";Database=" + database;
    }

private:
    WslContainer m_inner;
};

} // namespace wslc
