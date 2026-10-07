using Wslc.Testcontainers.Waiting;

namespace Wslc.Testcontainers.Modules.Keycloak;

/// <summary>
/// Testcontainers-style builder for Keycloak. Encapsulates the image, admin credentials, port
/// and readiness waits so tests don't memorize them. Defaults are for local tests only; override
/// <see cref="WithAdminUsername"/>/<see cref="WithAdminPassword"/> for anything shared.
/// </summary>
public sealed class KeycloakBuilder : WslModuleBuilder<KeycloakBuilder>
{
    private string _adminUsername = "admin";
    private string _adminPassword = "admin";

    /// <summary>Initializes the builder with the default Keycloak image and readiness preset.</summary>
    public KeycloakBuilder()
        : base(
            "docker.io/keycloak/keycloak:26.8",
            KeycloakContainer.DefaultPort,
            string.Empty)
    {
    }

    /// <summary>Sets the <c>KC_BOOTSTRAP_ADMIN_USERNAME</c> value. Defaults to <c>admin</c>.</summary>
    public KeycloakBuilder WithAdminUsername(string adminUsername)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(adminUsername);
        _adminUsername = adminUsername;
        return this;
    }

    /// <summary>Sets the <c>KC_BOOTSTRAP_ADMIN_PASSWORD</c> value. Defaults to <c>admin</c>; override for anything shared.</summary>
    public KeycloakBuilder WithAdminPassword(string adminPassword)
    {
        ArgumentNullException.ThrowIfNull(adminPassword);
        _adminPassword = adminPassword;
        return this;
    }

    /// <summary>Builds the container with the configured credentials. The container is not started.</summary>
    public KeycloakContainer Build() => new(BuildContainer(), _adminUsername, _adminPassword);

    // Keycloak does not emit a stable stdout readiness line before the HTTP listener accepts
    // requests, so readiness is the /realms/master HTTP check added in Configure instead of a
    // log message.
    /// <inheritdoc />
    protected override int ReadyMessageOccurrences => 0;

    /// <inheritdoc />
    protected override WslContainerBuilder Configure(WslContainerBuilder builder) =>
        builder
            .WithEnvironment("KC_BOOTSTRAP_ADMIN_USERNAME", _adminUsername)
            .WithEnvironment("KC_BOOTSTRAP_ADMIN_PASSWORD", _adminPassword)
            // The image entrypoint is kc.sh; the configured command becomes its CMD (start-dev).
            .WithCommand("start-dev")
            .WithWaitStrategy(
                Wait.ForWsl()
                    .WithTimeout(WaitTimeout)
                    .UntilHttpRequestSucceeds("/realms/master", KeycloakContainer.DefaultPort));
}
