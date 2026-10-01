using Wslc.Testcontainers.Provisioning;
using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers;

/// <summary>
/// Immutable Testcontainers-style builder for <see cref="WslContainer"/> instances.
/// Every <c>With...</c>/<c>From...</c> call returns a new builder sharing the accumulated configuration.
/// </summary>
public sealed class WslContainerBuilder
{
    private readonly WslContainerConfiguration _configuration;

    public WslContainerBuilder()
        : this(new WslContainerConfiguration())
    {
    }

    private WslContainerBuilder(WslContainerConfiguration configuration) => _configuration = configuration;

    /// <summary>Uses a container image. The image is pulled on first use and cached in the session storage.</summary>
    public WslContainerBuilder FromImage(string image)
    {
        RequireText(image, nameof(image));
        return new WslContainerBuilder(_configuration with
        {
            Image = image,
            TarballPath = null,
            TarballImageName = null,
        });
    }

    /// <summary>Imports a root filesystem tarball as a container image.</summary>
    /// <param name="tarballPath">Path to a tar archive.</param>
    /// <param name="imageName">Image reference to assign. Defaults to a WSLC-generated local name.</param>
    public WslContainerBuilder FromTarball(string tarballPath, string? imageName = null)
    {
        RequireText(tarballPath, nameof(tarballPath));
        return new WslContainerBuilder(_configuration with
        {
            TarballPath = tarballPath,
            TarballImageName = imageName,
            Image = null,
        });
    }

    /// <summary>Sets the long-running command started as the container init process.</summary>
    public WslContainerBuilder WithCommand(string command, params string[] arguments)
    {
        RequireText(command, nameof(command));
        ArgumentNullException.ThrowIfNull(arguments);
        return new WslContainerBuilder(_configuration with
        {
            Command = command,
            CommandArguments = (string[])arguments.Clone(),
        });
    }

    /// <summary>Sets the working directory used by the init process and command executions.</summary>
    public WslContainerBuilder WithWorkingDirectory(string workingDirectory)
    {
        RequireText(workingDirectory, nameof(workingDirectory));
        return new WslContainerBuilder(_configuration with { WorkingDirectory = workingDirectory });
    }

    /// <summary>Adds an environment variable scoped to the container processes.</summary>
    public WslContainerBuilder WithEnvironment(string name, string value)
    {
        RequireEnvironmentName(name);
        ArgumentNullException.ThrowIfNull(value);
        var environment = CopyEnvironment(_configuration.Environment, additionalCapacity: 1);
        environment[name] = value;
        return new WslContainerBuilder(_configuration with { Environment = environment });
    }

    /// <summary>Adds environment variables scoped to the container processes.</summary>
    public WslContainerBuilder WithEnvironmentVariables(IReadOnlyDictionary<string, string> variables)
    {
        ArgumentNullException.ThrowIfNull(variables);
        var environment = CopyEnvironment(_configuration.Environment, variables.Count);
        foreach (var pair in variables)
        {
            RequireEnvironmentName(pair.Key);
            environment[pair.Key] = pair.Value;
        }

        return new WslContainerBuilder(_configuration with { Environment = environment });
    }

    /// <summary>Declares a Linux service port that will be exposed on a dynamic Windows port.</summary>
    public WslContainerBuilder WithPort(int port)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(port, 1);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(port, 65535);

        return _configuration.Ports.Contains(port)
            ? this
            : new WslContainerBuilder(_configuration with { Ports = Append(_configuration.Ports, port) });
    }

    /// <summary>Adds a readiness strategy. All configured strategies must pass before startup completes.</summary>
    public WslContainerBuilder WithWaitStrategy(IWaitStrategy strategy)
    {
        ArgumentNullException.ThrowIfNull(strategy);
        return new WslContainerBuilder(_configuration with
        {
            WaitStrategies = Append(_configuration.WaitStrategies, strategy),
        });
    }

    /// <summary>Copies a Windows file into the container during startup.</summary>
    public WslContainerBuilder WithFile(string source, string destination)
    {
        RequireText(source, nameof(source));
        RequireText(destination, nameof(destination));
        if (!File.Exists(source))
        {
            throw new WslcException($"File source '{source}' does not exist. Only files are supported by WithFile.");
        }

        return new WslContainerBuilder(_configuration with
        {
            Files = Append(_configuration.Files, new WslFileCopy(source, destination)),
        });
    }

    /// <summary>Mounts a Windows directory into the container.</summary>
    public WslContainerBuilder WithVolume(string hostPath, string containerPath, bool readOnly = false)
    {
        RequireText(hostPath, nameof(hostPath));
        RequireText(containerPath, nameof(containerPath));
        if (!Directory.Exists(hostPath))
        {
            throw new WslcException($"Volume host path '{hostPath}' does not exist or is not a directory.");
        }

        return new WslContainerBuilder(_configuration with
        {
            Volumes = Append(_configuration.Volumes, new WslVolumeMount(Path.GetFullPath(hostPath), containerPath, readOnly)),
        });
    }

    /// <summary>
    /// Enables reuse across test runs. Reuse only applies when explicitly requested, is disabled
    /// under CI by default, and derives the instance identity from the builder configuration.
    /// </summary>
    public WslContainerBuilder WithReuse(bool reuse = true) =>
        new(_configuration with { Reuse = reuse });

    /// <summary>Overrides the overall startup timeout.</summary>
    public WslContainerBuilder WithStartupTimeout(TimeSpan timeout)
    {
        if (timeout <= TimeSpan.Zero)
        {
            throw new ArgumentOutOfRangeException(nameof(timeout), timeout, "Timeout must be positive.");
        }

        return new WslContainerBuilder(_configuration with { StartupTimeout = timeout });
    }

    /// <summary>Validates the configuration and creates the container. The container is not started.</summary>
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
                "No image source configured. Call FromImage(...) or FromTarball(...), or set " +
                $"{WslcEnvironment.DefaultImageVariable}.");
        }

        if (configuration.TarballPath is { } tarball && !File.Exists(tarball))
        {
            throw new WslcException($"Tarball '{tarball}' does not exist.");
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

    private static void RequireText(string value, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Value must not be empty.", parameterName);
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
