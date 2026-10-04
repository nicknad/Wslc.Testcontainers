#include <wslc/wslc.hpp>

#include <iostream>

/// <summary>Minimal end-to-end example: start an Alpine container and run a command.</summary>
int main()
{
    wslc::WslContainerBuilder builder;
    auto container = builder.WithImage("docker.io/library/alpine:latest").Build();

    try
    {
        container.Start();

        const wslc::ExecResult result = container.Exec("/bin/sh", {"-c", "echo hello from wslc"});
        std::cout << "exit=" << result.ExitCode << " stdout=" << result.StdoutText;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "wslc error: " << exception.what() << '\n';
        container.Dispose();
        return 1;
    }

    container.Dispose();
    return 0;
}
