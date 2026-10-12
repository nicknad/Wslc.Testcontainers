#include <gtest/gtest.h>

#include "wslc/wsl_container_builder.hpp"

using wslc::WslContainerBuilder;

TEST(ContainerLifecycle, DisposeCompletesLogsAndIsIdempotent)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").WithKeepAliveShell().Build();

    container.Dispose();
    container.Dispose();

    auto stream = container.SubscribeLogs();
    EXPECT_FALSE(stream.Next().has_value());
}

TEST(ContainerLifecycle, DisposeIsSafeForUnstartedContainers)
{
    WslContainerBuilder builder;
    auto container = builder.WithImage("alpine:latest").WithKeepAliveShell().Build();

    EXPECT_FALSE(container.IsStarted());
    container.Dispose();
    container.Dispose();
}
