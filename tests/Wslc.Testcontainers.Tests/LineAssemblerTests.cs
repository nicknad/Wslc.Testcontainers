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
}
