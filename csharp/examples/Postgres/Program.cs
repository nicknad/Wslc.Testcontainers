using Npgsql;
using Wslc.Testcontainers.Modules.PostgreSql;

// Console demo: start Postgres, print version, dispose.
// Requires Windows + WSL 2.9.3 with container support. Run with:
//   dotnet run --project examples/Postgres

await using var postgres = new PostgreSqlBuilder()
    .WithPassword("secret")
    .Build();

await postgres.StartAsync();

var connectionString = postgres.GetConnectionString();
Console.WriteLine($"postgres at {postgres.GetConnectEndpoint(PostgreSqlContainer.ContainerPort)}");

await using var connection = new NpgsqlConnection(connectionString);
await connection.OpenAsync();

await using var command = new NpgsqlCommand("SELECT version();", connection);
var version = await command.ExecuteScalarAsync();
Console.WriteLine($"version: {version}");
