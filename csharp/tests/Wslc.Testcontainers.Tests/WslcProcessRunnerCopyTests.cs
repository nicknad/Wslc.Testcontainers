using System.Text;
using Windows.Storage.Streams;
using Wslc.Testcontainers.Runtime;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class WslcProcessRunnerCopyTests
{
    [Fact]
    public async Task CopyStdoutAsync_writes_all_data_to_the_output()
    {
        var payload = Encoding.UTF8.GetBytes("wslc-copy-payload");
        using var input = new MemoryStream(payload);
        using IInputStream stdout = input.AsInputStream();
        using var output = new MemoryStream();

        await WslcProcessRunner.CopyStdoutAsync(
            stdout, output, "/tmp/source", "destination", TestContext.Current.CancellationToken);

        Assert.Equal(payload, output.ToArray());
    }

    [Fact]
    public async Task CopyStdoutAsync_observes_cancellation_while_the_pipe_is_idle()
    {
        using var idle = new IdleReadStream();
        using IInputStream stdout = idle.AsInputStream();
        using var output = new MemoryStream();

        using var cancellation = new CancellationTokenSource();
        var copy = WslcProcessRunner.CopyStdoutAsync(
            stdout, output, "/tmp/source", "destination", cancellation.Token);

        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => copy);
        Assert.Empty(output.ToArray());
    }

    /// <summary>A readable stream whose async reads stay pending until the object is dropped.</summary>
    private sealed class IdleReadStream : Stream
    {
        private readonly TaskCompletionSource<int> _read = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public override bool CanRead => true;

        public override bool CanSeek => false;

        public override bool CanWrite => false;

        public override long Length => throw new NotSupportedException();

        public override long Position
        {
            get => throw new NotSupportedException();
            set => throw new NotSupportedException();
        }

        public override void Flush()
        {
        }

        public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();

        public override Task<int> ReadAsync(byte[] buffer, int offset, int count, CancellationToken cancellationToken) =>
            _read.Task;

        public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) =>
            new(_read.Task);

        public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();

        public override void SetLength(long value) => throw new NotSupportedException();

        public override void Write(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    }
}
