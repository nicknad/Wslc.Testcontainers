#include "wslc/modules/rustfs.hpp"

#include "wslc/exceptions.hpp"
#include "wslc/waiting/wait.hpp"

#include <utility>

namespace wslc::modules
{

RustFsBuilder::RustFsBuilder()
    : WslModuleBuilder<RustFsBuilder>("docker.io/rustfs/rustfs:1.0.1", RustFsContainer::S3Port, "")
{
}

RustFsBuilder& RustFsBuilder::WithAccessKey(std::string accessKey)
{
    if (accessKey.empty())
    {
        throw WslException("Access key must not be empty.");
    }

    m_accessKey = std::move(accessKey);
    return *this;
}

RustFsBuilder& RustFsBuilder::WithSecretKey(std::string secretKey)
{
    if (secretKey.empty())
    {
        throw WslException("Secret key must not be empty.");
    }

    m_secretKey = std::move(secretKey);
    return *this;
}

RustFsContainer RustFsBuilder::Build()
{
    return RustFsContainer(BuildContainer(), m_accessKey, m_secretKey);
}

WslContainerBuilder& RustFsBuilder::Configure(WslContainerBuilder& builder)
{
    return builder.WithEnvironment("RUSTFS_ACCESS_KEY", m_accessKey)
        .WithEnvironment("RUSTFS_SECRET_KEY", m_secretKey)
        .WithEnvironment("RUSTFS_ADDRESS", ":9000")
        .WithEnvironment("RUSTFS_CONSOLE_ADDRESS", ":9001")
        .WithEnvironment("RUSTFS_CONSOLE_ENABLE", "true")
        // The Image entrypoint maps the configured command to its CMD: rustfs /data.
        .WithCommand("/data")
        .WithPort(RustFsContainer::ConsolePort)
        .WithWaitStrategy(wslc::waiting::ForWsl()
                              .WithTimeout(WaitTimeout())
                              .UntilHttpRequestSucceeds("/health", RustFsContainer::S3Port));
}

RustFsContainer::RustFsContainer(WslContainer inner, std::string accessKey, std::string secretKey)
    : WslModuleContainer(std::move(inner)), m_accessKey(std::move(accessKey)), m_secretKey(std::move(secretKey))
{
}

std::string RustFsContainer::GetEndpoint() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(S3Port);
    return "http://" + endpoint.Host + ":" + std::to_string(endpoint.Port);
}

} // namespace wslc::modules
