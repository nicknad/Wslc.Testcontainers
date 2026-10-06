#pragma once

#include "wslc/wsl_module_builder.hpp"
#include "wslc/wsl_module_container.hpp"

#include <string>

namespace wslc::modules
{

class RustFsContainer;

/// <summary>
/// Testcontainers-style builder for RustFS (S3-compatible object storage). Encapsulates the
/// Image, credentials, ports and readiness waits so tests don't memorize them. Defaults are the
/// Image's local-test credentials; override the access/secret key for anything shared.
/// </summary>
class RustFsBuilder : public WslModuleBuilder<RustFsBuilder>
{
public:
    /// <summary>Initializes the builder with the default RustFS Image and readiness preset.</summary>
    RustFsBuilder();

    /// <summary>Sets the RUSTFS_ACCESS_KEY value. Defaults to "rustfsadmin".</summary>
    RustFsBuilder& WithAccessKey(std::string accessKey);

    /// <summary>Sets the RUSTFS_SECRET_KEY value. Defaults to "rustfsadmin"; override for anything shared.</summary>
    RustFsBuilder& WithSecretKey(std::string secretKey);

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    RustFsContainer Build();

protected:
    WslContainerBuilder& Configure(WslContainerBuilder& builder) override;

    // RustFS logs to an observability pipeline rather than a stable stdout readiness line, so
    // readiness is the /health HTTP check added in Configure instead of a log message.
    int ReadyMessageOccurrences() const override { return 0; }

private:
    std::string m_accessKey = "rustfsadmin";
    std::string m_secretKey = "rustfsadmin";
};

/// <summary>
/// Typed RustFS container: renders the S3 endpoint and credentials for an AWS-style client
/// configuration.
/// </summary>
class RustFsContainer : public WslModuleContainer
{
public:
    /// <summary>The Linux TCP port the RustFS S3 API listens on by default (9000).</summary>
    static constexpr int S3Port = 9000;

    /// <summary>The Linux TCP port the RustFS web console listens on by default (9001).</summary>
    static constexpr int ConsolePort = 9001;

    RustFsContainer(const RustFsContainer&) = delete;
    RustFsContainer& operator=(const RustFsContainer&) = delete;
    RustFsContainer(RustFsContainer&&) noexcept = default;
    RustFsContainer& operator=(RustFsContainer&&) noexcept = default;

    /// <summary>Renders the S3 service URL (e.g. http://127.0.0.1:49153) for a client's service/endpoint
    /// configuration.</summary>
    std::string GetEndpoint() const;

    /// <summary>The access key clients must authenticate with.</summary>
    std::string AccessKey() const { return m_accessKey; }

    /// <summary>The secret key clients must authenticate with.</summary>
    std::string SecretKey() const { return m_secretKey; }

private:
    friend class RustFsBuilder;
    RustFsContainer(WslContainer inner, std::string accessKey, std::string secretKey);

    std::string m_accessKey;
    std::string m_secretKey;
};

} // namespace wslc::modules
