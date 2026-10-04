namespace Wslc.Testcontainers.Internal;

/// <summary>
/// Shared validation for Linux container paths and raw HTTP request paths. Both feed kernel
/// or socket boundaries, so they reject characters and prefixes that can escape the intended
/// target instead of silently normalizing them.
/// </summary>
internal static class Validation
{
    private static readonly string[] DeniedRoots = ["proc", "sys", "dev"];

    /// <summary>
    /// Requires an absolute Linux path that is safe to pass to the WSLC runtime as a working
    /// directory, volume target, or copy destination.
    /// </summary>
    public static string RequireContainerPath(string path, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("Container path must not be empty and must be an absolute Linux path (e.g. /tmp/file).", parameterName);
        }

        if (path[0] != '/')
        {
            throw new ArgumentException($"Container path '{path}' must be an absolute Linux path starting with '/'.", parameterName);
        }

        if (ContainsControlCharacter(path))
        {
            throw new ArgumentException($"Container path '{path}' must not contain control characters.", parameterName);
        }

        var firstSegment = FirstSegment(path);
        foreach (var segment in path.Split('/'))
        {
            if (segment == "..")
            {
                throw new ArgumentException($"Container path '{path}' must not contain '..' segments.", parameterName);
            }
        }

        if (Array.IndexOf(DeniedRoots, firstSegment) >= 0)
        {
            throw new ArgumentException($"Container path '{path}' targets the protected '/{firstSegment}' filesystem.", parameterName);
        }

        return path;
    }

    /// <summary>
    /// Requires an origin-form path-and-query for the raw HTTP probe. Spaces and control
    /// characters would terminate the request line or inject headers, so callers must
    /// percent-encode them.
    /// </summary>
    public static string RequireHttpPath(string value, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("HTTP wait path must not be empty.", parameterName);
        }

        if (value.Contains("://", StringComparison.Ordinal)
            || value.StartsWith("http:", StringComparison.OrdinalIgnoreCase)
            || value.StartsWith("https:", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException($"HTTP wait path '{value}' must be a path-and-query (e.g. /health), not a full URL.", parameterName);
        }

        if (value[0] != '/')
        {
            throw new ArgumentException($"HTTP wait path '{value}' must start with '/'.", parameterName);
        }

        foreach (var character in value)
        {
            if (character == ' ' || char.IsControl(character))
            {
                throw new ArgumentException($"HTTP wait path '{value}' must not contain spaces or control characters; percent-encode them.", parameterName);
            }
        }

        return value;
    }

    private static bool ContainsControlCharacter(string value)
    {
        foreach (var character in value)
        {
            if (char.IsControl(character))
            {
                return true;
            }
        }

        return false;
    }

    private static string FirstSegment(string path)
    {
        foreach (var segment in path.Split('/'))
        {
            if (segment.Length > 0 && segment != ".")
            {
                return segment;
            }
        }

        return string.Empty;
    }
}
