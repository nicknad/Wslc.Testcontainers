using System.Text;
using Wslc.Testcontainers.Runtime;
using Xunit;

namespace Wslc.Testcontainers.Tests;

public sealed class LineAssemblerTests
{
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
