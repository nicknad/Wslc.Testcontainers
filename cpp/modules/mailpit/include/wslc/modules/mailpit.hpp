#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class MailPitContainer;

/// <summary>
/// Testcontainers-style builder for Mailpit. Encapsulates the Image, ports and readiness waits
/// so tests don't memorize them. The SMTP port is the module's primary port; the HTTP UI/API
/// port is mapped as well.
/// </summary>
class MailPitBuilder : public WslModuleBuilder<MailPitBuilder>
{
public:
    /// <summary>Initializes the builder with the default Mailpit Image and readiness preset.</summary>
    MailPitBuilder();

    /// <summary>Builds the container. The container is not started.</summary>
    MailPitContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // Mailpit's startup log lines are not reliably captured before the HTTP server is up, so
    // readiness is the /livez HTTP check added in Configure instead of a log message.
    int ReadyMessageOccurrences() const override { return 0; }
};

/// <summary>
/// Typed Mailpit container: renders the SMTP and HTTP endpoints from the dynamic Host port
/// mappings.
/// </summary>
class MailPitContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port Mailpit accepts SMTP on by default (1025).</summary>
    static constexpr int SmtpPort = 1025;

    /// <summary>The Linux TCP port Mailpit serves its HTTP UI/API on by default (8025).</summary>
    static constexpr int HttpPort = 8025;

    MailPitContainer(const MailPitContainer&) = delete;
    MailPitContainer& operator=(const MailPitContainer&) = delete;
    MailPitContainer(MailPitContainer&&) noexcept = default;
    MailPitContainer& operator=(MailPitContainer&&) noexcept = default;

    /// <summary>Renders the host:port SMTP endpoint to point a mail client at.</summary>
    std::string GetSmtpEndpoint() const;

    /// <summary>Renders the Mailpit HTTP UI/API base URL (e.g. http://127.0.0.1:49153).</summary>
    std::string GetHttpEndpoint() const;

private:
    friend class MailPitBuilder;
    explicit MailPitContainer(WslContainer inner);
};

} // namespace wslc::modules
