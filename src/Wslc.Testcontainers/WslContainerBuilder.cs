using System.Net;
using Wslc.Testcontainers.Provisioning;
using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers;

/// <summary>
/// Mutable Testcontainers-style builder for <see cref="WslContainer"/> instances.
/// Every <c>With...</c>/<c>From...</c> call mutates this builder and returns it for chaining.
/// <see cref="Build"/> snapshots the current configuration, so later builder changes do not affect
/// containers already built. Builders are not thread-safe.
/// </summary>
public sealed class WslContainerBuilder
{
    private WslContainerConfiguration _configuration;

    /// <summary>
    /// Initializes a builder with no configuration. Call <see cref="WithImage"/> or
    /// <see cref="FromTarball"/> before <see cref="Build"/>.
    /// </summary>
    public WslContainerBuilder()
        : this(new WslContainerConfiguration())
    {
    }

    private WslContainerBuilder(WslContainerConfiguration configuration) => _configuration = configuration;

    /// <summary>
    /// Uses a container image. The image is pulled on first use and cached in the session storage.
    /// The image's ENTRYPOINT/CMD is not executed automatically; declare the service with
    /// <see cref="WithCommand"/> or use a module builder, otherwise only a keep-alive shell runs.
    /// </summary>
    public WslContainerBuilder WithImage(string image)
    {
        RequireText(image, nameof(image));
        _configuration = _configuration with
        {
            Image = image,
            TarballPath = null,
            TarballImageName = null,
        };
        return this;
    }

    /// <summary>Imports a root filesystem tarball as a container image. The tarball must exist when this is called.</summary>
    /// <param name="tarballPath">Path to a tar archive.</param>
    /// <param name="imageName">Image reference to assign. Defaults to a WSLC-generated local name.</param>
    public WslContainerBuilder FromTarball(string tarballPath, string? imageName = null)
    {
        RequireText(tarballPath, nameof(tarballPath));
        if (!File.Exists(tarballPath))
        {
            throw new WslcException($"Tarball '{tarballPath}' does not exist.");
        }

        _configuration = _configuration with
        {
            TarballPath = tarballPath,
            TarballImageName = imageName,
            Image = null,
        };
        return this;
    }

    /// <summary>
    /// Sets the long-running command started as the container init process. WSLC never runs the
    /// image's ENTRYPOINT/CMD automatically, so declare the service here or use a module builder.
    /// Without this, a keep-alive shell runs and the image's service never starts.
    /// </summary>
    public WslContainerBuilder WithCommand(string command, params string[] arguments)
    {
        RequireText(command, nameof(command));
        ArgumentNullException.ThrowIfNull(arguments);
        _configuration = _configuration with
        {
            Command = command,
            CommandArguments = (string[])arguments.Clone(),
        };
        return this;
    }

    /// <summary>Sets the working directory used by the init process and command executions.</summary>
    public WslContainerBuilder WithWorkingDirectory(string workingDirectory)
    {
        RequireText(workingDirectory, nameof(workingDirectory));
        _configuration = _configuration with { WorkingDirectory = workingDirectory };
        return this;
    }

    /// <summary>Adds an environment variable scoped to the container processes.</summary>
    public WslContainerBuilder WithEnvironment(string name, string value)
    {
        RequireEnvironmentName(name);
        ArgumentNullException.ThrowIfNull(value);
        var environment = CopyEnvironment(_configuration.Environment, additionalCapacity: 1);
        environment[name] = value;
        _configuration = _configuration with { Environment = environment };
        return this;
    }

    /// <summary>Adds environment variables scoped to the container processes.</summary>
    public WslContainerBuilder WithEnvironmentVariables(IReadOnlyDictionary<string, string> variables)
    {
        ArgumentNullException.ThrowIfNull(variables);
        var environment = CopyEnvironment(_configuration.Environment, variables.Count);
        foreach (var pair in variables)
        {
            RequireEnvironmentName(pair.Key);
            if (pair.Value is null)
            {
                throw new ArgumentException($"Environment variable '{pair.Key}' has null value.", nameof(variables));
            }

            environment[pair.Key] = pair.Value;
        }

        _configuration = _configuration with { Environment = environment };
        return this;
    }

    /// <summary>
    /// Declares a Linux TCP service port that will be exposed on a dynamic Windows port.
    /// UDP mappings are not supported: the WSLC runtime returns <c>E_NOTIMPL</c> for them.
    /// </summary>
    public WslContainerBuilder WithPort(int port)
    {
        ValidatePort(port);
        return AddPortMapping(port, bindAddress: null);
    }

