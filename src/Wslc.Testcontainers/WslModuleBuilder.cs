using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers;

/// <summary>
/// Base class for typed module builders. Owns the settings shared by the built-in modules:
/// image, exposed port, readiness waits and startup timeout.
/// </summary>
/// <typeparam name="TBuilder">The concrete builder type returned by fluent calls.</typeparam>
public abstract class WslModuleBuilder<TBuilder>
    where TBuilder : WslModuleBuilder<TBuilder>
{
    private readonly int _port;
    private readonly string _readyMessage;
    private string _image;
    private TimeSpan _timeout = TimeSpan.FromMinutes(2);

    /// <summary>Initializes the builder for a module container.</summary>
    protected WslModuleBuilder(string defaultImage, int port, string readyMessage)
    {
        _image = defaultImage;
        _port = port;
        _readyMessage = readyMessage;
    }

    /// <summary>Uses a container image. The image is pulled on first use.</summary>
    public TBuilder WithImage(string image)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(image);
        _image = image;
        return (TBuilder)this;
    }

    /// <summary>Overrides the overall startup timeout.</summary>
    public TBuilder WithStartupTimeout(TimeSpan timeout)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(timeout, TimeSpan.Zero);
        _timeout = timeout;
        return (TBuilder)this;
    }

    /// <summary>Builds the core container with the module readiness waits applied.</summary>
    protected IWslContainer BuildContainer()
    {
        var builder = new WslContainerBuilder()
            .FromImage(_image)
            .WithPort(_port)
            .WithWaitStrategy(Wait.ForWsl().WithTimeout(_timeout).UntilTcpPortIsAvailable(_port))
            .WithWaitStrategy(Wait.ForWsl().WithTimeout(_timeout).UntilMessageIsLogged(_readyMessage))
            .WithStartupTimeout(_timeout);
        Configure(builder);
        return builder.Build();
    }

    /// <summary>Applies module-specific settings to the core builder.</summary>
    protected virtual void Configure(WslContainerBuilder builder)
    {
    }
}
