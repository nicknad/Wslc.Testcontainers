#include "wslc/modules/rabbitmq.hpp"

#include "wslc/exceptions.hpp"

#include <string_view>
#include <utility>

namespace
{

constexpr char c_HexDigits[] = "0123456789ABCDEF";

// RFC 3986 unreserved characters; everything else is percent-encoded so credentials with
// separators (':', '@', '/') survive the userinfo section of the AMQP URI. The C# module
// matches this with Uri.EscapeDataString.
bool IsUnreserved(const unsigned char value)
{
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
           value == '-' || value == '_' || value == '.' || value == '~';
}

std::string PercentEncode(const std::string_view value)
{
    std::string encoded;
    encoded.reserve(value.size());
    for (const char character : value)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (IsUnreserved(byte))
        {
            encoded.push_back(character);
        }
        else
        {
            encoded.push_back('%');
            encoded.push_back(c_HexDigits[byte >> 4]);
            encoded.push_back(c_HexDigits[byte & 0x0F]);
        }
    }
    return encoded;
}

} // namespace

namespace wslc::modules
{

RabbitMqBuilder::RabbitMqBuilder()
    : WslModuleBuilder<RabbitMqBuilder>("docker.io/library/rabbitmq:4-alpine", RabbitMqContainer::DefaultPort,
                                        "Server startup complete")
{
}

RabbitMqBuilder& RabbitMqBuilder::WithUsername(std::string username)
{
    if (username.empty())
    {
        throw WslException("Username must not be empty.");
    }

    m_username = std::move(username);
    return *this;
}

RabbitMqBuilder& RabbitMqBuilder::WithPassword(std::string password)
{
    m_password = std::move(password);
    return *this;
}

RabbitMqContainer RabbitMqBuilder::Build()
{
    return RabbitMqContainer(BuildContainer(), m_username, m_password);
}

WslContainerBuilder& RabbitMqBuilder::Configure(WslContainerBuilder& builder)
{
    return builder.WithEnvironment("RABBITMQ_DEFAULT_USER", m_username)
        .WithEnvironment("RABBITMQ_DEFAULT_PASS", m_password)
        // Replicates the Image ENTRYPOINT/CMD (docker-entrypoint.sh rabbitmq-server). The
        // script re-execs itself as the rabbitmq user via su-exec before starting the broker;
        // without this the broker never starts and readiness can never succeed.
        .WithCommand("/usr/local/bin/docker-entrypoint.sh", {"rabbitmq-server"});
}

RabbitMqContainer::RabbitMqContainer(WslContainer inner, std::string username, std::string password)
    : WslModuleContainer(std::move(inner)), m_username(std::move(username)), m_password(std::move(password))
{
}

std::string RabbitMqContainer::GetConnectionString() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    return "amqp://" + PercentEncode(m_username) + ":" + PercentEncode(m_password) + "@" + endpoint.Host + ":" +
           std::to_string(endpoint.Port) + "/";
}

} // namespace wslc::modules