    /// <summary>
    /// Declares a Linux TCP service port bound to a specific Windows address (e.g.
    /// <c>0.0.0.0</c> to expose it on the LAN). The Windows port stays dynamic.
    /// When omitted the SDK default (loopback, <c>127.0.0.1</c>) is used.
    /// </summary>
    public WslContainerBuilder WithPort(int port, string bindAddress)
    {
        ValidatePort(port);
        ArgumentException.ThrowIfNullOrWhiteSpace(bindAddress);
        if (!IPAddress.TryParse(bindAddress, out var address))
        {
            throw new ArgumentException($"Bind address '{bindAddress}' is not a valid IP address.", nameof(bindAddress));
        }

        return AddPortMapping(port, address.ToString());
    }

    private WslContainerBuilder AddPortMapping(int port, string? bindAddress)
    {
        foreach (var existing in _configuration.PortMappings)
        {
            if (existing.ContainerPort == port)
            {
                if (!string.Equals(existing.BindAddress, bindAddress, StringComparison.OrdinalIgnoreCase))
                {
                    throw new WslcException(
                        $"Port {port} is already mapped with a different bind address. Declare each port once.");
                }

                return this;
            }
        }

        _configuration = _configuration with { PortMappings = Append(_configuration.PortMappings, new WslPortMapping(port, bindAddress)) };
        return this;
    }

