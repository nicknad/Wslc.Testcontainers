namespace Wslc.Testcontainers.Modules.MailPit;

/// <summary>
/// Typed Mailpit container: a module wrapper that renders the SMTP and HTTP endpoints from the
/// dynamic host port mappings.
/// </summary>
public sealed class MailPitContainer : WslModuleContainer
{
    /// <summary>The Linux TCP port Mailpit accepts SMTP on by default (1025).</summary>
    public const int SmtpPort = 1025;

    /// <summary>The Linux TCP port Mailpit serves its HTTP UI/API on by default (8025).</summary>
    public const int HttpPort = 8025;

    internal MailPitContainer(IWslContainer inner)
        : base(inner)
    {
    }

    /// <summary>Renders the <c>host:port</c> SMTP endpoint to point a mail client at.</summary>
    public string GetSmtpEndpoint()
    {
        var endpoint = GetConnectEndpoint(SmtpPort);
        return $"{endpoint.Address}:{endpoint.Port}";
    }

    /// <summary>Renders the Mailpit HTTP UI/API base URL (e.g. <c>http://127.0.0.1:49153</c>).</summary>
    public string GetHttpEndpoint() => FormatHttpEndpoint(GetConnectEndpoint(HttpPort));
}
