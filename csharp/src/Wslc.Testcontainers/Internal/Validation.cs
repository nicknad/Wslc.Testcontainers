namespace Wslc.Testcontainers.Internal;

/// <summary>
/// Shared validation for Linux container paths and raw HTTP request paths. Both feed kernel
/// or socket boundaries, so they reject characters and prefixes that can escape the intended
/// target instead of silently normalizing them.
/// </summary>
internal static class Validation
{
    private static readonly string[] DeniedRoots = ["proc", "sys", "dev"];

    /// <summary>Requires non-blank text (images, commands, wait names, file sources).</summary>
    public static string RequireText(string value, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Value must not be empty.", parameterName);
        }

        return value;
    }

    /// <summary>Requires a valid ASCII environment variable name ([A-Za-z_][A-Za-z0-9_]*).</summary>
    public static void RequireEnvironmentName(string name, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ArgumentException("Environment variable name must not be empty.", parameterName);
        }

        if (!IsAsciiLetter(name[0]) && name[0] != '_')
        {
            throw new ArgumentException($"Environment variable name '{name}' must start with a letter or underscore.", parameterName);
        }

        foreach (var character in name)
        {
            if (!IsAsciiLetterOrDigit(character) && character != '_')
            {
                throw new ArgumentException($"Environment variable name '{name}' contains invalid character '{character}'.", parameterName);
            }
        }
    }

    /// <summary>Requires a TCP port in [1, 65535].</summary>
    public static int ValidatePort(int port, string parameterName = "port")
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(port, 1, parameterName);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(port, 65535, parameterName);
        return port;
    }

    /// <summary>Requires a scratch volume name (non-empty, no path separators or whitespace).</summary>
    public static void RequireVolumeName(string value, string parameterName)
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

    private static bool IsAsciiLetter(char character) =>
        character is >= 'A' and <= 'Z' or >= 'a' and <= 'z';

    private static bool IsAsciiLetterOrDigit(char character) =>
        IsAsciiLetter(character) || character is >= '0' and <= '9';

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

        // Single pass over the segments: reject ASCII controls (matching the C++ byte check),
        // '..' escapes, and protected roots. Non-ASCII is allowed; UTF-8 continuation bytes are
        // never ASCII CTLs.
        string? firstSegment = null;
        foreach (var segment in path.Split('/'))
        {
            foreach (var character in segment)
            {
                if (character <= 0x1F || character == 0x7F)
                {
                    throw new ArgumentException($"Container path '{path}' must not contain control characters.", parameterName);
                }
            }

            if (segment == "..")
            {
                throw new ArgumentException($"Container path '{path}' must not contain '..' segments.", parameterName);
            }

            if (firstSegment is null && segment.Length > 0 && segment != ".")
            {
                firstSegment = segment;
            }
        }

        if (firstSegment is not null && Array.IndexOf(DeniedRoots, firstSegment) >= 0)
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

        // Reject HTTP CTLs (0x00-0x1F), SP (0x20) and DEL (0x7F) per RFC 9110. Non-ASCII is
        // allowed to match the C++ byte check: UTF-8 continuation bytes are >= 0x80 and never
        // equal CTLs, while C1 controls arrive multibyte.
        foreach (var character in value)
        {
            if (character <= ' ' || character == 0x7F)
            {
                throw new ArgumentException($"HTTP wait path '{value}' must not contain spaces or control characters; percent-encode them.", parameterName);
            }
        }

        return value;
    }
}
