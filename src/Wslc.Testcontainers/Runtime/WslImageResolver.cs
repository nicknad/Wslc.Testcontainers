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
        catch (Exception exception)
        {
            // Enumeration failed; fall back to pulling. Published so a persistent failure is visible.
            _publish(LogLine.Diagnostic($"could not enumerate cached images ({exception.Message}); pulling '{image}'"));
            return false;
        }
    }

    /// <summary>
    /// Compares two image references after Docker-style canonicalization: an implicit
    /// <c>docker.io</c> registry, an implicit <c>library/</c> namespace and an implicit
    /// <c>:latest</c> tag. The previous suffix heuristic matched across registries, so a
    /// cached image from an unrelated registry could satisfy the wrong reference.
    /// </summary>
    internal static bool MatchesImage(string candidate, string image) =>
        TryParseReference(candidate, out var candidateRepository, out var candidateTag) &&
        TryParseReference(image, out var imageRepository, out var imageTag) &&
        string.Equals(candidateRepository, imageRepository, StringComparison.OrdinalIgnoreCase) &&
        string.Equals(candidateTag, imageTag, StringComparison.OrdinalIgnoreCase);

    private static bool TryParseReference(string reference, out string repository, out string tag)
    {
        repository = string.Empty;
        tag = "latest";

        if (string.IsNullOrWhiteSpace(reference) || reference.Contains('@'))
        {
            return false;
        }

        var lastSlash = reference.LastIndexOf('/');
        var lastColon = reference.LastIndexOf(':');
        if (lastColon > lastSlash)
        {
            tag = reference[(lastColon + 1)..];
            reference = reference[..lastColon];
            if (tag.Length == 0)
            {
                return false;
            }
        }

        // The first segment is a registry only when it looks like a host: it contains a dot
        // or port, or is localhost. Otherwise it is a namespace on Docker Hub.
        var firstSlash = reference.IndexOf('/');
        if (firstSlash < 0)
        {
            repository = "docker.io/library/" + reference;
        }
        else
        {
            var firstSegment = reference[..firstSlash];
            var hasRegistry = firstSegment.Contains('.') ||
                firstSegment.Contains(':') ||
                string.Equals(firstSegment, "localhost", StringComparison.OrdinalIgnoreCase);
            repository = hasRegistry ? reference : "docker.io/" + reference;
        }

        return repository.Length > 0;
    }
}
