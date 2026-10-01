using Xunit;

namespace Customers.Tests;

// Binds all Postgres tests to one shared fixture (one container total).
[CollectionDefinition(Name)]
public sealed class PostgresCollection : ICollectionFixture<PostgresFixture>
{
    public const string Name = "postgres";
}
