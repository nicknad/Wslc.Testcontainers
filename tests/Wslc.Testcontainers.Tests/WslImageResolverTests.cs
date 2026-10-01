using Wslc.Testcontainers.Runtime;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslImageResolverTests
{
    [Theory]
    [InlineData("alpine:latest", "alpine:latest", true)]
    [InlineData("docker.io/library/alpine:latest", "alpine:latest", true)]
    [InlineData("alpine:latest", "alpine", true)]
    [InlineData("alpine", "alpine:latest", true)]
    [InlineData("alpine:3.19", "alpine:latest", false)]
    [InlineData("alpine:latest", "busybox:latest", false)]
    [InlineData("docker.io/library/alpine", "alpine:3.19", false)]
    public void Matches_image_handles_registries_and_implicit_latest(string candidate, string image, bool expected) =>
        Assert.Equal(expected, WslImageResolver.MatchesImage(candidate, image));
}
