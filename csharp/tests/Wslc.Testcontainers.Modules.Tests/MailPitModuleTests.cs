using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using Wslc.Testcontainers.Modules.MailPit;
using Wslc.Testcontainers.Modules.Tests.Support;
using Xunit;

namespace Wslc.Testcontainers.Modules.Tests;

public sealed class MailPitModuleTests
{
    [Fact]
    public void Builds_with_the_default_image_and_ports()
    {
        var container = new MailPitBuilder().Build();

        Assert.Equal("docker.io/axllent/mailpit:v1.31", container.Image);
        Assert.Equal(1025, MailPitContainer.SmtpPort);
        Assert.Equal(8025, MailPitContainer.HttpPort);
        Assert.False(container.IsStarted);
    }

    [IntegrationFact]
    public async Task Starts_and_serves_http_and_smtp_over_the_mapped_endpoints()
    {
        await using var mailpit = new MailPitBuilder().Build();

        await mailpit.StartAsync();

        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(TestContext.Current.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(15));
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(15) };
        var health = await client.GetAsync(new Uri($"{mailpit.GetHttpEndpoint()}/livez"), timeout.Token);
        Assert.True(health.IsSuccessStatusCode);

        var smtpEndpoint = mailpit.GetSmtpEndpoint();
        var separator = smtpEndpoint.LastIndexOf(':');
        using var smtp = new TcpClient();
        await smtp.ConnectAsync(
            smtpEndpoint[..separator],
            int.Parse(smtpEndpoint[(separator + 1)..], System.Globalization.CultureInfo.InvariantCulture),
            timeout.Token);
        using var stream = smtp.GetStream();
        var buffer = new byte[128];
        var read = await stream.ReadAsync(buffer, timeout.Token);

        Assert.StartsWith("220 ", Encoding.ASCII.GetString(buffer, 0, read), StringComparison.Ordinal);
    }
}
