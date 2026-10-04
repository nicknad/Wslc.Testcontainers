#include <gtest/gtest.h>

#include "internal/util.hpp"

using wslc::internal::IsContinuousIntegrationVariable;

TEST(Environment, ContinuousIntegrationBooleanVariables)
{
    EXPECT_TRUE(IsContinuousIntegrationVariable("CI", "1"));
    EXPECT_TRUE(IsContinuousIntegrationVariable("CI", "TRUE"));
    EXPECT_TRUE(IsContinuousIntegrationVariable("TF_BUILD", "True"));
    EXPECT_TRUE(IsContinuousIntegrationVariable("GITHUB_ACTIONS", "yes"));
    EXPECT_FALSE(IsContinuousIntegrationVariable("CI", "false"));
    EXPECT_FALSE(IsContinuousIntegrationVariable("CI", "0"));
    EXPECT_FALSE(IsContinuousIntegrationVariable("CI", ""));
    EXPECT_FALSE(IsContinuousIntegrationVariable("TF_BUILD", "not-a-bool"));
}

TEST(Environment, ContinuousIntegrationPresenceVariables)
{
    EXPECT_TRUE(IsContinuousIntegrationVariable("JENKINS_URL", "https://jenkins.example.com"));
    EXPECT_TRUE(IsContinuousIntegrationVariable("TEAMCITY_VERSION", "2025.1"));
    EXPECT_FALSE(IsContinuousIntegrationVariable("JENKINS_URL", ""));
    EXPECT_FALSE(IsContinuousIntegrationVariable("JENKINS_URL", "   "));
    EXPECT_FALSE(IsContinuousIntegrationVariable("TEAMCITY_VERSION", ""));
}

TEST(Environment, ContinuousIntegrationUnknownVariablesAreIgnored)
{
    EXPECT_FALSE(IsContinuousIntegrationVariable("SOME_OTHER", "true"));
}
