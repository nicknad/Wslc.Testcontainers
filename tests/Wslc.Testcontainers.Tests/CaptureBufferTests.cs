using System.Text;
using Wslc.Testcontainers.Runtime;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class CaptureBufferTests
{
    private const int MaxBytes = 1024 * 1024;

    [Fact]
    public void Keeps_the_newest_bytes_and_drops_the_oldest()
    {
        var buffer = new ContainerProcess.CaptureBuffer();
        buffer.Append(Encoding.UTF8.GetBytes(new string('a', 700_000)));
        buffer.Append(Encoding.UTF8.GetBytes(new string('b', 700_000)));

        var text = buffer.Decode();

        Assert.Equal(MaxBytes, text.Length);
        Assert.Equal(700_000 - (1_400_000 - MaxBytes), text.Count(c => c == 'a'));
        Assert.Equal(700_000, text.Count(c => c == 'b'));
        Assert.Equal(new string('b', 10), text[^10..]);
    }

    [Fact]
    public void Decodes_across_the_ring_wrap_boundary()
    {
        var buffer = new ContainerProcess.CaptureBuffer();
        buffer.Append(Encoding.UTF8.GetBytes(new string('a', 600_000)));
        buffer.Append(Encoding.UTF8.GetBytes(new string('b', 600_000)));
        buffer.Append(Encoding.UTF8.GetBytes(new string('c', 600_000)));

        var text = buffer.Decode();

        Assert.Equal(MaxBytes, text.Length);
        Assert.DoesNotContain('a', text);
        Assert.Equal(448_576, text.Count(c => c == 'b'));
        Assert.Equal(600_000, text.Count(c => c == 'c'));
        Assert.Equal(new string('c', 10), text[^10..]);
    }

    [Fact]
    public void Oversized_append_keeps_only_its_tail()
    {
        var buffer = new ContainerProcess.CaptureBuffer();
        buffer.Append(Encoding.UTF8.GetBytes(new string('z', MaxBytes + 5_000)));

        var text = buffer.Decode();

        Assert.Equal(MaxBytes, text.Length);
        Assert.All(text, c => Assert.Equal('z', c));
    }

    [Fact]
    public void Disposed_buffer_ignores_appends_and_decodes_empty()
    {
        var buffer = new ContainerProcess.CaptureBuffer();
        buffer.Append(Encoding.UTF8.GetBytes("before"));
        buffer.Dispose();
        buffer.Append(Encoding.UTF8.GetBytes("after"));

        Assert.Equal(string.Empty, buffer.Decode());
    }
}
