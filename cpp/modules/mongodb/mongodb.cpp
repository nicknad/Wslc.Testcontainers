#include "wslc/modules/mongodb.hpp"

#include "wslc/exceptions.hpp"

#include <string_view>
#include <utility>

namespace
{

constexpr char c_HexDigits[] = "0123456789ABCDEF";

// RFC 3986 unreserved characters; everything else is percent-encoded so credentials with
// separators (':', '@', '/') survive the userinfo section of the connection string. The C#
// module matches this with Uri.EscapeDataString.
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

MongoDbBuilder::MongoDbBuilder()
    : WslModuleBuilder<MongoDbBuilder>("docker.io/library/mongo:8", MongoDbContainer::DefaultPort,
                                       "Waiting for connections")
{
}

MongoDbBuilder& MongoDbBuilder::WithUsername(std::string username)
{
    if (username.empty())
    {
        throw WslException("Username must not be empty.");
    }

    m_username = std::move(username);
    return *this;
}

MongoDbBuilder& MongoDbBuilder::WithPassword(std::string password)
{
    m_password = std::move(password);
    return *this;
}

MongoDbContainer MongoDbBuilder::Build()
{
    if (m_username.has_value() != m_password.has_value())
    {
        throw WslException("MongoDB credentials need both WithUsername and WithPassword.");
    }

    return MongoDbContainer(BuildContainer(), m_username, m_password);
}

WslContainerBuilder& MongoDbBuilder::Configure(WslContainerBuilder& builder)
{
    if (m_username.has_value())
    {
        builder.WithEnvironment("MONGO_INITDB_ROOT_USERNAME", *m_username)
            .WithEnvironment("MONGO_INITDB_ROOT_PASSWORD", *m_password);
    }

    // The Image entrypoint maps the configured command to its CMD (mongod).
    return builder.WithCommand("mongod");
}

MongoDbContainer::MongoDbContainer(WslContainer inner, std::optional<std::string> username,
                                   std::optional<std::string> password)
    : WslModuleContainer(std::move(inner)), m_username(std::move(username)), m_password(std::move(password))
{
}

std::string MongoDbContainer::GetConnectionString() const
{
    const WslEndpoint endpoint = GetConnectEndpoint(DefaultPort);
    if (!m_username.has_value())
    {
        return "mongodb://" + endpoint.Host + ":" + std::to_string(endpoint.Port);
    }

    return "mongodb://" + PercentEncode(*m_username) + ":" + PercentEncode(*m_password) + "@" + endpoint.Host + ":" +
           std::to_string(endpoint.Port) + "/?authSource=admin";
}

} // namespace wslc::modules
