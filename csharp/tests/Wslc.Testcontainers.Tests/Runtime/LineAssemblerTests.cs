using System.Collections.Concurrent;
using System.Globalization;
using System.Text;
using Wslc.Testcontainers.Runtime;
using Xunit;

namespace Wslc.Testcontainers.Tests.Runtime;

public sealed class LineAssemblerTests
{
    [Fact]
    public async Task Concurrent_appends_and_flush_churn_preserve_every_line()
    {
        const int WriterCount = 4;
        const int LinesPerWriter = 2_000;
        var cancellationToken = TestContext.Current.CancellationToken;
        var published = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(line =>
        {
            lock (published)
            {
                published.Add(line);
            }
        });
        var failures = new ConcurrentBag<Exception>();
        using var start = new Barrier(WriterCount + 2);
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);

        var writers = new Task[WriterCount];
        for (var writer = 0; writer < WriterCount; writer++)
        {
            var id = writer;
            writers[id] = Task.Run(
                () =>
                {
                    try
                    {
                        start.SignalAndWait(cancellationToken);
                        for (var i = 0; i < LinesPerWriter; i++)
                        {
                            var terminator = i % 3 == 0 ? "\r\n" : "\n";
                            assembler.Append(Encoding.UTF8.GetBytes(MakeLine(id, i) + terminator));
                        }
                    }
                    catch (Exception ex)
                    {
                        failures.Add(ex);
                    }
                },
                cancellationToken);
        }

        var flusher = Task.Run(
            () =>
            {
                try
                {
                    start.SignalAndWait(cancellationToken);
                    while (!stop.IsCancellationRequested)
                    {
                        assembler.Flush();
                    }
                }
                catch (Exception ex)
                {
                    failures.Add(ex);
                }
            },
            cancellationToken);

        start.SignalAndWait(cancellationToken);
        await Task.WhenAll(writers);
        stop.Cancel();
        await flusher;
        assembler.Flush();

        Assert.Empty(failures);

        var expected = new HashSet<string>();
        for (var writer = 0; writer < WriterCount; writer++)
        {
            for (var i = 0; i < LinesPerWriter; i++)
            {
                expected.Add(MakeLine(writer, i));
            }
        }

