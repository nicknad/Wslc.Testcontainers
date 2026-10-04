using Npgsql;

namespace Customers.Tests;

// Minimal Npgsql-backed service. Creates its table on first use so each
// fresh container starts from a clean schema.
public sealed class CustomerService
{
    private readonly DbConnectionProvider _provider;

    public CustomerService(DbConnectionProvider provider)
    {
        ArgumentNullException.ThrowIfNull(provider);
        _provider = provider;
    }

    public void Create(Customer customer) =>
        CreateAsync(customer).GetAwaiter().GetResult();

    public async Task CreateAsync(Customer customer, CancellationToken cancellationToken = default)
    {
        using var connection = _provider.CreateConnection();
        await connection.OpenAsync(cancellationToken).ConfigureAwait(false);
        using (var init = new NpgsqlCommand(
            "CREATE TABLE IF NOT EXISTS customers (id INT PRIMARY KEY, name TEXT NOT NULL);",
            connection))
        {
            await init.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
        }

        using var insert = new NpgsqlCommand(
            "INSERT INTO customers (id, name) VALUES (@id, @name) ON CONFLICT (id) DO NOTHING;",
            connection);
        insert.Parameters.AddWithValue("id", customer.Id);
        insert.Parameters.AddWithValue("name", customer.Name);
        await insert.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task ResetAsync(CancellationToken cancellationToken = default)
    {
        using var connection = _provider.CreateConnection();
        await connection.OpenAsync(cancellationToken).ConfigureAwait(false);
        using var truncate = new NpgsqlCommand(
            "DROP TABLE IF EXISTS customers; CREATE TABLE customers (id INT PRIMARY KEY, name TEXT NOT NULL);",
            connection);
        await truncate.ExecuteNonQueryAsync(cancellationToken).ConfigureAwait(false);
    }

    public async Task<List<Customer>> GetCustomersAsync(CancellationToken cancellationToken = default)
    {
        using var connection = _provider.CreateConnection();
        await connection.OpenAsync(cancellationToken).ConfigureAwait(false);
        using var query = new NpgsqlCommand("SELECT id, name FROM customers ORDER BY id;", connection);
        using var reader = await query.ExecuteReaderAsync(cancellationToken).ConfigureAwait(false);
        var result = new List<Customer>();
        while (await reader.ReadAsync(cancellationToken).ConfigureAwait(false))
        {
            result.Add(new Customer(reader.GetInt32(0), reader.GetString(1)));
        }

        return result;
    }

    public IEnumerable<Customer> GetCustomers()
    {
        using var connection = _provider.CreateConnection();
        connection.Open();
        using var query = new NpgsqlCommand("SELECT id, name FROM customers ORDER BY id;", connection);
        using var reader = query.ExecuteReader();
        while (reader.Read())
        {
            yield return new Customer(reader.GetInt32(0), reader.GetString(1));
        }
    }
}
