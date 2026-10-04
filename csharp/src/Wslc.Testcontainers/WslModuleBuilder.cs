using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers;

/// <summary>
/// Base class for typed module builders. Owns the settings shared by the built-in modules:
/// image, exposed port, readiness waits and startup timeout.
/// </summary>
/// <remarks>
/// Like <see cref="WslContainerBuilder"/>, module builders are mutable: every <c>With...</c>
/// mutates the builder and returns it for chaining. <c>Build()</c> snapshots the configuration.
/// Builders are not thread-safe.
/// </remarks>
/// <typeparam name="TBuilder">The concrete builder type returned by fluent calls.</typeparam>
public abstract class WslModuleBuilder<TBuilder>
    where TBuilder : WslModuleBuilder<TBuilder>
{
    private readonly int _port;
    private readonly string _readyMessage;
    private string _image;
    private TimeSpan _timeout = TimeSpan.FromMinutes(2);
    private Func<WslContainerBuilder, WslContainerBuilder>? _customizer;
    private bool? _reuse;

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

    /// <summary>
    /// Overrides the per-wait readiness timeout applied to both the TCP and log-message waits.
    /// The overall startup budget is derived as <c>2 * timeout + 30s</c> so sequential waits
    /// cannot outlive startup (the core-builder validation would otherwise reject it).
    /// </summary>
    public TBuilder WithWaitTimeout(TimeSpan timeout)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(timeout, TimeSpan.Zero);
        _timeout = timeout;
        return (TBuilder)this;
    }

    /// <summary>Enables reuse for the built module (see core <c>WithReuse</c> for CI semantics).</summary>
    public TBuilder WithReuse(bool reuse = true)
    {
        _reuse = reuse;
        return (TBuilder)this;
    }

    /// <summary>
    /// Escape hatch for core settings the module does not expose (volumes, environment, extra
    /// waits/ports). Applied after module defaults so it can override them. Module-level
    /// <c>WithReuse</c> is applied last and wins over a customizer that sets reuse.
    /// </summary>
    public TBuilder WithContainerConfiguration(Func<WslContainerBuilder, WslContainerBuilder> customize)
    {
        ArgumentNullException.ThrowIfNull(customize);
        _customizer = _customizer is null ? customize : builder => customize(_customizer(builder));
        return (TBuilder)this;
    }

    /// <summary>
    /// How many times <c>_readyMessage</c> must appear before the module is considered ready.
    /// Modules whose entrypoint starts a temporary server (Postgres) override this.
    /// </summary>
    protected virtual int ReadyMessageOccurrences => 1;

    /// <summary>Builds the core container with the module readiness waits applied.</summary>
    protected IWslContainer BuildContainer()
    {
        var startupTimeout = ComputeStartupTimeout(_timeout);
        var builder = new WslContainerBuilder()
            .WithImage(_image)
            .WithPort(_port)
            .WithWaitStrategy(Wait.ForWsl().WithTimeout(_timeout).UntilTcpPortIsAvailable(_port))
            .WithWaitStrategy(Wait.ForWsl().WithTimeout(_timeout).UntilMessageIsLogged(_readyMessage, ReadyMessageOccurrences))
            .WithStartupTimeout(startupTimeout);
        builder = Configure(builder);
        if (_customizer is not null)
        {
            builder = _customizer(builder);
        }

        if (_reuse is not null)
        {
            builder = builder.WithReuse(_reuse.Value);
        }

        return builder.Build();
    }

    internal static TimeSpan ComputeStartupTimeout(TimeSpan perWaitTimeout)
    {
        // Sequential waits need sum(wait timeouts) + provisioning slack.
        try
        {
            checked
            {
                var doubled = perWaitTimeout + perWaitTimeout;
                return doubled + TimeSpan.FromSeconds(30);
            }
        }
        catch (OverflowException)
        {
            return TimeSpan.MaxValue;
        }
    }

    /// <summary>
    /// Applies module-specific settings to the core builder. The core builder is mutable, so
    /// overrides can mutate and return it (e.g. <c>builder.WithCommand(...)</c>).
    /// </summary>
    protected virtual WslContainerBuilder Configure(WslContainerBuilder builder) => builder;
}
