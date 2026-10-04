#include <gtest/gtest.h>

#include "internal/configuration.hpp"

#include <string>

using wslc::internal::WslNaming;

TEST(Naming, InstanceNamesAreUniqueAndPrefixed)
{
    const std::string first = WslNaming::CreateInstanceName("my-project-123");
    const std::string second = WslNaming::CreateInstanceName("my-project-123");

    EXPECT_EQ(first.rfind(WslNaming::Prefix, 0), 0u);
    EXPECT_EQ(first.rfind("wslc-my-project-123-", 0), 0u);
    EXPECT_NE(first, second);
}

TEST(Naming, ReuseNamesAreDerivedFromTheConfigurationHash)
{
    const std::string hash(64, 'a');

    EXPECT_EQ(WslNaming::CreateReuseName(hash), std::string("wslc-reuse-aaaaaaaaaaaa"));
}

TEST(Naming, SlugsAreSanitized)
{
    EXPECT_EQ(WslNaming::Slug("My Project/Alpha"), std::string("my-project-alpha"));
    EXPECT_EQ(WslNaming::Slug("___"), std::string("session"));
    EXPECT_EQ(WslNaming::Slug("simple"), std::string("simple"));
}

TEST(Naming, SlugsAreTruncated)
{
    EXPECT_EQ(WslNaming::Slug("abcdefghijklmnop", 10).size(), 10u);
}

TEST(Naming, OwnershipCheckMatchesOnlyManagedNames)
{
    EXPECT_TRUE(WslNaming::IsManaged("wslc-abc"));
    EXPECT_TRUE(WslNaming::IsManaged("WSLC-abc"));
    EXPECT_FALSE(WslNaming::IsManaged("Ubuntu"));
    EXPECT_FALSE(WslNaming::IsManaged("docker-desktop"));
}
