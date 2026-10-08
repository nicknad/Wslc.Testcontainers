#include "wslc/modules/mongodb.hpp"

#include "wslc/exceptions.hpp"

#include <utility>

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
        return "mongodb://" + FormatEndpoint(endpoint);
    }

    return "mongodb://" + PercentEncode(*m_username) + ":" + PercentEncode(*m_password) + "@" +
           FormatEndpoint(endpoint) + "/?authSource=admin";
}

} // namespace wslc::modules
