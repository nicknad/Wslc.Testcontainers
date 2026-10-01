using Npgsql;

namespace Customers.Tests;

// Thin wrapper so the service takes a connection string, like the
// Testcontainers example. Creates short-lived Npgsql connections.
public sealed class DbConnectionProvider
{
    private readonly string _connectionString;

    public DbConnectionProvider(string connectionString)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(connectionString);
        _connectionString = connectionString;
    }

    public string ConnectionString => _connectionString;

    public NpgsqlConnection CreateConnection() => new(_connectionString);
}