        lock (published)
        {
            Assert.Equal(expected.Count, published.Count);
            Assert.True(expected.SetEquals(published));
        }
    }

    [Fact]
    public async Task Fragmented_appends_with_concurrent_flushes_preserve_every_line()
    {
        const int LineCount = 2_000;
        var cancellationToken = TestContext.Current.CancellationToken;
        var published = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(line =>
        {
            lock (published)
            {
                published.Add(line);
            }
        });
        var failures = new ConcurrentBag<Exception>();
        using var stop = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var lineGate = new object();
        var expected = new List<string>();

        // Two flushers call Flush concurrently with the fragmenting writer. The gate keeps whole
        // logical lines from interleaving: callbacks are published outside the assembler's state
        // lock, so cross-caller publication order is not part of the contract. Flush racing with
        // a non-empty pending buffer is covered by Concurrent_flushes_publish_pending_data_exactly_once.
        var flushers = new Task[2];
        for (var flusher = 0; flusher < flushers.Length; flusher++)
        {
            flushers[flusher] = Task.Run(
                () =>
                {
                    try
                    {
                        while (!stop.IsCancellationRequested)
                        {
                            lock (lineGate)
                            {
                                assembler.Flush();
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        failures.Add(ex);
                    }
                },
                cancellationToken);
        }

        var writer = Task.Run(
            () =>
            {
                try
                {
                    for (var i = 0; i < LineCount; i++)
                    {
                        var line = MakeLine(0, i);
                        lock (lineGate)
                        {
                            // The line arrives in fragments with no newline until the last one.
                            var first = line.Length / 3;
                            assembler.Append(Encoding.UTF8.GetBytes(line[..first]));
                            assembler.Append(Encoding.UTF8.GetBytes(line[first..(2 * first)]));
                            assembler.Append(Encoding.UTF8.GetBytes(line[(2 * first)..]));
                            assembler.Append(Encoding.UTF8.GetBytes("\n"));
                        }

                        expected.Add(line);
                    }
                }
                catch (Exception ex)
                {
                    failures.Add(ex);
                }
            },
            cancellationToken);

        await writer;
        stop.Cancel();
        await Task.WhenAll(flushers);
        assembler.Flush();

        Assert.Empty(failures);
        lock (published)
        {
            Assert.Equal(expected, published);
        }
    }

    [Fact]
    public async Task Concurrent_flushes_publish_pending_data_exactly_once()
    {
        var cancellationToken = TestContext.Current.CancellationToken;
        for (var iteration = 0; iteration < 200; iteration++)
        {
            var published = new List<string>();
            var assembler = new ContainerProcess.LineAssembler(line =>
            {
                lock (published)
                {
                    published.Add(line);
                }
            });

            var pending = $"partial-{iteration}";
            assembler.Append(Encoding.UTF8.GetBytes(pending));

            using var gate = new Barrier(3);
            var first = Task.Run(
                () =>
                {
                    gate.SignalAndWait(cancellationToken);
                    assembler.Flush();
                },
                cancellationToken);
            var second = Task.Run(
                () =>
                {
                    gate.SignalAndWait(cancellationToken);
                    assembler.Flush();
                },
                cancellationToken);

            gate.SignalAndWait(cancellationToken);
            await Task.WhenAll(first, second);

            Assert.Equal(new[] { pending }, published);
        }
    }

    [Fact]
    public void Line_callback_can_reenter_the_assembler()
    {
        var lines = new List<string>();
        ContainerProcess.LineAssembler? assembler = null;
        var reentered = false;
        assembler = new ContainerProcess.LineAssembler(line =>
        {
            if (line == "third" && !reentered)
            {
                reentered = true;
                assembler!.Append(Encoding.UTF8.GetBytes("second\n"));
            }

            lines.Add(line);
        });

        assembler.Append(Encoding.UTF8.GetBytes("first\nthird\n"));
        assembler.Flush();

        Assert.Equal(new[] { "first", "second", "third" }, lines);
    }

    private static string MakeLine(int writer, int index)
    {
        var body = $"w{writer}-{index}";
        return $"{body}-{Checksum(body)}";
    }

    private static string Checksum(string text)
    {
        unchecked
        {
            var hash = 17u;
            foreach (var ch in text)
            {
                hash = (hash * 31) + ch;
            }

            return hash.ToString("x8", CultureInfo.InvariantCulture);
        }
    }

    [Fact]
    public void Splits_lines_and_trims_carriage_returns()
    {
        var lines = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(lines.Add);

        assembler.Append(Encoding.UTF8.GetBytes("one\r\ntwo\n"));

        Assert.Equal(new[] { "one", "two" }, lines);
    }

    [Fact]
    public void Keeps_partial_line_until_flush()
    {
        var lines = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(lines.Add);

        assembler.Append(Encoding.UTF8.GetBytes("partial"));
        Assert.Empty(lines);

        assembler.Flush();
        Assert.Equal(new[] { "partial" }, lines);
    }

    [Fact]
    public void Reassembles_multibyte_characters_split_across_chunks()
    {
        var lines = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(lines.Add);
        var bytes = Encoding.UTF8.GetBytes("héllo\n");

        // The first chunk ends in the middle of the two-byte 'é' sequence.
        assembler.Append(bytes[..2]);
        assembler.Append(bytes[2..]);

        Assert.Equal(new[] { "héllo" }, lines);
    }

    [Fact]
    public void Emits_every_line_from_a_single_large_chunk()
    {
        const int lineCount = 10_000;
        var payload = new StringBuilder(lineCount * 6);
        for (var i = 0; i < lineCount; i++)
        {
            payload.Append(i).Append('\n');
        }

        var lines = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(lines.Add);
        assembler.Append(Encoding.UTF8.GetBytes(payload.ToString()));

        Assert.Equal(lineCount, lines.Count);
        Assert.Equal("0", lines[0]);
        Assert.Equal("9999", lines[^1]);
    }

    [Fact]
    public void Trims_carriage_return_split_across_chunks()
    {
        var lines = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(lines.Add);

        assembler.Append(Encoding.UTF8.GetBytes("one\r"));
        assembler.Append(Encoding.UTF8.GetBytes("\ntwo"));

        Assert.Equal(new[] { "one" }, lines);

        assembler.Flush();
        Assert.Equal(new[] { "one", "two" }, lines);
    }

    [Fact]
    public void Splits_a_single_line_that_exceeds_the_pending_cap()
    {
        var lines = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(lines.Add);
        var payload = new string('x', (256 * 1024) + 1);

        assembler.Append(Encoding.UTF8.GetBytes(payload));
        assembler.Flush();

        Assert.Single(lines);
        Assert.Equal(payload, lines[0]);
    }

    [Fact]
    public void Scans_a_long_line_once_across_many_chunks()
    {
        var lines = new List<string>();
        var assembler = new ContainerProcess.LineAssembler(lines.Add);

        for (var i = 0; i < 1_000; i++)
        {
            assembler.Append(Encoding.UTF8.GetBytes("chunk"));
        }

        assembler.Append(Encoding.UTF8.GetBytes("\nnext\n"));

        Assert.Equal(new[] { string.Concat(Enumerable.Repeat("chunk", 1_000)), "next" }, lines);
    }
}
