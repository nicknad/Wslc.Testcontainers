using Wslc.Testcontainers.Testing;
using Xunit;

namespace Customers.Tests;

// Direct translation of the Testcontainers example to Wslc.
// Requires Windows + WSL 2.9.3 with container support: pulls postgres:15-alpine.
// Shares one container across the collection via PostgresFixture; each test
// resets state so data never leaks between tests.
[Collection(PostgresCollection.Name)]
public sealed class CustomerServiceTest
{
    private readonly PostgresFixture _fixture;
    private readonly ITestOutputHelper _output;

    public CustomerServiceTest(PostgresFixture fixture, ITestOutputHelper output)
    {
        _fixture = fixture;
        _output = output;
    }

    [IntegrationFact]
    public async Task ShouldReturnTwoCustomers()
    {
        // Given a clean database on the shared container.
        var customerService = new CustomerService(_fixture.Provider);
        var cancellationToken = TestContext.Current.CancellationToken;
        await customerService.ResetAsync(cancellationToken);

        try
        {
            // When
            await customerService.CreateAsync(new Customer(1, "George"), cancellationToken);
            await customerService.CreateAsync(new Customer(2, "John"), cancellationToken);
            var customers = await customerService.GetCustomersAsync(cancellationToken);

            // Then
            Assert.Equal(2, customers.Count);
        }
        catch (Exception ex)
        {
            // Dump bounded diagnostics, then rethrow so xUnit reports the failure.
            // The stream is infinite, so bound the read with a short timeout.
            _output.WriteLine(ex.ToString());
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(2));
            try
            {
                await LogDumper.DumpAsync(_fixture.Logs(cts.Token), _output.WriteLine, maxLines: 100, cts.Token);
            }
            catch (OperationCanceledException)
            {
                // Expected when the log stream outlives the timeout.
            }

            throw;
        }
    }
}
