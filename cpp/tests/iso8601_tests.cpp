#include <gtest/gtest.h>

#include "internal/util.hpp"

#include <chrono>
#include <string>

TEST(Iso8601, RoundTripPreservesMilliseconds)
{
    const auto parsed = wslc::internal::ParseIso8601("2026-03-04T05:06:07.123Z");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(wslc::internal::FormatIso8601(*parsed), "2026-03-04T05:06:07.123Z");
}

TEST(Iso8601, OffsetsResolveToTheSameInstant)
{
    const auto utc = wslc::internal::ParseIso8601("2026-03-04T05:06:07Z");
    const auto plusTwo = wslc::internal::ParseIso8601("2026-03-04T07:06:07+02:00");
    const auto minusHalf = wslc::internal::ParseIso8601("2026-03-04T04:36:07-00:30");

    ASSERT_TRUE(utc.has_value());
    EXPECT_EQ(plusTwo, utc);
    EXPECT_EQ(minusHalf, utc);
}

TEST(Iso8601, AcceptsSpaceSeparatorAndLowercaseZulu)
{
    const auto spaced = wslc::internal::ParseIso8601("2026-03-04 05:06:07z");
    const auto canonical = wslc::internal::ParseIso8601("2026-03-04T05:06:07Z");

    ASSERT_TRUE(spaced.has_value());
    EXPECT_EQ(spaced, canonical);
}

TEST(Iso8601, RejectsImpossibleDates)
{
    EXPECT_EQ(wslc::internal::ParseIso8601("2026-13-01T00:00:00Z"), std::nullopt);
    EXPECT_EQ(wslc::internal::ParseIso8601("2026-02-30T00:00:00Z"), std::nullopt);
    EXPECT_EQ(wslc::internal::ParseIso8601("2023-02-29T00:00:00Z"), std::nullopt);
    EXPECT_EQ(wslc::internal::ParseIso8601("2026-03-04T25:00:00Z"), std::nullopt);
    EXPECT_EQ(wslc::internal::ParseIso8601("not-a-date"), std::nullopt);
    EXPECT_EQ(wslc::internal::ParseIso8601("2026-03-04"), std::nullopt);
}

TEST(Iso8601, AcceptsLeapDays)
{
    EXPECT_TRUE(wslc::internal::ParseIso8601("2024-02-29T00:00:00Z").has_value());
}
