#include <gtest/gtest.h>

#include "internal/util.hpp"
#include "wslc/environment.hpp"
#include "wslc/exceptions.hpp"

#include <chrono>
#include <filesystem>
#include <string>

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

TEST(Environment, EffectiveReuseRequiresConfigurationAndEnvironmentAllowance)
{
    EXPECT_TRUE(wslc::internal::IsReuseEffective(true, false, true));
    EXPECT_FALSE(wslc::internal::IsReuseEffective(true, false, false));
    EXPECT_TRUE(wslc::internal::IsReuseEffective(std::nullopt, true, true));
    EXPECT_FALSE(wslc::internal::IsReuseEffective(std::nullopt, true, false));
    EXPECT_FALSE(wslc::internal::IsReuseEffective(std::nullopt, false, true));
    EXPECT_FALSE(wslc::internal::IsReuseEffective(false, true, true));
}

TEST(Environment, DataDirectoryMakesRelativeValuesAbsolute)
{
    const std::filesystem::path resolved = wslc::internal::ResolveDataDirectory(std::string("relative\\dir"));

    EXPECT_TRUE(resolved.is_absolute());
    EXPECT_EQ(resolved, std::filesystem::absolute(std::filesystem::path(L"relative\\dir")));
}

TEST(Environment, DataDirectoryAcceptsLocalAbsolutePath)
{
    const std::filesystem::path resolved = wslc::internal::ResolveDataDirectory(std::string("C:\\wslc\\data"));

    EXPECT_EQ(resolved, std::filesystem::path(L"C:\\wslc\\data"));
}

TEST(Environment, DataDirectoryRejectsUncAndDevicePaths)
{
    EXPECT_THROW(wslc::internal::ResolveDataDirectory(std::string("\\\\server\\share")), wslc::WslException);
    EXPECT_THROW(wslc::internal::ResolveDataDirectory(std::string("\\\\?\\C:\\x")), wslc::WslException);
    EXPECT_THROW(wslc::internal::ResolveDataDirectory(std::string("\\\\.\\x")), wslc::WslException);
}

TEST(Environment, SessionIdAcceptsValidValues)
{
    EXPECT_EQ(wslc::internal::ResolveSessionId(std::string("a")), "a");
    EXPECT_EQ(wslc::internal::ResolveSessionId(std::string("abc-DEF_123")), "abc-DEF_123");

    const std::string maximum(wslc::internal::c_maxSessionIdLength, 'a');
    EXPECT_EQ(wslc::internal::ResolveSessionId(maximum), maximum);
}

TEST(Environment, SessionIdRejectsInvalidValues)
{
    const std::string empty;
    const std::string tooLong(wslc::internal::c_maxSessionIdLength + 1, 'a');
    const std::string space("has space");
    const std::string dot("has.dot");
    const std::string slash("has/slash");
    const std::string backslash("has\\backslash");

    EXPECT_THROW((wslc::internal::ResolveSessionId(empty)), wslc::WslException);
    EXPECT_THROW((wslc::internal::ResolveSessionId(tooLong)), wslc::WslException);
    EXPECT_THROW((wslc::internal::ResolveSessionId(space)), wslc::WslException);
    EXPECT_THROW((wslc::internal::ResolveSessionId(dot)), wslc::WslException);
    EXPECT_THROW((wslc::internal::ResolveSessionId(slash)), wslc::WslException);
    EXPECT_THROW((wslc::internal::ResolveSessionId(backslash)), wslc::WslException);
}

TEST(Environment, GeneratedSessionIdMatchesCharsetAndLengthBound)
{
    const std::string generated = wslc::internal::ResolveSessionId(std::nullopt);

    EXPECT_FALSE(generated.empty());
    EXPECT_LE(generated.size(), wslc::internal::c_maxSessionIdLength);
    for (const char character : generated)
    {
        const bool allowed = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                             (character >= '0' && character <= '9') || character == '-' || character == '_';
        EXPECT_TRUE(allowed) << generated;
    }
}

TEST(Environment, SanitizedSessionIdIsTruncatedAndStaysInTheCharset)
{
    const std::string sanitized = wslc::internal::SanitizeSessionId(std::string(100, '.'));

    EXPECT_EQ(sanitized, std::string(wslc::internal::c_maxSessionIdLength, '_'));
}

TEST(Environment, ParseTimeoutAcceptsSecondsAndClockTimes)
{
    using namespace std::chrono_literals;

    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("60")), std::chrono::milliseconds(60'000));
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("1.5")), std::chrono::milliseconds(1'500));
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("00:01:00")), std::chrono::milliseconds(60'000));
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("00:00:01.500")), std::chrono::milliseconds(1'500));
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("1.00:00:00")), std::chrono::milliseconds(86'400'000));
}

TEST(Environment, ParseTimeoutRejectsNonPositiveAndInvalid)
{
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("0")), std::nullopt);
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("-5")), std::nullopt);
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("abc")), std::nullopt);
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("")), std::nullopt);
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::string("   ")), std::nullopt);
    EXPECT_EQ(wslc::WslEnvironment::ParseTimeout(std::nullopt), std::nullopt);
}
