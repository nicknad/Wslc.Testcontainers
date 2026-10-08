namespace Wslc.Testcontainers.Modules.MongoDb;

/// <summary>
/// Typed MongoDB container: a module wrapper that renders a MongoDB driver connection string
/// from the dynamic host port mapping.
/// </summary>
public sealed class MongoDbContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port MongoDB listens on by default (27017).</summary>
    public const int DefaultPort = 27017;

    private readonly string? _username;
    private readonly string? _password;

    internal MongoDbContainer(IWslContainer inner, string? username, string? password)
        : base(inner)
    {
        _username = username;
        _password = password;
    }

    /// <summary>
    /// Renders a MongoDB driver connection string for the running container, e.g.
    /// <c>mongodb://127.0.0.1:49153</c> or, with credentials,
    /// <c>mongodb://user:password@127.0.0.1:49153/?authSource=admin</c>.
    /// </summary>
    public string GetConnectionString()
    {
        var endpoint = GetConnectEndpoint(DefaultPort);
        if (_username is null)
        {
            return $"mongodb://{FormatEndpoint(endpoint)}";
        }

        var user = Uri.EscapeDataString(_username);
        var password = Uri.EscapeDataString(_password!);
        return $"mongodb://{user}:{password}@{FormatEndpoint(endpoint)}/?authSource=admin";
    }
}
