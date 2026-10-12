#include <gtest/gtest.h>

#include "wslc/platform.hpp"
#include "wslc/wsl_container_builder.hpp"

#include <string>

using wslc::PlatformNotSupportedException;
using wslc::WslContainerBuilder;
using wslc::WslPlatform;

TEST(Platform, IsSupportedMatchesOsAndArchitecture)
{
#if defined(_M_X64)
    EXPECT_TRUE(WslPlatform::IsSupported());
#else
    EXPECT_FALSE(WslPlatform::IsSupported());
#endif
}

TEST(Platform, ThrowIfUnsupportedMatchesIsSupported)
{
    if (WslPlatform::IsSupported())
    {
        WslPlatform::ThrowIfUnsupported();
    }
    else
    {
        EXPECT_THROW(WslPlatform::ThrowIfUnsupported(), PlatformNotSupportedException);
    }
}

TEST(Platform, DescribeCurrentMentionsArchitecture)
{
    const std::string description = WslPlatform::DescribeCurrent();

    EXPECT_FALSE(description.empty());
#if defined(_M_ARM64)
    EXPECT_NE(description.find("Arm64"), std::string::npos);
#elif defined(_M_X64)
    EXPECT_NE(description.find("X64"), std::string::npos);
#endif
}

TEST(Platform, WslVersionGateEnforcesMinimum)
{
    EXPECT_TRUE(WslPlatform::IsWslVersionSupported(2, 9, 3));
    EXPECT_FALSE(WslPlatform::IsWslVersionSupported(2, 9, 2));
    EXPECT_FALSE(WslPlatform::IsWslVersionSupported(2, 8, 99));
    EXPECT_TRUE(WslPlatform::IsWslVersionSupported(2, 10, 0));
    EXPECT_TRUE(WslPlatform::IsWslVersionSupported(3, 0, 0));
    EXPECT_TRUE(WslPlatform::IsWslVersionSupported(3, 0, 1));
    EXPECT_FALSE(WslPlatform::IsWslVersionSupported(1, 99, 99));
}

TEST(Platform, BuildFailsFastOnUnsupportedPlatform)
{
    WslContainerBuilder builder;
    builder.WithImage("docker.io/library/alpine:latest").WithKeepAliveShell();

    if (!WslPlatform::IsSupported())
    {
        EXPECT_THROW(builder.Build(), PlatformNotSupportedException);
    }
    else
    {
        auto container = builder.Build();
        EXPECT_EQ(container.Image().value_or(""), std::string("docker.io/library/alpine:latest"));
    }
}
