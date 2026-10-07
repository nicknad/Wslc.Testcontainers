namespace Wslc.Testcontainers.Modules.Nats;

/// <summary>
/// Testcontainers-style builder for NATS. Encapsulates the image, credentials, port and
/// readiness waits so tests don't memorize them. Defaults are for local tests only; set both
/// <see cref="WithUsername"/> and <see cref="WithPassword"/> for shared instances.
/// </summary>
public sealed class NatsBuilder : WslModuleBuilder<NatsBuilder>
{
    private string? _username;
    private string? _password;
    private bool _jetStream;

    /// <summary>Initializes the builder with the default NATS image and readiness preset.</summary>
    public NatsBuilder()
        : base(
            "docker.io/library/nats:2-alpine",
            NatsContainer.DefaultPort,
            "Server is ready")
    {
    }

    /// <summary>Enables JetStream persistence (the <c>-js</c> flag). Off by default.</summary>
    public NatsBuilder WithJetStream(bool enabled = true)
    {
        _jetStream = enabled;
        return this;
    }

    /// <summary>Requires clients to authenticate with this user. Requires <see cref="WithPassword"/>.</summary>
    public NatsBuilder WithUsername(string username)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(username);
        _username = username;
        return this;
    }

    /// <summary>Sets the password for <see cref="WithUsername"/>.</summary>
    public NatsBuilder WithPassword(string password)
    {
        ArgumentNullException.ThrowIfNull(password);
        _password = password;
        return this;
    }

    /// <summary>Builds the container with the configured options. The container is not started.</summary>
    public NatsContainer Build()
    {
        if ((_username is null) != (_password is null))
        {
            throw new WslException("NATS credentials need both WithUsername and WithPassword.");
        }

        return new(BuildContainer(), _username, _password);
    }

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder)
    {
        var arguments = new List<string>();
        if (_jetStream)
        {
            arguments.Add("-js");
        }

        if (_username is not null)
        {
            arguments.Add("--user");
            arguments.Add(_username);
            arguments.Add("--pass");
            arguments.Add(_password!);
        }

        // The image entrypoint maps the configured command to its CMD (nats-server).
        return builder.WithCommand("nats-server", [.. arguments]);
    }
}