    private static void ValidatePort(int port)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(port, 1);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(port, 65535);
    }

    /// <summary>Adds a readiness strategy. All configured strategies must pass before startup completes.</summary>
    public WslContainerBuilder WithWaitStrategy(IWaitStrategy strategy)
    {
        ArgumentNullException.ThrowIfNull(strategy);
        _configuration = _configuration with
        {
            WaitStrategies = Append(_configuration.WaitStrategies, strategy),
        };
        return this;
    }

    /// <summary>
    /// Copies a Windows file into the container during startup.
    /// Use <paramref name="hostPath"/> for the Windows source file and
    /// <paramref name="containerPath"/> for the absolute Linux destination (e.g. <c>/app/config.json</c>).
    /// Files larger than 1 GiB are rejected to avoid filling container disk.
    /// </summary>
    public WslContainerBuilder WithFile(string hostPath, string containerPath)
    {
        RequireText(hostPath, nameof(hostPath));
        RequireContainerPath(containerPath, nameof(containerPath));
        if (!File.Exists(hostPath))
        {
            throw new WslcException($"File source '{hostPath}' does not exist. Only files are supported by WithFile.");
        }

        const long MaxCopyBytes = 1024L * 1024L * 1024L;
        var length = new FileInfo(hostPath).Length;
        if (length > MaxCopyBytes)
        {
            throw new WslcException($"File '{hostPath}' exceeds 1 GiB limit ({length} bytes) and cannot be copied into the container.");
        }

        _configuration = _configuration with
        {
            Files = Append(_configuration.Files, new WslFileCopy(hostPath, containerPath)),
        };
        return this;
    }

    /// <summary>
    /// Mounts a Windows directory into the container as read-write. Follows <c>host, container</c>
    /// order like <c>docker run -v</c>; both are validated so a swapped call fails fast.
    /// </summary>
    /// <param name="hostPath">Existing Windows directory.</param>
    /// <param name="containerPath">Absolute Linux destination (e.g. <c>/workspace</c>).</param>
    public WslContainerBuilder WithVolume(string hostPath, string containerPath) =>
        WithVolume(hostPath, containerPath, VolumeAccess.ReadWrite);

    /// <summary>Mounts a Windows directory into the container with an explicit access mode.</summary>
    public WslContainerBuilder WithVolume(string hostPath, string containerPath, VolumeAccess access)
    {
        RequireText(hostPath, nameof(hostPath));
        RequireContainerPath(containerPath, nameof(containerPath));
        if (!Directory.Exists(hostPath))
        {
            throw new WslcException($"Volume host path '{hostPath}' does not exist or is not a directory.");
        }

        _configuration = _configuration with
        {
            Volumes = Append(_configuration.Volumes, new WslVolumeMount(Path.GetFullPath(hostPath), containerPath, access == VolumeAccess.ReadOnly)),
        };
        return this;
    }

    /// <summary>Mounts a Windows directory into the container as read-only.</summary>
    public WslContainerBuilder WithReadOnlyVolume(string hostPath, string containerPath) =>
        WithVolume(hostPath, containerPath, VolumeAccess.ReadOnly);

    /// <summary>
    /// Mounts a session VHD volume (native Linux filesystem, ext4) into the container.
    /// The volume is created when the container starts and is <b>recreated empty on every
    /// start</b> — it is size-limited scratch space, not persistence, and does not follow
    /// Docker's named-volume semantics. Prefer over bind mounts when the data must not be
    /// exposed as Windows host files; the backing VHD still lives inside the session
    /// storage directory under <c>%LOCALAPPDATA%</c>.
    /// </summary>
    /// <param name="name">Session volume name (non-empty, no path separators).</param>
    /// <param name="containerPath">Absolute Linux destination (e.g. <c>/data</c>).</param>
    /// <param name="sizeBytes">VHD size in bytes (must be positive).</param>
    /// <param name="access">Read-write (default) or read-only mount.</param>
    /// <param name="type">Dynamic (default) or fixed VHD allocation.</param>
    public WslContainerBuilder WithSessionVolume(
        string name,
        string containerPath,
        ulong sizeBytes,
        VolumeAccess access = VolumeAccess.ReadWrite,
        VhdAllocationType type = VhdAllocationType.Dynamic)
    {
        RequireVolumeName(name, nameof(name));
        RequireContainerPath(containerPath, nameof(containerPath));
        if (sizeBytes == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(sizeBytes), sizeBytes, "Session volume size must be positive.");
        }

        if (_configuration.SessionVolumes.Any(volume => string.Equals(volume.Name, name, StringComparison.OrdinalIgnoreCase)))
        {
            throw new WslcException($"A session volume '{name}' is already configured. Volume names must be unique per container.");
        }

        _configuration = _configuration with
        {
            SessionVolumes = Append(_configuration.SessionVolumes, new WslSessionVolume(name, containerPath, access == VolumeAccess.ReadOnly, sizeBytes, type)),
        };
        return this;
    }

    /// <summary>
    /// Sets the container networking mode. The default is <see cref="ContainerNetworkMode.Bridged"/>.
    /// <see cref="ContainerNetworkMode.None"/> fully isolates the container (no NIC):
    /// no <c>WithPort</c> and no network wait strategies may be combined with it.
    /// Note that detection covers only the built-in TCP/HTTP wait strategies; a custom
    /// <see cref="IWaitStrategy"/> that needs the network bypasses this validation.
    /// </summary>
    public WslContainerBuilder WithNetworkingMode(ContainerNetworkMode mode)
    {
        if (!Enum.IsDefined(mode))
        {
            throw new ArgumentOutOfRangeException(nameof(mode), mode, "Unknown networking mode.");
        }

        _configuration = _configuration with { NetworkingMode = mode };
        return this;
    }

    /// <summary>Caps the session CPU count. Null (default) leaves the runtime default.</summary>
    public WslContainerBuilder WithCpuCount(uint cpuCount)
    {
        if (cpuCount == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(cpuCount), cpuCount, "CPU count must be positive.");
        }

        _configuration = _configuration with { CpuCount = cpuCount };
        return this;
    }

    /// <summary>Caps the session memory in megabytes. Null (default) leaves the runtime default.</summary>
    public WslContainerBuilder WithMemoryMB(uint megabytes)
    {
        if (megabytes == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(megabytes), megabytes, "Memory limit must be positive.");
        }

        _configuration = _configuration with { MemorySizeInMB = megabytes };
        return this;
    }

    /// <summary>
    /// Enables reuse across test runs. The instance name is derived from the configuration hash.
    /// Reuse requires <c>WSLC_REUSE</c> truthy (or explicit <c>true</c> here) <i>and</i> is still
    /// disabled under CI unless <c>WSLC_REUSE_IN_CI</c> is truthy. When disabled, startup logs a
    /// diagnostic and falls back to an ephemeral instance — check logs if reuse seems ignored.
    /// The session VHD (including the pulled image cache) is kept between runs; session volumes
    /// are still recreated empty on every start. Reusable instances are never auto-deleted; run
    /// the reaper purge to reclaim disk.
    /// </summary>
    public WslContainerBuilder WithReuse(bool reuse = true)
    {
        _configuration = _configuration with { Reuse = reuse };
        return this;
    }

    /// <summary>
    /// Overrides the overall startup timeout. Must be &gt;= the sum of configured wait-strategy
    /// timeouts (waits run sequentially); <see cref="Build"/> throws otherwise with guidance.
    /// </summary>
    public WslContainerBuilder WithStartupTimeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout), timeout, "Timeout must be positive.");
        }

        _configuration = _configuration with { StartupTimeout = timeout };
        return this;
    }

    /// <summary>Validates the configuration and creates the container. The container is not started.</summary>
    /// <exception cref="WslcException">
    /// The configuration is incomplete or inconsistent: no image source, ports or network waits
    /// combined with <see cref="ContainerNetworkMode.None"/>, or a startup timeout smaller than
    /// the sum of wait timeouts.
    /// </exception>
    public WslContainer Build()
    {
        WslPlatform.ThrowIfUnsupported();
        var configuration = ApplyEnvironmentDefaults(_configuration);
        Validate(configuration);
        return new WslContainer(configuration);
    }

    private static WslContainerConfiguration ApplyEnvironmentDefaults(WslContainerConfiguration configuration)
    {
        if (configuration.Image is null &&
            configuration.TarballPath is null &&
            WslcEnvironment.DefaultImage is { } defaultImage)
        {
            return configuration with { Image = defaultImage };
        }

        return configuration;
    }

    private static void Validate(WslContainerConfiguration configuration)
    {
        if (configuration.Image is null && configuration.TarballPath is null)
        {
            throw new WslcException(
                "No image source configured. Call WithImage(...) or FromTarball(...), or set " +
                $"{WslcEnvironment.DefaultImageVariable}.");
        }

        if (configuration.NetworkingMode == ContainerNetworkMode.None)
        {
            if (configuration.PortMappings.Count > 0)
            {
                throw new WslcException(
                    "NetworkingMode.None provides no network: remove WithPort(...) declarations or use Bridged networking.");
            }

            var networkWait = FindNetworkWaitStrategy(configuration.WaitStrategies);
            if (networkWait is not null)
            {
                throw new WslcException(
                    $"NetworkingMode.None provides no network: wait strategy '{networkWait}' can never succeed. Remove it or use Bridged networking.");
            }
        }

        if (configuration.WaitStrategies.Count > 0)
        {
            var totalWaits = TimeSpan.Zero;
            foreach (var strategy in configuration.WaitStrategies)
            {
                totalWaits += strategy.Timeout;
            }

            if (totalWaits > configuration.StartupTimeout)
            {
                throw new WslcException(
                    $"Startup timeout {configuration.StartupTimeout.TotalSeconds:0.###}s is smaller than the sum of wait-strategy timeouts {totalWaits.TotalSeconds:0.###}s. " +
                    $"Waits run sequentially, so startup would always fire first. Increase WithStartupTimeout(...) or reduce wait WithTimeout(...) values.");
            }
        }
    }

    private static Dictionary<string, string> CopyEnvironment(
        IReadOnlyDictionary<string, string> environment,
        int additionalCapacity)
    {
        var copy = new Dictionary<string, string>(environment.Count + additionalCapacity, StringComparer.Ordinal);
        foreach (var pair in environment)
        {
            copy[pair.Key] = pair.Value;
        }

        return copy;
    }

    private static string? FindNetworkWaitStrategy(IReadOnlyList<IWaitStrategy> strategies)
    {
        foreach (var strategy in strategies)
        {
            if (strategy is CompositeWaitStrategy composite)
            {
                var nested = FindNetworkWaitStrategy(composite.Strategies);
                if (nested is not null)
                {
                    return nested;
                }
            }
            else if (strategy is TcpPortWaitStrategy or HttpWaitStrategy)
            {
                return strategy.Name;
            }
        }

        return null;
    }

    private static void RequireVolumeName(string value, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Volume name must not be empty.", parameterName);
        }

        foreach (var character in value)
        {
            if (character is '/' or '\\' || char.IsWhiteSpace(character))
            {
                throw new ArgumentException($"Volume name '{value}' must not contain path separators or whitespace.", parameterName);
            }
        }
    }

    private static void RequireText(string value, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Value must not be empty.", parameterName);
        }
    }

    private static void RequireContainerPath(string path, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("Container path must not be empty and must be an absolute Linux path (e.g. /tmp/file).", parameterName);
        }

        if (!path.StartsWith('/'))
        {
            throw new ArgumentException($"Container path '{path}' must be an absolute Linux path starting with '/'.", parameterName);
        }
    }

    private static T[] Append<T>(IReadOnlyList<T> source, T item)
    {
        var result = new T[source.Count + 1];
        for (var i = 0; i < source.Count; i++)
        {
            result[i] = source[i];
        }

        result[source.Count] = item;
        return result;
    }

    private static void RequireEnvironmentName(string name)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ArgumentException("Environment variable name must not be empty.", nameof(name));
        }

        if (!char.IsLetter(name[0]) && name[0] != '_')
        {
            throw new ArgumentException($"Environment variable name '{name}' must start with a letter or underscore.", nameof(name));
        }

        foreach (var character in name)
        {
            if (!char.IsLetterOrDigit(character) && character != '_')
            {
                throw new ArgumentException($"Environment variable name '{name}' contains invalid character '{character}'.", nameof(name));
            }
        }
    }
}
