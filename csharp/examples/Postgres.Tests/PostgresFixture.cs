using Wslc.Testcontainers.Modules.PostgreSql;
using Xunit;

namespace Customers.Tests;

// Shared container for the whole test collection. Starting Postgres costs
// ~60-120s cold, so share one instance and reset state per test instead of
// starting one container per test class.
public sealed class PostgresFixture : IAsyncLifetime
{
    private readonly PostgreSqlContainer _postgres = new PostgreSqlBuilder()
        .WithImage("docker.io/library/postgres:15-alpine")
        .Build();

    public string ConnectionString => _postgres.GetConnectionString();

    public DbConnectionProvider Provider => new(ConnectionString);

    public IAsyncEnumerable<Wslc.Testcontainers.LogLine> Logs(
        CancellationToken cancellationToken = default) =>
        _postgres.SubscribeLogs(cancellationToken);

    // Real-runtime tests are opt-in; skip starting (and pulling postgres) unless enabled.
    public ValueTask InitializeAsync() =>
        Environment.GetEnvironmentVariable(IntegrationFactAttribute.EnvironmentVariable) == "1"
            ? new ValueTask(_postgres.StartAsync())
            : ValueTask.CompletedTask;

    public ValueTask DisposeAsync() => _postgres.DisposeAsync();
}
