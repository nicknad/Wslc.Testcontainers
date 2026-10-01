using Microsoft.WSL.Containers;
using Wslc.Testcontainers.Provisioning;

namespace Wslc.Testcontainers.Runtime;

/// <summary>Pulls or imports the image required by a container configuration.</summary>
internal sealed class WslImageResolver
{
    private readonly Session _session;
    private readonly WslContainerConfiguration _configuration;
    private readonly Action<LogLine> _publish;

    public WslImageResolver(Session session, WslContainerConfiguration configuration, Action<LogLine> publish)
    {
        _session = session;
        _configuration = configuration;
        _publish = publish;
    }

    public async Task<string> ResolveAsync(string fallbackImageName, CancellationToken cancellationToken)
    {
        if (_configuration.Image is { } image)
        {
            if (!ImageExists(image))
            {
                _publish(LogLine.Diagnostic($"pulling image '{image}'"));
                var pull = _session.PullImageAsync(new PullImageOptions(image));
                pull.Progress = (_, progress) =>
                    _publish(LogLine.Diagnostic($"pull {progress.Status} {progress.CurrentBytes}/{progress.TotalBytes}"));
                await pull;
            }

            return image;
        }

        var tarballImageName = _configuration.TarballImageName ?? fallbackImageName;
        _publish(LogLine.Diagnostic($"importing image '{tarballImageName}' from '{_configuration.TarballPath}'"));
        var import = _session.ImportImageAsync(Path.GetFullPath(_configuration.TarballPath!), tarballImageName);
        import.Progress = (_, progress) => _publish(LogLine.Diagnostic($"import {progress.Status}"));
        await import;
        return tarballImageName;
    }

    private bool ImageExists(string image)
    {
        try
        {
            foreach (var info in _session.GetImages())
            {
                if (MatchesImage(info.Name, image))
                {
                    return true;
                }
            }

            return false;
        }
        catch
        {
            return false;
        }
    }

    internal static bool MatchesImage(string candidate, string image)
    {
        if (string.Equals(candidate, image, StringComparison.OrdinalIgnoreCase) ||
            candidate.EndsWith("/" + image, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        if (!image.Contains(':') && string.Equals(candidate, image + ":latest", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return !candidate.Contains(':') && string.Equals(candidate + ":latest", image, StringComparison.OrdinalIgnoreCase);
    }
}
