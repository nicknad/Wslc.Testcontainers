#include <wslc/modules/postgresql.hpp>

#include <iostream>

/// <summary>Starts the typed Postgres module and prints its connection string.</summary>
int main()
{
    wslc::modules::PostgreSqlBuilder builder;
    auto postgres = builder.WithPassword("secret").Build();

    try
    {
        postgres.Start();

        std::cout << "Postgres ready\n";
        std::cout << postgres.GetConnectionString() << '\n';

        const wslc::ExecResult result =
            postgres.Exec("/bin/sh", {"-c", "PGPASSWORD=secret psql -U postgres -d customers -tAc 'SELECT version()'"});
        std::cout << result.Stdout;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "wslc error: " << exception.what() << '\n';
        postgres.Dispose();
        return 1;
    }

    postgres.Dispose();
    return 0;
}
