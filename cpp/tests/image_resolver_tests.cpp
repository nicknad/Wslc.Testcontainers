#include <gtest/gtest.h>

#include "internal/image_resolver.hpp"

#include <string>

using wslc::internal::ImageResolver;

TEST(ImageResolver, MatchesImageHandlesRegistriesAndImplicitLatest)
{
    EXPECT_TRUE(ImageResolver::MatchesImage("alpine:latest", "alpine:latest"));
    EXPECT_TRUE(ImageResolver::MatchesImage("docker.io/library/alpine:latest", "alpine:latest"));
    EXPECT_TRUE(ImageResolver::MatchesImage("alpine:latest", "alpine"));
    EXPECT_TRUE(ImageResolver::MatchesImage("alpine", "alpine:latest"));
    EXPECT_FALSE(ImageResolver::MatchesImage("alpine:3.19", "alpine:latest"));
    EXPECT_FALSE(ImageResolver::MatchesImage("alpine:latest", "busybox:latest"));
    EXPECT_FALSE(ImageResolver::MatchesImage("docker.io/library/alpine", "alpine:3.19"));
    EXPECT_FALSE(ImageResolver::MatchesImage("evil/team/postgres:15", "team/postgres:15"));
    EXPECT_TRUE(ImageResolver::MatchesImage("docker.io/team/postgres:15", "team/postgres:15"));
    EXPECT_FALSE(ImageResolver::MatchesImage("registry.example.com:5000/postgres:15", "postgres:15"));
    EXPECT_TRUE(ImageResolver::MatchesImage("localhost:5000/postgres:15", "localhost:5000/postgres:15"));
    EXPECT_FALSE(ImageResolver::MatchesImage(
        "alpine@sha256:0000000000000000000000000000000000000000000000000000000000000000", "alpine"));
}
