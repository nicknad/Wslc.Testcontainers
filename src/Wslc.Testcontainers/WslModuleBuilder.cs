using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers;

/// <summary>
/// Base class for typed module builders. Owns the settings shared by the built-in modules:
/// image, exposed port, readiness waits and startup timeout.
/// </summary>
/// <remarks>
/// Like <see cref="WslContainerBuilder"/>, module builders are immutable: every <c>With...</c>
/// returns a new builder. Do not reuse a builder after branching — each branch is independent.
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
        var clone = Clone();
        clone._image = image;
        return clone;
    }

    /// <summary>
    /// Overrides the readiness budget. The value is applied as the per-wait timeout for both the
    /// TCP and log-message waits; the overall startup timeout is derived as <c>2 * timeout + 30s</c>
    /// so sequential waits cannot outlive startup (the core-builder validation would otherwise reject it).
    /// </summary>
    public TBuilder WithStartupTimeout(TimeSpan timeout)
    {
        ArgumentOutOfRangeException.ThrowIfLessThanOrEqual(timeout, TimeSpan.Zero);
        var clone = Clone();
        clone._timeout = timeout;
        return clone;
    }

    /// <summary>Enables reuse for the built module (see core <c>WithReuse</c> for CI semantics).</summary>
    public TBuilder WithReuse(bool reuse = true)
    {
        var clone = Clone();
        clone._reuse = reuse;
        return clone;
    }

    /// <summary>
    /// Escape hatch for core settings the module does not expose (volumes, environment, extra
    /// waits/ports). Applied after module defaults so it can override them.
    /// </summary>
    public TBuilder WithContainerConfiguration(Func<WslContainerBuilder, WslContainerBuilder> customize)
    {
        ArgumentNullException.ThrowIfNull(customize);
        var clone = Clone();
        var previous = clone._customizer;
        clone._customizer = previous is null ? customize : builder => customize(previous(builder));
        return clone;
    }

    /// <summary>Builds the core container with the module readiness waits applied.</summary>
    protected IWslContainer BuildContainer()
    {
        var startupTimeout = ComputeStartupTimeout(_timeout);
        var builder = new WslContainerBuilder()
            .FromImage(_image)
            .WithPort(_port)
            .WithWaitStrategy(Wait.ForWsl().WithTimeout(_timeout).UntilTcpPortIsAvailable(_port))
            .WithWaitStrategy(Wait.ForWsl().WithTimeout(_timeout).UntilMessageIsLogged(_readyMessage))
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

    /// <summary>Creates an independent copy of this builder (immutable fluent pattern).</summary>
    protected TBuilder Clone() => (TBuilder)MemberwiseClone();

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
    /// Applies module-specific settings to the core builder. The core builder is immutable,
    /// so overrides must return the updated builder (e.g. <c>builder.WithCommand(...)</c>).
    /// </summary>
    protected virtual WslContainerBuilder Configure(WslContainerBuilder builder) => builder;
}
